#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "agl_shell.grpc.pb.h"
#include "agl-shell-client-protocol.h"

namespace {
       const char kDefaultGrpcServiceAddress[] = "127.0.0.1:14005";
}


class GrpcServiceImpl final : public agl_shell_ipc::AglShellManagerService::CallbackService {

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
};


class Shell {
public:
	std::shared_ptr<struct agl_shell> m_shell;
	Shell(std::shared_ptr<struct agl_shell> shell) : m_shell(shell) { }
	void ActivateApp(const std::string &app_id, const std::string &output_name);
	void DeactivateApp(const std::string &app_id);
	void SetAppSplit(const std::string &app_id, uint32_t orientation);
	void SetAppFloat(const std::string &app_id);

};
