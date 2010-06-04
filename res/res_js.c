/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * res_js: Javascript in your dialplan
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */


#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <jsstddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jstypes.h>
#include <jsarena.h>
#include <jsutil.h>
#include <jsprf.h>
#include <jsapi.h>
#include <jsatom.h>
#include <jscntxt.h>
#include <jsdbgapi.h>
#include <jsemit.h>
#include <jsfun.h>
#include <jsgc.h>
#include <jslock.h>
#include <jsobj.h>
#include <jsparse.h>
#include <jsscope.h>
#include <jsscript.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/app.h"
#include "callweaver/options.h"
#include "callweaver/musiconhold.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/keywords.h"

#define EXITCODE_RUNTIME_ERROR 3
#define EXITCODE_FILE_NOT_FOUND 4

#include "callweaver.h"

CALLWEAVER_FILE_VERSION(__FILE__, "$Revision$")

size_t gStackChunkSize = 8192;

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif


static void *js_function;
static const char js_func_name[] = "JS";
static const char js_func_synopsis[] = "Executes a JavaScript function.";
static const char js_func_syntax[] = "JS(script_path)";
static const char js_func_desc[] = "Executes JavaScript Code\n"
	"If the script sets the channel variable JSFUNC\n"
	"that val will be returned to the dialplan.";


static jsuword gStackBase;
int gExitCode = 0;
JSBool gQuitting = JS_FALSE;
FILE *gErrFile = NULL;
FILE *gOutFile = NULL;

static
JSClass global_class = {
    "Global", JSCLASS_HAS_PRIVATE, 
    JS_PropertyStub,  JS_PropertyStub,  JS_PropertyStub,  JS_PropertyStub, 
    JS_EnumerateStub, JS_ResolveStub,   JS_ConvertStub,   JS_FinalizeStub
};

static const char tdesc[] = "Embedded JavaScript Application";

static void *app;
static const char name[] = "JavaScript";
static const char synopsis[] = "Embedded JavaScript Application";
static const char syntax[] = "";

static char global_dir[128] = "/usr/local/callweaver/logic";


static void
js_error(JSContext *cx, const char *message, JSErrorReport *report)
{
	CW_UNUSED(cx);
	CW_UNUSED(report);

	if (message)
		cw_log(CW_LOG_ERROR, "%s\n", message);
}

enum jchan_flags {
	JC_SECURE_FLAG = (1 << 0), 
	JC_BREACH_FATAL = (1 << 1), 
	JC_DEAD_FLAG = (1 << 2), 
	JC_LAST_FLAG = (1 << 31)
};

#define jc_test_flag(obj, flag) (obj->flags & flag) 
#define jc_add_flag(obj, flag) (obj->flags |= flag) 
#define jc_clear_flag(obj, flag) (obj->flags &= ~flag) 


#define MAX_LIST 256

static const char global_config_file[] = "js.conf";
static char global_app_list[MAX_LIST][MAX_LIST];
static char global_var_list[MAX_LIST][MAX_LIST];
static char global_func_list[MAX_LIST][MAX_LIST];
static int global_app_option_whitelist = 0;
static int global_var_option_whitelist = 0;
static int global_func_option_whitelist = 0;

struct jchan {
	struct cw_channel *chan;
	const char *name;
	char *context;
	char *exten;
	char *cid_num;
	char *cid_name;
	char *musicclass;
	int priority;
	int flags;
};

enum its_tinyid {
    CHAN_NAME, CHAN_CONTEXT, CHAN_EXTEN, CHAN_PRIORITY, CHAN_CALLERID_NUM, CHAN_CALLERID_NAME, CHAN_MUSICCLASS
};


static int stackDummy;
static JSRuntime *rt;


static int process_config(void) {
	struct cw_config *cfg;
    struct cw_variable *v;
	int i = 0, j = 0, k = 0;
	memset(global_app_list, 0, MAX_LIST * MAX_LIST);
	memset(global_var_list, 0, MAX_LIST * MAX_LIST);
	memset(global_func_list, 0, MAX_LIST * MAX_LIST);

	if ((cfg = cw_config_load(global_config_file))) {
		for (v = cw_variable_browse(cfg, "general"); v ; v = v->next) {
			if (!strcmp(v->name, "global_dir")) {
				strncpy(global_dir, v->value, sizeof(global_dir));
			}
		}
		for (v = cw_variable_browse(cfg, "security"); v ; v = v->next) {
			if (!strcasecmp(v->name, "app_list_type")) 
				global_app_option_whitelist = !strcasecmp(v->value, "white") ? 1 : 0;
			else if (!strcasecmp(v->name, "var_list_type"))
				global_var_option_whitelist = !strcasecmp(v->value, "white") ? 1 : 0;
			else if (!strcasecmp(v->name, "func_list_type"))
				global_func_option_whitelist = !strcasecmp(v->value, "white") ? 1 : 0;
			else if (!strcasecmp(v->name, "app") && i < MAX_LIST)
				strncpy(global_app_list[i++], v->value, MAX_LIST);
			else if (!strcasecmp(v->name, "var") && j < MAX_LIST)
				strncpy(global_app_list[j++], v->value, MAX_LIST);
			else if (!strcasecmp(v->name, "func") && j < MAX_LIST)
				strncpy(global_func_list[k++], v->value, MAX_LIST);
			
		}
		cw_config_destroy(cfg);
	} else {
		cw_log(CW_LOG_WARNING, "Cannot open %s\n", global_config_file);
		return -1;
	}

	return 0;
}


static int eval_some_js(char *code, JSContext *cx, JSObject *obj, jsval *rval) {
	JSScript *script;
	JS_ClearPendingException(cx);
	char *cptr;
	char path[512];
	int res = 0;

	if (code[0] == '~') {
		cptr = code + 1;
		script = JS_CompileScript(cx, obj, cptr, strlen(cptr), "inline", 1);
	} else {
		if (code[0] == '/') {
			script = JS_CompileFile(cx, obj, code);
		} else {
			snprintf(path, sizeof(path), "%s/%s", global_dir, code);
			script = JS_CompileFile(cx, obj, path);
		}
	}

	if (script) {
		res = JS_ExecuteScript(cx, obj, script, rval) == JS_TRUE ? 0 : -1;
		JS_DestroyScript(cx, script);
	}

	return res;
}


static JSBool
chan_up(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	JSBool arg = JS_FALSE;
	
	if (argc > 0) {
		arg = JSVAL_TO_BOOLEAN(argv[0]);
	}

	*rval = BOOLEAN_TO_JSVAL( cw_check_hangup(jc->chan) ? JS_FALSE : JS_TRUE );

	return arg == JS_TRUE ? *rval : JS_TRUE;
}

static JSBool
chan_waitfordigit(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char ret[2];
	int digit;
	
	if (argc > 0) {
		digit = cw_waitfordigit(jc->chan, JSVAL_TO_INT(argv[0]));
		if (digit <= 0) {
			*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
			return JS_TRUE;
		} 
		ret[0] = (char) digit;
		ret[1] = '\0';
		*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, ret) );
		
		return JS_TRUE;
	}

	cw_log(CW_LOG_ERROR, "Invalid Arguements.\n");
    return JS_FALSE;

}


static JSBool
chan_setmoh(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *class = NULL;
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(rval);

	if (argc > 0)
		class = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));
	if(class && jc->chan)
		strncpy(jc->chan->musicclass, class, sizeof(jc->chan->musicclass)-1);
	return JS_TRUE;
}

static JSBool
chan_mohstart(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *class = NULL;
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(rval);

	if (argc > 0)
		class = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));
	if(jc->chan)
		cw_moh_start(jc->chan, class);
	
	return JS_TRUE;
}

static JSBool
chan_mohstop(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(rval);

	if(jc->chan)
		cw_moh_stop(jc->chan);
	return JS_TRUE;
}

static JSBool
chan_wait(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(rval);

	if (argc > 0) {
		cw_safe_sleep(jc->chan, JSVAL_TO_INT(argv[0]));
		return JS_TRUE;
	}

	cw_log(CW_LOG_ERROR, "Invalid Arguements.\n");
    return JS_FALSE;

}

static JSBool
chan_getdigits(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char buf[512];
	char path_info[256];
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char *filename = NULL;
	char *path = NULL;
	struct cw_var_t *var;
	int maxdigits = 0;
	int timeout = 0;

	if (argc > 0)
		filename = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));

	if (argc > 1)
		maxdigits = JSVAL_TO_INT(argv[1]);
	if (argc > 2)
		timeout = JSVAL_TO_INT(argv[2]);


	if (strstr(filename, ".."))
		return JS_FALSE;

	if ((var = pbx_builtin_getvar_helper(jc->chan, CW_KEYWORD_private_sound_dir, "private_sound_dir"))) {
		snprintf(path_info, sizeof(path_info), "%s/%s", var->value, filename);
		cw_object_put(var);
		path = path_info;
	} else 
		path = filename;


	memset(buf, 0, sizeof(buf));
	cw_app_getdata(jc->chan, path && !cw_strlen_zero(path) ? path : NULL, buf, maxdigits, timeout);
	if (!cw_strlen_zero(buf)) {
		*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, buf) );
	} else 
		*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
	return JS_TRUE;
}

static JSBool
chan_getvar(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	JSString *str = NULL;
	char *varname = NULL;
	struct cw_var_t *var;
	int x = 0, deny = 0;

	if (argc > 0) {
		if ((str = JS_ValueToString(cx, argv[0])) && (varname = JS_GetStringBytes(str))) {
			if (!strncmp(varname, "private_", 8)) {
				*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
				return JS_TRUE;
			}

			if (jc_test_flag(jc, JC_SECURE_FLAG)) {
				if (global_var_option_whitelist)
					deny = 1;
				for (x=0; x < MAX_LIST; x++) {
					if (!strcasecmp(global_app_list[x], varname)) {
						deny = !deny;
						break;
					}
				}
				
				if (deny) {
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_3"Usage of Var [%s] Blocked by security measures.\n", varname);
					if (jc_test_flag(jc, JC_BREACH_FATAL)) {
						cw_log(CW_LOG_WARNING, "Execution Halted by security measures.\n");
						cw_softhangup(jc->chan, CW_SOFTHANGUP_EXPLICIT);
						return JS_FALSE;
					} else 
						return JS_TRUE;
				}
				
			}

			if ((var = pbx_builtin_getvar_helper(jc->chan, cw_hash_var_name(varname), varname))) {
				*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, var->value));
				cw_object_put(var);
			} else 
				*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
		}
		return JS_TRUE;
	}

   return JS_FALSE;
}


static JSBool
chan_setvar(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	JSString *str = NULL;
    char *varname = NULL;
	char *varval = NULL;
	int x = 0, deny = 0;
	
    if (argc > 1) {
		if ((str = JS_ValueToString(cx, argv[0])) && (varname = JS_GetStringBytes(str))) {
			if (!strncmp(varname, "private_", 8)) {
				*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
				return JS_TRUE;
			}
			if (jc_test_flag(jc, JC_SECURE_FLAG)) {
				if (global_var_option_whitelist)
					deny = 1;
				for (x=0; x < MAX_LIST; x++) {
					if (!strcasecmp(global_app_list[x], varname)) {
						deny = !deny;
						break;
					}
				}
				
				if (deny) {
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_3"Usage of Var [%s] Blocked by security measures.\n", varname);
					if (jc_test_flag(jc, JC_BREACH_FATAL)) {
						cw_log(CW_LOG_WARNING, "Execution Halted by security measures.\n");
						cw_softhangup(jc->chan, CW_SOFTHANGUP_EXPLICIT);
						return JS_FALSE;
					} else 
						return JS_TRUE;
				}
				
			}
			if ((varval = JS_GetStringBytes(JS_ValueToString(cx, argv[1])))) {
				pbx_builtin_setvar_helper(jc->chan, varname, varval);
				*rval = BOOLEAN_TO_JSVAL( JS_TRUE );
			} else 
				*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
		}
		return JS_TRUE;
	}

   return JS_FALSE;
}


static JSBool
chan_exec(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char *appname = NULL, *data = NULL;
	int x = 0;
	int deny = 0;

	if (argc > 0)
		appname = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));
	if (argc > 1)
		data = JS_GetStringBytes(JS_ValueToString(cx, argv[1]));
	
	if (appname) {
		if (jc_test_flag(jc, JC_SECURE_FLAG)) {
			if (global_app_option_whitelist)
				deny = 1;
			for (x=0; x < MAX_LIST; x++) {
				if (!strcasecmp(global_app_list[x], appname)) {
					deny = !deny;
					break;
				}
			}

			if (deny) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3"Execution of [%s] Blocked by security measures.\n", appname);
				if (jc_test_flag(jc, JC_BREACH_FATAL)) {
					cw_log(CW_LOG_WARNING, "Execution Halted by security measures.\n");
					cw_softhangup(jc->chan, CW_SOFTHANGUP_EXPLICIT);
					return JS_FALSE;
				} else 
					return JS_TRUE;
			}
			
		}

		data = strdup(data ? data : "");
		*rval = BOOLEAN_TO_JSVAL ( cw_function_exec_str(jc->chan, cw_hash_string(appname), appname, data, NULL) ? JS_FALSE : JS_TRUE );
		if (data)
			free(data);

		return JS_TRUE;
	}

	cw_log(CW_LOG_ERROR, "Invalid Arguements.\n");
	return JS_FALSE;
}



static JSBool
chan_execfunc(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char *fdata = NULL, *fname = NULL;
	char *args, *p;
	int x = 0;
	int deny = 0;

	if (argc > 0)
		fdata = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));

	if (fdata && (fname = cw_strdupa(fdata))) {
		if ((args = strchr(fname, '('))) {
			*(args++) = '\0';
			if ((p = strrchr(args, ')')))
				*p = '\0';
		}

		if (jc_test_flag(jc, JC_SECURE_FLAG)) {
			if (global_func_option_whitelist)
				deny = 1;
			for (x=0; x < MAX_LIST; x++) {
				if (!strcasecmp(global_func_list[x], fname)) {
					deny = !deny;
					break;
				}
			}

			if (deny) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3"Execution of [%s] Blocked by security measures.\n", fdata);
				if (jc_test_flag(jc, JC_BREACH_FATAL)) {
					cw_log(CW_LOG_WARNING, "Execution Halted by security measures.\n");
					cw_softhangup(jc->chan, CW_SOFTHANGUP_EXPLICIT);
					return JS_FALSE;
				} else 
					return JS_TRUE;
			}
			
		}

		if (!cw_function_exec_str(jc->chan, cw_hash_string(fname), fname, args, &ds))
			*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, ds.data) );

		free(fname);
		return JS_TRUE;
	}

	cw_log(CW_LOG_ERROR, "Invalid Arguements.\n");
	return JS_FALSE;
}

static JSBool
chan_hangup(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(rval);

	cw_softhangup(jc->chan, CW_SOFTHANGUP_EXPLICIT);
	return JS_TRUE;
}


static JSBool
chan_answer(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);

	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(rval);

	cw_answer(jc->chan);
	return JS_TRUE;
}

static JSBool
chan_recordfile(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char path_info[256] = "";
	struct cw_var_t *var;
	char *filename = NULL;
	const char *silence = "", *maxduration = "", *options = "";
	
	if (argc > 0)
		filename = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));
	if (argc > 1)
		silence = JS_GetStringBytes(JS_ValueToString(cx, argv[1]));
	if (argc > 2)
		maxduration = JS_GetStringBytes(JS_ValueToString(cx, argv[2]));
	if (argc > 3)
		options = JS_GetStringBytes(JS_ValueToString(cx, argv[3]));
	
	if (strstr(filename, ".."))
		return JS_FALSE;

	if (filename) {
		if ((var = pbx_builtin_getvar_helper(jc->chan, CW_KEYWORD_private_sound_dir, "private_sound_dir"))) {
			snprintf(path_info, sizeof(path_info), "%s/%s,%s,%s,%s", var->value, filename, silence, maxduration, options);
			cw_object_put(var);
		} else
			snprintf(path_info, sizeof(path_info), "%s,%s,%s,%s", filename, silence, maxduration, options);
	} else {
		cw_log(CW_LOG_ERROR, "Invalid Arguments.\n");
		return JS_FALSE;
	}
	
	*rval = BOOLEAN_TO_JSVAL ( cw_function_exec_str(jc->chan, CW_KEYWORD_Record, "Record", path_info, NULL) ? JS_FALSE : JS_TRUE );
	return JS_TRUE;
	
}

static JSBool
chan_streamfile(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char path_info[256];
	char ret[2];
	struct cw_var_t *var;
	JSString *str = NULL;
	char *filename = NULL;
	char *path = NULL;
	int res = 0;

	if (argc > 0) {
		if ((str = JS_ValueToString(cx, argv[0])) && (filename = JS_GetStringBytes(str))) {
			if (strstr(filename, ".."))
				return JS_FALSE;
			if ((var = pbx_builtin_getvar_helper(jc->chan, CW_KEYWORD_private_sound_dir, "private_sound_dir"))) {
				snprintf(path_info, sizeof(path_info), "%s/%s", var->value, filename);
				cw_object_put(var);
				path = path_info;
			} else 
				path = filename;

			res = cw_streamfile(jc->chan, path, jc->chan->language);
			if (!res) {
				if ((res = cw_waitstream(jc->chan, CW_DIGIT_ANY)) < 0)
					return JS_FALSE;
				if (res > 0) {
					ret[0] = (char) res;
					ret[1] = '\0';
					*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, ret) );
				} else 
					*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
			} else
				return JS_FALSE;

			return JS_TRUE;
		}
	}
	return JS_FALSE;

}


static JSBool
chan_dbdel(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{

        char *family, *key;
        JSString *str = NULL;
        int res;

	CW_UNUSED(obj);
	CW_UNUSED(argc);

        *rval = BOOLEAN_TO_JSVAL (JSVAL_FALSE);

        if ( !(( str = JS_ValueToString(cx, argv[0])) && ( family = JS_GetStringBytes(str)))) {
                return JS_FALSE;
        }
        if ( !(( str = JS_ValueToString(cx, argv[1])) && ( key = JS_GetStringBytes(str)))) {
                return JS_FALSE;
        }

        res = cw_db_del(family, key);
        if (!res) {
	        *rval = BOOLEAN_TO_JSVAL ( JSVAL_TRUE);
                return JS_TRUE;
        }

        return JS_TRUE; // return *val as JSVAL_FALSE
}
static JSBool
chan_dbput(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
        char *family, *key, *value;
        JSString *str = NULL;
        int res;

	CW_UNUSED(obj);
	CW_UNUSED(argc);

        *rval = BOOLEAN_TO_JSVAL (JSVAL_NULL);

        if ( !(( str = JS_ValueToString(cx, argv[0])) && ( family = JS_GetStringBytes(str)))) {
                return JS_FALSE; 
        }
        if ( !(( str = JS_ValueToString(cx, argv[1])) && ( key = JS_GetStringBytes(str)))) {
                return JS_FALSE;
        }
        if ( !(( str = JS_ValueToString(cx, argv[2])) && ( value = JS_GetStringBytes(str)))) {
                return JS_FALSE;
        }

        res = cw_db_put(family, key, value);

        if (!res) {
		*rval = BOOLEAN_TO_JSVAL ( JSVAL_TRUE);		
                return JS_TRUE;
        }

        return JS_FALSE;

}
static JSBool
chan_dbget(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
        JSString *str = NULL;
        char *family, *key;

	CW_UNUSED(obj);
	CW_UNUSED(argc);

	*rval = BOOLEAN_TO_JSVAL(JSVAL_NULL);
 
        if (!(( str = JS_ValueToString(cx, argv[0])) && ( family = JS_GetStringBytes(str) ))) {
                return JS_FALSE;
        }
        if (!(( str = JS_ValueToString(cx, argv[1])) && ( key = JS_GetStringBytes(str) ))) {
                return JS_FALSE;
        }

	if (!cw_db_get(family, key, &ds)) {
		if (!ds.used)
			*rval = BOOLEAN_TO_JSVAL(JSVAL_NULL);  // NULL
		else
			*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, ds.data)); // Not null

		cw_dynstr_free(&ds);
	}

        return JS_TRUE;
}

static JSFunctionSpec chan_methods[] = {
    {"GetVar", chan_getvar, 1}, 
    {"SetVar", chan_setvar, 2}, 
    {"DBGet", chan_dbget, 2},
    {"DBPut", chan_dbput, 3},
    {"DBDel", chan_dbdel, 2},
    {"StreamFile", chan_streamfile, 1}, 
    {"RecordFile", chan_recordfile, 1}, 
    {"Exec", chan_exec, 2}, 
    {"ExecFunc", chan_execfunc, 2}, 
    {"Hangup", chan_hangup, 0}, 
    {"Answer", chan_answer, 0}, 
	{"GetDigits", chan_getdigits, 2}, 
	{"WaitForDigit", chan_waitfordigit, 1}, 
	{"Wait", chan_wait, 1}, 
	{"Up", chan_up, 1}, 
	{"StartMusic", chan_mohstart, 1}, 
	{"StopMusic", chan_mohstop, 0}, 
	{"SetMusic", chan_setmoh, 1}, 
	{0}
};


static JSPropertySpec chan_props[] = {
    {"name", CHAN_NAME, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"context", CHAN_CONTEXT, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"exten", CHAN_EXTEN, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"priority", CHAN_PRIORITY, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"callerid_num", CHAN_CALLERID_NUM, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"callerid_name", CHAN_CALLERID_NAME, JSPROP_READONLY|JSPROP_PERMANENT}, 
	{"musicclass", CHAN_MUSICCLASS, JSPROP_READONLY|JSPROP_PERMANENT}, 
    {0}
};

static JSBool
chan_getProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
	struct jchan *jc = JS_GetPrivate(cx, obj);
	char *propname = JS_GetStringBytes(JS_ValueToString(cx, id));
	JSBool res = JS_TRUE;

	/* numbers are our props anything else is a method */
	if (!isdigit(propname[0]))
		return JS_TRUE;

	switch (atoi(propname)) {
	case CHAN_NAME:
		if (!jc->name) {
			jc->name = jc->chan->name;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->name) );
		}
		break;

	case CHAN_CONTEXT:
		if (!jc->context) {
			jc->context = jc->chan->context;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->context) );
		}
		break;

	case CHAN_EXTEN:
		if (!jc->exten) {
			jc->exten = jc->chan->exten;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->exten) );
		}
		break;

	case CHAN_PRIORITY:
		if (!jc->priority) {
			jc->priority = jc->chan->priority;
			*vp = INT_TO_JSVAL ( jc->priority );
		}
		break;

	case CHAN_CALLERID_NUM:
		if (!jc->cid_num) {
			jc->cid_num = jc->chan->cid.cid_num;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->cid_num ) );
		}
		break;

	case CHAN_CALLERID_NAME:
		if (!jc->cid_name) {
			jc->cid_name = jc->chan->cid.cid_name;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->cid_name ) );
		}
		break;

	case CHAN_MUSICCLASS:
		if (!jc->musicclass) {
            jc->musicclass = jc->chan->musicclass;
			*vp = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, jc->musicclass ) );
		}
		break;

	default:
		res = JS_FALSE;
		break;

	}

    return res;
}


JSClass chan_class = {
    "Chan", JSCLASS_HAS_PRIVATE, 
	JS_PropertyStub,  JS_PropertyStub,  chan_getProperty,  JS_PropertyStub, 
    JS_EnumerateStub, JS_ResolveStub,   JS_ConvertStub,   JS_FinalizeStub
};



#ifdef HAVE_CURL

struct config_data {
	JSContext *cx;
	JSObject *obj;
	char *name;
};

static size_t realtime_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
    register int realsize = size * nmemb;
	char *line = NULL, *nextline = NULL, *val = NULL, *p = NULL;
	jsval rval;
	struct config_data *config_data = data;
	char code[256];

	if (config_data->name && (line = cw_strdupa((char *) ptr))) {
		while (line) {
			if ((nextline = strchr(line, '\n'))) {
				*nextline = '\0';
				nextline++;
			}
			
			if ((val = strchr(line, '='))) {
                *val = '\0';
                val++;
                if (val[0] == '>') {
                    *val = '\0';
                    val++;
                }
				
                for (p = line; p && *p == ' '; p++);
                line = p;
                for (p=line+strlen(line)-1;*p == ' '; p--)
                    *p = '\0';
                for (p = val; p && *p == ' '; p++);
                val = p;
                for (p=val+strlen(val)-1;*p == ' '; p--)
                    *p = '\0';

				snprintf(code, sizeof(code), "~%s[\"%s\"] = \"%s\"", config_data->name, line, val);
				eval_some_js(code, config_data->cx, config_data->obj, &rval);

            }

			line = nextline;
		}
	} 
	return realsize;

}


static JSBool
js_fetchurl(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *url = NULL, *name = NULL;
	CURL *curl_handle = NULL;
	struct config_data config_data;
	
	if ( argc > 0 && (url = JS_GetStringBytes(JS_ValueToString(cx, argv[0])))) {
		if (argc > 1)
			name = JS_GetStringBytes(JS_ValueToString(cx, argv[1]));
		curl_global_init(CURL_GLOBAL_ALL);
		curl_handle = curl_easy_init();
		if (!strncasecmp(url, "https", 5)) {
			curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0);
			curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0);
		}
		config_data.cx = cx;
		config_data.obj = obj;
		if (name)
			config_data.name = name;
		curl_easy_setopt(curl_handle, CURLOPT_URL, url);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, realtime_callback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&config_data);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "callweaver-js/1.0");
		curl_easy_perform(curl_handle);
		curl_easy_cleanup(curl_handle);
    } else {
		cw_log(CW_LOG_ERROR, "Error!\n");
		return JS_FALSE;
	}

	return JS_TRUE;
}
#endif


static int write_buf(int fd, const char *buf) {

	size_t len = strlen(buf);
	if (fd && write(fd, buf, len) != len) {
		close(fd);
		return 0;
	}

	return 1;
}


static JSBool
js_unlinksound(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char path_info[256];
	struct cw_channel *chan = JS_GetPrivate(cx, obj);
	struct cw_var_t *var;
	char *path = NULL, *filename = NULL;

	CW_UNUSED(rval);

	if (strstr(filename, ".."))
		return JS_FALSE;

	if ( chan && argc > 0 && (filename = JS_GetStringBytes(JS_ValueToString(cx, argv[0])))) {
		if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_private_sound_dir, "private_sound_dir"))) {
			snprintf(path_info, sizeof(path_info), "%s/%s", var->value, filename);
			cw_object_put(var);
			path = path_info;
		} else
			return JS_FALSE;

		if (path)
			unlink(path);
	}
	
	return JS_FALSE;
}


#define B64BUFFLEN 1024
static const char c64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static JSBool
js_email(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char path_info[256];
	char filename[80], buf[B64BUFFLEN];
	unsigned char in[B64BUFFLEN];
	unsigned char out[B64BUFFLEN+512];
	char *to = NULL, *from = NULL, *headers, *body = NULL, *file = NULL;
	const char *bound = "XXXX_boundary_XXXX";
	char *path = NULL;
	struct cw_var_t *var;
	struct cw_channel *chan = JS_GetPrivate(cx, obj);
	int fd = 0, ifd = 0;
	int x=0, y=0, bytes=0, ilen=0;
	unsigned int b=0, l=0;

	CW_UNUSED(rval);

	if ( chan && 
		 argc > 3 && 
		 (from = JS_GetStringBytes(JS_ValueToString(cx, argv[0]))) &&
		 (to = JS_GetStringBytes(JS_ValueToString(cx, argv[1]))) &&
		 (headers = JS_GetStringBytes(JS_ValueToString(cx, argv[2]))) &&
		 (body = JS_GetStringBytes(JS_ValueToString(cx, argv[3]))) 
		 ) {
		if ( argc > 4)
			file = JS_GetStringBytes(JS_ValueToString(cx, argv[4]));
		
		
		snprintf(filename, 80, "/tmp/mail.%ld.%ld", time(NULL), (long)pthread_self());

		if ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644))) {
			if (file) {
				if (strstr(file, ".."))
					return JS_FALSE;
				if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_private_sound_dir, "private_sound_dir"))) {
					snprintf(path_info, sizeof(path_info), "%s/%s", var->value, file);
					cw_object_put(var);
					path = path_info;
				} else 
					path = file;

				if ((ifd = open(path, O_RDONLY)) < 1) {
					return JS_FALSE;
				}

				
				snprintf(buf, B64BUFFLEN, "MIME-Version: 1.0\nContent-Type: multipart/mixed; boundary=\"%s\"\n", bound);
				if (!write_buf(fd, buf))
					return JS_FALSE;
			}
			
			if (!write_buf(fd, headers))
				return JS_FALSE;

			if (!write_buf(fd, "\n\n"))
				return JS_FALSE;
			
			if (file) {
				snprintf(buf, B64BUFFLEN, "--%s\nContent-Type: text/plain\n\n", bound);
				if (!write_buf(fd, buf))
					return JS_FALSE;
			}
			
			if (!write_buf(fd, body))
				return JS_FALSE;
			
			if (file) {
				snprintf(buf, B64BUFFLEN, "\n\n--%s\nContent-Type: application/octet-stream\nContent-Transfer-Encoding: base64\nContent-Description: Sound attachment.\nContent-Disposition: attachment; filename=\"%s\"\n\n", bound, file);
				if (!write_buf(fd, buf))
					return JS_FALSE;
				
				while((ilen=read(ifd, in, B64BUFFLEN))) {
					for (x=0;x<ilen;x++) {
						b = (b<<8) + in[x];
						l += 8;
						while (l >= 6) {
							out[bytes++] = c64[(b>>(l-=6))%64];
							if (++y!=72)
								continue;
							out[bytes++] = '\n';
							y=0;
						}
					}
					if (write(fd, &out, bytes) != bytes) { 
						return -1;
					} else 
						bytes=0;
					
				}
				
				if (l > 0)
					out[bytes++] = c64[((b%16)<<(6-l))%64];
				if (l != 0) while (l < 6)
					out[bytes++] = '=', l += 2;
	
				if (write(fd, &out, bytes) != bytes) { 
					return -1;
				}

			}
			

			
			if (file) {
				snprintf(buf, B64BUFFLEN, "\n\n--%s--\n.\n", bound);
				if (!write_buf(fd, buf))
					return JS_FALSE;
			}
		}

		if (fd)
			close(fd);
		if (ifd)
			close(ifd);

		snprintf(buf, B64BUFFLEN, "/bin/cat %s | /usr/sbin/sendmail -tf \"%s\" %s", filename, from, to);
		cw_safe_system(buf);
		unlink(filename);

		if (option_verbose > 2) {
			if (file)
				cw_verbose(VERBOSE_PREFIX_3"Emailed file [%s] to [%s]\n", filename, to);
			else
				cw_verbose(VERBOSE_PREFIX_3"Emailed data to [%s]\n", to);
		}
		return JS_TRUE;
	}
	

	return JS_FALSE;
}

static JSBool
js_die(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *msg = NULL;

	CW_UNUSED(obj);
	CW_UNUSED(rval);

	if ( argc > 0 && (msg = JS_GetStringBytes(JS_ValueToString(cx, argv[0])))) {
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3"Javascript Die: %s\n", msg);
	}
	return JS_FALSE;
}

static JSBool
js_getvar(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	JSString *str;
	char *varname;
	struct cw_var_t *var;

	CW_UNUSED(obj);

	if (argc > 0) {
		if ((str = JS_ValueToString(cx, argv[0])) && (varname = JS_GetStringBytes(str))) {
			if ((var = pbx_builtin_getvar_helper(NULL, cw_hash_var_name(varname), varname))) {
				*rval = STRING_TO_JSVAL ( JS_NewStringCopyZ(cx, var->value));
				cw_object_put(var);
			} else 
				*rval = BOOLEAN_TO_JSVAL( JS_FALSE );
		}
		return JS_TRUE;
	}

	return JS_FALSE;
}

static JSBool
js_verbose(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	JSString *str;
	const char *prefix = "";
	int32 level = 0;
	JSBool ok;

	CW_UNUSED(obj);
	CW_UNUSED(argc);
	CW_UNUSED(rval);

	if ((ok = JS_ValueToInt32(cx, argv[0], &level))) {
		if ((str = JS_ValueToString(cx, argv[1]))) {
			if (option_verbose >= level) {
				switch(level) {
				case 1:
					prefix = VERBOSE_PREFIX_1;
					break;
				case 2:
					prefix = VERBOSE_PREFIX_2;
					break;
				case 3:
					prefix = VERBOSE_PREFIX_3;
					break;
				case 4:
					prefix = VERBOSE_PREFIX_4;
					break;
				default:
					prefix = "";
					break;
				};

				cw_verbose("%s%s", prefix, JS_GetStringBytes(str));
			}
		} else
			return JS_FALSE;
	} else
		return JS_FALSE;

	return JS_TRUE;

}


static JSBool
js_include(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *code;
	if ( argc > 0 && (code = JS_GetStringBytes(JS_ValueToString(cx, argv[0])))) {
		eval_some_js(code, cx, obj, rval);
		return JS_TRUE;
	}
	cw_log(CW_LOG_ERROR, "Invalid Arguements\n");
	return JS_FALSE;
}

static JSBool
js_log(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
	char *log;
	char *msg;

	CW_UNUSED(obj);
	CW_UNUSED(rval);

	if (argc > 1) {
		if ((log = JS_GetStringBytes(JS_ValueToString(cx, argv[0]))) &&
		   (msg = JS_GetStringBytes(JS_ValueToString(cx, argv[1])))) {

			if (!strcasecmp(log, "LOG_EVENT"))
				cw_log(CW_LOG_EVENT, msg);
			else if (!strcasecmp(log, "LOG_NOTICE"))
				cw_log(CW_LOG_NOTICE, msg);
			else if (!strcasecmp(log, "LOG_WARNING"))
				cw_log(CW_LOG_WARNING, msg);
			else if (!strcasecmp(log, "LOG_ERROR"))
				cw_log(CW_LOG_ERROR, msg);
			else if (!strcasecmp(log, "LOG_VERBOSE"))
				cw_log(CW_LOG_VERBOSE, msg);
			else
				cw_log(CW_LOG_EVENT, msg);
			return JS_TRUE;
		} 
	} 
	cw_log(CW_LOG_ERROR, "Invalid Arguements\n");
	return JS_FALSE;

}


static JSFunctionSpec callweaver_functions[] = {
	{"Verbose", js_verbose, 2}, 
	{"Log", js_log, 2}, 
	{"Include", js_include, 1}, 
	{"GetVar", js_getvar, 1}, 
	{"DBGet", chan_dbget, 2},
	{"DBPut", chan_dbput, 3},
	{"DBDel", chan_dbdel, 2},
	{"Die", js_die, 1}, 
	{"Email", js_email, 1}, 
	{"UnLinkSound", js_unlinksound, 1}, 
#ifdef HAVE_CURL
	{"FetchURL", js_fetchurl, 1}, 
#endif
	{0}
};

static JSFunctionSpec secure_callweaver_functions[] = {
	{"Verbose", js_verbose, 2}, 
	{"Log", js_log, 2}, 
	{"Include", js_include, 1}, 
	{"Die", js_die, 1}, 
	{"Email", js_email, 1}, 
	{"UnLinkSound", js_unlinksound, 1}, 
#ifdef HAVE_CURL
	{"FetchURL", js_fetchurl, 1}, 
#endif 
	{0}
};


static JSObject *new_jchan(JSContext *cx, JSObject *obj, struct cw_channel *chan, struct jchan *jc, int flags) {
	JSObject *Chan;
	if ((Chan = JS_DefineObject(cx, obj, "Chan", &chan_class, NULL, 0))) {
		memset(jc, 0, sizeof(struct jchan));
		jc->chan = chan;
		jc->flags = flags;
		if ((JS_SetPrivate(cx, Chan, jc) &&
			  JS_DefineProperties(cx, Chan, chan_props) &&
			  JS_DefineFunctions(cx, Chan, chan_methods))) {
			return Chan;
		}
	}
	
	return NULL;
}

static int js_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char buf[512];
	struct localuser *u;
	char *arg, *nextarg;
	JSContext *cx;
	JSObject *glob, *Chan;
	int res=-1;
	jsval rval;
	struct jchan jc;
	int x = 0, y = 0;
	int flags = 0;

	CW_UNUSED(result);

	if (argc < 1 || !argv[0][0]) {
		cw_log(CW_LOG_ERROR, "js requires an argument (filename|code)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (argv[0][0] == '-') {
		argv[0]++;
		for (; argv[0][0]; argv[0]++) {
			switch (argv[0][0]) {
				case 'f': flags |= JC_BREACH_FATAL | JC_SECURE_FLAG; break;
				case 's': flags |= JC_SECURE_FLAG; break;
			}
		}
		argv++, argc--;
	}

    if ((cx = JS_NewContext(rt, gStackChunkSize))) {
		JS_SetErrorReporter(cx, js_error);
		if ((glob = JS_NewObject(cx, &global_class, NULL, NULL)) && 
			JS_DefineFunctions(cx, glob, (flags & JC_SECURE_FLAG) ? secure_callweaver_functions : callweaver_functions) &&
			JS_InitStandardClasses(cx, glob) &&
			(Chan = new_jchan(cx, glob, chan, &jc, flags))) {
			JS_SetGlobalObject(cx, glob);
			JS_SetPrivate(cx, glob, chan);
			res = 0;
	
			for (; argc; argv++, argc--) {
				if ((arg = strchr(argv[0], ':'))) {
					for (y=0;(arg=strchr(arg, ':'));y++)
						arg++;
					arg = strchr(argv[0], ':');
					*arg = '\0';
					arg++;
					snprintf(buf, sizeof(buf), "~var Argv = new Array(%d);", y);
					eval_some_js(buf, cx, glob, &rval);
					snprintf(buf, sizeof(buf), "~var argc = %d", y);
					eval_some_js(buf, cx, glob, &rval);
					do {
						if ((nextarg = strchr(arg, ':'))) {
							*nextarg = '\0';
							nextarg++;
						}
						snprintf(buf, sizeof(buf), "~Argv[%d] = \"%s\";", x++, arg);
						eval_some_js(buf, cx, glob, &rval);
						arg = nextarg;
					} while (arg);
				}
				if ((res=eval_some_js(argv[0], cx, glob, &rval)) < 0) {
					break;
				} 
			}
		}
	}
	
	
	if (cx)
		JS_DestroyContext(cx);

	LOCAL_USER_REMOVE(u);
	return res;
}

static int function_js_read(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	struct cw_var_t *var;

	if (argc < 1 || !argv[0][0])
		return cw_function_syntax(js_func_syntax);

	if (js_exec(chan, argc, argv, NULL) > -1 && (var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_JSFUNC, "JSFUNC"))) {
		cw_dynstr_printf(result, "%s", var->value);
		cw_object_put(var);
		return 0;
	}
	return -1;
}

static int reload_module(void) {
	return process_config();
}

static int unload_module(void)
{
	int res = 0;

	if (rt)
		JS_DestroyRuntime(rt);
    JS_ShutDown();
	cw_unregister_function(js_function); 
	res |= cw_unregister_function(app);
	return res;
}

static int load_module(void)
{

	gStackBase = (jsuword)&stackDummy;
	gErrFile = stderr;
	gOutFile = stdout;


    rt = JS_NewRuntime(64L * 1024L * 1024L);
    if (!rt)
        return -1;

	process_config();
	js_function = cw_register_function(js_func_name, function_js_read, js_func_synopsis, js_func_syntax, js_func_desc); 
	app = cw_register_function(name, js_exec, synopsis, syntax, tdesc);
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
