/*
  Copyright (c) 2017, Jan-Philipp Litza <janphilipp@litza.de>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "uclient.h"

#include <libubox/uloop.h>

#include <limits.h>
#include <stdio.h>


#define TIMEOUT_MSEC 300000

const char *const user_agent = "Gluon Autoupdater (using libuclient)";

struct uclient_data {
	int retries;
	int err_code;
};

#define uc_data(cl) ((struct uclient_data *)cl->priv)


enum uclient_own_error_code {
	UCLIENT_ERROR_REDIRECT_FAILED = 32,
	UCLIENT_ERROR_TOO_MANY_REDIRECTS,
	UCLIENT_ERROR_CONNECTION_RESET_PREMATURELY,
	UCLIENT_ERROR_STATUS_CODE = 1024,
};


const char *uclient_get_errmsg(int code) {
	static char http_code_errmsg[16];
	if (code & UCLIENT_ERROR_STATUS_CODE) {
		snprintf(http_code_errmsg, 16, "HTTP error %d",
			code ^ UCLIENT_ERROR_STATUS_CODE);
		return http_code_errmsg;
	}
	switch(code) {
	case UCLIENT_ERROR_CONNECT:
		return "Connection failed";
		break;
	case UCLIENT_ERROR_TIMEDOUT:
		return "Connection timed out";
		break;
	case UCLIENT_ERROR_REDIRECT_FAILED:
		return "Failed to redirect";
		break;
	case UCLIENT_ERROR_TOO_MANY_REDIRECTS:
		return "Too many redirects";
		break;
	case UCLIENT_ERROR_CONNECTION_RESET_PREMATURELY:
		return "Connection reset prematurely";
	default:
		return "Unknown error";
		break;
	}
}


static void request_done(struct uclient *cl, int err_code) {
	uc_data(cl)->err_code = err_code;
	uclient_disconnect(cl);
	uloop_end();
}


static void header_done_cb(struct uclient *cl) {
	if (uc_data(cl)->retries < 10) {
		int ret = uclient_http_redirect(cl);
		if (ret < 0) {
			request_done(cl, UCLIENT_ERROR_REDIRECT_FAILED);
			return;
		}
		if (ret > 0) {
			uc_data(cl)->retries++;
			return;
		}
	}
	uc_data(cl)->retries = 0;

	switch (cl->status_code) {
	case 200:
		break;
	case 301:
	case 302:
	case 307:
		request_done(cl, UCLIENT_ERROR_TOO_MANY_REDIRECTS);
		break;
	default:
		request_done(cl, UCLIENT_ERROR_STATUS_CODE | cl->status_code);
	}
}


static void eof_cb(struct uclient *cl) {
	request_done(cl, cl->data_eof? 0 : UCLIENT_ERROR_CONNECTION_RESET_PREMATURELY);
}


int get_url(const char *url, void (*read_cb)(struct uclient *cl)) {
	struct uclient_data d = { };
	struct uclient_cb cb = {
		.header_done = header_done_cb,
		.data_read = read_cb,
		.data_eof = eof_cb,
		.error = request_done,
	};

	struct uclient *cl = uclient_new(url, NULL, &cb);
	cl->priv = &d;
	uclient_set_timeout(cl, TIMEOUT_MSEC);
	uclient_connect(cl);
	uclient_http_set_request_type(cl, "GET");
	uclient_http_reset_headers(cl);
	uclient_http_set_header(cl, "User-Agent", user_agent);
	uclient_request(cl);
	uloop_run();
	uclient_free(cl);

	return d.err_code;
}
