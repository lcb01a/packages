/*
  Copyright (c) 2016, Jan-Philipp Litza <janphilipp@litza.de>
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


#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>


void run_dir(const char *dir) {
	DIR *dp = opendir(dir);
	if (dp == NULL) {
		fprintf(stderr, "autoupdater: warning: couldn't open directory %s: ", dir);
		perror(NULL);
		return;
	}

	int n_files = 0;
	int max_n_files = 8;
	char **filenames = malloc(max_n_files * sizeof(char*));
	struct dirent *file;
	while ((file = readdir(dp))) {
		if (file->d_name[0] == '.')
			continue;

		if (file->d_type != DT_REG && file->d_type != DT_UNKNOWN)
			continue;

		char buf[PATH_MAX];
		snprintf(buf, PATH_MAX, "%s/%s", dir, file->d_name);

		if (access(buf, X_OK))
			continue;

		if (n_files >= max_n_files) {
			max_n_files *= 2;
			filenames = realloc(filenames, max_n_files * sizeof(char*));
		}

		filenames[n_files] = malloc(strlen(file->d_name) * sizeof(char));
		strcpy(filenames[n_files], file->d_name);
		n_files++;
	}
	closedir(dp);

	qsort(filenames, n_files, sizeof(char*), (int (*)(const void *, const void *)) &strcmp);

	for (size_t i = 0; i < n_files; i++) {
		pid_t pid = fork();
		if (pid == 0) {
			int null_fd = open("/dev/null", O_RDWR);
			dup2(null_fd, 0);
			dup2(null_fd, 1);
			dup2(null_fd, 2);
			for (int i = 3; i < 65536; i++)
				close(i);
			char buf[PATH_MAX];
			snprintf(buf, PATH_MAX, "%s/%s", dir, filenames[i]);
			execl(buf, buf, (char *)NULL);
			fprintf(stderr, "autoupdater: warning: failed executing %s: ", buf);
			perror(NULL);
			exit(EXIT_FAILURE);
		} else if (waitpid(pid, NULL, 0) != pid) {
			fprintf(stderr, "autoupdater: warning: failed waiting for child %d: ", pid);
			perror(NULL);
		}
	}
}
