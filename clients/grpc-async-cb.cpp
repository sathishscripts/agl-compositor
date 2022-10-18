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

#include "log.h"
#include "agl_shell.grpc.pb.h"
#include "grpc-async-cb.h"

Lister::Lister(Shell *shell) : m_shell(shell)
{
	// don't call NextWrite() just yet we do it explicitly when getting
	// the events from the compositor
}

void
Lister::OnDone()
{
	delete this;
}

void Lister::OnWriteDone(bool ok)
{
	LOG("ok %d\n", ok);
	if (ok) {
		// normally we should finish here, but we don't do that to keep
		// the channel open
		//Finish(grpc::Status::OK);
	}
}

void 
Lister::NextWrite(void)
{
	// we're going to have a Lister instance per client so we're safe here
	StartWrite(&m_shell->m_shell_data->current_app_state);
}

grpc::ServerUnaryReactor *
GrpcServiceImpl::ActivateApp(grpc::CallbackServerContext *context,
                            const ::agl_shell_ipc::ActivateRequest* request,
                            google::protobuf::Empty* /*response*/)
{
	LOG("activating app %s on output %s\n", request->app_id().c_str(),
						request->output_name().c_str());

	m_aglShell->ActivateApp(request->app_id(), request->output_name());

	grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
	reactor->Finish(grpc::Status::OK);
	return reactor;
}

grpc::ServerUnaryReactor *
GrpcServiceImpl::DeactivateApp(grpc::CallbackServerContext *context,
                              const ::agl_shell_ipc::DeactivateRequest* request,
                              google::protobuf::Empty* /*response*/)
{
	m_aglShell->DeactivateApp(request->app_id());

	grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
	reactor->Finish(grpc::Status::OK);
	return reactor;
}

grpc::ServerUnaryReactor *
GrpcServiceImpl::SetAppFloat(grpc::CallbackServerContext *context,
                            const ::agl_shell_ipc::FloatRequest* request,
                            google::protobuf::Empty* /* response */)
{
	m_aglShell->SetAppFloat(request->app_id());

	grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
	reactor->Finish(grpc::Status::OK);
	return reactor;
}

grpc::ServerUnaryReactor *
GrpcServiceImpl::SetAppSplit(grpc::CallbackServerContext *context,
           const ::agl_shell_ipc::SplitRequest* request,
           google::protobuf::Empty* /*response*/)
{
	m_aglShell->SetAppSplit(request->app_id(), request->tile_orientation());

	grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
	reactor->Finish(grpc::Status::OK);
	return reactor;
}

grpc::ServerWriteReactor<::agl_shell_ipc::AppState>*
GrpcServiceImpl::AppStatusState(grpc::CallbackServerContext* context,
				 const google::protobuf::Empty*)
{

	Lister *n = new Lister(m_aglShell);

	m_aglShell->m_shell_data->server_context_list.push_back(std::pair(context, n));
	LOG("added lister %p\n", static_cast<void *>(n));

	// just return  a Lister to keep the channel open
	return n;
}
