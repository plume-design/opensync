/*
 * Copyright (c) 2018  Joachim Nilsson <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "test_mdnsd.h"
#include "util.h"

struct conf_srec {
	char   *type;

	char   *name;
	int     port;

	char   *target;
	char   *cname;

	char   *txt[42];
	size_t  txt_num;
};


static char *chomp(char *str)
{
	char *p;

	if (!str || strlen(str) < 1) {
		errno = EINVAL;
		return NULL;
	}

	p = str + strlen(str) - 1;
        while (*p == '\n')
		*p-- = 0;

	return str;
}

static int match(char *key, char *token)
{
	return !strcmp(key, token);
}

static void read_line(char *line, struct conf_srec *srec)
{
	char *arg, *token;

	arg = chomp(line);
	DBG("Got line: '%s'", line);

	if (line[0] == '#')
		return;

	token = strsep(&arg, " \t");
	if (!token || !arg) {
		DBG("Skipping, token:%s, arg:%s", token, arg);
		return;
	}

	if (match(token, "type"))
		srec->type = strdup(arg);
	if (match(token, "name"))
		srec->name = strdup(arg);
	if (match(token, "port"))
		srec->port = atoi(arg);
	if (match(token, "target"))
		srec->target = strdup(arg);
	if (match(token, "cname"))
		srec->cname = strdup(arg);
	if (match(token, "txt") && srec->txt_num < NELEMS(srec->txt))
		srec->txt[srec->txt_num++] = strdup(arg);
}

static int parse(char *fn, struct conf_srec *srec)
{
	FILE *fp;
	char line[256];

	DBG("Attempting to read %s ...", fn);
	fp = fopen(fn, "r");
	if (!fp)
		return 1;

	while (fgets(line, sizeof(line), fp))
		read_line(line, srec);
	fclose(fp);
	DBG("Finished reading %s ...", fn);

	return 0;
}

static int load(mdns_daemon_t *d, char *path, char *hostname)
{
	struct conf_srec srec;
	unsigned char *packet;
	mdns_record_t *r;
	size_t i;
	xht_t *h;
	char hlocal[256], nlocal[256], tlocal[256];
	int len = 0;

	memset(&srec, 0, sizeof(srec));
	if (parse(path, &srec)) {
		ERR("Failed reading %s: %s", path, strerror(errno));
		return 1;
	}

	if (!srec.name)
		srec.name = hostname;
	if (!srec.type)
		srec.type = strdup("_http._tcp");

	snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", srec.name, srec.type);
	snprintf(nlocal, sizeof(nlocal), "%s.local.", srec.name);
	snprintf(tlocal, sizeof(tlocal), "%s.local.", srec.type);
	if (!srec.target)
		srec.target = strdup(hlocal);
	/* Announce that we have a $type service */
	r = mdnsd_set_record(d, 1, tlocal, DISCO_NAME, QTYPE_PTR, 120, mdnsd_conflict, NULL);
	r = mdnsd_set_record(d, 1, srec.target, tlocal, QTYPE_PTR, 120, mdnsd_conflict, NULL);

	r = mdnsd_set_record(d, 0, NULL, hlocal, QTYPE_SRV, 120, mdnsd_conflict, NULL);
	mdnsd_set_srv(d, r, 0, 0, srec.port, nlocal);

	r = mdnsd_set_record(d, 0, NULL, nlocal, QTYPE_A, 120, mdnsd_conflict, NULL);
	mdnsd_set_ip(d, r, mdnsd_get_address(d));

	if (srec.cname)
		r = mdnsd_set_record(d, 1, srec.cname, nlocal, QTYPE_CNAME, 120, mdnsd_conflict, NULL);
	r = mdnsd_set_record(d, 0, NULL, hlocal, QTYPE_TXT, 45,  mdnsd_conflict, NULL);
	h = xht_new(11);
	for (i = 0; i < srec.txt_num; i++) {
		char *ptr;

		ptr = strchr(srec.txt[i], '=');
		if (!ptr)
			continue;
		*ptr++ = 0;

		xht_set(h, srec.txt[i], ptr);
	}
	packet = sd2txt(h, &len);
	xht_free(h);
	mdnsd_set_raw(d, r, (char *)packet, len);
	free(packet);

	return 0;
}

int conf_init(mdns_daemon_t *d, char *path)
{
	struct stat st;
	char hostname[HOST_NAME_MAX];
	int rc = 0;

	gethostname(hostname, sizeof(hostname));

	if (stat(path, &st)) {
		if (ENOENT == errno)
			ERR("No such file or directory: %s", path);
		else
			ERR("Cannot determine path type: %s", strerror(errno));
		return 1;
	}

	if (S_ISDIR(st.st_mode)) {
		glob_t gl;
		size_t i;
		char pat[strlen(path) + 64];
		int flags = GLOB_ERR;

		STRSCPY(pat, path);
		if (pat[strlen(path) - 1] != '/')
			strcat(pat, "/");
		strcat(pat, "*.service");

#ifdef GLOB_TILDE
		/* E.g. musl libc < 1.1.21 does not have this GNU LIBC extension  */
		flags |= GLOB_TILDE;
#else
		/* Simple homegrown replacement that at least handles leading ~/ */
		if (!strncmp(pat, "~/", 2)) {
			const char *home;

			home = getenv("HOME");
			if (home) {
				memmove(pat + strlen(home), pat, strlen(path));
				memcpy(pat, home, strlen(home));
			}
		}
#endif

		if (glob(pat, flags, NULL, &gl)) {
			ERR("No .service files found in %s", pat);
			return 1;
		}

		for (i = 0; i < gl.gl_pathc; i++)
			rc |= load(d, gl.gl_pathv[i], hostname);

		globfree(&gl);
	} else
		rc |= load(d, path, hostname);

	return rc;
}

