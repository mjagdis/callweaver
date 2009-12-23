/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 * \brief I/O Event Managment
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h> /* for memset */
#include <sys/ioctl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/io.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"


#ifdef HAVE_EPOLL


int cw_io_run(cw_io_context_t ioc, int howlong)
{
	struct epoll_event events[16];
	struct cw_io_rec *ior;
	int nevents;
	int x;

	nevents = epoll_wait(ioc, events, arraysize(events), howlong);

	for (x = 0; x < nevents; x++) {
		ior = events[x].data.ptr;

		if (ior->callback) {
			if (!ior->callback(ior, ior->fd, events[x].events, ior->data)) {
				/* Time to delete them since they returned a 0 */
				cw_io_remove(ioc, ior);
			}
		}
	}

	return nevents;
}


#else /* HAVE_EPOLL */


struct io_context {
	struct pollfd *fds;	/* Poll structures */
	struct cw_io_rec **ior;	/* Associated I/O records */
	unsigned int cur;	/* First free slot */
	unsigned int slots;	/* Total number of slots */
	unsigned int removed;	/* Number of entries removed this pass */
};


cw_io_context_t cw_io_context_create(int slots)
{
	cw_io_context_t tmp;

	if ((tmp = malloc(sizeof(*tmp)))) {
		tmp->cur = 0;
		tmp->slots = slots;
		tmp->removed = 0;
		if ((tmp->fds = malloc(slots * sizeof(tmp->fds[0])))) {
			if ((tmp->ior =  malloc(slots * sizeof(tmp->ior[0]))))
				return tmp;

			free(tmp->fds);
		}

		free(tmp);
	}

	return NULL;
}


void cw_io_context_destroy(cw_io_context_t ioc)
{
	/* Free associated memory with an I/O context */
	if (ioc->fds)
		free(ioc->fds);
	if (ioc->ior)
		free(ioc->ior);
	free(ioc);
}


int cw_io_add(cw_io_context_t ioc, struct cw_io_rec *ior, int fd, short events)
{
	void *ptr;
	int change;

	if (ioc->cur < ioc->slots) {
do_add:
		ioc->fds[ioc->cur].fd = fd;
		ioc->fds[ioc->cur].events = events;
		ioc->fds[ioc->cur].revents = 0;

		ior->id = ioc->cur;
		ioc->ior[ioc->cur++] = ior;
		return 0;
	}

	change = ioc->slots >> 2;
	if (change < 4)
		change = 4;
	else if (change > 256)
		change = 256;

	ioc->slots += change;

	if ((ptr = realloc(ioc->ior, ioc->slots * sizeof(ioc->ior[0])))) {
		ioc->ior = ptr;

		if ((ptr = realloc(ioc->fds, ioc->slots * sizeof(struct pollfd)))) {
			ioc->fds = ptr;
			goto do_add;
		}
	}

	ioc->slots -= change;
	return -1;
}


void cw_io_remove(cw_io_context_t ioc, struct cw_io_rec *ior)
{
	ioc->ior[ior->id] = NULL;
	ior->id = UINT_MAX;
	ioc->removed++;
}


int cw_io_run(cw_io_context_t ioc, int howlong)
{
	int res;
	int x;

	/* Clean out removed entries */
	for (x = 0; ioc->removed && x < ioc->cur; x++) {
		if (!ioc->ior[x] && --ioc->cur > 0) {
			ioc->fds[x] = ioc->fds[ioc->cur];
			ioc->ior[x] = ioc->ior[ioc->cur];
			ioc->ior[x]->id = x;
			ioc->removed--;
		}
	}

	if ((res = poll(ioc->fds, ioc->cur, howlong)) > 0) {
		int events = res;
		int origcnt = ioc->cur;
		for (x = 0; events && x < origcnt; x++) {
			if (ioc->fds[x].revents) {
				events--;
				if (ioc->ior[x]) {
					if (ioc->ior[x]->callback) {
						if (!ioc->ior[x]->callback(ioc->ior[x], ioc->fds[x].fd, ioc->fds[x].revents, ioc->ior[x]->data))
							cw_io_remove(ioc, ioc->ior[x]);
					}
				}
			}
		}
	}

	return res;
}

#endif /* HAVE_EPOLL */
