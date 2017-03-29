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


#include "util.h"

#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>


void run_dir(const char *dir) {
	char pat[strlen(dir) + 3];
	sprintf(pat, "%s/*", dir);
	glob_t globbuf;
	if (glob(pat, 0, NULL, &globbuf))
		return;

	for (char **path = globbuf.gl_pathv; path != NULL; path++) {
		pid_t pid = fork();
		if (pid < 0) {
			fputs("autoupdater: warning: failed to fork: ", stderr);
			perror(NULL);
		} if (pid == 0) {
			int null_fd = open("/dev/null", O_RDWR);
			dup2(null_fd, 0);
			dup2(null_fd, 1);
			dup2(null_fd, 2);
			close(null_fd);
			execl(*path, *path, (char *)NULL);
			fprintf(stderr, "autoupdater: warning: failed executing %s: ", *path);
			perror(NULL);
			exit(EXIT_FAILURE);
		} else if (waitpid(pid, NULL, 0) != pid) {
			fprintf(stderr, "autoupdater: warning: failed waiting for child %d: ", pid);
			perror(NULL);
		}
	}

	globfree(&globbuf);
}


void randomize(void) {
	struct timespec tv;
	if (clock_gettime(CLOCK_MONOTONIC, &tv)) {
		fprintf(stderr, "autoupdater: error: clock_gettime:");
		perror(NULL);
		exit(1);
	}

	srandom(tv.tv_nsec);
}


float get_uptime(void) {
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
