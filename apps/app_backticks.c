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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "callweaver.h"

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


static int do_backticks(char *command, char *buf, size_t len)
{
        int fds[2];
	pid_t pid = 0;
	int n, ret = -1;

        if (pipe(fds)) {
                opbx_log(OPBX_LOG_ERROR, "Pipe failed: %s\n", strerror(errno));
        } else {
                pid = fork();
                if (pid < 0) {
                        opbx_log(OPBX_LOG_ERROR, "Fork failed: %s\n", strerror(errno));
                        close(fds[0]);
                        close(fds[1]);
                } else if (pid) { /* parent */
                        close(fds[1]);
			if (buf) {
				/* Reserve the last for null */
				len--;
                        	while (len && (n = read(fds[0], buf, len)) > 0) {
					buf += n;
					len -= n;
				}
				*buf = '\0';
			}
			/* Dump any remaining input */
			while (read(fds[0], &n, sizeof(n)) > 0);
			waitpid(pid, &ret, 0);
                } else { /* child */
                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);

                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);

                        system(command);
                        opbx_log(OPBX_LOG_ERROR, "system(\"%s\") failed\n", command);
                        _exit(0);
                }
        }

        return ret;
}

static int backticks_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char buf[1024] = "";
	struct localuser *u;

	if (argc != 2)
		return opbx_function_syntax(backticks_syntax);

	LOCAL_USER_ADD(u);

	do_backticks(argv[1], buf, sizeof(buf));
	pbx_builtin_setvar_helper(chan, argv[0], buf);

	LOCAL_USER_REMOVE(u);
	return 0;
}


static int function_backticks(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
        if (argc > 0)
		do_backticks(argv[0], buf, len);

        return 0;
}


static int unload_module(void)
{
	int res = 0;

        opbx_unregister_function(backticks_function);
        res |= opbx_unregister_function(backticks_app);
	return res;
}

static int load_module(void)
{
        backticks_function = opbx_register_function(backticks_func_name, function_backticks, backticks_func_synopsis, backticks_func_syntax, backticks_func_descrip);
        backticks_app = opbx_register_function(backticks_name, backticks_exec, backticks_synopsis, backticks_syntax, backticks_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
