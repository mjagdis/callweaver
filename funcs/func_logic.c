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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
#include "callweaver/config.h"		/* for opbx_true */

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
"NOTE: Both true and false are evaluated regardless of which is to be returned\n";

static void *if_time_function;
static const char if_time_func_name[] = "IFTIME";
static const char if_time_func_synopsis[] = "Temporal Conditional: Returns the data following '?' if true else the data following ':'";
static const char if_time_func_syntax[] = "IFTIME(timespec ? [true] [: false])";
static const char if_time_func_desc[] =
"Returns the value of [true] if the current time meets timespec, otherwise returns\n"
"the value of [false]\n"
"NOTE: Both true and false are evaluated regardless of which is to be returned\n";


static int builtin_function_isnull(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	opbx_copy_string(buf, (argc > 0 && argv[0][0] ? "0" : "1"), len);
	return 0;
}

static int builtin_function_exists(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	opbx_copy_string(buf, (argc > 0 && argv[0][0] ? "1" : "0"), len);
	return 0;
}

static int builtin_function_iftime(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct opbx_timing timing;
	char *s, *q, **a;
	int i, n, l, first;

	/* First argument is "<timespec ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?')))
		return opbx_function_syntax(if_time_func_syntax);

	/* Trim trailing space from the timespec */
	q = s;
	do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

	if (!opbx_build_timing(&timing, argv[0])) {
		opbx_log(OPBX_LOG_ERROR, "Invalid time specification\n");
		return -1;
	}

	if (buf) {
		do { *(s++) = '\0'; } while (isspace(*s));
		n = 0;
		if (opbx_check_timing(&timing)) {
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

		len--; /* one for the terminating null */
		q = buf;
		for (i = 0; len && i < n; i++) {
			if (len > 0 && !first) {
				*(q++) = ',';
				len--;
			} else
				first = 0;
			l = strlen(a[i]);
			if (l > len)
				l = len;
			memcpy(q, a[i], l);
			q += l;
			len -= l;
		}
		*q = '\0';
	}

	return 0;
}

static int builtin_function_if(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char *s, *q, **a;
	int i, n, l, first;

	/* First argument is "<condition ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?')))
		return opbx_function_syntax(if_func_syntax);

	if (buf) {
		/* Trim trailing space from the condition */
		q = s;
		do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

		do { *(s++) = '\0'; } while (isspace(*s));

		n = 0;
		if (opbx_true(argv[0])) {
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

		len--; /* one for the terminating null */
		q = buf;
		for (i = 0; len && i < n; i++) {
			if (len > 0 && !first) {
				*(q++) = ',';
				len--;
			} else
				first = 0;
			l = strlen(a[i]);
			if (l > len)
				l = len;
			memcpy(q, a[i], l);
			q += l;
			len -= l;
		}
		*q = '\0';
	}

	return 0;
}

static const char tdesc[] = "logic functions";

static int unload_module(void)
{
        int res = 0;

	res |= opbx_unregister_function(isnull_function);
	res |= opbx_unregister_function(exists_function);
	res |= opbx_unregister_function(if_function);
	res |= opbx_unregister_function(if_time_function);
        return res;
}

static int load_module(void)
{
	isnull_function = opbx_register_function(isnull_func_name, builtin_function_isnull, isnull_func_synopsis, isnull_func_syntax, isnull_func_desc);
        exists_function = opbx_register_function(exists_func_name, builtin_function_exists, exists_func_synopsis, exists_func_syntax, exists_func_desc);
        if_function = opbx_register_function(if_func_name, builtin_function_if, if_func_synopsis, if_func_syntax, if_func_desc);
        if_time_function = opbx_register_function(if_time_func_name, builtin_function_iftime, if_time_func_synopsis, if_time_func_syntax, if_time_func_desc);
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
