#pragma once

#include <memory>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <mutex>
#include <condition_variable>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "shell.h"
#include "agl_shell.grpc.pb.h"

namespace {
       const char kDefaultGrpcServiceAddress[] = "127.0.0.1:14005";
}

class Lister : public grpc::ServerWriteReactor<::agl_shell_ipc::AppState> {
public:
	Lister(Shell *aglShell);
	void OnDone() override;
	void OnWriteDone(bool ok) override;
	void NextWrite(void);
private:
	Shell *m_shell;
};

class GrpcServiceImpl final : public agl_shell_ipc::AglShellManagerService::CallbackService {
public:
	GrpcServiceImpl(Shell *aglShell) : m_aglShell(aglShell) {}

	grpc::ServerUnaryReactor *ActivateApp(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::ActivateRequest* request,
			google::protobuf::Empty* /*response*/) override;

	grpc::ServerUnaryReactor *DeactivateApp(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::DeactivateRequest* request,
			google::protobuf::Empty* /*response*/) override;

	grpc::ServerUnaryReactor *SetAppSplit(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::SplitRequest* request,
			google::protobuf::Empty* /*response*/) override;

	grpc::ServerUnaryReactor *SetAppFloat(grpc::CallbackServerContext *context,
			const ::agl_shell_ipc::FloatRequest* request,
			google::protobuf::Empty* /*response*/) override;

	grpc::ServerWriteReactor< ::agl_shell_ipc::AppState>* AppStatusState(
	      ::grpc::CallbackServerContext* /*context*/,
	      const ::google::protobuf::Empty* /*request*/)  override;
private:
       Shell *m_aglShell;

       std::mutex m_done_mutex;
       std::condition_variable m_done_cv;
       bool m_done = false;

};
