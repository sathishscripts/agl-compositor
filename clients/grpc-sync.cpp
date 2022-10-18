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
#include "grpc-sync.h"

grpc::ServerUnaryReactor *
GrpcServiceImpl::ActivateApp(grpc::CallbackServerContext *context,
                            const ::agl_shell_ipc::ActivateRequest* request,
                            google::protobuf::Empty* /*response*/)
{
	fprintf(stderr, "activating app %s on output %s\n",
			request->app_id().c_str(),
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

grpc::ServerUnaryReactor *
GrpcServiceImpl::AppStatusState(grpc::CallbackServerContext *context,
           google::protobuf::Empty*,
	   ::grpc::ServerWriter<::agl_shell_ipc::AppState>* writer)
{
	(void) writer;
	grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
	reactor->Finish(grpc::Status::OK);

	return reactor;
}
