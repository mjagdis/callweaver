/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*
 *
 * SayUnixTime application
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/options.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/say.h"


static char *tdesc = "Say time";

static char *app_sayunixtime = "SayUnixTime";
static char *app_datetime = "DateTime";

static char *sayunixtime_synopsis = "Says a specified time in a custom format";

static char *sayunixtime_descrip =
"SayUnixTime([unixtime][|[timezone][|format]])\n"
"  unixtime: time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone: timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format:   a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n"
"  Returns 0 or -1 on hangup.\n";
static char *datetime_descrip =
"DateTime([unixtime][|[timezone][|format]])\n"
"  unixtime: time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone: timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format:   a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n"
"  Returns 0 or -1 on hangup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sayunixtime_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s,*zone=NULL,*timec,*format;
	time_t unixtime;
	struct timeval tv;
	
	LOCAL_USER_ADD(u);

	tv = opbx_tvnow();
	unixtime = (time_t)tv.tv_sec;

	if( !strcasecmp(chan->language, "da" ) ) {
		format = "A dBY HMS";
	} else if ( !strcasecmp(chan->language, "de" ) ) {
		format = "A dBY HMS";
	} else {
		format = "ABdY 'digits/at' IMp";
	} 

	if (data) {
		s = data;
		s = opbx_strdupa(s);
		if (s) {
			timec = strsep(&s,"|");
			if ((timec) && (*timec != '\0')) {
				long timein;
				if (sscanf(timec,"%ld",&timein) == 1) {
					unixtime = (time_t)timein;
				}
			}
			if (s) {
				zone = strsep(&s,"|");
				if (zone && (*zone == '\0'))
					zone = NULL;
				if (s) {
					format = s;
				}
			}
		} else {
			opbx_log(LOG_ERROR, "Out of memory error\n");
		}
	}

	if (chan->_state != OPBX_STATE_UP) {
		res = opbx_answer(chan);
	}
	if (!res)
		res = opbx_say_date_with_format(chan, unixtime, OPBX_DIGIT_ANY, chan->language, format, zone);

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = opbx_unregister_application(app_sayunixtime);
	if (! res)
		return opbx_unregister_application(app_datetime);
	else
		return res;
}

int load_module(void)
{
	int res;
	res = opbx_register_application(app_sayunixtime, sayunixtime_exec, sayunixtime_synopsis, sayunixtime_descrip);
	if (! res)
		return opbx_register_application(app_datetime, sayunixtime_exec, sayunixtime_synopsis, datetime_descrip);
	else
		return res;
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


