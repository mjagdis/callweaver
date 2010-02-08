/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2010, Eris Associates Limited, UK
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


static void append_string(struct cw_dynstr **ds_p, const char *s)
{
	cw_dynstr_printf(ds_p, "\"");

	while (*s) {
		int n = strcspn(s, "\"");
		cw_dynstr_printf(ds_p, "%.*s", n, s);
		if (!s[n])
			break;
		cw_dynstr_printf(ds_p, "\"\"");
		s += n + 1;
	}

	cw_dynstr_printf(ds_p, "\",");
}


static void append_times(struct cw_dynstr **ds_p, const struct cw_cdr *cdr)
{
	cw_dynstr_printf(ds_p, "%d,%d,", cdr->duration, cdr->billsec);
}


static void append_date(struct cw_dynstr **ds_p, const struct timeval tv)
{
	struct tm tm;

	if (!cw_tvzero(tv)) {
		cw_dynstr_need(ds_p, MAX_DATE_LEN + sizeof(",") - 1);
		localtime_r(&tv.tv_sec, &tm);
		(*ds_p)->used += strftime((*ds_p)->data + (*ds_p)->used, (*ds_p)->size - (*ds_p)->used, DATE_FORMAT ",", &tm);
	}
}


static void build_csv_record(struct cw_dynstr **ds_p, const struct cw_cdr *cdr)
{
	append_string(ds_p, cdr->accountcode);
	append_string(ds_p, cdr->src);
	append_string(ds_p, cdr->dst);
	append_string(ds_p, cdr->dcontext);
	append_string(ds_p, cdr->clid);
	append_string(ds_p, cdr->channel);
	append_string(ds_p, cdr->dstchannel);
	append_string(ds_p, cdr->lastapp);
	append_string(ds_p, cdr->lastdata);
	append_date(ds_p, cdr->start);
	append_date(ds_p, cdr->answer);
	append_date(ds_p, cdr->end);
	append_times(ds_p, cdr);
	append_string(ds_p, cw_cdr_disp2str(cdr->disposition));
	append_string(ds_p, cw_cdr_flags2str(cdr->amaflags));
#ifdef CSV_LOGUNIQUEID
	append_string(ds_p, cdr->uniqueid);
#endif
#ifdef CSV_LOGUSERFIELD
	append_string(ds_p, cdr->userfield);
#endif

	/* Replace trailing comma with a newline */
	if ((*ds_p) && !(*ds_p)->error)
		(*ds_p)->data[(*ds_p)->used - 1] = '\n';
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
	struct cw_dynstr *ds = NULL;

	pthread_mutex_lock(&csv_lock);

	if (!csvmaster_open()) {
		while ((cdrset = batch)) {
			batch = batch->batch_next;

			while ((cdr = cdrset)) {
				build_csv_record(&ds, cdr);

				if (ds && !ds->error) {
					cdrset = cdrset->next;

					fwrite(ds->data, 1, ds->used, csvmaster_fd);

					if (!cw_strlen_zero(cdr->accountcode)) {
						static char badacct = 0;
						static char toolong = 0;
						static char acctfailed = 0;
						int d, err;

						if (!strchr(cdr->accountcode, '/') && (cdr->accountcode[0] != '.' || cdr->accountcode[1] != '.')) {
							if (snprintf(csvacct_path + csvacct_offset, sizeof(csvacct_path) - csvacct_offset, "%s.csv", cdr->accountcode) < sizeof(csvacct_path) - csvacct_offset) {
								if (!(err = ((d = open_cloexec(csvacct_path, O_WRONLY|O_APPEND|O_CREAT, 0666)) < 0))) {
									err = (write(d, ds->data, ds->used) != ds->used);
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

					cw_dynstr_reset(&ds);
				} else {
					if (ds)
						cw_dynstr_free(&ds);
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
					sleep(1);
				}
			}
		}

		fflush(csvmaster_fd);
		fsync(fileno(csvmaster_fd));
	}

	pthread_mutex_unlock(&csv_lock);

	if (ds)
		cw_dynstr_free(&ds);

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
	snprintf(csvmaster_path, sizeof(csvmaster_path), "%s/%s/%s", cw_config_CW_LOG_DIR, CSV_LOG_DIR, CSV_MASTER);
	csvacct_offset = snprintf(csvacct_path, sizeof(csvacct_path), "%s/%s/", cw_config_CW_LOG_DIR, CSV_LOG_DIR);

	csvmaster_open();

	cw_cdrbe_register(&cdrbe);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, release_module, desc)
