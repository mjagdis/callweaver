/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, 2010, Eris Associates Ltd., UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 * \brief Core functions
 * 
 */
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/say.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/keywords.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"
#include "callweaver/musiconhold.h"


static const char tdesc[] = "Core functions";


#define BACKGROUND_SKIP        (1 << 0)
#define BACKGROUND_NOANSWER    (1 << 1)
#define BACKGROUND_MATCHEXTEN    (1 << 2)
#define BACKGROUND_PLAYBACK    (1 << 3)

CW_DECLARE_OPTIONS(background_opts,{
    ['s'] = { BACKGROUND_SKIP },
    ['n'] = { BACKGROUND_NOANSWER },
    ['m'] = { BACKGROUND_MATCHEXTEN },
    ['p'] = { BACKGROUND_PLAYBACK },
});

#define WAITEXTEN_MOH        (1 << 0)

CW_DECLARE_OPTIONS(waitexten_opts,{
    ['m'] = { WAITEXTEN_MOH, 1 },
});


static int argtol(const char *arg, long double scale)
{
	long double secs;
	char *end;
	int res = 0;

	secs = strtold(arg, &end);
	if (!*end && !isnan(secs)) {
		res = INT_MAX;

		if (!isinf(secs)) {
			feclearexcept(FE_ALL_EXCEPT);
			errno = 0;
			res = lroundl(secs * scale);
			if (errno || fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_UNDERFLOW))
				res = 0;
			else if (fetestexcept(FE_OVERFLOW))
				res = INT_MAX;
		}
	}

	return res;
}


static void wait_for_hangup(struct cw_channel *chan, char *s)
{
	struct cw_frame *f;
	int waittime;

	if (s && s[0] && (waittime = argtol(s, 1000)) >= 0)
		cw_safe_sleep(chan, waittime);
	else {
		while (cw_waitfor(chan, -1) >= 0 && (f = cw_read(chan)))
			cw_fr_free(f);
	}
}


static int pbx_builtin_exten(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->exten);
	return 0;
}


static int pbx_builtin_context(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->context);
	return 0;
}


static int pbx_builtin_priority(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%d", chan->priority);
	return 0;
}


static int pbx_builtin_channel(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->name);
	return 0;
}


static int pbx_builtin_uniqueid(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->uniqueid);
	return 0;
}


static int pbx_builtin_hangupcause(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%d", chan->hangupcause);
	return 0;
}


static int pbx_builtin_accountcode(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->accountcode);
	return 0;
}


static int pbx_builtin_language(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_dynstr_printf(result, "%s", chan->language);
	return 0;
}


static int pbx_builtin_hint(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan) {
		cw_get_hint(result, NULL, chan, chan->context, chan->exten);
		cw_get_hint(NULL, result, chan, chan->context, chan->exten);
	}
	return 0;
}


static int pbx_builtin_hintname(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result && chan)
		cw_get_hint(NULL, result, chan, chan->context, chan->exten);
	return 0;
}


static int pbx_builtin_epoch(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result)
		cw_dynstr_printf(result, "%u", (unsigned int)time(NULL));
	return 0;
}


static int pbx_builtin_datetime(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct tm tm;
	time_t now = time(NULL);

	CW_UNUSED(chan);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result) {
		localtime_r(&now, &tm);
		cw_dynstr_printf(result, "%02d%02d%04d-%02d:%02d:%02d",
			tm.tm_mday,
			tm.tm_mon + 1,
			tm.tm_year + 1900,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec
		);
	}

	return 0;
}


static int pbx_builtin_timestamp(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct tm tm;
	time_t now = time(NULL);

	CW_UNUSED(chan);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (result) {
		localtime_r(&now, &tm);
		/* e.g. 20031130-150612 */
		cw_dynstr_printf(result, "%04d%02d%02d-%02d%02d%02d",
			tm.tm_year + 1900,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec
		);
	}

	return 0;
}


static int pbx_builtin_progress(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	cw_indicate(chan, CW_CONTROL_PROGRESS);
	return 0;
}

static int pbx_builtin_ringing(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	cw_indicate(chan, CW_CONTROL_RINGING);
	return 0;
}

static int pbx_builtin_busy(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	cw_indicate(chan, CW_CONTROL_BUSY);
	if (chan->_state != CW_STATE_UP)
		cw_setstate(chan, CW_STATE_BUSY);
	wait_for_hangup(chan, (argc > 0 ? argv[0] : NULL));
	return -1;
}

static int pbx_builtin_congestion(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	cw_indicate(chan, CW_CONTROL_CONGESTION);
	if (chan->_state != CW_STATE_UP)
		cw_setstate(chan, CW_STATE_BUSY);
	wait_for_hangup(chan, (argc > 0 ? argv[0] : NULL));
	return -1;
}

static int pbx_builtin_answer(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	int delay = (argc > 0 && argv[0][0] ? argtol(argv[0], 1) : 0);
	int res;

	CW_UNUSED(result);

	if (chan->_state == CW_STATE_UP)
		delay = 0;

	if (!(res = cw_answer(chan)) && delay)
		res = cw_safe_sleep(chan, delay);

	return res;
}

static int pbx_builtin_setlanguage(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;

	CW_UNUSED(result);

	if (!deprecation_warning) {
		cw_log(CW_LOG_WARNING, "SetLanguage is deprecated, please use Set(LANGUAGE()=language) instead.\n");
		deprecation_warning = 1;
	}

	/* Copy the language as specified */
	if (argc > 0)
		cw_copy_string(chan->language, argv[0], sizeof(chan->language));

	return 0;
}

static int pbx_builtin_resetcdr(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *p;
	int flags = 0;

	CW_UNUSED(result);

	for (; argc; argv++, argc--) {
		for (p = argv[0]; *p; p++) {
			switch (*p) {
				case 'a':
					flags |= CW_CDR_FLAG_LOCKED;
					break;
				case 'v':
					flags |= CW_CDR_FLAG_KEEP_VARS;
					break;
				case 'w':
					flags |= CW_CDR_FLAG_POSTED;
					break;
			}
		}
	}

	cw_cdr_reset(chan->cdr, flags);
	return 0;
}

static int pbx_builtin_setaccount(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	cw_cdr_setaccount(chan, (argc > 0 ? argv[0] : ""));
	return 0;
}

static int pbx_builtin_setamaflags(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	cw_cdr_setamaflags(chan, (argc > 0 ? argv[0] : ""));
	return 0;
}

static int pbx_builtin_hangup(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	int n;

	CW_UNUSED(result);

	if (argc > 0 && (n = atoi(argv[0])) > 0)
		chan->hangupcause = n;

	/* Just return non-zero and it will hang up */
	return -1;
}

static int pbx_builtin_goto(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *context, *exten;
	int res;

	CW_UNUSED(result);

	context = exten = NULL;
	if (argc > 2) context = (argv++)[0];
	if (argc > 1) exten = (argv++)[0];
	res = cw_explicit_goto(chan, context, exten, argv[0]);
	if (!res && option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Goto (%s, %s, %d)\n", chan->context, chan->exten, chan->priority + 1);
	return res;
}

static int pbx_builtin_gotoiftime(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    char tmp[1024];
    struct cw_timing timing;
    char *s, *q;

    CW_UNUSED(result);

    s = NULL;
    if (argc > 3) {
    	if ((s = strchr(argv[3], '?'))) {
            /* Trim trailing spaces from the timespec (before the '?') */
            for (q = s - 1; q >= argv[3] && isspace(*q); *(q--) = '\0');
            /* Trim leading spaces before context,exten,priority (after the '?') */
    	    do { *(s++) = '\0'; } while (isspace(*s));
	}
    }

    if (!s || !*s || argc > 6) {
        cw_log(CW_LOG_WARNING, "GotoIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>?[[context,]extension,]priority\n");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s,%s,%s,%s", argv[0], argv[1], argv[2], argv[3]);
    cw_build_timing(&timing, tmp);

    if (cw_check_timing(&timing)) {
    	argv[3] = s;
	argv += 3;
    	argc -= 3;
	return pbx_builtin_goto(chan, argc, argv, NULL);
    }

    return 0;
}

static int pbx_builtin_execiftime(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    char tmp[1024];
    struct cw_timing timing;
    char *s, *args, *p;
    int res;

    CW_UNUSED(result);

    s = NULL;
    if (argc > 3) {
    	if ((s = strchr(argv[3], '?')))
    	    do { *(s++) = '\0'; } while (isspace(*s));
    }

    if (!s || !*s) {
        cw_log(CW_LOG_WARNING, "ExecIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>?<funcname>[(<args>)]\n");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s,%s,%s,%s", argv[0], argv[1], argv[2], argv[3]);
    cw_build_timing(&timing, tmp);

    if (cw_check_timing(&timing)) {
	    if ((args = strchr(s, '(')) && (p = strrchr(s, ')'))) {
		*(args++) = '\0';
		*p = '\0';
		res = cw_function_exec_str(chan, cw_hash_string(s), s, args, NULL);
	    } else {
		res = cw_function_exec(chan, cw_hash_string(s), s, argc - 4, argv + 5, NULL);
	    }
	    if (res && errno == ENOENT)
		cw_log(CW_LOG_ERROR, "No such function \"%s\"\n", s);
    }

    return 0;
}

static int pbx_builtin_wait(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	int res = 0;

	CW_UNUSED(result);

	/* Wait for "n" seconds */
	if (argc > 0 && argv[0][0])
		res = cw_safe_sleep(chan, argtol(argv[0], 1000));

	return 0;
}

static int pbx_builtin_waitexten(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct cw_flags flags = {0};
    char *mohclass = NULL;
    int ms, res;

    CW_UNUSED(result);

    if (argc > 1) {
        char *opts[1];

        cw_parseoptions(waitexten_opts, &flags, opts, argv[1]);
        if (cw_test_flag(&flags, WAITEXTEN_MOH))
            mohclass = opts[0];
    }
    
    if (cw_test_flag(&flags, WAITEXTEN_MOH))
        cw_moh_start(chan, mohclass);

    /* Wait for "n" seconds */
    if (argc < 1 || !(ms = (int)(atof(argv[0]) * 1000.0)))
        ms = (chan->pbx ? chan->pbx->rtimeout * 1000 : 10000);

    res = cw_waitfordigit(chan, ms);
    if (!res)
    {
        if (cw_exists_extension(chan, chan->context, chan->exten, chan->priority + 1, chan->cid.cid_num))
        {
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "Timeout on %s, continuing...\n", chan->name);
        }
        else if (cw_exists_extension(chan, chan->context, "t", 1, chan->cid.cid_num))
        {
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "Timeout on %s, going to 't'\n", chan->name);
            cw_copy_string(chan->exten, "t", sizeof(chan->exten));
            chan->priority = 0;
        }
        else
        {
            cw_log(CW_LOG_WARNING, "Timeout but no rule 't' in context '%s'\n", chan->context);
            res = -1;
        }
    }

    if (cw_test_flag(&flags, WAITEXTEN_MOH))
        cw_moh_stop(chan);

    return res;
}

static int pbx_builtin_background(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    int res = 0;
    char *options = NULL; 
    char *filename = NULL;
    char *front = NULL, *back = NULL;
    char *lang = NULL;
    char *context = NULL;
    struct cw_flags flags = {0};
    unsigned int hash = 0;

    CW_UNUSED(result);

    switch (argc)
    {
    case 4:
        context = argv[3];
    case 3:
        lang = argv[2];
    case 2:
        options = argv[1];
        hash = cw_hash_string(options);
    case 1:
        filename = argv[0];
        break;
    default:
        cw_log(CW_LOG_WARNING, "Background requires an argument (filename)\n");
        return -1;
    }

    if (!lang)
        lang = chan->language;

    if (!context)
        context = chan->context;

    if (options)
    {
        if (hash == CW_KEYWORD_SKIP)
            flags.flags = BACKGROUND_SKIP;
        else if (hash == CW_KEYWORD_NOANSWER)
            flags.flags = BACKGROUND_NOANSWER;
        else
            cw_parseoptions(background_opts, &flags, NULL, options);
    }

    /* Answer if need be */
    if (chan->_state != CW_STATE_UP)
    {
        if (cw_test_flag(&flags, BACKGROUND_SKIP))
            return 0;
        if (!cw_test_flag(&flags, BACKGROUND_NOANSWER))
            res = cw_answer(chan);
    }

    if (!res)
    {
        /* Stop anything playing */
        cw_stopstream(chan);
        /* Stream a file */
        front = filename;
        while (!res  &&  front)
        {
            if ((back = strchr(front, '&')))
            {
                *back = '\0';
                back++;
            }
            res = cw_streamfile(chan, front, lang);
            if (!res)
            {
                if (cw_test_flag(&flags, BACKGROUND_PLAYBACK))
                {
                    res = cw_waitstream(chan, "");
                }
                else
                {
                    if (cw_test_flag(&flags, BACKGROUND_MATCHEXTEN))
                        res = cw_waitstream_exten(chan, context);
                    else
                        res = cw_waitstream(chan, CW_DIGIT_ANY);
                }
                cw_stopstream(chan);
            }
            else
            {
                cw_log(CW_LOG_WARNING, "cw_streamfile failed on %s for %s, %s, %s, %s\n", chan->name, argv[0], argv[1], argv[2], argv[3]);
                res = 0;
                break;
            }
            front = back;
        }
    }
    if (context != chan->context  &&  res)
    {
        snprintf(chan->exten, sizeof(chan->exten), "%c", res);
        cw_copy_string(chan->context, context, sizeof(chan->context));
        chan->priority = 0;
        return 0;
    }
    return res;
}

static int pbx_builtin_atimeout(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    static int deprecation_warning = 0;
    int x = (argc > 0 ? atoi(argv[0]) : 0);

    CW_UNUSED(result);

    if (!deprecation_warning)
    {
        cw_log(CW_LOG_WARNING, "AbsoluteTimeout is deprecated, please use Set(TIMEOUT(absolute)=timeout) instead.\n");
        deprecation_warning = 1;
    }
            
    /* Set the absolute maximum time how long a call can be connected */
    cw_channel_setwhentohangup(chan, x);
    if (option_verbose > 2)
        cw_verbose( VERBOSE_PREFIX_3 "Set Absolute Timeout to %d\n", x);
    return 0;
}

static int pbx_builtin_rtimeout(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    static int deprecation_warning = 0;

    CW_UNUSED(argc);
    CW_UNUSED(result);

    if (!deprecation_warning)
    {
        cw_log(CW_LOG_WARNING, "ResponseTimeout is deprecated, please use Set(TIMEOUT(response)=timeout) instead.\n");
        deprecation_warning = 1;
    }

    /* If the channel is not in a PBX, return now */
    if (!chan->pbx)
        return 0;

    /* Set the timeout for how long to wait between digits */
    chan->pbx->rtimeout = atoi(argv[0]);
    if (option_verbose > 2)
        cw_verbose( VERBOSE_PREFIX_3 "Set Response Timeout to %d\n", chan->pbx->rtimeout);
    return 0;
}

static int pbx_builtin_dtimeout(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    static int deprecation_warning = 0;

    CW_UNUSED(argc);
    CW_UNUSED(result);

    if (!deprecation_warning)
    {
        cw_log(CW_LOG_WARNING, "DigitTimeout is deprecated, please use Set(TIMEOUT(digit)=timeout) instead.\n");
        deprecation_warning = 1;
    }

    /* If the channel is not in a PBX, return now */
    if (!chan->pbx)
        return 0;

    /* Set the timeout for how long to wait between digits */
    chan->pbx->dtimeout = atoi(argv[0]);
    if (option_verbose > 2)
        cw_verbose( VERBOSE_PREFIX_3 "Set Digit Timeout to %d\n", chan->pbx->dtimeout);
    return 0;
}

static int pbx_builtin_setvar(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *value = NULL;

	if (argc < 1) {
		cw_log(CW_LOG_WARNING, "Set requires at least one variable name/value pair.\n");
		return 0;
	}

	/* check for a trailing flags argument */
	if ((argc > 1)  &&  !strchr(argv[argc-1], '=')) {
		argc--;
		if (strchr(argv[argc], 'g'))
			chan = NULL;
	}

	for (; argc; argv++, argc--) {
		if ((value = strchr(argv[0], '='))) {
			char *args, *p;
			int l;
			*(value++) = '\0';
			if ((args = strchr(argv[0], '(')) && (p = strrchr(args, ')'))) {
				int res;

				/* In the old world order funcs were made to behave like variables
				 * for the sake of Set(FUNC(args)=value). In the new world order
				 * all funcs that set values use FUNC(args, value) however the
				 * old style Set() notation is retained (for now) to support
				 * existing dialplans. If used the value is simply appended to
				 * the arg list here.
				 */
				static int deprecated = 0;
				if (!deprecated) {
					cw_log(CW_LOG_WARNING, "Set(FUNC(args)=value) is deprecated. Use FUNC(args, value) instead\n");
					deprecated = 1;
				}
				*(args++) = '\0';
				l = strlen(value) + 1;
				*(p++) = ',';
				memmove(p, value, l);
				res = cw_function_exec_str(chan, cw_hash_string(argv[0]), argv[0], args, NULL);
				if (res && errno == ENOENT)
					cw_log(CW_LOG_ERROR, "No such function \"%s\"\n", argv[0]);
			} else {
				pbx_builtin_setvar_helper(chan, argv[0], value);
			}
		} else {
			cw_log(CW_LOG_WARNING, "Ignoring entry '%s' with no '=' (and not last 'options' entry)\n", argv[0]);
		}
	}

	if (result && value)
		cw_dynstr_printf(result, "%s", value);

	return 0;
}

static int pbx_builtin_setvar_old(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;

	CW_UNUSED(result);

	if (!deprecation_warning) {
		cw_log(CW_LOG_WARNING, "SetVar is deprecated, please use Set instead.\n");
		deprecation_warning = 1;
	}

	return pbx_builtin_setvar(chan, argc, argv, NULL);
}

static int pbx_builtin_importvar(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_channel *chan2;
	char *channel, *s;
	int res = 0;

	CW_UNUSED(result);

	if (argc != 2 || !(channel = strchr(argv[0], '=')))
		return cw_function_syntax("ImportVar(newvar=channelname,variable)");

	s = channel;
	do { *(s--) = '\0'; } while (isspace(*s));
	do { channel++; } while (isspace(*channel));

	if ((chan2 = cw_get_channel_by_name_locked(channel))) {
		struct cw_object *obj = NULL;
		const char *value = NULL;

		if ((obj = cw_registry_find(&chan->vars, 1, cw_hash_var_name(argv[1]), argv[1]))) {
			struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
			value = var->value;
		}

		cw_channel_unlock(chan2);
		cw_object_put(chan2);

		pbx_builtin_setvar_helper(chan, argv[0], value);

		if (obj)
			cw_object_put_obj(obj);

		res = 0;
	}

	return res;
}

static int pbx_builtin_setglobalvar(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);
	CW_UNUSED(result);

	for (; argc; argv++, argc--) {
 		char *value;
		if ((value = strchr(argv[0], '='))) {
			*(value++) = '\0';
			pbx_builtin_setvar_helper(NULL, argv[0], value);
		} else {
			cw_log(CW_LOG_WARNING, "Ignoring entry '%s' with no '='\n", argv[0]);
		}
	}

	return(0);
}

static int pbx_builtin_noop(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	// The following is added to relax dialplan execution.
	// When doing small loops with lots of iteration, this
	// allows other threads to re-schedule smoothly.
	// This will for sure dramatically slow down benchmarks but
	// will improve performance under load or in particular circumstances.

	// sched_yield(); // This doesn't seem to have the effect we want.
	usleep(1);
	return 0;
}

static int pbx_builtin_gotoif(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *s, *q;
	int i;

	CW_UNUSED(result);

	/* First argument is "<condition ? ..." */
	if (argc > 0) {
		q = s = strchr(argv[0], '?');
		if (s) {
			/* Trim trailing space from the condition */
			do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

			do { *(s++) = '\0'; } while (isspace(*s));

			if (pbx_checkcondition(argv[0])) {
				/* True: we want everything between '?' and ':' */
				argv[0] = s;
				for (i = 0; i < argc; i++) {
					if ((s = strchr(argv[i], ':'))) {
						do { *(s--) = '\0'; } while (s >= argv[i] && isspace(*s));
						argc = i + 1;
						break;
					}
				}
				return (argc != 1 || argv[0][0] ? pbx_builtin_goto(chan, argc, argv, NULL) : 0);
			} else {
				/* False: we want everything after ':' (if anything) */
				argv[0] = s;
				for (i = 0; i < argc; i++) {
					if ((s = strchr(argv[i], ':'))) {
						do { *(s++) = '\0'; } while (isspace(*s));
						argv[i] = s;
						return (argc - i != 1 || s[0] ? pbx_builtin_goto(chan, argc - i, argv + i, NULL) : 0);
					}
				}
				/* No ": ..." so we just drop through */
				return 0;
			}
		}
	}
    
	return cw_function_syntax("GotoIf(boolean ? [[[context,]exten,]priority] [: [[context,]exten,]priority])");
}           

static int pbx_builtin_saynumber(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    CW_UNUSED(result);

    if (argc < 1) {
        cw_log(CW_LOG_WARNING, "SayNumber requires an argument (number)\n");
        return -1;
    }
    if (argc > 1) { 
        argv[1][0] = tolower(argv[1][0]);
        if (!strchr("fmcn", argv[1][0])) {
            cw_log(CW_LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
            return -1;
        }
    }
    return cw_say_number(chan, atoi(argv[0]), "", chan->language, (argc > 1 ? argv[1] : NULL));
}

static int pbx_builtin_saydigits(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    int res = 0;

    CW_UNUSED(result);

    for (; !res && argc; argv++, argc--)
        res = cw_say_digit_str(chan, argv[0], "", chan->language);
    return res;
}
    
static int pbx_builtin_saycharacters(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    int res = 0;

    CW_UNUSED(result);

    for (; !res && argc; argv++, argc--)
        res = cw_say_character_str(chan, argv[0], "", chan->language);
    return res;
}
    
static int pbx_builtin_sayphonetic(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    int res = 0;

    CW_UNUSED(result);

    for (; !res && argc; argv++, argc--)
        res = cw_say_phonetic_str(chan, argv[0], "", chan->language);
    return res;
}


static struct cw_func func_list[] =
{
	/* These applications are built into the PBX core and do not
	   need separate modules */
	{
		.name = "EXTEN",
		.handler = pbx_builtin_exten,
		.synopsis = "Return the current dialplan extension",
		.syntax = "EXTEN()",
		.description = "Return the number current dialplan extension.\n",
	},
	{
		.name = "CONTEXT",
		.handler = pbx_builtin_context,
		.synopsis = "Return the current dialplan context",
		.syntax = "CONTEXT()",
		.description = "Return the number current dialplan context.\n",
	},
	{
		.name = "PRIORITY",
		.handler = pbx_builtin_priority,
		.synopsis = "Return the current dialplan priority",
		.syntax = "PRIORITY()",
		.description = "Return the number current dialplan priority.\n",
	},
	{
		.name = "CHANNEL",
		.handler = pbx_builtin_channel,
		.synopsis = "Return the name of the channel",
		.syntax = "CHANNEL()",
		.description = "Return the name of the channel.\n",
	},
	{
		.name = "UNIQUEID",
		.handler = pbx_builtin_uniqueid,
		.synopsis = "Return a unique ID for the channel",
		.syntax = "UNIQUEID()",
		.description = "Return a unique ID for the channel.\n",
	},
	{
		.name = "HANGUPCAUSE",
		.handler = pbx_builtin_hangupcause,
		.synopsis = "Return the hangup cause for the channel",
		.syntax = "HANGUPCAUSE()",
		.description = "Return the hangup cause for the channel.\n",
	},
	{
		.name = "ACCOUNTCODE",
		.handler = pbx_builtin_accountcode,
		.synopsis = "Return the account code of the channel",
		.syntax = "ACCOUNTCODE()",
		.description = "Return the account code of the channel.\n",
	},
	{
		.name = "LANGUAGE",
		.handler = pbx_builtin_language,
		.synopsis = "Return the language of the channel",
		.syntax = "LANGUAGE()",
		.description = "Return the language of the channel.\n",
	},

	{
		.name = "HINT",
		.handler = pbx_builtin_hint,
		.synopsis = "Return the hint for the channel",
		.syntax = "HINT()",
		.description = "Return the hint for the channel.\n",
	},
	{
		.name = "HINTNAME",
		.handler = pbx_builtin_hintname,
		.synopsis = "Return the hint name for the channel",
		.syntax = "HINTNAME()",
		.description = "Return the hint name for the channel.\n",
	},

	{
		.name = "EPOCH",
		.handler = pbx_builtin_epoch,
		.synopsis = "Return the number of seconds since the system's epoch",
		.syntax = "EPOCH()",
		.description = "Return the number of seconds since the system's epoch.\n",
	},
	{
		.name = "DATETIME",
		.handler = pbx_builtin_datetime,
		.synopsis = "Return the current date and time.",
		.syntax = "DATETIME()",
		.description = "Return the current date and time..\n",
	},
	{
		.name = "TIMESTAMP",
		.handler = pbx_builtin_timestamp,
		.synopsis = "Return the current date and time.",
		.syntax = "DATETIME()",
		.description = "Return the current date and time..\n",
	},

	{
		.name = "AbsoluteTimeout",
		.handler = pbx_builtin_atimeout,
		.synopsis = "Set absolute maximum time of call",
		.syntax = "AbsoluteTimeout(seconds)",
		.description = "Set the absolute maximum amount of time permitted for a call.\n"
		"A setting of 0 disables the timeout.  Always returns 0.\n" 
		"AbsoluteTimeout has been deprecated in favor of Set(TIMEOUT(absolute)=timeout)\n"
	},

	{
		.name = "Answer",
		.handler = pbx_builtin_answer, 
		.synopsis = "Answer a channel if ringing", 
		.syntax = "Answer([delay])",
		.description = "If the channel is ringing, answer it, otherwise do nothing. \n"
		"If delay is specified, callweaver will pause execution for the specified amount\n"
		"of milliseconds if an answer is required, in order to give audio a chance to\n"
		"become ready. Returns 0 unless it tries to answer the channel and fails.\n"   
	},

	{
		.name = "Background",
		.handler = pbx_builtin_background,
		.synopsis = "Play a file while awaiting extension",
		.syntax = "Background(filename1[&filename2...][, options[, langoverride][, context]])",
		.description = "Plays given files, while simultaneously waiting for the user to begin typing\n"
		"an extension. The timeouts do not count until the last BackGround\n"
		"application has ended. Options may also be included following a pipe \n"
		"symbol. The 'langoverride' may be a language to use for playing the prompt\n"
		"which differs from the current language of the channel.  The optional\n"
		"'context' can be used to specify an optional context to exit into.\n"
		"Returns -1 if thhe channel was hung up, or if the file does not exist./n"
		"Returns 0 otherwise.\n\n"
		"  Options:\n"
		"    's' - causes the playback of the message to be skipped\n"
		"          if the channel is not in the 'up' state (i.e. it\n"
		"          hasn't been answered yet.) If this happens, the\n"
		"          application will return immediately.\n"
		"    'n' - don't answer the channel before playing the files\n"
		"    'm' - only break if a digit hit matches a one digit\n"
		"         extension in the destination context\n"
	},

	{
		.name = "Busy",
		.handler = pbx_builtin_busy,
		.synopsis = "Indicate busy condition and stop",
		.syntax = "Busy([timeout])",
		.description = "Requests that the channel indicate busy condition and then waits\n"
		"for the user to hang up or the optional timeout to expire.\n"
		"Always returns -1." 
	},

	{
		.name = "Congestion",
		.handler = pbx_builtin_congestion,
		.synopsis = "Indicate congestion and stop",
		.syntax = "Congestion([timeout])",
		.description = "Requests that the channel indicate congestion and then waits for\n"
		"the user to hang up or for the optional timeout to expire.\n"
		"Always returns -1." 
	},

	{
		.name = "DigitTimeout",
		.handler = pbx_builtin_dtimeout,
		.synopsis = "Set maximum timeout between digits",
		.syntax = "DigitTimeout(seconds)",
		.description = "Set the maximum amount of time permitted between digits when the\n"
		"user is typing in an extension. When this timeout expires,\n"
		"after the user has started to type in an extension, the extension will be\n"
		"considered complete, and will be interpreted. Note that if an extension\n"
		"typed in is valid, it will not have to timeout to be tested, so typically\n"
		"at the expiry of this timeout, the extension will be considered invalid\n"
		"(and thus control would be passed to the 'i' extension, or if it doesn't\n"
		"exist the call would be terminated). The default timeout is 5 seconds.\n"
		"Always returns 0.\n" 
		"DigitTimeout has been deprecated in favor of Set(TIMEOUT(digit)=timeout)\n"
	},

	{
		.name = "ExecIfTime",
		.handler = pbx_builtin_execiftime,
		.synopsis = "Conditional application execution on current time",
		.syntax = "ExecIfTime(times, weekdays, mdays, months ? appname[, arg, ...])",
		.description = "If the current time matches the specified time, then execute the specified\n"
		"application. Each of the elements may be specified either as '*' (for always)\n"
		"or as a range. See the 'include' syntax for details. It will return whatever\n"
		"<appname> returns, or a non-zero value if the application is not found.\n"
	},

	{
		.name = "Goto",
		.handler = pbx_builtin_goto, 
		.synopsis = "Goto a particular priority, extension, or context",
		.syntax = "Goto([[context, ]extension, ]priority)",
		.description = "Set the  priority to the specified\n"
		"value, optionally setting the extension and optionally the context as well.\n"
		"The extension BYEXTENSION is special in that it uses the current extension,\n"
		"thus  permitting you to go to a different context, without specifying a\n"
		"specific extension. Always returns 0, even if the given context, extension,\n"
		"or priority is invalid.\n" 
	},

	{
		.name = "GotoIf",
		.handler = pbx_builtin_gotoif,
		.synopsis = "Conditional goto",
		.syntax = "GotoIf(condition ? [context, [exten, ]]priority|label [: [context, [exten, ]]priority|label])",
		.description = "Go to label 1 if condition is\n"
		"true, to label2 if condition is false. Either label1 or label2 may be\n"
		"omitted (in that case, we just don't take the particular branch) but not\n"
		"both. Look for the condition syntax in examples or documentation." 
	},

	{
		.name = "GotoIfTime",
		.handler = pbx_builtin_gotoiftime,
		.synopsis = "Conditional goto on current time",
		.syntax = "GotoIfTime(times, weekdays, mdays, months ? [[context, ]extension, ]priority|label)",
		.description = "If the current time matches the specified time, then branch to the specified\n"
		"extension. Each of the elements may be specified either as '*' (for always)\n"
		"or as a range. See the 'include' syntax for details." 
	},

	{
		.name = "Hangup",
		.handler = pbx_builtin_hangup,
		.synopsis = "Unconditional hangup",
		.syntax = "Hangup()",
		.description = "Unconditionally hangs up a given channel by returning -1 always.\n" 
	},

	{
		.name = "ImportVar",
		.handler = pbx_builtin_importvar,
		.synopsis = "Import a variable from a channel into a new variable",
		.syntax = "ImportVar(newvar=channelname, variable)",
		.description = "This application imports a\n"
		"variable from the specified channel (as opposed to the current one)\n"
		"and stores it as a variable in the current channel (the channel that\n"
		"is calling this application). If the new variable name is prefixed by\n"
		"a single underscore \"_\", then it will be inherited into any channels\n"
		"created from this one. If it is prefixed with two underscores,then\n"
		"the variable will have infinite inheritance, meaning that it will be\n"
		"present in any descendent channel of this one.\n"
	},

	{
		.name = "NoOp",
		.handler = pbx_builtin_noop,
		.synopsis = "No operation",
		.syntax = "NoOp()",
		.description = "No-operation; Does nothing except relaxing the dialplan and \n"
		"re-scheduling over threads. It's necessary and very useful in tight loops." 
	},

	{
		.name = "Progress",
		.handler = pbx_builtin_progress,
		.synopsis = "Indicate progress",
		.syntax = "Progress()",
		.description = "Request that the channel indicate in-band progress is \n"
		"available to the user.\nAlways returns 0.\n" 
	},

	{
		.name = "ResetCDR",
		.handler = pbx_builtin_resetcdr,
		.synopsis = "Resets the Call Data Record",
		.syntax = "ResetCDR([options])",
		.description = "Causes the Call Data Record to be reset, optionally\n"
		"storing the current CDR before zeroing it out\n"
		" - if 'w' option is specified record will be stored.\n"
		" - if 'a' option is specified any stacked records will be stored.\n"
		" - if 'v' option is specified any variables will be saved.\n"
		"Always returns 0.\n"  
	},

	{
		.name = "ResponseTimeout",
		.handler = pbx_builtin_rtimeout,
		.synopsis = "Set maximum timeout awaiting response",
		.syntax = "ResponseTimeout(seconds)",
		.description = "Set the maximum amount of time permitted after\n"
		"falling through a series of priorities for a channel in which the user may\n"
		"begin typing an extension. If the user does not type an extension in this\n"
		"amount of time, control will pass to the 't' extension if it exists, and\n"
		"if not the call would be terminated. The default timeout is 10 seconds.\n"
		"Always returns 0.\n"  
		"ResponseTimeout has been deprecated in favor of Set(TIMEOUT(response)=timeout)\n"
	},

	{
		.name = "Ringing",
		.handler = pbx_builtin_ringing,
		.synopsis = "Indicate ringing tone",
		.syntax = "Ringing()",
		.description = "Request that the channel indicate ringing tone to the user.\n"
		"Always returns 0.\n" 
	},

	{
		.name = "SayAlpha",
		.handler = pbx_builtin_saycharacters,
		.synopsis = "Say Alpha",
		.syntax = "SayAlpha(string)",
		.description = "Spells the passed string\n" 
	},

	{
		.name = "SayDigits",
		.handler = pbx_builtin_saydigits,
		.synopsis = "Say Digits",
		.syntax = "SayDigits(digits)",
		.description = "Says the passed digits. SayDigits is using the\n" 
		"current language setting for the channel. (See app setLanguage)\n"
	},

	{
		.name = "SayNumber",
		.handler = pbx_builtin_saynumber,
		.synopsis = "Say Number",
		.syntax = "SayNumber(digits[, gender])",
		.description = "Says the passed number. SayNumber is using\n" 
		"the current language setting for the channel. (See app SetLanguage).\n"
	},

	{
		.name = "SayPhonetic",
		.handler = pbx_builtin_sayphonetic,
		.synopsis = "Say Phonetic",
		.syntax = "SayPhonetic(string)",
		.description = "Spells the passed string with phonetic alphabet\n" 
	},

	{
		.name = "Set",
		.handler = pbx_builtin_setvar,
	  	.synopsis = "Set channel variable(s)",
	  	.syntax = "Set(name1=value1, name2=value2, ...[, options])",
	  	.description = "This function can be used to set the value of channel variables.\n"
	  	"It will accept up to 24 name/value pairs.\n"
	  	"When setting variables, if the variable name is prefixed with _,\n"
	  	"the variable will be inherited into channels created from the\n"
	  	"current channel. If the variable name is prefixed with __,\n"
	  	"the variable will be inherited into channels created from the\n"
	  	"current channel and all child channels.\n"
	  	"The last argument, if it does not contain '=', is interpreted\n"
	  	"as a string of options. The valid options are:\n"
	  	"  g - Set variable globally instead of on the channel\n"
	},

	{
		.name = "SET",
		.handler = pbx_builtin_setvar,
	  	.synopsis = "[DEPRECATED: use Set()]",
	  NULL,
	  NULL,
	},

	{
		.name = "SetAccount",
		.handler = pbx_builtin_setaccount,
		.synopsis = "Sets account code",
		.syntax = "SetAccount([account])",
		.description = "Set the channel account code for billing\n"
		"purposes. Always returns 0.\n"
	},

	{
		.name = "SetAMAFlags",
		.handler = pbx_builtin_setamaflags,
		.synopsis = "Sets AMA Flags",
		.syntax = "SetAMAFlags([flag])",
		.description = "Set the channel AMA Flags for billing\n"
		"purposes. Always returns 0.\n"
	},

	{
		.name = "SetGlobalVar",
		.handler = pbx_builtin_setglobalvar,
		.synopsis = "Set global variable to value",
		.syntax = "SetGlobalVar(#n=value)",
		.description = "Sets global variable n to value. Global\n" 
		"variable are available across channels.\n"
	},

	{
		.name = "SetLanguage",
		.handler = pbx_builtin_setlanguage,
		.synopsis = "Sets channel language",
		.syntax = "SetLanguage(language)",
		.description = "Set the channel language to 'language'. This\n"
		"information is used for the syntax in generation of numbers, and to choose\n"
		"a natural language file when available.\n"
		"  For example, if language is set to 'fr' and the file 'demo-congrats' is \n"
		"requested to be played, if the file 'fr/demo-congrats' exists, then\n"
		"it will play that file, and if not will play the normal 'demo-congrats'.\n"
		"For some language codes, SetLanguage also changes the syntax of some\n"
		"CallWeaver functions, like SayNumber.\n"
		"Always returns 0.\n"
		"SetLanguage has been deprecated in favor of Set(LANGUAGE()=language)\n"
	},

	{
		.name = "SetVar",
		.handler = pbx_builtin_setvar_old,
	  	.synopsis = "Set channel variable(s)",
	  	.syntax = "SetVar(name1=value1, name2=value2, ...[, options])",
	  	.description = "SetVar has been deprecated in favor of Set.\n"
	},

	{
		.name = "Wait",
		.handler = pbx_builtin_wait, 
		.synopsis = "Waits for some time", 
		.syntax = "Wait(seconds)",
		.description = "Waits for a specified number of seconds, then returns 0.\n"
		"seconds can be passed with fractions of a second. (eg: 1.5 = 1.5 seconds)\n" 
	},

	{
		.name = "WaitExten",
		.handler = pbx_builtin_waitexten, 
		.synopsis = "Waits for an extension to be entered", 
		.syntax = "WaitExten([seconds][, options])",
		.description = "Waits for the user to enter a new extension for the \n"
		"specified number of seconds, then returns 0. Seconds can be passed with\n"
		"fractions of a seconds (eg: 1.5 = 1.5 seconds) or if unspecified the\n"
		"default extension timeout will be used.\n"
		"  Options:\n"
		"    'm[(x)]' - Provide music on hold to the caller while waiting for an extension.\n"
		"               Optionally, specify the class for music on hold within parenthesis.\n"
	},
};


static int unload_module(void)
{
	int i, res = 0;

	for (i = 0;  i < arraysize(func_list);  i++)
		cw_function_unregister(&func_list[i]);

	return res;
}

static int load_module(void)
{
	int i;

	for (i = 0;  i < arraysize(func_list);  i++)
		cw_function_register(&func_list[i]);

	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
