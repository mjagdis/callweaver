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
 * \brief Digital Milliwatt Test
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"

static const char tdesc[] = "Digital Milliwatt (mu-law) Test Application";

static void *milliwatt_app;
static const char milliwatt_name[] = "Milliwatt";
static const char milliwatt_synopsis[] = "Generate a Constant 1000Hz tone at 0dbm (mu-law)";
static const char milliwatt_syntax[] = "Milliwatt()";
static const char milliwatt_descrip[] = 
"Generate a Constant 1000Hz tone at 0dbm (mu-law)\n";


static char digital_milliwatt[] = {0x1e,0x0b,0x0b,0x1e,0x9e,0x8b,0x8b,0x9e} ;


struct gen_state {
	int index;
	struct cw_frame f;
	uint8_t buf[640 + CW_FRIENDLY_OFFSET];
};


static void *milliwatt_alloc(struct cw_channel *chan, void *params)
{
	struct gen_state *state;

	if ((state = malloc(sizeof(*state))))
		state->index = 0;
	return state;
}

static void milliwatt_release(struct cw_channel *chan, void *data)
{
	free(data);
	return;
}

static struct cw_frame *milliwatt_generate(struct cw_channel *chan, void *data, int samples)
{
	struct gen_state *state = data;
	int i;

	if (samples > sizeof(state->buf) - CW_FRIENDLY_OFFSET)
		samples = sizeof(state->buf) - CW_FRIENDLY_OFFSET;

	cw_fr_init_ex(&state->f, CW_FRAME_VOICE, CW_FORMAT_ULAW);
	state->f.offset = CW_FRIENDLY_OFFSET;
	state->f.data = state->buf + CW_FRIENDLY_OFFSET;
	state->f.datalen = state->f.samples = samples;
	/* create a buffer containing the digital milliwatt pattern */
	for (i = 0;  i < samples;  i++)
	{
		state->buf[CW_FRIENDLY_OFFSET + i] = digital_milliwatt[state->index];
		state->index = (state->index + 1) & 7;
	}
	return &state->f;
}

static struct cw_generator milliwattgen = 
{
	alloc: milliwatt_alloc,
	release: milliwatt_release,
	generate: milliwatt_generate,
} ;

static int milliwatt_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;

	LOCAL_USER_ADD(u);

	cw_set_write_format(chan, CW_FORMAT_ULAW);
	cw_set_read_format(chan, CW_FORMAT_ULAW);
	if (chan->_state != CW_STATE_UP)
	{
		cw_answer(chan);
	}
	if (cw_generator_activate(chan, &chan->generator, &milliwattgen, "milliwatt") < 0)
	{
		cw_log(CW_LOG_WARNING,"Failed to activate generator on '%s'\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	while(!cw_safe_sleep(chan, 10000));

	cw_generator_deactivate(&chan->generator);
	LOCAL_USER_REMOVE(u);
	return -1;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(milliwatt_app);
	return res;
}

static int load_module(void)
{
	if (!milliwattgen.is_initialized)
		cw_object_init(&milliwattgen, CW_OBJECT_CURRENT_MODULE, 0);

	milliwatt_app = cw_register_function(milliwatt_name, milliwatt_exec, milliwatt_synopsis, milliwatt_syntax, milliwatt_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
