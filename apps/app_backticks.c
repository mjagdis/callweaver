/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * BackTicks Application For CallWeaver
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
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
 * \brief Execute a shell command and save the result as a variable
 * 
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"
#include "callweaver/options.h"


static const char tdesc[] = "backticks";

static void *backticks_app;
static const char backticks_name[] = "BackTicks";
static const char backticks_synopsis[] = "Execute a shell command and save the result as a variable.";
static const char backticks_syntax[] = "BackTicks(varname, command)";
static const char backticks_descrip[] =
	"Be sure to include a full path!\n";

static void *backticks_function;
static const char backticks_func_name[] = "BACKTICKS";
static const char backticks_func_synopsis[] = "Executes a shell command.";
static const char backticks_func_syntax[] = "BACKTICKS(command)";
static const char backticks_func_descrip[] =
	"Executes a shell command and evaluates to the result.";


static int do_backticks(char *command, struct cw_dynstr *result)
{
        int fds[2];
	pid_t pid = 0;
	int ret = -1;

        if (pipe(fds)) {
                cw_log(CW_LOG_ERROR, "Pipe failed: %s\n", strerror(errno));
        } else {
#if defined(HAVE_WORKING_FORK)
                pid = fork();
#else
                pid = vfork();
#endif
                if (pid < 0) {
                        cw_log(CW_LOG_ERROR, "Fork failed: %s\n", strerror(errno));
                        close(fds[0]);
                        close(fds[1]);
                } else if (pid) { /* parent */
			ssize_t n = 1;

                        close(fds[1]);

			if (result) {
				cw_dynstr_need(result, 512);
				while (!result->error && (n = read(fds[0], &result->data[cw_dynstr_end(result)], cw_dynstr_space(result))) > 0) {
					result->used += n;
					if (cw_dynstr_space(result) < 64)
						cw_dynstr_need(result, 256);
				}
				/* Add a terminating null by printing an empty string */
				cw_dynstr_printf(result, "%s", "");
			}

			/* Dump any remaining input */
			if (n > 0) {
				char buf[256];

				while (read(fds[0], buf, sizeof(buf)) > 0);
			}

			waitpid(pid, &ret, 0);
                } else { /* child */
                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);

                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);

                        system(command);
                        cw_log(CW_LOG_ERROR, "system(\"%s\") failed\n", command);
                        _exit(0);
                }
        }

        return ret;
}

static int backticks_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	struct localuser *u;
	int ret = -1;

	CW_UNUSED(result);

	if (argc != 2)
		return cw_function_syntax(backticks_syntax);

	LOCAL_USER_ADD(u);

	do_backticks(argv[1], &ds);

	if (!ds.error) {
		pbx_builtin_setvar_helper(chan, argv[0], ds.data);
		ret = 0;
	}

	cw_dynstr_free(&ds);

	LOCAL_USER_REMOVE(u);
	return ret;
}


static int function_backticks(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

        if (argc > 0)
		do_backticks(argv[0], result);

        return 0;
}


static int unload_module(void)
{
	int res = 0;

        cw_unregister_function(backticks_function);
        res |= cw_unregister_function(backticks_app);
	return res;
}

static int load_module(void)
{
        backticks_function = cw_register_function(backticks_func_name, function_backticks, backticks_func_synopsis, backticks_func_syntax, backticks_func_descrip);
        backticks_app = cw_register_function(backticks_name, backticks_exec, backticks_synopsis, backticks_syntax, backticks_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
