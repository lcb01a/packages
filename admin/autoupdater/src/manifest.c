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


#include <libplatforminfo.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hexutil.h"
#include "manifest.h"


void free_manifest(struct manifest *m) {
	free(m->image_filename);
	m->image_filename = NULL;

	free(m->version);
	m->version = NULL;

	for (int i = 0; i < m->n_signatures; i++) {
		free(m->signatures[i]);
	}
	free(m->signatures);
	m->signatures = NULL;
}


void parse_line(char *line, struct manifest *m, const char *branch) {
	if (line == NULL)
		return;

	else if (!strcmp(line, "---")) {
		m->sep_found = true;
	}

	else if (m->sep_found) {
		ecdsa_signature_t *sig = malloc(sizeof(ecdsa_signature_t));
		if (!parsehex(sig, line, sizeof(*sig))) {
			free(sig);
			fprintf(stderr, "autoupdater: warning: garbage in signature area: %s\n", line);
			return;
		}
		m->n_signatures++;
		m->signatures = realloc(m->signatures, m->n_signatures * sizeof(ecdsa_signature_t));
		m->signatures[m->n_signatures - 1] = sig;
	}

	else {
		ecdsa_sha256_update(&m->hash_ctx, line, strlen(line));
		ecdsa_sha256_update(&m->hash_ctx, "\n", 1);

		if (!strncmp(line, "BRANCH=", 7) && !strcmp(&line[7], branch)) {
			m->branch_ok = true;
		}

		else if (!strncmp(line, "DATE=", 5)) {
			struct tm timestamp;
			char *rem = strptime(&line[5], "%Y-%m-%dT%H:%M:%S%z", &timestamp);
			if (rem != NULL && *rem == '\0')
				m->date = mktime(&timestamp);
		}

		else if (!strncmp(line, "PRIORITY=", 9)) {
			m->priority = strtof(&line[9], NULL);
		}

		else {
			char *model = strtok(line, " ");
			char *version = strtok(NULL, " ");
			char *checksum = strtok(NULL, " ");
			char *filename = strtok(NULL, " ");

			if (model == NULL || strcmp(model, platforminfo_get_image_name()))
				return;

			if (version == NULL || filename == NULL)
				return;

			if (checksum == NULL || !parsehex(m->image_hash, checksum, ECDSA_SHA256_HASH_SIZE))
				return;

			m->version = realloc(m->version, (strlen(version) + 1) * sizeof(char));
			if (m->version != NULL)
				strcpy(m->version, version);

			m->image_filename = realloc(m->image_filename, (strlen(filename) + 1) * sizeof(char));
			if (m->image_filename != NULL)
				strcpy(m->image_filename, filename);

		}
	}
}


void parse_manifest(const char *file, struct manifest *m, char *branch) {
	char *line = NULL;
	size_t len = NULL;
	ssize_t read;
	FILE f = fopen(file, "r");
	if (!f)
		return;

	while ((read = getline(&line, &len, f)) != -1)
		parse_line(line, m, branch);

	free(line);
	fclose(f);
}
