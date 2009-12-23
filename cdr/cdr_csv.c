/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Comma Separated Value CDR records.
 * 
 * \arg See also \ref cwCDR
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"


static const char name[] = "csv";
static const char desc[] = "Comma Separated Values CDR Backend";


#define CSV_LOG_DIR "/cdr-csv"
#define CSV_MASTER  "/Master.csv"

#define DATE_FORMAT "%Y-%m-%d %T"
#define MAX_DATE_LEN sizeof("YYYY-mm-dd HH:MM:SS")

/* #define CSV_LOGUNIQUEID 1 */
/* #define CSV_LOGUSERFIELD 1 */


static pthread_mutex_t csv_lock = PTHREAD_MUTEX_INITIALIZER;

static char csvmaster_path[CW_CONFIG_MAX_PATH];
static FILE *csvmaster_fd;

static char csvacct_path[CW_CONFIG_MAX_PATH];
static int csvacct_offset;

static char *gbuf;
static size_t gbufsize;
#define atleast(n)	((n + 255) / 256)


static int expand(size_t n)
{
	char *nbuf;

	if ((nbuf = realloc(gbuf, gbufsize + n))) {
		gbuf = nbuf;
		gbufsize += n;
		return 0;
	}

	return -1;
}


static int append_string(size_t *pos, char *buf, size_t bufsize, const char *s)
{
	int res = -1;

	if (bufsize - *pos < 3 && expand(atleast(3)))
		goto out;

	buf[(*pos)++] = '\"';

	for (; *s; s++) {
		if (bufsize - *pos < 2 + 2 && expand(atleast(2)))
			goto out;
		if ((buf[(*pos)++] = *s) == '\"')
			buf[(*pos)++] = '\"';
	}

	buf[*pos + 0] = '\"';
	buf[*pos + 1] = ',';
	*pos += 2;

	res = 0;

out:
	return res;
}


static int append_times(size_t *pos, char *buf, size_t bufsize, const struct cw_cdr *cdr)
{
	int res = -1;
	int n;

	for (;;) {
		n = snprintf(buf + *pos, bufsize - *pos, "%d,%d,", cdr->duration, cdr->billsec);
		if (bufsize - *pos >= n + 1)
			break;
		if (expand(atleast(n + 1)))
			goto out;
	}

	*pos += n;
	res = 0;

out:
	return res;
}


static int append_date(size_t *pos, char *buf, size_t bufsize, const struct timeval tv)
{
	struct tm tm;
	int res = -1;

	if (!cw_tvzero(tv)) {
		if (bufsize - *pos < MAX_DATE_LEN + 1 && expand(atleast(MAX_DATE_LEN + 1)))
			goto out;
		localtime_r(&tv.tv_sec, &tm);
		*pos += strftime(buf + *pos, bufsize - *pos, DATE_FORMAT ",", &tm);
	}

	res = 0;

out:
	return res;
}


static char *build_csv_record(size_t *pos, const struct cw_cdr *cdr)
{
	*pos = 0;

	if (!append_string(pos, gbuf, gbufsize, cdr->accountcode)
	&& !append_string(pos, gbuf, gbufsize, cdr->src)
	&& !append_string(pos, gbuf, gbufsize, cdr->dst)
	&& !append_string(pos, gbuf, gbufsize, cdr->dcontext)
	&& !append_string(pos, gbuf, gbufsize, cdr->clid)
	&& !append_string(pos, gbuf, gbufsize, cdr->channel)
	&& !append_string(pos, gbuf, gbufsize, cdr->dstchannel)
	&& !append_string(pos, gbuf, gbufsize, cdr->lastapp)
	&& !append_string(pos, gbuf, gbufsize, cdr->lastdata)
	&& !append_date(pos, gbuf, gbufsize, cdr->start)
	&& !append_date(pos, gbuf, gbufsize, cdr->answer)
	&& !append_date(pos, gbuf, gbufsize, cdr->end)
	&& !append_times(pos, gbuf, gbufsize, cdr)
	&& !append_string(pos, gbuf, gbufsize, cw_cdr_disp2str(cdr->disposition))
	&& !append_string(pos, gbuf, gbufsize, cw_cdr_flags2str(cdr->amaflags))
#ifdef CSV_LOGUNIQUEID
	&& !append_string(pos, gbuf, gbufsize, cdr->uniqueid)
#endif
#ifdef CSV_LOGUSERFIELD
	&& !append_string(pos, gbuf, gbufsize, cdr->userfield)
#endif
	) {
		/* Trim off trailing comma */
		gbuf[*pos - 1] = '\n';
		return gbuf;
	}

	return NULL;
}


static int csvmaster_open(void)
{
	static dev_t dev;
	static ino_t ino;
	struct stat st;
	int d;

	if (csvmaster_fd) {
		if (!stat(csvmaster_path, &st) && st.st_dev == dev && st.st_ino == ino)
			return 0;

		fclose(csvmaster_fd);
	}

	if ((d = open_cloexec(csvmaster_path, O_WRONLY|O_APPEND|O_CREAT, 0666)) >= 0
	&& (csvmaster_fd = fdopen(d, "a")))
		return 0;

	cw_log(CW_LOG_ERROR, "%s: %s\n", csvmaster_path, strerror(errno));

	if (d >= 0)
		close(d);

	return -1;
}


static int csv_log(struct cw_cdr *batch)
{
	struct cw_cdr *cdrset, *cdr;
	char *buf;
	size_t len;

	pthread_mutex_lock(&csv_lock);

	if (!csvmaster_open()) {
		while ((cdrset = batch)) {
			batch = batch->batch_next;

			while ((cdr = cdrset)) {
				cdrset = cdrset->next;

				if ((buf = build_csv_record(&len, cdr))) {
					fwrite(buf, len, 1, csvmaster_fd);

					if (!cw_strlen_zero(cdr->accountcode)) {
						static char badacct = 0;
						static char toolong = 0;
						static char acctfailed = 0;
						int d, err;

						if (!strchr(cdr->accountcode, '/') && (cdr->accountcode[0] != '.' || cdr->accountcode[1] != '.')) {
							if (snprintf(csvacct_path + csvacct_offset, sizeof(csvacct_path) - csvacct_offset, "%s.csv", cdr->accountcode) < sizeof(csvacct_path) - csvacct_offset) {
								if (!(err = ((d = open_cloexec(csvacct_path, O_WRONLY|O_APPEND|O_CREAT, 0666)) < 0))) {
									err = (write(d, buf, len) != len);
									err |= fsync(d);
									err |= close(d);
								}
								if (err && !acctfailed) {
									cw_log(CW_LOG_ERROR, "%s: %s (further similar messages suppressed)\n", csvacct_path, strerror(errno));
									acctfailed = 1;
								}
							} else if (!toolong) {
								cw_log(CW_LOG_ERROR, "Account specific CSV file path for \"%s\" is too long (further similar messages suppressed)\n", cdr->accountcode);
								toolong = 1;
							}
						} else if (!badacct) {
							cw_log(CW_LOG_ERROR, "Account code \"%s\" insecure for writing CSV file (further similar messages suppressed)\n", cdr->accountcode);
							badacct = 1;
						}
					}
				}
			}
		}

		fflush(csvmaster_fd);
		fsync(fileno(csvmaster_fd));
	}

	pthread_mutex_unlock(&csv_lock);

	return 0;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = csv_log,
};


static void release_module(void)
{
	if (csvmaster_fd)
		fclose(csvmaster_fd);
}


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}


static int load_module(void)
{
	if (!(gbuf = malloc(1024))) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		return -1;
	}
	gbufsize = 1024;

	snprintf(csvmaster_path, sizeof(csvmaster_path), "%s/%s/%s", cw_config_CW_LOG_DIR, CSV_LOG_DIR, CSV_MASTER);
	csvacct_offset = snprintf(csvacct_path, sizeof(csvacct_path), "%s/%s/", cw_config_CW_LOG_DIR, CSV_LOG_DIR);

	csvmaster_open();

	cw_cdrbe_register(&cdrbe);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, release_module, desc)
