/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<You Email Here>>
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
 * \brief Skeleton application
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"

static char tdesc[] = "Trivial skeleton Application";

static void *skel_app = NULL;
static const char skel_name[] = "Skel";
static const char skel_synopsis[] = "Skeleton application.";
static const char skel_syntax[] = "Skel()";
static const char skel_descrip[] = "This application is a template to build other applications from.\n"
 " It shows you the basic structure to create your own CallWeaver applications.\n";


/*! Skeleton function handler
 *
 * \param chan the channel the function is being run on
 * \param argc the number of entries in argv (not counting the terminal NULL)
 * \param argv an array of argc argument strings followed by a NULL
 * \param result an optional pointer to the space any result is to be written (in string form) to
 * \param result_max the maximum number of data bytes that may be written to the result buffer.
 * Note that the caller has already reserved space for a terminating null so all bytes may be written.
 *
 * Return 0 on success, -1 on failure.
 * A return of -1 causes further processing to be aborted and
 * the channel is hung up. You SHOULD log an error before
 * returning -1.
 */
static int skel_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	int res = 0;
	struct localuser *u;

	/* Check the argument count is within range and any
	 * required arguments are none blank.
	 */
	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(skel_syntax);

	LOCAL_USER_ADD(u);
	
	/* Do our thing here.
	 * The argv array is private and modifiable as are the strings
	 * pointed to by the argv[] elements (but don't assume they are
	 * contiguous and can be glued together by overwriting the
	 * terminating nulls!)
	 * If you pass argv data to something outside your control
	 * you should assume it has been trashed and is unusable
	 * on return. If you want to preserve it either malloc
	 * and copy or use cw_strdupa()
	 */

	/* If we have been given a result buffer and we have a result
	 * to return put at most result_len bytes into the result
	 * buffer. Note that the caller has already reserved a byte
	 * for a terminating null so the full result_len bytes are
	 * available for data.
	 */
	if (result) {
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}


/* \brief unload this module (CallWeaver continues running)
 * 
 * This is _only_ called if the module is explicitly unloaded.
 * It is _not_ called if CallWeaver exits completely. If you need
 * to perform clean up on exit you should register functions
 * using cw_atexit_register - and remember to remove them
 * with cw_atexit_unregister in your unload_module and
 * call them yourself if necessary.
 */
static int unload_module(void)
{
	int res = 0;

	/* Unregister _everything_ that you registered in your
	 * load_module routine. Return zero if unregistering was
	 * successful and you are happy for the module to be
	 * removed. Otherwise return non-zero.
	 * If you allow the module to be removed while things
	 * are still registered you _will_ crash CallWeaver!
	 */
	if (skel_app)
		res |= cw_unregister_function(skel_app);
	return res;
}

/* \brief Load this module.
 * \param module A cookie that should be passed t the various
 * registration functions to say what module is registering
 * things
 *
 * This is called automatically when Callweaver loads this module.
 * It should perform any necessary registrations of functions,
 * cli commands, channels etc.
 * If successful it should return 0. If there is a problem at
 * any stage it SHOULD return -1 and Callweaver will call the
 * unload_module function and clean up as soon as it is safe
 * (from the core's perspective) to do so. You MUST NOT call
 * unload_module yourself and SHOULD NOT return 0 if there
 * are errors unless the module is prepared to work around
 * any (possibly undefined) resulting loss of capability.
 */
static int load_module(void)
{
	skel_app = cw_register_function(skel_name, skel_exec, skel_synopsis, skel_syntax, skel_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
