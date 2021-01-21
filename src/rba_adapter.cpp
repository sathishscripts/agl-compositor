/*
 * Copyright (c) 2020 DENSO CORPORATION.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string>
#include <iostream>
#include <unistd.h>

#include "rba_adapter.h"
#include <libweston/libweston.h>

#include "RBAJsonParser.hpp"
#include "RBAArbitrator.hpp"
using namespace std;

#define JSONFILE "/etc/rba/RBAModel.json"
rba::RBAJsonParser parser;
rba::RBAModel* model = nullptr;
rba::RBAArbitrator* arb = nullptr;
unique_ptr<rba::RBAResult> result = nullptr;

bool rba_adapter_initialize(void)
{
	if (arb == nullptr) {
		if (access(JSONFILE, F_OK) == -1) {
			weston_log("Unable to find %s file!!\n", JSONFILE);
			return false;
		}
		model = parser.parse(JSONFILE);
		if (model == nullptr) {
			weston_log("RBAmodel is NULL\n");
			return false;
		}
		arb = new rba::RBAArbitrator(model);
		if (arb == nullptr) {
			weston_log("RBAArbitrator is NULL\n");
			return false;
		}
		return true;
	}
	weston_log("RBAArbitrator model is already created\n");
	return true;
}

bool rba_adapter_arbitrate(const char *app_id)
{
	string id(app_id);

	result = arb->execute(id+ "/NORMAL", true);

	if (result->getStatusType() == rba::RBAResultStatusType::UNKNOWN_CONTENT_STATE) {
		weston_log("ERROR: Unknown context app: %s\n", app_id);
		return false;
	}
	if (result->getStatusType() == rba::RBAResultStatusType::FAILED ||
	    result->getStatusType() == rba::RBAResultStatusType::CANCEL_ERROR) {
		weston_log("ERROR: execution failed or cancel for app: %s\n", app_id);
		return false;
	}
	return true;
}
