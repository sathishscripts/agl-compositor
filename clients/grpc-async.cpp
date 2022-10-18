#include <cstdio>
#include <ctime>
#include <algorithm>
#include <queue>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "agl_shell.grpc.pb.h"
#include "grpc-async.h"

void
CallData::Proceed(void)
{
	switch (m_status) {
	case CREATE:
		// Make this instance progress to the PROCESS state.
		m_status = PROCESS;
		std::cout << "Creating Call data for new client connections: "
			<< this << std::endl;

		// As part of the initial CREATE state, we *request* that the
		// system start processing AppStatusState requests.
		//
		// In this request, "this" acts are the tag uniquely
		// identifying the request (so that different CallData
		// instances can serve different requests concurrently), in
		// this case the memory address of this CallData instance.
		m_service->RequestAppStatusState(&m_ctx, &m_request, &m_responder,
						 m_cq, m_cq, (void *) this);
		break;
	case PROCESS:
		// Spawn a new CallData instance to serve new clients while we
		// process the one for this CallData. The instance will
		// deallocate itself as part of its FINISH state.
		CallData *cd = new CallData(m_service, m_cq);

		// The actual processing.
		m_status = PROCESSING;
		m_repliesSent++;
		break;
	case PROCESSING:
		if (m_repliesSent == MAX_REPLIES) {
			// And we are done! Let the gRPC runtime know we've
			// finished, using the memory address of this instance
			// as the uniquely identifying tag for the event.
			m_status = FINISH;
			m_responder.Finish(Status::OK, this);
		} else {
			// The actual processing.
			m_status = PROCESSING;
			m_repliesSent++;
		}
		break;
	case FINISH:
		GPR_ASSERT(m_status == FINISH);
		std::cout << "Completed RPC for: " << this << std::endl;
		// Once in the FINISH state, deallocate ourselves (CallData).
		delete this;
		break;
	default:
		break;
	}
}

GrpcServiceImpl::~GrpcServiceImpl()
{
	m_server->Shutdown();
	// Always shutdown the completion queue after the server.
	m_cq->Shutdown();
}

void
GrpcServiceImpl::Run(void)
{
	std::string server_address(kDefaultGrpcServiceAddress);

	grpc::ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

	builder.RegisterService(&m_service);
	m_cq = builder.AddCompletionQueue();

	m_server = builder.BuildAndStart();
	std::cout << "Server listening on " << server_address << std::endl;

	// Proceed to the server's main loop.
	HandleRpcs();
}

void
GrpcServiceImpl::HandleRpcs(void)
{
	// Spawn a new CallData instance to serve new clients.
	CallData *cd = new CallData(&m_service, m_cq.get());

	// uniquely identifies a request.
	void *tag;
	bool ok;

	// Block waiting to read the next event from the completion queue. The
	// event is uniquely identified by its tag, which in this case is the
	// memory address of a CallData instance.
	//
	// The return value of Next should always be checked. This return value
	// tells us whether there is any kind of event or cq_ is shutting down.
	while (true) {
		std::cout << "Blocked on next waiting for events" << std::endl;
		GPR_ASSERT(m_cq->Next(&tag, &ok));
		GPR_ASSERT(ok);

		std::cout << "Calling tag " << tag << " with Proceed()" << std::endl;
		static_cast<CallData*>(tag)->Proceed();
	}
}
