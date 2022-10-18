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


class GrpcServiceImpl final : public agl_shell_ipc::AglShellManagerService::CallbackService {
public:
	GrpcServiceImpl(Shell *aglShell) : m_aglShell(aglShell) {}

	grpc::ServerUnaryReactor *ActivateApp(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::ActivateRequest* request,
			google::protobuf::Empty* /*response*/);

	grpc::ServerUnaryReactor *DeactivateApp(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::DeactivateRequest* request,
			google::protobuf::Empty* /*response*/);

	grpc::ServerUnaryReactor *SetAppSplit(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::SplitRequest* request,
			google::protobuf::Empty* /*response*/);

	grpc::ServerUnaryReactor *SetAppFloat(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::FloatRequest* request,
			google::protobuf::Empty* /*response*/);
	grpc::ServerUnaryReactor *AppStatusState(grpc::CallbackServerContext *context,
			google::protobuf::Empty *empty,
			::grpc::ServerWriter<::agl_shell_ipc::AppState>* writer);
private:
       Shell *m_aglShell;
};
