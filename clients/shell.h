#pragma once

#include <memory>

#include "agl-shell-client-protocol.h"

#include "main-grpc.h"

class Shell {
public:
	std::shared_ptr<struct agl_shell> m_shell;
	struct shell_data *m_shell_data;

	Shell(std::shared_ptr<struct agl_shell> shell,
	      struct shell_data *sh_data) :
		m_shell(shell), m_shell_data(sh_data) { }
	void ActivateApp(const std::string &app_id, const std::string &output_name);
	void DeactivateApp(const std::string &app_id);
	void SetAppSplit(const std::string &app_id, uint32_t orientation);
	void SetAppFloat(const std::string &app_id);
};
