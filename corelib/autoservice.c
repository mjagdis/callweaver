/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
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
 * \brief Automatic channel service routines
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>			/* For PI */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/pbx.h"
#include "callweaver/frame.h"
#include "callweaver/sched.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/file.h"
#include "callweaver/translate.h"
#include "callweaver/manager.h"
#include "callweaver/chanvars.h"
#include "callweaver/indications.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

#define MAX_AUTOMONS 256

CW_MUTEX_DEFINE_STATIC(autolock);

struct asent {
	struct cw_channel *chan;
	struct asent *next;
};

static struct asent *aslist = NULL;
static pthread_t asthread = CW_PTHREADT_NULL;

static __attribute__((noreturn)) void *autoservice_run(void *data)
{
	struct cw_channel *mons[MAX_AUTOMONS];
	int x;
	int ms;
	struct cw_channel *chan;
	struct asent *as;
	struct cw_frame *f;

	CW_UNUSED(data);

	for (;;) {
		x = 0;
		cw_mutex_lock(&autolock);
		as = aslist;
		while(as) {
			if (!as->chan->_softhangup) {
				if (x < MAX_AUTOMONS)
					mons[x++] = as->chan;
				else
					cw_log(CW_LOG_WARNING, "Exceeded maximum number of automatic monitoring events.  Fix autoservice.c\n");
			}
			as = as->next;
		}
		cw_mutex_unlock(&autolock);

/* 		if (!aslist)
			break; */
		ms = 500;
		chan = cw_waitfor_n(mons, x, &ms);
		if (chan) {
			/* Read and ignore anything that occurs */
			f = cw_read(chan);
			if (f)
				cw_fr_free(f);
		}
	}
}

int cw_autoservice_start(struct cw_channel *chan)
{
	int res = -1;
	struct asent *as;
	int needstart;
	cw_mutex_lock(&autolock);
	needstart = (pthread_equal(asthread, CW_PTHREADT_NULL) ? 1 : 0 /* aslist ? 0 : 1 */);
	as = aslist;
	while(as) {
		if (as->chan == chan)
			break;
		as = as->next;
	}
	if (!as) {
		as = malloc(sizeof(struct asent));
		if (as) {
			memset(as, 0, sizeof(struct asent));
			as->chan = chan;
			as->next = aslist;
			aslist = as;
			res = 0;
			if (needstart) {
				if (cw_pthread_create(&asthread, &global_attr_default, autoservice_run, NULL)) {
					cw_log(CW_LOG_WARNING, "Unable to create autoservice thread :(\n");
					free(aslist);
					aslist = NULL;
					res = -1;
				} else
					pthread_kill(asthread, SIGURG);
			}
		}
	}
	cw_mutex_unlock(&autolock);
	return res;
}

int cw_autoservice_stop(struct cw_channel *chan)
{
	int res = -1;
	struct asent *as, *prev;
	cw_mutex_lock(&autolock);
	as = aslist;
	prev = NULL;
	while(as) {
		if (as->chan == chan)
			break;
		prev = as;
		as = as->next;
	}
	if (as) {
		if (prev)
			prev->next = as->next;
		else
			aslist = as->next;
		free(as);
		if (!chan->_softhangup)
			res = 0;
	}
	if (!pthread_equal(asthread, CW_PTHREADT_NULL))
		pthread_kill(asthread, SIGURG);
	cw_mutex_unlock(&autolock);
	/* Wait for it to un-block */
	while(cw_test_flag(chan, CW_FLAG_BLOCKING))
		usleep(1000);
	return res;
}
