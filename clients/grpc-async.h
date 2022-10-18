#pragma once

#include <memory>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "shell.h"
#include "agl_shell.grpc.pb.h"

namespace {
       const char kDefaultGrpcServiceAddress[] = "127.0.0.1:14005";
}

class CallData {
public:
	// Take in the "service" instance (in this case representing an
	// asynchronous server) and the completion queue "cq" used for
	// asynchronous communication with the gRPC runtime.
	CallData(Greeter::AsyncService* service, grpc::ServerCompletionQueue* cq)
		: m_service(service), m_cq(cq), m_repliesSent(0), 
		m_responder(&m_ctx), m_status(CREATE) { Proceed(); }
	void Proceed();
private:
	// The means of communication with the gRPC runtime for an asynchronous
	// server.
	Greeter::AsyncService *m_service;
	// The producer-consumer queue where for asynchronous server
	// notifications.
	grpc::ServerCompletionQueue *m_cq;
	// Context for the rpc, allowing to tweak aspects of it such as the use
	// of compression, authentication, as well as to send metadata back to
	// the client.
	grpc::ServerContext m_ctx;

	// What we send back to the client.
	::agl_shell_ipc::AppState m_reply;

	uint32_t m_repliesSent;
	const uint32_t MAX_REPLIES = 5;

	// The means to get back to the client.
	grpc::ServerAsyncWriter<::agl_shell_ipc::AppState> m_responder;

	// Let's implement a tiny state machine with the following states.
	enum CallStatus {
		CREATE,
		PROCESS,
		PROCESSING,
		FINISH
	};

	// The current serving state.
	CallStatus m_status;
};


class GrpcServiceImpl final {
public:
	GrpcServiceImpl(Shell *aglShell) : m_aglShell(aglShell) {}
	~GrpcServiceImpl();
	void Run();

	// This can be run in multiple threads if needed.
	void HandleRpcs();

private:
       Shell *m_aglShell;

       std::unique_ptr<grpc::ServerCompletionQueue> m_cq;
       Greeter::AsyncService m_service;
       std::unique_ptr<grpc::Server> m_server;
};
