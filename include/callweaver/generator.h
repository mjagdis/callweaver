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
struct cw_generator {
	struct cw_object obj;
	void *(*alloc)(struct cw_channel *chan, void *params);
	void (*release)(struct cw_channel *chan, void *data);
	struct cw_frame *(*generate)(struct cw_channel *chan, void *data, int samples);
	int is_initialized;
};

struct cw_generator_instance {
	pthread_t tid;
	struct cw_channel *chan;
	struct cw_generator *class;
	void *pvt;
};


extern CW_API_PUBLIC const struct cw_object_isa cw_object_isa_generator;


extern CW_API_PUBLIC void cw_generator_deactivate(struct cw_generator_instance *gen);
extern CW_API_PUBLIC int cw_generator_activate(struct cw_channel *chan, struct cw_generator_instance *gen, struct cw_generator *class, void *params);

#define cw_generator_is_active(chan) (!pthread_equal((chan)->generator.tid, CW_PTHREADT_NULL))

#define cw_generator_is_self(chan) ({ \
	const typeof(chan) __chan = (chan); \
	!pthread_equal(__chan->generator.tid, CW_PTHREADT_NULL) \
		? pthread_equal(__chan->generator.tid, pthread_self()) \
		: 1; \
})

#endif /* _CALLWEAVER_GENERATOR_H */

