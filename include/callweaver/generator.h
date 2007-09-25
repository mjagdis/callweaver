/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Mike Jagdis <mjagdis@eris-associates.co.uk>
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

#ifndef _CALLWEAVER_GENERATOR_H
#define _CALLWEAVER_GENERATOR_H


#include <pthread.h>
#include <time.h>

#include "callweaver/object.h"


/*! Data structure that defines a class of generator */
struct opbx_generator {
	struct opbx_object obj;
	void *(*alloc)(struct opbx_channel *chan, void *params);
	void (*release)(struct opbx_channel *chan, void *data);
	int (*generate)(struct opbx_channel *chan, void *data, int samples);
	int is_initialized;
};

struct opbx_generator_instance {
	pthread_t tid;
	struct opbx_generator *class;
	void *pvt;
	int gen_samp;
	struct timespec interval;
};

extern void opbx_generator_deactivate(struct opbx_channel *chan);
extern int opbx_generator_activate(struct opbx_channel *chan, struct opbx_generator *class, void *params);

#define opbx_generator_is_active(chan) (!pthread_equal((chan)->generator.tid, OPBX_PTHREADT_NULL))

#define opbx_generator_is_self(chan) ({ \
	const typeof(chan) __chan = (chan); \
	!pthread_equal(__chan->generator.tid, OPBX_PTHREADT_NULL) \
		? pthread_equal(__chan->generator.tid, pthread_self()) \
		: 1; \
})

#endif /* _CALLWEAVER_GENERATOR_H */

