/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief Conditional logic dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/config.h"		/* for cw_true */

static void *isnull_function;
static const char isnull_func_name[] = "ISNULL";
static const char isnull_func_synopsis[] = "NULL Test: Returns 1 if NULL or 0 otherwise";
static const char isnull_func_syntax[] = "ISNULL(data)";
static const char isnull_func_desc[] = "";

static void *exists_function;
static const char exists_func_name[] = "EXISTS";
static const char exists_func_synopsis[] = "Existence Test: Returns 1 if exists, 0 otherwise";
static const char exists_func_syntax[] = "EXISTS(data)";
static const char exists_func_desc[] = "";

static void *if_function;
static const char if_func_name[] = "IF";
static const char if_func_synopsis[] = "Conditional: Returns the data following '?' if true else the data following ':'";
static const char if_func_syntax[] = "IF(expr ? [true] [: false])";
static const char if_func_desc[] =
"Returns the value of [true] if expr evaluates to true, otherwise returns\n"
"the value of [false]\n"
"NOTE: Both true and false are evaluated regardless of which is to be returned\n\n"
"DEPRECATED\n"
"This will fail if the true and/or false values evaluate to a string containing\n"
"a comma, colon or question mark.\n\n"
"Replace IF( A ? B : C ) with $[ A ? B :: C ]\n"
"e.g. for ${IF($[ ${A} = \"debug\" ] ? ${B} : ${C})}\n"
"     use $[ $[ \"${A}\" = \"debug\" ] ? \"${B}\" :: \"${C}\" ]\n";

static void *if_time_function;
static const char if_time_func_name[] = "IFTIME";
static const char if_time_func_synopsis[] = "Temporal Conditional: Returns the data following '?' if true else the data following ':'";
static const char if_time_func_syntax[] = "IFTIME(timespec ? [true] [: false])";
static const char if_time_func_desc[] =
"Returns the value of [true] if the current time meets timespec, otherwise returns\n"
"the value of [false]\n"
"NOTE: Both true and false are evaluated regardless of which is to be returned\n";


static int builtin_function_isnull(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	cw_dynstr_printf(result, "%s", (argc > 0 && argv[0][0] ? "0" : "1"));
	return 0;
}

static int builtin_function_exists(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	cw_dynstr_printf(result, "%s", (argc > 0 && argv[0][0] ? "1" : "0"));
	return 0;
}

static int builtin_function_iftime(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_timing timing;
	char *s, *q, **a = NULL;
	int i, n;

	CW_UNUSED(chan);

	/* First argument is "<timespec ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?')))
		return cw_function_syntax(if_time_func_syntax);

	/* Trim trailing space from the timespec */
	q = s;
	do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

	if (!cw_build_timing(&timing, argv[0])) {
		cw_log(CW_LOG_ERROR, "Invalid time specification\n");
		return -1;
	}

	if (result) {
		do { *(s++) = '\0'; } while (isspace(*s));
		n = 0;
		if (cw_check_timing(&timing)) {
			/* True: we want everything between '?' and ':' */
			argv[0] = s;
			a = argv;
			for (i = 0; i < argc; i++) {
				if ((s = strchr(argv[i], ':'))) {
					do { *(s--) = '\0'; } while (s >= argv[i] && isspace(*s));
					n = i + 1;
					break;
				}
			}
		} else {
			/* False: we want everything after ':' (if anything) */
			argv[0] = s;
			for (i = 0; i < argc; i++) {
				if ((s = strchr(argv[i], ':'))) {
					do { *(s++) = '\0'; } while (isspace(*s));
					argv[i] = s;
					a = argv + i;
					n = argc - i;
					break;
				}
			}
			/* No ": ..." so we just drop through */
		}

		if (n) {
			cw_dynstr_printf(result, "%s", a[i]);
			for (i = 1; i < n; i++)
				cw_dynstr_printf(result, ",%s", a[i]);
		}
	}

	return 0;
}

static int builtin_function_if(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;
	char *s, *q, **a = NULL;
	int i, n;

	CW_UNUSED(chan);

	if (!deprecation_warning) {
		cw_log(CW_LOG_WARNING, "IF(... ? ... : ...) is deprecated, please use $[ ... ? ... :: ... ] instead.\n");
		deprecation_warning = 1;
	}

	/* First argument is "<condition ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?')))
		return cw_function_syntax(if_func_syntax);

	if (result) {
		/* Trim trailing space from the condition */
		q = s;
		do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

		do { *(s++) = '\0'; } while (isspace(*s));

		n = 0;
		if (cw_true(argv[0])) {
			/* True: we want everything between '?' and ':' */
			argv[0] = s;
			a = argv;
			for (i = 0; i < argc; i++) {
				if ((s = strchr(argv[i], ':'))) {
					do { *(s--) = '\0'; } while (s >= argv[i] && isspace(*s));
					n = i + 1;
					break;
				}
			}
		} else {
			/* False: we want everything after ':' (if anything) */
			argv[0] = s;
			for (i = 0; i < argc; i++) {
				if ((s = strchr(argv[i], ':'))) {
					do { *(s++) = '\0'; } while (isspace(*s));
					argv[i] = s;
					a = argv + i;
					n = argc - i;
					break;
				}
			}
			/* No ": ..." so we just drop through */
		}

		if (n) {
			cw_dynstr_printf(result, "%s", a[0]);
			for (i = 1; i < n; i++)
				cw_dynstr_printf(result, ",%s", a[i]);
		}
	}

	return 0;
}

static const char tdesc[] = "logic functions";

static int unload_module(void)
{
        int res = 0;

	res |= cw_unregister_function(isnull_function);
	res |= cw_unregister_function(exists_function);
	res |= cw_unregister_function(if_function);
	res |= cw_unregister_function(if_time_function);
        return res;
}

static int load_module(void)
{
	isnull_function = cw_register_function(isnull_func_name, builtin_function_isnull, isnull_func_synopsis, isnull_func_syntax, isnull_func_desc);
        exists_function = cw_register_function(exists_func_name, builtin_function_exists, exists_func_synopsis, exists_func_syntax, exists_func_desc);
        if_function = cw_register_function(if_func_name, builtin_function_if, if_func_synopsis, if_func_syntax, if_func_desc);
        if_time_function = cw_register_function(if_time_func_name, builtin_function_iftime, if_time_func_synopsis, if_time_func_syntax, if_time_func_desc);
        return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
