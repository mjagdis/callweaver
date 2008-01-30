/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * LDAP Directory lookup function
 *
 * Copyright (C) 2004,2005 Sven Slezak <sunny@mezzo.net>
 * http://www.mezzo.net/asterisk
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <ldap.h>

#include "callweaver/options.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/lock.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"

#define LDAP_CONFIG "ldap.conf"

char *replace_cw_vars(struct cw_channel *chan, const char *string);
int ldap_lookup(char *host, int port, int version, int timeout, char *user, char *pass, char *base, char *scope, char *filter, char *attribute, char *result);
static int strconvert(const char *incharset,
                      const char *outcharset,
#if 0
                      const char *in,
#else
                      char *in,
#endif
                      char *out);
static char *strtrim(char *string);

static const char tdesc[] = "LDAP directory lookup function for CallWeaver extension logic.";

static void *g_app = "LDAPget";
static const char g_name[] = "LDAPget";
static const char g_synopsis[] = "Retrieve a value from an ldap directory";
static const char g_syntax[] = "LDAPget(varname=config-file-section/key)";
static const char g_descrip[] =
"Retrieves a value from an LDAP directory and stores it in the given variable.\n"
"Upon exit, set the variable LDAPSTATUS to either SUCCESS or FAILURE.\n"
"Always returns 0.\n";

static int ldap_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
    struct localuser *u;
    char *varname;
    char *config;
    char *keys = NULL;
    char *key = NULL;
    char *tail = NULL;
    char *result_conv;
    struct cw_config *cfg;
    int port = LDAP_PORT;
    int version = LDAP_VERSION2;
    int timeout = 10;
    char *temp;
    char *host;
    char *user;
    char *pass;
    char *base;
    char *scope;
    char *filter;
    char *_filter;
    char *attribute;
    char *convert_from = NULL;
    char *convert_to = NULL;

    if (argc != 1)
    {
        pbx_builtin_setvar_helper(chan, "LDAPSTATUS", "FAILURE");
        return cw_function_syntax(g_syntax);
    }

    LOCAL_USER_ADD(u);

    if (strchr(argv[0], '='))
    {
        varname = strsep (&argv[0], "=");
        if (strchr(argv[0], '/'))
        {
            config = strsep(&argv[0], "/");
            keys = strsep(&argv[0], "\0");
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "LDAPget: varname=%s, config-section=%s, keys=%s\n", varname, config, keys);
        }
        else
        {
            config = strsep(&argv[0], "\0");
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "LDAPget: varname=%s, config-section=%s\n", varname, config);
        }
        if (!varname  ||  !config)
        {
            cw_log(CW_LOG_WARNING, "Ignoring; Syntax error in argument\n");
            pbx_builtin_setvar_helper(chan, "LDAPSTATUS", "FAILURE");
            return 0;
        }
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Ignoring, no parameters\n");
        pbx_builtin_setvar_helper(chan, "LDAPSTATUS", "FAILURE");
        return 0;
    }

    cfg = cw_config_load(LDAP_CONFIG);
    if (!cfg)
    {
        cw_log(CW_LOG_WARNING, "No such configuration file %s\n", LDAP_CONFIG);
        return -1;
    }
    if (!(host = cw_variable_retrieve(cfg, config, "host")))
    {
        host = "localhost";
    }
    if ((temp = cw_variable_retrieve(cfg, config, "port")))
    {
        port = atoi(temp);
    }
    if ((temp = cw_variable_retrieve(cfg, config, "timeout")))
    {
        timeout = atoi(temp);
    }
    if ((temp = cw_variable_retrieve(cfg, config, "version")))
    {
        version = atoi(temp);
    }
    user = cw_variable_retrieve(cfg, config, "user");
    pass = cw_variable_retrieve(cfg, config, "pass");
    base = cw_variable_retrieve(cfg, config, "base");
    if (!base)
        base = "";
    base = replace_cw_vars(chan, base);
    if (!(scope = cw_variable_retrieve(cfg, config, "scope")))
    {
        scope = "sub";
    }
    if (!(_filter = cw_variable_retrieve(cfg, config, "filter")))
    {
        _filter = "(&(objectClass=person)(telephoneNumber=${CALLERIDNUM}))";
    }
    if (!(attribute = cw_variable_retrieve(cfg, config, "attribute")))
    {
        attribute = "cn";
    }
    if ((temp = cw_variable_retrieve(cfg, config, "convert")))
    {
        if (strchr(temp, ','))
        {
            convert_from = strtrim(strsep(&temp, ","));
            convert_to = strtrim(strsep(&temp, "\0"));
        }
        else
        {
            cw_log(CW_LOG_WARNING, "syntax error: convert = <source-charset>,<destination charset>\n");
        }
    }

    if (option_verbose > 3)
        cw_verbose (VERBOSE_PREFIX_4 "LDAPget: ldap://%s/%s?%s?%s?%s\n", host, base, attribute, scope, _filter);

    filter = replace_cw_vars(chan, _filter);
    if (option_verbose > 3)
        cw_verbose (VERBOSE_PREFIX_4 "LDAPget: %s\n", filter);

    if (keys  &&  strstr(filter, "%s") != NULL)
    {
        filter = (char *) realloc(filter, (strlen(filter)+strlen(keys)+1)*sizeof(char));
        while ((key = strsep(&keys, "|")) != NULL)
        {
            if ((tail = strstr(filter, "%s")) != NULL)
            {
                memmove(tail + strlen(key), tail + 2, strlen(tail + 2) + 1);
                memcpy(tail, key, strlen(key));
            }
        }
    }

    if (option_verbose > 2)
        cw_verbose (VERBOSE_PREFIX_3 "LDAPget: ldap://%s/%s?%s?%s?%s\n", host, base, attribute, scope, filter);

    if (ldap_lookup(host, port, version, timeout, user, pass, base, scope, filter, attribute, result)) {

        if (convert_from)
        {
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "LDAPget: convert: %s -> %s\n", convert_from, convert_to);
            result_conv = malloc(strlen(result) * 2);
            strconvert(convert_from, convert_to, result, result_conv);
            strcpy(result, result_conv);
            free(result_conv);
        }
        
        if (strcmp("CALLERIDNAME", varname) == 0)
        {
            cw_set_callerid(chan, NULL, result, NULL);
            if (option_verbose > 2)
                cw_verbose (VERBOSE_PREFIX_3 "LDAPget: set CIDNAME to \"%s\"\n", result);
        }
        else
        {
            if (option_verbose > 2)
                cw_verbose (VERBOSE_PREFIX_3 "LDAPget: set %s='%s'\n", varname, result);
            pbx_builtin_setvar_helper(chan, varname, result);
        }
        
    }
    else
    {
        pbx_builtin_setvar_helper(chan, "LDAPSTATUS", "FAILURE");
        return 0;
    }
    cw_config_destroy(cfg);
    free(filter);
    free(base);
    pbx_builtin_setvar_helper(chan, "LDAPSTATUS", "SUCCESS");

    LOCAL_USER_REMOVE(u);
    return 0;
}

int ldap_lookup(char *host,
                int port,
                int version,
                int timeout,
                char *user,
                char *pass, 
                char *base,
                char *scope,
                char *filter,
                char *attribute,
                char *result)
{
    char *attrs[] = {NULL};
    char **values;
    LDAP *ld;
    LDAPMessage *res, *entry;
    int ret, ldap_scope = LDAP_SCOPE_SUBTREE;

    ld = ldap_init(host, port);
    if (!ld)
    {
        cw_log(CW_LOG_WARNING, "LDAPget: unable to initialize ldap connection to %s:%d\n", host, port);
        return 0;
    }
    ldap_set_option(ld, LDAP_OPT_TIMELIMIT, &timeout);
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    if (user)
    {
        if (option_verbose > 2)
            cw_verbose(VERBOSE_PREFIX_3 "LDAPget: bind to %s as %s\n", host, user);
        ret = ldap_simple_bind_s(ld, user, pass);
    }
    else
    {
        if (option_verbose > 2)
            cw_verbose(VERBOSE_PREFIX_3 "LDAPget: bind to %s anonymously\n", host);
        ret = ldap_simple_bind_s(ld, NULL, NULL);
    }
    if (ret)
    {
        cw_log(CW_LOG_WARNING, "LDAPget: bind failed: %s\n", ldap_err2string(ret));
        ldap_unbind(ld);
        return 0;
    }

    if (strncmp(scope, "sub", 3) == 0)
    {
        ldap_scope = LDAP_SCOPE_SUBTREE;
    }
    else if (strncmp(scope, "base", 4) == 0)
    {
        ldap_scope = LDAP_SCOPE_BASE;
    }
    else if (strncmp(scope, "one", 3) == 0)
    {
        ldap_scope = LDAP_SCOPE_ONELEVEL;
    }

    ret = ldap_search_s(ld, base, ldap_scope, filter, attrs, 0, &res);
    if (ret)
    {
        cw_log(CW_LOG_DEBUG, "LDAPget: search failed: %s\n", ldap_err2string(ret));
        ldap_msgfree(res);
        ldap_unbind(ld);
        return 0;
    }

    entry = ldap_first_entry(ld, res);
    if (!entry)
    {
        if (option_verbose > 2)
            cw_verbose (VERBOSE_PREFIX_3 "LDAPget: Value not found in directory.\n");
        ldap_msgfree(res);
        ldap_unbind(ld);
        return 0;
    }

    values = ldap_get_values(ld, entry, attribute);
    if (!(values  &&  values[0]))
    {
        if (option_verbose > 2)
            cw_verbose (VERBOSE_PREFIX_3 "LDAPget: %s not found.\n", attribute);
        ldap_msgfree(res);
        ldap_unbind(ld);
        return 0;
    }
    memset(result, 0, strlen(values[0]));
    strncpy(result, values[0], strlen(values[0]));
    result[strlen(values[0])] = '\0';
    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "LDAPget: %s=%s\n", attribute, result);
    ldap_value_free(values);
    ldap_msgfree(res);
    ldap_unbind_s(ld);
    return 1;
}


char *replace_cw_vars(struct cw_channel *chan, const char *_string)
{
    char *var_start;
    char *var_end;
    char *key;
    char *value;
    char *string;
    int begin;
    int end;

    if (!_string)
        return "";
    string = (char *) malloc((strlen(_string) + 1)*sizeof(char));
    memcpy(string, _string, strlen(_string) + 1);

    while ((var_start = strstr(string, "${"))  &&  (var_end = strchr(var_start,'}')))
    {
        begin = strlen(string) - strlen(var_start); 
        end = strlen(string) - strlen(var_end);
        key = (char *) alloca((end - begin - 1)*sizeof(char));
        memcpy(key, var_start + 2, end - begin - 2);
        key[end - begin - 2] = '\0';
        value = pbx_builtin_getvar_helper(chan, key);
        if (value)
        {
            string = (char *) realloc(string, (strlen(string) - (end-begin + 1)+strlen(value) + 1)*sizeof(char));
            memmove(var_start+strlen(value), var_end + 1, strlen(var_end + 1) + 1);
            memcpy(var_start, value, strlen(value));
        }
        else
        {
            memmove(var_start, var_end + 1, strlen(var_end + 1) + 1);
        }
    }
    return string;
}

static int strconvert(const char *incharset,
                      const char *outcharset,
#if 0
                      const char *in,
#else
                      char *in,
#endif
                      char *out) 
{
    iconv_t cd;
    size_t incount;
    size_t outcount;
    size_t result;

    incount = outcount = strlen(in) * 2;
    if ((cd = iconv_open(outcharset, incharset)) == (iconv_t) -1)
    {
        if (errno == EINVAL) cw_log(CW_LOG_DEBUG, "conversion from '%s' to '%s' not available", incharset, outcharset);
        *out = L'\0';
        return -1;
    }
    result = iconv(cd, &in, &incount, &out, &outcount);
    iconv_close(cd);
    out[strlen(out)] = '\0';
    return 1;
}

static char *strtrim(char *str) 
{
    char *p;
    char c;

    p = strchr(str, '\0');
    while (--p >= str  &&  isspace(*p))
        /*dummy*/;
    p[1] = '\0';
    while ((c = *str++) != '\0'  &&  isspace(c))
        /*dummy*/;
    return str - 1;
}

static int load_module(void)
{
    g_app = cw_register_function(g_name, ldap_exec, g_synopsis, g_syntax, g_descrip);
    return 0;
}

static int unload_module(void)
{
    int res = 0;

    res |= cw_unregister_function(g_app);
    return res;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
