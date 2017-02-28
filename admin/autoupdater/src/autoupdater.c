/*
  Copyright (c) 2017, Matthias Schiffer <mschiffer@universe-factory.net>
                      Jan-Philipp Litza <janphilipp@litza.de>
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


#include "manifest.h"
#include "settings.h"
#include "uclient.h"
#include "util.h"
#include "version.h"

#include <libplatforminfo.h>
#include <libubox/uloop.h>
#include <ecdsautil/ecdsa.h>
#include <ecdsautil/sha256.h>

#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>


#define MAX_LINE_LENGTH 512
#define STRINGIFY(str) #str

const char *const download_d_dir = "/usr/lib/autoupdater/download.d";
const char *const abort_d_dir = "/usr/lib/autoupdater/abort.d";
const char *const upgrade_d_dir = "/usr/lib/autoupdater/upgrade.d";
const char *const lockfile = "/var/run/autoupdater.lock";
const char *const firmware_path = "/tmp/firmware.bin";
const char *const sysupgrade_path = "/sbin/sysupgrade";


struct recv_manifest_ctx {
	struct settings *s;
	struct manifest m;
	char buf[MAX_LINE_LENGTH + 1];
	char *ptr;
};

struct recv_image_ctx {
	int fd;
	ecdsa_sha256_context_t hash_ctx;
};


static void usage(void) {
	fputs("\n"
		"Usage: autoupdater [options] [<mirror> ...]\n\n"
		"Possible options are:\n"
		"  -b, --branch BRANCH  Override the branch given in the configuration.\n\n"
		"  -f, --force          Always upgrade to a new version, ignoring its priority\n"
		"                       and whether the autoupdater even is enabled.\n\n"
		"  -h, --help           Show this help.\n\n"
		"  --fallback           Upgrade if and only if the upgrade timespan of the new\n"
		"                       version has passed for at least 24 hours.\n\n"
		"  <mirror> ...         Override the mirror URLs given in the configuration. If\n"
		"                       specified, these are not shuffled.\n\n",
		stderr
	);
}


static void parse_args(int argc, char *argv[], struct settings *settings) {
	enum option_values {
		OPTION_BRANCH = 'b',
		OPTION_FORCE = 'f',
		OPTION_HELP = 'h',
		OPTION_FALLBACK = 256,
	};

	const struct option options[] = {
		{"branch",   required_argument, NULL, OPTION_BRANCH},
		{"force",    no_argument,	NULL, OPTION_FORCE},
		{"fallback", no_argument,	NULL, OPTION_FALLBACK},
		{"help",     no_argument,	NULL, OPTION_HELP},
	};

	while (true) {
		int c = getopt_long(argc, argv, "b:fh", options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case OPTION_BRANCH:
			settings->branch = optarg;
			break;

		case OPTION_FORCE:
			settings->force = true;
			break;

		case OPTION_FALLBACK:
			settings->fallback = true;
			break;

		case OPTION_HELP:
			usage();
			exit(0);

		default:
			usage();
			exit(1);
		}
	}

	if (optind < argc) {
		settings->n_mirrors = argc - optind;
		settings->mirrors = malloc(settings->n_mirrors * sizeof(char *));
		for (int i = optind; i < argc; i++) {
			settings->mirrors[i - optind] = argv[i];
		}
	}
}


static void randomize(void) {
	struct timespec tv;
	if (clock_gettime(CLOCK_MONOTONIC, &tv)) {
		fprintf(stderr, "autoupdater: error: clock_gettime: %m\n");
		exit(1);
	}

	srandom(tv.tv_nsec);
}


static float get_uptime(void) {
	FILE *f = fopen("/proc/uptime", "r");
	if (f) {
		float uptime;
		int match = fscanf(f, "%f", &uptime);
		fclose(f);

		if (match == 1)
			return uptime;
	}

	fputs("autoupdater: error: unable to determine uptime\n", stderr);
	exit(1);
}


static float get_probability(time_t date, float priority, bool fallback) {
	float seconds = priority * 86400;
	time_t diff = time(NULL) - date;

	if (diff < 0) {
		/*
		 When the difference is negative, there are two possibilities: the
		 manifest contains an incorrect date, or our own clock is wrong. As there
		 isn't anything sensible to do for an incorrect manifest, we'll assume
		 the latter is the case and update anyways as we can't do anything better
		*/
		fputs("autoupdater: warning: clock seems to be incorrect.\n", stderr);

		if (get_uptime() < 600)
			/*
			 If the uptime is very low, it's possible we just didn't get the
			 time over NTP yet, so we'll just wait until the next time the
			 updater runs
			*/
			return 0;
		else
			/*
			 Will give 1 when priority == 0, and lower probabilities the higher
			 the priority value is (similar to the old static probability system)
			*/
			return powf(0.75f, priority);
	}
	else if (fallback) {
		if (diff >= seconds + 86400)
			return 1;
		else
			return 0;
	}
	else if (diff >= seconds) {
		return 1;
	}
	else {
		float x = diff/seconds;

		/*
		 This is the simplest polynomial with value 0 at 0, 1 at 1, and which has a
		 first derivative of 0 at both 0 and 1 (we all love continuously differentiable
		 functions, right?)
		*/
		return 3*x*x - 2*x*x*x;
	}
}


/** Receives data from uclient, chops it to lines and hands it to \ref parse_line */
static void recv_manifest_cb(struct uclient *cl) {
	struct recv_manifest_ctx *ctx = uclient_get_custom(cl);
	char *newline;
	int len;

	while (true) {
		if (ctx->ptr - ctx->buf == MAX_LINE_LENGTH) {
			fputs("autoupdater: error: encountered manifest line exceeding limit of " STRINGIFY(MAX_LINE_LENGTH) " characters\n", stderr);
			break;
		}
		len = uclient_read(cl, ctx->ptr, MAX_LINE_LENGTH - (ctx->ptr - ctx->buf));
		if (len <= 0)
			break;
		ctx->ptr[len] = '\0';

		char *line = ctx->buf;
		while (true) {
			newline = strchr(line, '\n');
			if (newline == NULL)
				break;
			*newline = '\0';

			parse_line(line, &ctx->m, ctx->s->branch, platforminfo_get_image_name());
			line = newline + 1;
		}

		// Move the beginning of the next line to the beginning of the
		// buffer. We cannot use strcpy here because the memory areas
		// might overlap!
		int n = strlen(line);
		memmove(ctx->buf, line, n);
		ctx->ptr = ctx->buf + n;
	}
}


/** Receives data from uclient and writes it to file */
static void recv_image_cb(struct uclient *cl) {
	struct recv_image_ctx *ctx = uclient_get_custom(cl);
	char buf[1024];
	int len;

	while (true) {
		len = uclient_read(cl, buf, sizeof(buf));
		if (!len)
			return;

		if (write(ctx->fd, buf, len) < len) {
			fputs("autoupdater: error: downloading firmware image failed: ", stderr);
			perror(NULL);
			return;
		}
		ecdsa_sha256_update(&ctx->hash_ctx, buf, len);
	}
}


static bool autoupdate(const char *mirror, struct settings *s) {
	struct recv_manifest_ctx manifest_ctx = { .s = s };
	manifest_ctx.ptr = manifest_ctx.buf;
	struct manifest *m = &manifest_ctx.m;

	/**** Get and check manifest *****************************************/
	/* Construct manifest URL */
	char manifest_url[strlen(mirror) + strlen(s->branch) + 11];
	sprintf(manifest_url, "%s/%s.manifest", mirror, s->branch);

	/* Download manifest */
	ecdsa_sha256_init(&m->hash_ctx);
	int err_code = get_url(manifest_url, recv_manifest_cb, &manifest_ctx);
	if (err_code != 0) {
		fprintf(stderr, "autoupdater: warning: error downloading manifest: %s\n", uclient_get_errmsg(err_code));
		return false;
	}

	/* Check manifest */
	if (!m->branch_ok) {
		fprintf(stderr, "autoupdater: warning: manifest %s is not for branch %s\n", manifest_url, s->branch);
		return false;
	}

	if (!m->image_filename || !m->version) {
		fprintf(stderr, "autoupdater: warning: no matching firmware found (model %s)\n", platforminfo_get_image_name());
		return false;
	}

	/* Check manifest signatures */
	{
		ecc_int256_t hash;
		ecdsa_sha256_final(&m->hash_ctx, hash.p);
		ecdsa_verify_context_t ctxs[m->n_signatures];
		for (size_t i = 0; i < m->n_signatures; i++)
			ecdsa_verify_prepare_legacy(&ctxs[i], &hash, m->signatures[i]);

		long unsigned int good_signatures = ecdsa_verify_list_legacy(ctxs, m->n_signatures, s->pubkeys, s->n_pubkeys);
		if (good_signatures < s->good_signatures) {
			fprintf(stderr, "autoupdater: warning: manifest %s only carried %lu valid signatures, %lu are required\n", manifest_url, good_signatures, s->good_signatures);
			return false;
		}
	}

	/* Check version and update probability */
	if (!newer_than(m->version, s->old_version)) {
		puts("No new firmware available.");
		return true;
	}

	if (!s->force && random() >= RAND_MAX * get_probability(m->date, m->priority, s->fallback)) {
		fputs("autoupdater: info: no autoupdate this time. Use -f to override.\n", stderr);
		return true;
	}

	/**** Download and verify image file *********************************/
	/* Begin download of the image */
	run_dir(download_d_dir);

	struct recv_image_ctx image_ctx = { };
	image_ctx.fd = open(firmware_path, O_WRONLY|O_CREAT, 0600);
	if (image_ctx.fd < 0) {
		fprintf(stderr, "autoupdater: error: failed opening firmware file %s\n", firmware_path);
		goto fail_after_download;
	}

	/* Download image and calculate SHA256 checksum */
	{
		char image_url[strlen(mirror) + strlen(m->image_filename) + 2];
		sprintf(image_url, "%s/%s", mirror, m->image_filename);
		ecdsa_sha256_init(&image_ctx.hash_ctx);
		int err_code = get_url(image_url, &recv_image_cb, &image_ctx);
		if (err_code != 0) {
			fprintf(stderr, "autoupdater: warning: error downloading image: %s\n", uclient_get_errmsg(err_code));
			close(image_ctx.fd);
			goto fail_after_download;
		}
	}
	close(image_ctx.fd);

	/* Verify image checksum */
	{
		ecc_int256_t hash;
		ecdsa_sha256_final(&image_ctx.hash_ctx, hash.p);
		if (memcmp(hash.p, m->image_hash, ECDSA_SHA256_HASH_SIZE)) {
			fputs("autoupdater: warning: invalid image checksum!\n", stderr);
			goto fail_after_download;
		}
	}

	/**** Call sysupgrade ************************************************/
	/* Begin upgrade */
	run_dir(upgrade_d_dir);

	/* Close stdin and stderr to not give sysupgrade unneccessary SIGHUPs */
	{
		int null_fd = open("/dev/null", O_RDWR);
		dup2(null_fd, 0);
		dup2(null_fd, 2);
		close(null_fd);
	}

	execl(sysupgrade_path, sysupgrade_path, firmware_path, NULL);

	/* execl() shouldn't return */
	fputs("autoupdater: error: failed to call sysupgrade\n", stderr);

fail_after_download:
	unlink(firmware_path);
	run_dir(abort_d_dir);
	return false;
}


static bool lock_autoupdater(void) {
	int fd = open(lockfile, O_CREAT|O_RDONLY, 0666);
	if (fd < 0) {
		fprintf(stderr, "autoupdater: error: unable to open lock file: %m\n");
		return false;
	}

	if (flock(fd, LOCK_EX|LOCK_NB)) {
		fputs("autoupdater: error: another instance is currently running\n", stderr);
		return false;
	}
	return true;
}


int main(int argc, char *argv[]) {
	struct settings s = { };
	parse_args(argc, argv, &s);

	if (!platforminfo_get_image_name()) {
		fputs("autoupdater: error: unsupported hardware model\n", stderr);
		return EXIT_FAILURE;
	}

	bool external_mirrors = s.n_mirrors > 0;
	load_settings(&s);
	randomize();

	if (!lock_autoupdater())
		return EXIT_FAILURE;

	uloop_init();

	size_t mirrors_left = s.n_mirrors;
	while (mirrors_left) {
		const char **mirror = s.mirrors;
		size_t i = external_mirrors? 0 : random() % mirrors_left;

		/* Move forward by i non-NULL entries */
		while (true) {
			while (!*mirror)
				mirror++;

			if (!i)
				break;

			mirror++;
			i--;
		}

		if (autoupdate(*mirror, &s))
			return EXIT_SUCCESS;

		/* When the update has failed, remove the mirror from the list */
		*mirror = NULL;
		mirrors_left--;
	}

	uloop_done();

	fputs("autoupdater: error: no usable mirror found\n", stderr);
	return EXIT_FAILURE;
}
