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
 * \brief Custom Comma Separated Value CDR records.
 * 
 * \arg See also \ref cwCDR
 *
 * Logs in LOG_DIR/cdr_custom
 */
#include <stdio.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/app.h"
#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"

#define CUSTOM_LOG_DIR "/cdr_custom"

#define DATE_FORMAT "%Y-%m-%d %T"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>


static const char name[] = "cdr-custom";
static const char desc[] = "Customizable Comma Separated Values CDR Backend";


static pthread_mutex_t csv_lock = PTHREAD_MUTEX_INITIALIZER;

static struct cw_channel *chan;
static char *csvmaster_path, *values_str;
static struct cw_dynargs values = CW_DYNARRAY_INIT;

static FILE *csvmaster_fd;

static struct cw_dynstr evalbuf = CW_DYNSTR_INIT;


static int csvmaster_open(void)
{
	static dev_t dev;
	static ino_t ino;
	struct stat st;
	int d, res;

	res = -1;

	if (csvmaster_path) {
		res = 0;
		if (csvmaster_fd) {
			if (!stat(csvmaster_path, &st) && st.st_dev == dev && st.st_ino == ino)
				goto out;

			fclose(csvmaster_fd);
		}

		if ((d = open_cloexec(csvmaster_path, O_WRONLY|O_APPEND|O_CREAT, 0666)) >= 0
		&& (csvmaster_fd = fdopen(d, "a")))
			goto out;

		cw_log(CW_LOG_ERROR, "%s: %s\n", csvmaster_path, strerror(errno));

		if (d >= 0)
			close(d);

		res = -1;
	}

out:
	return res;
}


static int custom_log(struct cw_cdr *batch)
{
	struct cw_cdr *cdrset, *cdr;

	pthread_mutex_lock(&csv_lock);

	if (!csvmaster_open()) {
		while ((cdrset = batch)) {
			batch = batch->batch_next;

			while ((cdr = cdrset)) {
				int i;

				chan->cdr = cdr;

				pbx_substitute_variables(chan, NULL, values_str, &evalbuf);

				if (!evalbuf.error && !cw_separate_app_args(&values, evalbuf.data, ",", '\0', NULL)) {
					cdrset = cdrset->next;

					for (i = 0; i < values.used; i++) {
						char *p;

						fputs((i ? ",\"" : "\""), csvmaster_fd);

						while ((p = strchr(values.data[i], '"'))) {
							fwrite(values.data[i], sizeof(char), p - values.data[i] + 1, csvmaster_fd);
							putc('"', csvmaster_fd);
							values.data[i] = p + 1;
						}
						fputs(values.data[i], csvmaster_fd);

						putc('"', csvmaster_fd);
					}

					putc('\n', csvmaster_fd);

					cw_dynargs_reset(&values);
					cw_dynstr_reset(&evalbuf);
				} else {
					cw_dynargs_free(&values);
					cw_dynstr_free(&evalbuf);
					usleep(10);
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
	.handler = custom_log,
};


static int load_config(int reload) 
{
	struct cw_config *cfg;
	struct cw_variable *var;
	int res = -1;

	pthread_mutex_lock(&csv_lock);

	if (values_str) {
		free(values_str);
		values_str = NULL;
	}
	cw_dynargs_reset(&values);

	if (csvmaster_path) {
		free(csvmaster_path);
		csvmaster_path = NULL;
	}

	if ((cfg = cw_config_load("cdr_custom.conf"))) {
		var = cw_variable_browse(cfg, "mappings");
		while (var) {
			if (!cw_strlen_zero(var->name) && !cw_strlen_zero(var->value)) {
				if ((csvmaster_path = malloc(strlen(cw_config[CW_LOG_DIR]) + 1 + strlen(name) + 1 + strlen(var->name) + 1))) {
					sprintf((char *)csvmaster_path, "%s/%s/%s", cw_config[CW_LOG_DIR], name, var->name);

					if (!(values_str = strdup(var->value))) {
						free(csvmaster_path);
						csvmaster_path = NULL;
						cw_log(CW_LOG_ERROR, "Out of memory!\n");
					}
				}
			} else
				cw_log(CW_LOG_NOTICE, "Mapping must have both filename and format at line %d\n", var->lineno);

			if (var->next) {
				cw_log(CW_LOG_NOTICE, "Sorry, only one mapping is supported at this time, mapping '%s' will be ignored at line %d.\n", var->next->name, var->next->lineno); 
				break;
			}

			var = var->next;
		}
		cw_config_destroy(cfg);
		if (csvmaster_path)
			res = 0;
	} else {
		if (reload)
			cw_log(CW_LOG_WARNING, "Failed to reload configuration file.\n");
		else
			cw_log(CW_LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
	}

	pthread_mutex_unlock(&csv_lock);
	return res;
}


static void release_module(void)
{
	if (values_str)
		free(values_str);
	cw_dynargs_free(&values);

	if (csvmaster_fd)
		fclose(csvmaster_fd);

	cw_dynstr_free(&evalbuf);

	if (chan)
		cw_channel_free(chan);
}


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}


static int load_module(void)
{
	if ((chan = cw_channel_alloc(0, NULL))) {
		if (!load_config(0)) {
			csvmaster_open();
			cw_cdrbe_register(&cdrbe);
			return 0;
		}
		cw_channel_free(chan);
	}

	return -1;
}


static int reload_module(void)
{
	return load_config(1);
}


MODULE_INFO(load_module, reload_module, unload_module, release_module, desc)
