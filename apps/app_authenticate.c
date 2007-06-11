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
 * \brief Execute arbitrary authenticate commands
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/app.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/utils.h"

static char *tdesc = "Authentication Application";

static void *auth_app;
static const char *auth_name = "Authenticate";
static const char *auth_synopsis = "Authenticate a user";
static const char *auth_syntax = "Authenticate(password[, options])";
static const char *auth_descrip =
"Requires a user to enter a"
"given password in order to continue execution.  If the\n"
"password begins with the '/' character, it is interpreted as\n"
"a file which contains a list of valid passwords (1 per line).\n"
"an optional set of options may be provided by concatenating any\n"
"of the following letters:\n"
"     a - Set account code to the password that is entered\n"
"     d - Interpret path as database key, not literal file\n"
"     m - Interpret path as a file which contains a list of\n"
"         account codes and password hashes delimited with ':'\n"
"         one per line. When password matched, corresponding\n"
"         account code will be set\n"
"     j - Support jumping to n+101\n"
"     r - Remove database key upon successful entry (valid with 'd' only)\n"
"\n"
"When using a database key, the value associated with the key can be\n"
"anything.\n"
"Returns 0 if the user enters a valid password within three\n"
"tries, or -1 on hangup.  If the priority n+101 exists and invalid\n"
"authentication was entered, and the 'j' flag was specified, processing\n"
"will jump to n+101 and 0 will be returned.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int auth_exec(struct opbx_channel *chan, int argc, char **argv)
{
	int res=0;
	int jump = 0;
	int retries;
	struct localuser *u;
	char passwd[256];
	char *prompt;
	
	if (argc < 1 || argc > 2) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", auth_syntax);
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	if (chan->_state != OPBX_STATE_UP) {
		res = opbx_answer(chan);
		if (res) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}
	
	if (argc > 1 && strchr(argv[1], 'j'))
		jump = 1;
	/* Start asking for password */
	prompt = "agent-pass";
	for (retries = 0; retries < 3; retries++) {
		res = opbx_app_getdata(chan, prompt, passwd, sizeof(passwd) - 2, 0);
		if (res < 0)
			break;
		res = 0;
		if (argv[0][0] == '/') {
			if (argc > 1 && strchr(argv[1], 'd')) {
				char tmp[256];
				/* Compare against a database key */
				if (!opbx_db_get(argv[0] + 1, passwd, tmp, sizeof(tmp))) {
					/* It's a good password */
					if (argc > 1 && strchr(argv[1], 'r')) {
						opbx_db_del(argv[0] + 1, passwd);
					}
					break;
				}
			} else {
				/* Compare against a file */
				FILE *f;
				f = fopen(argv[0], "r");
				if (f) {
					char buf[256] = "";
					char md5passwd[33] = "";
					char *md5secret = NULL;

					while (!feof(f)) {
						fgets(buf, sizeof(buf), f);
						if (!feof(f) && !opbx_strlen_zero(buf)) {
							buf[strlen(buf) - 1] = '\0';
							if (argc > 1 && strchr(argv[1], 'm')) {
								md5secret = strchr(buf, ':');
								if (md5secret == NULL)
									continue;
								*md5secret = '\0';
								md5secret++;
								opbx_md5_hash(md5passwd, passwd);
								if (!strcmp(md5passwd, md5secret)) {
									if (argc > 1 && strchr(argv[1], 'a'))
										opbx_cdr_setaccount(chan, buf);
									break;
								}
							} else {
								if (!strcmp(passwd, buf)) {
									if (argc > 1 && strchr(argv[1], 'a'))
										opbx_cdr_setaccount(chan, buf);
									break;
								}
							}
						}
					}
					fclose(f);
					if (!opbx_strlen_zero(buf)) {
						if (argc > 1 && strchr(argv[1], 'm')) {
							if (md5secret && !strcmp(md5passwd, md5secret))
								break;
						} else {
							if (!strcmp(passwd, buf))
								break;
						}
					}
				} else 
					opbx_log(LOG_WARNING, "Unable to open file '%s' for authentication: %s\n", argv[0], strerror(errno));
			}
		} else {
			/* Compare against a fixed password */
			if (!strcmp(passwd, argv[0])) 
				break;
		}
		prompt="auth-incorrect";
	}
	if ((retries < 3) && !res) {
		if (argc > 1 && strchr(argv[1], 'a') && !strchr(argv[1], 'm')) 
			opbx_cdr_setaccount(chan, passwd);
		res = opbx_streamfile(chan, "auth-thankyou", chan->language);
		if (!res)
			res = opbx_waitstream(chan, "");
	} else {
		if (jump && opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101) == 0 ) {
			res = 0;
		} else {
			if (!opbx_streamfile(chan, "vm-goodbye", chan->language))
				res = opbx_waitstream(chan, "");
			res = -1;
		}
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(auth_app);
	return res;
}

int load_module(void)
{
	auth_app = opbx_register_application(auth_name, auth_exec, auth_synopsis, auth_syntax, auth_descrip);
	return 0;
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


