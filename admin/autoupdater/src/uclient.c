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

#include <stdio.h>


#define TIMEOUT_MSEC 300000

const char *const user_agent = "Gluon Autoupdater (using libuclient)";


static void request_done(struct uclient *cl) {
	uclient_disconnect(cl);
	uloop_end();
}


static void header_done_cb(struct uclient *cl) {
	static int retries;
	if (retries < 10) {
		int ret = uclient_http_redirect(cl);
		if (ret < 0) {
			fprintf(stderr, "autoupdater: error: failed to redirect to %s on %s\n", cl->url->location, cl->url->host);
			request_done(cl);
			return;
		}
		if (ret > 0) {
			retries++;
			return;
		}
	}
	retries = 0;

	if (cl->status_code != 200) {
		fprintf(stderr, "HTTP error %d\n", cl->status_code);
		request_done(cl);
	}
}


static void eof_cb(struct uclient *cl) {
	if (!cl->data_eof) {
		fputs("Connection reset prematurely\n", stderr);
	}
	request_done(cl);
}


static void handle_uclient_error(struct uclient *cl, int code) {
	const char *type;

	switch(code) {
	case UCLIENT_ERROR_CONNECT:
		type = "Connection failed";
		break;
	case UCLIENT_ERROR_TIMEDOUT:
		type = "Connection timed out";
		break;
	default:
		type = "Unknown error";
		break;
	}

	fprintf(stderr, "Connection error: %s\n", type);

	request_done(cl);
}


void get_url(const char *url, void (*read_cb)(struct uclient *cl)) {
	struct uclient_cb cb = {
		.header_done = header_done_cb,
		.data_read = read_cb,
		.data_eof = eof_cb,
		.error = handle_uclient_error,
	};

	struct uclient *cl = uclient_new(url, NULL, &cb);
	uclient_set_timeout(cl, TIMEOUT_MSEC);
	uclient_connect(cl);
	uclient_http_set_request_type(cl, "GET");
	uclient_http_reset_headers(cl);
	uclient_http_set_header(cl, "User-Agent", user_agent);
	uclient_request(cl);
	uloop_run();
	uclient_free(cl);
}
