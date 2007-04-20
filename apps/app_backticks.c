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


static char *tdesc = "backticks";
static char *app = "BackTicks";
static char *synopsis = "Execute a shell command and save the result as a variable.";
static char *desc = ""
"  BackTicks(<VARNAME>|<command>)\n\n"
"Be sure to include a full path!\n"

;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;


static char *do_backticks(char *command, char *buf, size_t len)
{
        int fds[2], pid = 0;
        char *ret = NULL;

        memset(buf, 0, len);

        if (pipe(fds)) {
                opbx_log(LOG_WARNING, "Pipe/Exec failed\n");
        } else { /* good to go*/

                pid = fork();

                if (pid < 0) { /* ok maybe not */
                        opbx_log(LOG_WARNING, "Fork failed\n");
                        close(fds[0]);
                        close(fds[1]);
                } else if (pid) { /* parent */
                        close(fds[1]);
                        read(fds[0], buf, len);
                        ret = buf;
                } else { /* child */
                        char *argv[255] = {0};
                        int argc = 0;
                        char *p;
                        char *mycmd = opbx_strdupa(command);

                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);
                        argv[argc++] = mycmd;

                        close(fds[0]);
                        dup2(fds[1], STDOUT_FILENO);
                        argv[argc++] = mycmd;

                        do {
                                if ((p = strchr(mycmd, ' '))) {
                                        *p = '\0';
                                        mycmd = ++p;
                                        argv[argc++] = mycmd;
                                }
                        } while (p);

                        execv(argv[0], argv);
                        /* DoH! */
                        opbx_log(LOG_ERROR, "exec of %s failed\n", argv[0]);
                        exit(0);
                }
        }

        return buf;
}

static int backticks_exec(struct opbx_channel *chan, void *data)
{
        int res = 0;
        struct localuser *u;
        const char *usage = "Usage: Backticks(<VARNAME>|<command>)";
        char buf[1024], *argv[2], *mydata;
        int argc = 0;

        if (!data) {
                opbx_log(LOG_WARNING, "%s\n", usage);
                return -1;
        }


        LOCAL_USER_ADD(u);
        /* Do our thing here */

        if (!(mydata = opbx_strdupa(data))) {
                opbx_log(LOG_ERROR, "Memory Error!\n");
                res = -1;
        } else {
                if((argc = opbx_separate_app_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]))) < 2) {
                        opbx_log(LOG_WARNING, "%s\n", usage);
                        res = -1;
                }

                if (do_backticks(argv[1], buf, sizeof(buf))) {
                        pbx_builtin_setvar_helper(chan, argv[0], buf);
                } else {
                        opbx_log(LOG_WARNING, "No Data!\n");
                        res = -1;
                }
        }
        LOCAL_USER_REMOVE(u);
        return res;
}


static char *function_backticks(struct opbx_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
        char *ret = NULL;

        if (do_backticks(data, buf, len)) {
                ret = buf;
        }

        return ret;
}

static struct opbx_custom_function backticks_function = {
        .name = "BACKTICKS",
        .desc = "Executes a shell command and evaluates to the result.",
        .syntax = "BACKTICKS(<command>)",
        .synopsis = "Executes a shell command.",
        .read = function_backticks
};


int unload_module(void)
{
        STANDARD_HANGUP_LOCALUSERS;
        opbx_custom_function_unregister(&backticks_function);
        return opbx_unregister_application(app);
}

int load_module(void)
{
        opbx_custom_function_register(&backticks_function);
        return opbx_register_application(app, backticks_exec, synopsis, desc);
}

char *description(void)
{
        return tdesc;
}

int usecount(void)
{
        int res;
        STANDARD_USECOUNT(res);
        return res;
}
