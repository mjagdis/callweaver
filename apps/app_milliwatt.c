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

static void *milliwatt_alloc(struct opbx_channel *chan, void *params)
{
	int *indexp;
	indexp = malloc(sizeof(int));
	if (indexp == NULL) return(NULL);
	*indexp = 0;
	return(indexp);
}

static void milliwatt_release(struct opbx_channel *chan, void *data)
{
	free(data);
	return;
}

static int milliwatt_generate(struct opbx_channel *chan, void *data, int samples)
{
	struct opbx_frame wf;
	unsigned char waste[OPBX_FRIENDLY_OFFSET];
	unsigned char buf[640];
	int i, *indexp = (int *) data;

	if (samples > sizeof(buf))
	{
		opbx_log(OPBX_LOG_WARNING,"Only doing %d samples (%d requested)\n",(int)sizeof(buf),samples);
		samples = sizeof(buf);
	}
	waste[0] = 0; /* make compiler happy */
	opbx_fr_init_ex(&wf, OPBX_FRAME_VOICE, OPBX_FORMAT_ULAW, "app_milliwatt");
	wf.offset = OPBX_FRIENDLY_OFFSET;
	wf.data = buf;
	wf.datalen = samples;
	wf.samples = samples;
	/* create a buffer containing the digital milliwatt pattern */
	for (i = 0;  i < samples;  i++)
	{
		buf[i] = digital_milliwatt[(*indexp)++];
		*indexp &= 7;
	}
	return opbx_write(chan,&wf);
}

static struct opbx_generator milliwattgen = 
{
	alloc: milliwatt_alloc,
	release: milliwatt_release,
	generate: milliwatt_generate,
} ;

static int milliwatt_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;

	LOCAL_USER_ADD(u);

	opbx_set_write_format(chan, OPBX_FORMAT_ULAW);
	opbx_set_read_format(chan, OPBX_FORMAT_ULAW);
	if (chan->_state != OPBX_STATE_UP)
	{
		opbx_answer(chan);
	}
	if (opbx_generator_activate(chan,&milliwattgen,"milliwatt") < 0)
	{
		opbx_log(OPBX_LOG_WARNING,"Failed to activate generator on '%s'\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	while(!opbx_safe_sleep(chan, 10000));

	opbx_generator_deactivate(chan);
	LOCAL_USER_REMOVE(u);
	return -1;
}

static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(milliwatt_app);
	return res;
}

static int load_module(void)
{
	if (!milliwattgen.is_initialized)
		opbx_object_init(&milliwattgen, get_modinfo()->self, -1);

	milliwatt_app = opbx_register_function(milliwatt_name, milliwatt_exec, milliwatt_synopsis, milliwatt_syntax, milliwatt_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
