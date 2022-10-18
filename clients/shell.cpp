#include <cstdio>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <string>
#include <queue>

#include "main-grpc.h"
#include "shell.h"

void
Shell::ActivateApp(const std::string &app_id, const std::string &output_name)
{
	struct window_output *woutput, *w_output;
	struct agl_shell *shell = this->m_shell.get();

	woutput = nullptr;
	w_output = nullptr;

	wl_list_for_each(woutput, &m_shell_data->output_list, link) {
		if (woutput->name && !strcmp(woutput->name, output_name.c_str())) {
			w_output = woutput;
			break;
		}
	}

	// else, get the first one available
	if (!w_output)
		w_output = wl_container_of(m_shell_data->output_list.prev,
					   w_output, link);

	agl_shell_activate_app(shell, app_id.c_str(), w_output->output);
	wl_display_flush(m_shell_data->wl_display);
}

void
Shell::DeactivateApp(const std::string &app_id)
{
	(void) app_id;
}

void
Shell::SetAppFloat(const std::string &app_id)
{
	(void) app_id;
}

void
Shell::SetAppSplit(const std::string &app_id, uint32_t orientation)
{
	(void) app_id;
	(void) orientation;
}
