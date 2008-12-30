/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Limited, UK
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Core PBX function registry.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/function.h"
#include "callweaver/channel.h"
#include "callweaver/app.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"


static const char *func_object_name(struct cw_object *obj)
{
	struct cw_func *it = container_of(obj, struct cw_func, obj);
	return it->name;
}

static int func_object_cmp(struct cw_object *a, struct cw_object *b)
{
	struct cw_func *func_a = container_of(a, struct cw_func, obj);
	struct cw_func *func_b = container_of(b, struct cw_func, obj);

	return strcmp(func_a->name, func_b->name);
}

static int func_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_func *it = container_of(obj, struct cw_func, obj);
	return (!strcmp(it->name, pattern));
}

const struct cw_object_isa cw_object_isa_function = {
	.name = func_object_name,
	.cmp = func_object_cmp,
	.match = func_object_match,
};

struct cw_registry func_registry = {
	.name = "Function",
	.lock = CW_MUTEX_INIT_VALUE,
};


/*! \brief Logs a syntax error for a function
 *
 * \param syntax	the correct usage message
 *
 * \return -1. Propogating this back up the call chain will hang up the current call.
 */
int cw_function_syntax(const char *syntax)
{
	cw_log(CW_LOG_ERROR, "Syntax: %s\n", syntax);
	return -1;
}


/*! \brief Find a function object based on its hash
 *
 * Look up the given hash of the function name and return
 * a pointer to the function object. The returned pointer
 * is already reference counted and must be released
 * after use by passing it to cw_object_put().
 *
 * \note The name argument is only used to log "not found"
 * messages. If it is given as NULL cw_find_function()
 * will fail silently.
 *
 * \param hash		the hash of the function name to find
 * 			(given by cw_hash_app_name())
 * \param name		the name of the function (may be NULL)
 *
 * \return a pointer to the function object or NULL if
 * not found
 */
static struct cw_func* cw_find_function(unsigned int hash, const char *name) 
{
	struct cw_object *obj = cw_registry_find(&func_registry, name);

	if (!obj)
		cw_log(CW_LOG_ERROR, "No such function \"%s\"\n", name);

	if (obj)
		return container_of(obj, struct cw_func, obj);

	return NULL;
}


/*! \brief Executes a function using an array of arguments
 *
 * Executes an application on a channel with the given arguments
 * and returns any result as a string in the given result buffer.
 *
 * \param chan		channel to execute on
 * \param hash		hash of the name of function to execute (from cw_hash_app_name())
 * \param name		name of function to execute
 * \param argc		the number of arguments
 * \param argv		an array of pointers to argument strings
 * \param result	where to write any result
 * \param result_max	how much space is available in out for a result
 *
 * \return 0 on success, -1 on failure
 */
int cw_function_exec(struct cw_channel *chan, unsigned int hash, const char *name, int argc, char **argv, char *out, size_t outlen)
{
	struct cw_func *func;
	const char *saved_c_appl;
	int ret = -1;

	if (!(func = cw_find_function(hash, name)))
		goto out;

	/* FIXME: The last argument to cw_cdr_setapp should be the
	 * argv as a comma separated string. But doing that costs.
	 * Is it really needed? (It's for lastdata in CDR)
	 * N.B. The reason we don't do this in the args-as-string wrapper
	 * is because real languages will probably want to pass args as
	 * arrays. So we better get used to it now.
	 * N.N.B. The reason we don't do the setapp if there is a buffer
	 * for a return value is because the post-hangup CDR generation
	 * seems to keep expanding ${CDR(...)} expressions. So if we log
	 * expression context function calls CDRs will always say the
	 * last app was "CDR".
	 */
	if (chan->cdr && !out && !cw_check_hangup(chan))
		cw_cdr_setapp(chan->cdr, name, argv[0]);

	/* save channel values - for the sake of CDR and debug output from DumpChan and the CLI <bleurgh> */
	saved_c_appl = chan->appl;
	chan->appl = name;

	ret = (*func->handler)(chan, argc, argv, out, outlen);
	cw_object_put(func);

	/* restore channel values */
	chan->appl= saved_c_appl;

out:
	return ret;
}

/*! \brief Executes a function using an argument string
 *
 * Executes an application on a channel with the given arguments
 * and returns any result as a string in the given result buffer.
 * The argument string contains zero or more comma separated
 * arguments. Arguments may be quoted and/or contain backslash
 * escaped characters as allowed by cw_separate_app_args().
 * They may NOT contain variables or expressions that require
 * expansion - these should have been expanded prior to calling
 * cw_function_exec().
 *
 * \param chan		channel to execute on
 * \param hash		hash of the name of function to execute (from cw_hash_app_name())
 * \param name		name of function to execute
 * \param args		the argument string
 * \param result	where to write any result
 * \param result_max	how much space is available in out for a result
 *
 * \return 0 on success, -1 on failure
 */
int cw_function_exec_str(struct cw_channel *chan, unsigned int hash, const char *name, char *args, char *out, size_t outlen)
{
	char *argv[100];
	int ret;

	if (!args)
		args = "";

	/* Save the last byte for a terminating '\0' */
	outlen--;

	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "%s: Call %s(%s)\n", (chan ? chan->name : "[no channel]"), name, args);

	ret = cw_function_exec(chan, hash, name, cw_separate_app_args(args, ',', arraysize(argv), argv), argv, out, outlen);

	if (out)
		out[outlen] = '\0';

	if (option_debug && option_verbose > 5)
               	cw_log(CW_LOG_DEBUG, "%s:  ret %d: %s\n",
			(chan ? chan->name : "[no channel]"), ret, (out ? out : ""));

	return ret;
}


static void complete_show_functions(int fd, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 2) {
		if (!strncasecmp(argv[2], "like", lastarg_len))
			cw_cli(fd, "like\n");
		if (!strncasecmp(argv[2], "describing", lastarg_len))
			cw_cli(fd, "describing\n");
	}
}


struct funcs_print_args {
	int fd;
	int like, describing, matches;
	int argc;
	char **argv;
};

static int funcs_print(struct cw_object *obj, void *data)
{
	struct cw_func *it = container_of(obj, struct cw_func, obj);
	struct funcs_print_args *args = data;
	int printapp = 1;

	if (args->like) {
		if (!strcasestr(it->name, args->argv[3]))
			printapp = 0;
	} else if (args->describing) {
		/* Match all words on command line */
		int i;
		for (i = 3;  i < args->argc;  i++) {
			if (!strcasestr(it->synopsis, args->argv[i]) && !strcasestr(it->description, args->argv[i])) {
				printapp = 0;
				break;
			}
		}
	}

	if (printapp) {
		args->matches++;
		cw_cli(args->fd,"  %20s: %s\n", it->name, it->synopsis);
	}

	return 0;
}

static int handle_show_functions(int fd, int argc, char *argv[])
{
	struct funcs_print_args args = {
		.fd = fd,
		.matches = 0,
		.argc = argc,
		.argv = argv,
	};

	if ((argc == 4) && (!strcmp(argv[2], "like")))
		args.like = 1;
	else if ((argc > 3) && (!strcmp(argv[2], "describing")))
		args.describing = 1;

	cw_cli(fd, "    -= %s CallWeaver Functions =-\n", (args.like || args.describing ? "Matching" : "Registered"));

	cw_registry_iterate(&func_registry, funcs_print, &args);

	cw_cli(fd, "    -= %d Functions %s =-\n", args.matches, (args.like || args.describing ? "Matching" : "Registered"));
	return RESULT_SUCCESS;
}

static int handle_show_function(int fd, int argc, char *argv[])
{
	struct cw_func *acf;

	if (argc < 3)
        return RESULT_SHOWUSAGE;

	if (!(acf = cw_find_function(cw_hash_app_name(argv[2]), argv[2]))) {
		cw_cli(fd, "No function by that name registered.\n");
		return RESULT_FAILURE;
	}

	cw_cli(fd,
             "\n  -= Info about function '%s' =- \n\n[Syntax]\n%s\n\n[Synopsis]\n%s\n\n[Description]\n%s\n",
             (acf->name)  ?  acf->name  :  "N/A",
             (acf->syntax)  ?  acf->syntax  :  "N/A",
             (acf->synopsis)  ?  acf->synopsis  :  "N/A",
             (acf->description)  ?  acf->description  :  "N/A");

	return RESULT_SUCCESS;
}

struct complete_show_func_args {
	int fd;
	char *word;
	int word_len;
};

static int complete_show_func_one(struct cw_object *obj, void *data)
{
	struct cw_func *it = container_of(obj, struct cw_func, obj);
	struct complete_show_func_args *args = data;

	if (!strncasecmp(args->word, it->name, args->word_len))
		cw_cli(args->fd, "%s\n", it->name);

	return 0;
}
static void complete_show_function(int fd, char *argv[], int lastarg, int lastarg_len)
{
	struct complete_show_func_args args = {
		.fd = fd,
		.word = argv[lastarg],
		.word_len = lastarg_len,
	};

	cw_registry_iterate(&func_registry, complete_show_func_one, &args);
}


static struct cw_clicmd cli_list[] = {
	{
		.cmda = { "show", "functions", NULL },
		.handler = handle_show_functions,
		.generator = complete_show_functions,
		.summary = "Shows registered dialplan functions",
		.usage = "Usage: show functions [{like|describing} <text>]\n"
		"       List functions which are currently available.\n"
		"       If 'like', <text> will be a substring of the function name\n"
		"       If 'describing', <text> will be a substring of the description\n",
	},
	{
		.cmda = { "show" , "function", NULL },
		.handler = handle_show_function,
		.generator = complete_show_function,
		.summary = "Describe a specific dialplan function",
		.usage = "Usage: show function <function>\n"
		"       Describe a particular dialplan function.\n",
	},

	/* DEPRECATED: replaced by functions above */
	{
		.cmda = { "show", "applications", NULL },
		.handler = handle_show_functions,
		.generator = complete_show_functions,
		.summary = "Shows registered dialplan applications [DEPRECATED: use \"show functions ...\"]",
		.usage = "DEPRECATED: use \"show functions ...\"",
	},
	{
		.cmda = { "show", "application", NULL },
		.handler = handle_show_function,
		.generator = complete_show_function,
		.summary = "Describe a specific dialplan application [DEPRECATED: use \"show function ...\"]",
		.usage = "DEPRECATED: use \"show function ...\"",
	},
};


void cw_function_registry_initialize(void)
{
	cw_cli_register_multiple(cli_list, arraysize(cli_list));
}
