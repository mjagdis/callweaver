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
 *
 * \brief Core PBX routines.
 * 
 */
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/cli.h"
#include "callweaver/switch.h"
#include "callweaver/pbx.h"
#include "callweaver/channel.h"
#include "callweaver/chanvars.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/file.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/cdr.h"
#include "callweaver/config.h"
#include "callweaver/manager.h"
#include "callweaver/callweaver_expr.h"
#include "callweaver/linkedlists.h"
#include "callweaver/say.h"
#include "callweaver/utils.h"
#include "callweaver/causes.h"
#include "callweaver/musiconhold.h"
#include "callweaver/app.h"
#include "callweaver/devicestate.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/keywords.h"


#ifdef LOW_MEMORY
#define EXT_DATA_SIZE 256
#else
#define EXT_DATA_SIZE 8192
#endif

#define SWITCH_DATA_LENGTH 256

#define VAR_BUF_SIZE 4096

#define    VAR_NORMAL        1
#define    VAR_SOFTTRAN    2
#define    VAR_HARDTRAN    3


struct cw_context;

/* cw_exten: An extension */
struct cw_exten
{
    char *exten;                  /* Extension name -- shouldn't this be called "ident" ? */
    unsigned int hash;            /* Hash of the extension name */
    int matchcid;                 /* Match caller id ? */
    char *cidmatch;               /* Caller id to match for this extension */
    int priority;                 /* Priority */
    char *label;                  /* Label */
    struct cw_context *parent;  /* The context this extension belongs to  */
    unsigned int apphash;         /* Hash of application to execute */
    char *app;                    /* Name of application to execute */
    void *data;                   /* Data to use (arguments) */
    void (*datad)(void *);        /* Data destructor */
    struct cw_exten *peer;      /* Next higher priority with our extension */
    const char *registrar;        /* Registrar */
    struct cw_exten *next;      /* Extension with a greater ID */
    char stuff[0];
};

/* cw_include: include= support in extensions.conf */
struct cw_include
{
    char *name;        
    char *rname;                /* Context to include */
    const char *registrar;        /* Registrar */
    int hastime;                /* If time construct exists */
    struct cw_timing timing;    /* time construct */
    struct cw_include *next;    /* Link them together */
    char stuff[0];
};

/* cw_sw: Switch statement in extensions.conf */
struct cw_sw
{
    char *name;
    const char *registrar;        /* Registrar */
    char *data;                    /* Data load */
    int eval;
    struct cw_sw *next;        /* Link them together */
    char *tmpdata;
    char stuff[0];
};

struct cw_ignorepat
{
    const char *registrar;
    struct cw_ignorepat *next;
    char pattern[0];
};

/* cw_context: An extension context */
struct cw_context
{
    cw_mutex_t lock;             /* A lock to prevent multiple threads from clobbering the context */
    unsigned int hash;            /* Hashed context name */
    struct cw_exten *root;    /* The root of the list of extensions */
    struct cw_context *next;    /* Link them together */
    struct cw_include *includes;    /* Include other contexts */
    struct cw_ignorepat *ignorepats;    /* Patterns for which to continue playing dialtone */
    const char *registrar;        /* Registrar */
    struct cw_sw *alts;        /* Alternative switches */
    char name[0];                /* Name of the context */
};

/* cw_state_cb: An extension state notify */
struct cw_state_cb
{
    int id;
    void *data;
    cw_state_cb_type callback;
    struct cw_state_cb *next;
};
        
/* Hints are pointers from an extension in the dialplan to one or more devices (tech/name) */
struct cw_hint
{
    struct cw_exten *exten;    /* Extension */
    int laststate;                /* Last known state */
    struct cw_state_cb *callbacks;    /* Callback list for this extension */
    struct cw_hint *next;        /* Pointer to next hint in list */
};

int cw_pbx_outgoing_cdr_failed(void);

static int autofallthrough = 0;

CW_MUTEX_DEFINE_STATIC(maxcalllock);
static int countcalls = 0;

static struct cw_context *contexts = NULL;
CW_MUTEX_DEFINE_STATIC(conlock);         /* Lock for the cw_context list */

CW_MUTEX_DEFINE_STATIC(hintlock);        /* Lock for extension state notifys */
static int stateid = 1;
struct cw_hint *hints = NULL;
struct cw_state_cb *statecbs = NULL;


static int cw_switch_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_switch *switch_a = container_of(*objp_a, struct cw_switch, obj);
	const struct cw_switch *switch_b = container_of(*objp_b, struct cw_switch, obj);

	return strcmp(switch_a->name, switch_b->name);
}

static int switch_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_switch *sw = container_of(obj, struct cw_switch, obj);
	return strcmp(sw->name, pattern);
}

struct cw_registry switch_registry = {
	.name = "Switch",
	.qsort_compare = cw_switch_qsort_compare_by_name,
	.match = switch_object_match,
};


static inline struct cw_switch *pbx_findswitch(const char *name)
{
	struct cw_object *obj = cw_registry_find(&switch_registry, 0, 0, name);

	if (obj)
		return container_of(obj, struct cw_switch, obj);
	return NULL;
}


/*! \brief  handle_show_switches: CLI support for listing registred dial plan switches */
static int switch_print(struct cw_object *obj, void *data)
{
	struct cw_switch *sw = container_of(obj, struct cw_switch, obj);
	int *fd = data;

        cw_cli(*fd, "%s: %s\n", sw->name, sw->description);
	return 0;
}

static int handle_show_switches(int fd, int argc, char *argv[])
{
	cw_cli(fd, "\n    -= Registered CallWeaver Alternative Switches =-\n");
	cw_registry_iterate_ordered(&cdrbe_registry, switch_print, &fd);
	return RESULT_SUCCESS;
}

/*! \brief  handle_show_globals: CLI support for listing global variables */
struct handle_show_globals_args {
	int fd;
	int count;
};

static int handle_show_globals_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct handle_show_globals_args *args = data;

	args->count++;
        cw_cli(args->fd, "  %s=%s\n", cw_var_name(var), var->value);
	return 0;
}

static int handle_show_globals(int fd, int argc, char *argv[])
{
	struct handle_show_globals_args args = {
		.fd = fd,
		.count = 0,
	};

	cw_registry_iterate_ordered(&var_registry, handle_show_globals_one, &args);

	cw_cli(fd, "\n    -- %d variables\n", args.count);
	return RESULT_SUCCESS;
}

/*! \brief  CLI support for setting global variables */
static int handle_set_global(int fd, int argc, char *argv[])
{
    if (argc != 4)
        return RESULT_SHOWUSAGE;
    pbx_builtin_setvar_helper(NULL, argv[2], argv[3]);
    cw_cli(fd, "\n    -- Global variable %s set to %s\n", argv[2], argv[3]);
    return RESULT_SUCCESS;
}

int pbx_checkcondition(char *condition)
{
    if (condition && *condition) {
        if (isdigit(*condition)) {
            /* Numbers are true if non-zero */
            return atoi(condition);
        }
        /* Non-empty strings are true */
        return 1;
    }
    /* NULL and empty strings are false */
    return 0;
}


/* Go no deeper than this through includes (not counting loops) */
#define CW_PBX_MAX_STACK    128

#define HELPER_EXISTS 0
#define HELPER_EXEC 1
#define HELPER_CANMATCH 2
#define HELPER_MATCHMORE 3
#define HELPER_FINDLABEL 4


static inline int include_valid(struct cw_include *i)
{
    if (!i->hastime)
        return 1;

    return cw_check_timing(&(i->timing));
}

static void pbx_destroy(struct cw_pbx *p)
{
    free(p);
}

const char *cw_extension_match_to_str(int match)
{
    switch (match)
    {
    case EXTENSION_MATCH_FAILURE:
        return "Failure";
    case EXTENSION_MATCH_EXACT:
        return "Exact";
    case EXTENSION_MATCH_OVERLENGTH:
        return "Overlength";
    case EXTENSION_MATCH_INCOMPLETE:
        return "Incomplete";
    case EXTENSION_MATCH_STRETCHABLE:
        return "Stretchable";
    case EXTENSION_MATCH_POSSIBLE:
        return "Possible";
    }
    return "???";
}

int cw_extension_pattern_match(const char *destination, const char *pattern)
{
    unsigned int pattern_len;
    unsigned int destination_len;
    int i;
    int limit;
    char *where;
    const char *d;
    const char *p;

    /* If there is nothing to match, we consider the match incomplete */
    if (destination[0] == '\0')
    {
        /* A blank pattern is an odd thing to have, but let's be comprehensive and
           allow for it. */
        if (pattern[0] == '\0')
            return EXTENSION_MATCH_EXACT;
        return EXTENSION_MATCH_INCOMPLETE;
    }
    /* All patterns begin with _ */
    if (pattern[0] != '_')
    {
        /* Its not really a pattern. We need a solid partial/full match. */
        pattern_len = strlen(pattern);
        destination_len = strlen(destination);
        if (pattern_len > destination_len)
        {
            if (memcmp(pattern, destination, destination_len))
                return EXTENSION_MATCH_FAILURE;
            return EXTENSION_MATCH_INCOMPLETE;
        }
        else
        {
            if (memcmp(pattern, destination, pattern_len))
                return EXTENSION_MATCH_FAILURE;
            if (pattern_len == destination_len)
                return EXTENSION_MATCH_EXACT;
            return EXTENSION_MATCH_OVERLENGTH;
        }
        return EXTENSION_MATCH_INCOMPLETE;
    }

    d = destination;
    p = pattern;
    /* Skip the initial '_' */
    p++;
    while (*d == '-')
        d++;
    if (*d == '\0')
        return EXTENSION_MATCH_INCOMPLETE;
    while (*d  &&  *p  &&  *p != '/')
    {
        while (*d == '-')
            d++;
        if (*d == '\0')
            break;
        switch (toupper(*p))
        {
        case '[':
            if ((where = strchr(++p, ']')) == NULL)
            {
                cw_log(CW_LOG_WARNING, "Bad usage of [] in extension pattern '%s'", pattern);
                return EXTENSION_MATCH_FAILURE;
            }
            limit = (int) (where - p);
            for (i = 0;  i < limit;  i++)
            {
                if (i < limit - 2)
                {
                    if (p[i + 1] == '-')
                    {
                        if (*d >= p[i]  &&  *d <= p[i + 2])
                            break;
                        i += 2;
                        continue;
                    }
                }
                if (*d == p[i])
                    break;
            }
            if (i >= limit)
                return EXTENSION_MATCH_FAILURE;
            p += limit;
            break;
        case 'X':
            if (*d < '0'  ||  *d > '9')
                return EXTENSION_MATCH_FAILURE;
            break;
        case 'Z':
            if (*d < '1'  ||  *d > '9')
                return EXTENSION_MATCH_FAILURE;
            break;
        case 'N':
            if (*d < '2'  ||  *d > '9')
                return EXTENSION_MATCH_FAILURE;
            break;
        case '.':
        case '~':
            /* A hard match - can be relied upon. */
            return EXTENSION_MATCH_STRETCHABLE;
        case '!':
            /* A soft match - acceptable, might there might be a better match. */
            return EXTENSION_MATCH_POSSIBLE;
        case ' ':
        case '-':
            /* Ignore these characters */
            d--;
            break;
        default:
            if (*d != *p)
                return EXTENSION_MATCH_FAILURE;
            break;
        }
        d++;
        p++;
    }
    /* If we ran off the end of the destination and the pattern ends in '!', match */
    if (*d == '\0')
    {
        if (*p == '!')
            return EXTENSION_MATCH_POSSIBLE;
        if (*p == '\0'  ||  *p == '/')
            return EXTENSION_MATCH_EXACT;
        return EXTENSION_MATCH_INCOMPLETE;
    }
    if (*p == '\0'  ||  *p == '/')
        return EXTENSION_MATCH_OVERLENGTH;
    return EXTENSION_MATCH_FAILURE;
}

static int cw_extension_match(const char *pattern, const char *data)
{
    int match;

    match = cw_extension_pattern_match(data, pattern);
    if (match == EXTENSION_MATCH_POSSIBLE)
        return 2;
    return (match == EXTENSION_MATCH_EXACT  ||  match == EXTENSION_MATCH_STRETCHABLE)  ?  1  :  0;
}

struct cw_context *cw_context_find(const char *name)
{
    struct cw_context *tmp;
    unsigned int hash;
    
    cw_mutex_lock(&conlock);
    if (name)
    {
        hash = cw_hash_string(name);
        for (tmp = contexts; tmp; tmp = tmp->next)
            if (hash == tmp->hash && !strcmp(name, tmp->name))
                break;
    }
    else
    {
        tmp = contexts;
    }
    cw_mutex_unlock(&conlock);
    return tmp;
}

#define STATUS_NO_CONTEXT    1
#define STATUS_NO_EXTENSION    2
#define STATUS_NO_PRIORITY    3
#define STATUS_NO_LABEL        4
#define STATUS_SUCCESS        5

static int matchcid(const char *cidpattern, const char *callerid)
{
    /* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
       failing to get a number should count as a match, otherwise not */

    if (callerid == NULL)
        return (cidpattern[0])  ?  0  :  1;

    switch (cw_extension_pattern_match(callerid, cidpattern))
    {
    case EXTENSION_MATCH_EXACT:
    case EXTENSION_MATCH_STRETCHABLE:
    case EXTENSION_MATCH_POSSIBLE:
        return 1;
    }
    return 0;
}

static struct cw_exten *pbx_find_extension(struct cw_channel *chan, struct cw_context *bypass, const char *context, const char *exten, int priority, const char *label, const char *callerid, int action, char *incstack[], int *stacklen, int *status, struct cw_switch **swo, char **data, const char **foundcontext)
{
    int x, res;
    struct cw_context *tmp;
    struct cw_exten *e, *eroot;
    struct cw_include *i;
    struct cw_sw *sw;
    struct cw_switch *asw;
    unsigned int hash;

    /* Initialize status if appropriate */
    if (!*stacklen)
    {
        *status = STATUS_NO_CONTEXT;
        *swo = NULL;
        *data = NULL;
    }
    /* Check for stack overflow */
    if (*stacklen >= CW_PBX_MAX_STACK)
    {
        cw_log(CW_LOG_WARNING, "Maximum PBX stack exceeded\n");
        return NULL;
    }
    /* Check first to see if we've already been checked */
    for (x = 0;  x < *stacklen;  x++)
    {
        if (!strcasecmp(incstack[x], context))
            return NULL;
    }
    hash = cw_hash_string(context);
    if (bypass)
        tmp = bypass;
    else
        tmp = contexts;
    while (tmp)
    {
        /* Match context */
        if (bypass || (hash == tmp->hash && !strcmp(context, tmp->name)))
        {
            struct cw_exten *earlymatch = NULL;

            if (*status < STATUS_NO_EXTENSION)
                *status = STATUS_NO_EXTENSION;
            for (eroot = tmp->root;  eroot;  eroot = eroot->next)
            {
                int match = 0;
                int res = 0;

                /* Match extension */
                match = cw_extension_pattern_match(exten, eroot->exten);
                res = 0;
		if (!(eroot->matchcid  &&  !matchcid(eroot->cidmatch, callerid)))
		{
                    switch (action)
                    {
                        case HELPER_EXISTS:
                        case HELPER_EXEC:
                        case HELPER_FINDLABEL:
                    	    /* We are only interested in exact matches */
                    	    res = (match == EXTENSION_MATCH_POSSIBLE  ||  match == EXTENSION_MATCH_EXACT  ||  match == EXTENSION_MATCH_STRETCHABLE);
                    	    break;
                        case HELPER_CANMATCH:
                            /* We are interested in exact or incomplete matches */
                            res = (match == EXTENSION_MATCH_POSSIBLE  ||  match == EXTENSION_MATCH_EXACT  ||  match == EXTENSION_MATCH_STRETCHABLE  ||  match == EXTENSION_MATCH_INCOMPLETE);
                    	    break;
                        case HELPER_MATCHMORE:
                    	    /* We are only interested in incomplete matches */
                    	    if (match == EXTENSION_MATCH_POSSIBLE  &&  earlymatch == NULL) 
			    {
                               /* It matched an extension ending in a '!' wildcard
                               So just record it for now, unless there's a better match */
                               earlymatch = eroot;
                               res = 0;
                               break;
                            }
                            res = (match == EXTENSION_MATCH_STRETCHABLE  ||  match == EXTENSION_MATCH_INCOMPLETE)  ?  1  :  0;
                            break;
                    }
		}
                if (res)
                {
                    e = eroot;
                    if (*status < STATUS_NO_PRIORITY)
                        *status = STATUS_NO_PRIORITY;
                    while (e)
                    {
                        /* Match priority */
                        if (action == HELPER_FINDLABEL)
                        {
                            if (*status < STATUS_NO_LABEL)
                                *status = STATUS_NO_LABEL;
                             if (label  &&  e->label  &&  !strcmp(label, e->label))
                            {
                                *status = STATUS_SUCCESS;
                                *foundcontext = context;
                                return e;
                            }
                        }
                        else if (e->priority == priority)
                        {
                            *status = STATUS_SUCCESS;
                            *foundcontext = context;
                            return e;
                        }
                        e = e->peer;
                    }
                }
            }
            if (earlymatch)
            {
                /* Bizarre logic for HELPER_MATCHMORE. We return zero to break out 
                   of the loop waiting for more digits, and _then_ match (normally)
                   the extension we ended up with. We got an early-matching wildcard
                   pattern, so return NULL to break out of the loop. */
                return NULL;
            }
            /* Check alternative switches */
            sw = tmp->alts;
            while (sw)
            {
                if ((asw = pbx_findswitch(sw->name)))
                {
                    /* Substitute variables now */
                    if (sw->eval) 
                        pbx_substitute_variables(chan, &chan->vars, sw->data, sw->tmpdata, SWITCH_DATA_LENGTH);
                    if (action == HELPER_CANMATCH)
                        res = asw->canmatch ? asw->canmatch(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
                    else if (action == HELPER_MATCHMORE)
                        res = asw->matchmore ? asw->matchmore(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
                    else
                        res = asw->exists ? asw->exists(chan, context, exten, priority, callerid, sw->eval ? sw->tmpdata : sw->data) : 0;
                    if (res)
                    {
                        /* Got a match */
                        *swo = asw;
                        *data = sw->eval ? sw->tmpdata : sw->data;
                        *foundcontext = context;
                        return NULL;
                    }
                    cw_object_put(asw);
                }
                else
                {
                    cw_log(CW_LOG_WARNING, "No such switch '%s'\n", sw->name);
                }
                sw = sw->next;
            }
            /* Setup the stack */
            incstack[*stacklen] = tmp->name;
            (*stacklen)++;
            /* Now try any includes we have in this context */
            i = tmp->includes;
            while (i)
            {
                if (include_valid(i))
                {
                    if ((e = pbx_find_extension(chan, bypass, i->rname, exten, priority, label, callerid, action, incstack, stacklen, status, swo, data, foundcontext))) 
                        return e;
                    if (*swo) 
                        return NULL;
                }
                i = i->next;
            }
            break;
        }
        tmp = tmp->next;
    }
    return NULL;
}

/*! \brief  pbx_retrieve_variable: Support for CallWeaver built-in variables and
      functions in the dialplan
  ---*/

// There are 5 different scenarios to be covered:
// 
// 1) built-in variables living in channel c's variable list
// 2) user defined variables living in channel c's variable list
// 3) user defined variables not living in channel c's variable list
// 4) built-in global variables, that is globally visible, not bound to any channel
// 5) user defined variables living in the global dialplan variable list (&globals)
//
// This function is safeguarded against the following cases:
//
// 1) if channel c doesn't exist (is NULL), scenario #1 and #2 searches are skipped
// 2) if channel c's variable list doesn't exist (is NULL), scenario #2 search is skipped
// 3) if NULL is passed in for parameter headp, scenario #3 search is skipped
// 4) global dialplan variable list doesn't exist (&globals is NULL), scenario #5 search is skipped
//
// This function is known NOT to be safeguarded against the following cases:
//
// 1) ret is NULL
// 2) workspace is NULL
// 3) workspacelen is larger than the actual buffer size of workspace
// 4) workspacelen is larger than VAR_BUF_SIZE
//
// NOTE: There may be further unsafeguarded cases not yet documented here!

void pbx_retrieve_variable(struct cw_channel *c, const char *varname, char **ret, char *workspace, int workspacelen, struct cw_registry *var_reg)
{
    char tmpvar[80];
    struct tm brokentime;
    char *first, *second;
    struct cw_object *obj;
    struct cw_var_t *var;
    time_t thistime;
    int offset, offset2;
    int no_match_yet;
    unsigned int hash;

    // warnings for (potentially) unsafe pre-conditions
    // TODO: these cases really ought to be safeguarded against
        
    if (ret == NULL)
        cw_log(CW_LOG_WARNING, "NULL passed in parameter 'ret'\n");

    if (workspace == NULL)
        cw_log(CW_LOG_WARNING, "NULL passed in parameter 'workspace'\n");
    
    if (workspacelen == 0)
        cw_log(CW_LOG_WARNING, "Zero passed in parameter 'workspacelen'\n");

#if 0
    if (workspacelen > VAR_BUF_SIZE)
        cw_log(CW_LOG_WARNING, "VAR_BUF_SIZE exceeded by parameter 'workspacelen' (%d)\n", workspacelen);
#endif

    // actual work starts here
    
    if (c)
        var_reg = &c->vars;

    *ret = NULL;
    
    // check for slicing modifier
    if /* sliced */ ((first = strchr(varname, ':')))
    {
        // remove characters counting from end or start of string */
        cw_copy_string(tmpvar, varname, sizeof(tmpvar));
        first = strchr(tmpvar, ':');
        if (!first)
            first = tmpvar + strlen(tmpvar);
        *first='\0';
        pbx_retrieve_variable(c, tmpvar, ret, workspace, workspacelen - 1, var_reg);
        if (!(*ret)) 
            return;
        offset = atoi(first + 1);    /* The number of characters, 
                       positive: remove # of chars from start
                       negative: keep # of chars from end */
                        
         if ((second = strchr(first + 1, ':')))
        {    
            *second='\0';
            offset2 = atoi(second+1);        /* Number of chars to copy */
        }
        else if (offset >= 0)
        {
            offset2 = strlen(*ret)-offset;    /* Rest of string */
        }
        else
        {
            offset2 = abs(offset);
        }

        if (abs(offset) > strlen(*ret))
        {
            /* Offset beyond string */
            if (offset >= 0) 
                offset = strlen(*ret);
            else 
                offset =- strlen(*ret);
        }

        if ((offset < 0  &&  offset2 > -offset)  ||  (offset >= 0  &&  offset + offset2 > strlen(*ret)))
        {
            if (offset >= 0) 
                offset2 = strlen(*ret) - offset;
            else 
                offset2 = strlen(*ret) + offset;
        }
        if (offset >= 0)
            *ret += offset;
        else
            *ret += strlen(*ret)+offset;
        (*ret)[offset2] = '\0';        /* Cut at offset2 position */
    }
    else /* not sliced */
    {
        no_match_yet = 0;
        obj = NULL;
        hash = cw_hash_var_name(varname);

        if (c)
        {
            // ----------------------------------------------
            // search builtin channel variables (scenario #1)
            // ----------------------------------------------

            if (hash == CW_KEYWORD_CALLERID && !strcmp(varname, "CALLERID"))
            {
                if (c->cid.cid_num)
                {
                    if (c->cid.cid_name)
                        snprintf(workspace, workspacelen, "\"%s\" <%s>", c->cid.cid_name, c->cid.cid_num);
                    else
                        cw_copy_string(workspace, c->cid.cid_num, workspacelen);
                    *ret = workspace;
                }
                else if (c->cid.cid_name)
                {
                    cw_copy_string(workspace, c->cid.cid_name, workspacelen);
                    *ret = workspace;
                }
                else
                {
                    *ret = NULL;
                }
            }
            else if (hash == CW_KEYWORD_CALLERIDNUM && !strcmp(varname, "CALLERIDNUM"))
            {
                if (c->cid.cid_num)
                {
                    cw_copy_string(workspace, c->cid.cid_num, workspacelen);
                    *ret = workspace;
                }
                else
                {
                    *ret = NULL;
                }
            }
            else if (hash == CW_KEYWORD_CALLERIDNAME && !strcmp(varname, "CALLERIDNAME"))
            {
                if (c->cid.cid_name)
                {
                    cw_copy_string(workspace, c->cid.cid_name, workspacelen);
                    *ret = workspace;
                }
                else
                    *ret = NULL;
            }
            else if (hash == CW_KEYWORD_CALLERANI && !strcmp(varname, "CALLERANI"))
            {
                if (c->cid.cid_ani)
                {
                    cw_copy_string(workspace, c->cid.cid_ani, workspacelen);
                    *ret = workspace;
                }
                else
                    *ret = NULL;
            }            
            else if (hash == CW_KEYWORD_CALLINGPRES && !strcmp(varname, "CALLINGPRES"))
            {
                snprintf(workspace, workspacelen, "%d", c->cid.cid_pres);
                *ret = workspace;
            }            
            else if (hash == CW_KEYWORD_CALLINGANI2 && !strcmp(varname, "CALLINGANI2"))
            {
                snprintf(workspace, workspacelen, "%d", c->cid.cid_ani2);
                *ret = workspace;
            }            
            else if (hash == CW_KEYWORD_CALLINGTON && !strcmp(varname, "CALLINGTON"))
            {
                snprintf(workspace, workspacelen, "%d", c->cid.cid_ton);
                *ret = workspace;
            }            
            else if (hash == CW_KEYWORD_CALLINGTNS && !strcmp(varname, "CALLINGTNS"))
            {
                snprintf(workspace, workspacelen, "%d", c->cid.cid_tns);
                *ret = workspace;
            }            
            else if (hash == CW_KEYWORD_DNID && !strcmp(varname, "DNID"))
            {
                if (c->cid.cid_dnid)
                {
                    cw_copy_string(workspace, c->cid.cid_dnid, workspacelen);
                    *ret = workspace;
                }
                else
                {
                    *ret = NULL;
                }
            }            
            else if (hash == CW_KEYWORD_HINT && !strcmp(varname, "HINT"))
            {
                if (!cw_get_hint(workspace, workspacelen, NULL, 0, c, c->context, c->exten))
                    *ret = NULL;
                else
                    *ret = workspace;
            }
            else if (hash == CW_KEYWORD_HINTNAME && !strcmp(varname, "HINTNAME"))
            {
                if (!cw_get_hint(NULL, 0, workspace, workspacelen, c, c->context, c->exten))
                    *ret = NULL;
                else
                    *ret = workspace;
            }
            else if (hash == CW_KEYWORD_EXTEN && !strcmp(varname, "EXTEN"))
            {
                cw_copy_string(workspace, c->exten, workspacelen);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_RDNIS && !strcmp(varname, "RDNIS"))
            {
                if (c->cid.cid_rdnis)
                {
                    cw_copy_string(workspace, c->cid.cid_rdnis, workspacelen);
                    *ret = workspace;
                }
                else
                {
                    *ret = NULL;
                }
            }
            else if (hash == CW_KEYWORD_CONTEXT && !strcmp(varname, "CONTEXT"))
            {
                cw_copy_string(workspace, c->context, workspacelen);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_PRIORITY && !strcmp(varname, "PRIORITY"))
            {
                snprintf(workspace, workspacelen, "%d", c->priority);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_CHANNEL && !strcmp(varname, "CHANNEL"))
            {
                cw_copy_string(workspace, c->name, workspacelen);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_UNIQUEID && !strcmp(varname, "UNIQUEID"))
            {
                snprintf(workspace, workspacelen, "%s", c->uniqueid);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_HANGUPCAUSE && !strcmp(varname, "HANGUPCAUSE"))
            {
                snprintf(workspace, workspacelen, "%d", c->hangupcause);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_ACCOUNTCODE && !strcmp(varname, "ACCOUNTCODE"))
            {
                cw_copy_string(workspace, c->accountcode, workspacelen);
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_LANGUAGE && !strcmp(varname, "LANGUAGE"))
            {
                cw_copy_string(workspace, c->language, workspacelen);
                *ret = workspace;
            }
	    else if (hash == CW_KEYWORD_SYSTEMNAME && !strcmp(varname, "SYSTEMNAME"))
	    {
		cw_copy_string(workspace, cw_config_CW_SYSTEM_NAME, workspacelen);
		*ret = workspace;
	    }	
            else
            {
                // ---------------------------------------------------
                // search user defined channel variables (scenario #2)
                // ---------------------------------------------------
                no_match_yet = 1;
                if ((obj = cw_registry_find(&c->vars, 1, hash, varname))) {
                    var = container_of(obj, struct cw_var_t, obj);
                    cw_copy_string(workspace, var->value, workspacelen);
                    no_match_yet = 0;
                    *ret = workspace;
                }
            }            
        }
        else /* channel does not exist */
        {
            // -------------------------------------------------------------------------
            // search for user defined variables not bound to this channel (scenario #3)
            // -------------------------------------------------------------------------
            no_match_yet = 1;
            if (var_reg && (obj = cw_registry_find(var_reg, 1, hash, varname))) {
                var = container_of(obj, struct cw_var_t, obj);
                cw_copy_string(workspace, var->value, workspacelen);
                no_match_yet = 0;
                *ret = workspace;
            }
        }

        if (no_match_yet)
        {
            // ------------------------------------
            // search builtin globals (scenario #4)
            // ------------------------------------
            if (hash == CW_KEYWORD_EPOCH && !strcmp(varname, "EPOCH"))
            {
                snprintf(workspace, workspacelen, "%u",(int)time(NULL));
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_DATETIME && !strcmp(varname, "DATETIME"))
            {
                thistime = time(NULL);
                localtime_r(&thistime, &brokentime);
                snprintf(workspace, workspacelen, "%02d%02d%04d-%02d:%02d:%02d",
                         brokentime.tm_mday,
                         brokentime.tm_mon+1,
                         brokentime.tm_year+1900,
                         brokentime.tm_hour,
                         brokentime.tm_min,
                         brokentime.tm_sec
                         );
                *ret = workspace;
            }
            else if (hash == CW_KEYWORD_TIMESTAMP && !strcmp(varname, "TIMESTAMP"))
            {
                thistime=time(NULL);
                localtime_r(&thistime, &brokentime);
                /* 20031130-150612 */
                snprintf(workspace, workspacelen, "%04d%02d%02d-%02d%02d%02d",
                         brokentime.tm_year+1900,
                         brokentime.tm_mon+1,
                         brokentime.tm_mday,
                         brokentime.tm_hour,
                         brokentime.tm_min,
                         brokentime.tm_sec
                         );
                *ret = workspace;
            }
            else if (!(*ret))
            {
                // -----------------------------------------
                // search user defined globals (scenario #5)
                // -----------------------------------------
                if ((obj = cw_registry_find(&var_registry, 1, hash, varname))) {
                    var = container_of(obj, struct cw_var_t, obj);
                    cw_copy_string(workspace, var->value, workspacelen);
                    *ret = workspace;
                }
            }
        }

        if (obj)
            cw_object_put_obj(obj);
    }
}

int pbx_substitute_variables(struct cw_channel *c, struct cw_registry *var_reg, const char *cp1, char *cp2, int count)
{
    char *cp4 = 0;
    const char *tmp, *whereweare;
    int length;
    char *ltmp = NULL, *var = NULL;
    char *nextvar, *nextexp, *nextthing;
    char *vars, *vare;
    char *args, *p;
    int pos, brackets, needsub, len;
    
    /* Substitutes variables into cp2, based on string cp1 */

    /* Save the last byte for a terminating '\0' */
    count--;

    whereweare = tmp = cp1;
    while (*whereweare) {
        /* Assume we're copying the whole remaining string */
        pos = strlen(whereweare);
        nextvar = NULL;
        nextexp = NULL;
        nextthing = strchr(whereweare, '$');
        if (nextthing) {
            switch (nextthing[1]) {
            case '{':
                nextvar = nextthing;
                pos = nextvar - whereweare;
                break;
            case '[':
                nextexp = nextthing;
                pos = nextexp - whereweare;
                break;
            default:
                pos = nextthing - whereweare + 1;
                break;
            }
        }

        if (pos)
        {
            /* Can't copy more than 'count' bytes */
            len = (pos <= count ? pos : count);
            memcpy(cp2, whereweare, len);
            count -= len;
            cp2 += len;
            whereweare += pos;
        }
        
        if (nextvar)
        {
            /* We have a variable.  Find the start and end, and determine
               if we are going to have to recursively call ourselves on the
               contents */
            vars = vare = nextvar + 2;
            brackets = 1;
            needsub = 0;

            /* Find the end of it */
            while (brackets  &&  *vare)
            {
                if ((vare[0] == '$')  &&  (vare[1] == '{'))
                {
                    needsub++;
                }
                else if (vare[0] == '{')
                {
                    brackets++;
                }
                else if (vare[0] == '}')
                {
                    brackets--;
                }
                else if ((vare[0] == '$')  &&  (vare[1] == '['))
                {
                    needsub++;
                }
                vare++;
            }
            if (brackets)
                cw_log(CW_LOG_NOTICE, "Error in extension logic (missing '}')\n");
            len = vare - vars - 1;

            /* Skip totally over variable string */
            whereweare += (len + 3);

            if (!var)
                var = alloca(VAR_BUF_SIZE);

            /* Store variable name (and truncate) */
            cw_copy_string(var, vars, len + 1);

            /* Substitute if necessary */
            if (needsub)
            {
                if (!ltmp)
                    ltmp = alloca(VAR_BUF_SIZE);

                if (pbx_substitute_variables(c, var_reg, var, ltmp, VAR_BUF_SIZE))
			break;
                vars = ltmp;
            }
            else
            {
                vars = var;
            }

            *cp2 = '\0';
            if ((args = strchr(vars, '(')) && (p = strrchr(args, ')'))) {
                int offset = 0, length = count;
                *(args++) = '\0';
                *p = '\0';
                if (p[1] == ':')
                    sscanf(p+2, "%d:%d", &offset, &length);
                len = cw_function_exec_str(c, cw_hash_string(vars), vars, args, cp2, count+1);
		if (len)
			break;
                cp4 = cp2;
                if (offset < 0) {
                    for (len = count; len && *cp4; cp4++, len--);
                    *cp4 = '\0';
                    if ((cp4 += offset) < cp2)
                        cp4 = cp2;
                } else
                    while (count && *cp4 && offset-- > 0) { cp4++; count--; }
                while (count && *cp4 && length-- > 0) { *(cp2++) = *(cp4++); count--; }
            } else {
                /* Retrieve variable value */
                pbx_retrieve_variable(c, vars, &cp4, cp2, count, var_reg);
                if (cp4 == cp2) {
	            while (count && *cp2) cp2++, count--;;
		} else if (cp4)
	            while (count && *cp4) {
                        *(cp2++) = *(cp4++);
                        count--;
                    }
            }
        }
        else if (nextexp)
        {
            /* We have an expression.  Find the start and end, and determine
               if we are going to have to recursively call ourselves on the
               contents */
            vars = vare = nextexp + 2;
            brackets = 1;
            needsub = 0;

            /* Find the end of it */
            while (brackets  &&  *vare)
            {
                if ((vare[0] == '$') && (vare[1] == '['))
                {
                    needsub++;
                    brackets++;
                    vare++;
                }
                else if (vare[0] == '[')
                {
                    brackets++;
                }
                else if (vare[0] == ']')
                {
                    brackets--;
                }
                else if ((vare[0] == '$')  &&  (vare[1] == '{'))
                {
                    needsub++;
                    vare++;
                }
                vare++;
            }
            if (brackets)
                cw_log(CW_LOG_NOTICE, "Error in extension logic (missing ']')\n");
            len = vare - vars - 1;
            
            /* Skip totally over expression */
            whereweare += (len + 3);
            
            if (!var)
                var = alloca(VAR_BUF_SIZE);

            /* Store variable name (and truncate) */
            cw_copy_string(var, vars, len + 1);
            
            /* Substitute if necessary */
            if (needsub)
            {
                if (!ltmp)
                    ltmp = alloca(VAR_BUF_SIZE);

                if (pbx_substitute_variables(c, var_reg, var, ltmp, VAR_BUF_SIZE - 1))
			break;
                vars = ltmp;
            }
            else
            {
                vars = var;
            }

            length = cw_expr(vars, cp2, count);

            if (length)
            {
                cw_log(CW_LOG_DEBUG, "Expression result is '%s'\n", cp2);
                count -= length;
                cp2 += length;
            }
        }
    }

    /* We reserved space for this at the beginning */
    *cp2 = '\0';

    if (!count)
	    cw_log(CW_LOG_ERROR, "Insufficient space. The result may have been truncated.\n");

    return 0;
}


static int pbx_extension_helper(struct cw_channel *c, struct cw_context *con, const char *context, const char *exten, int priority, const char *label, const char *callerid, int action) 
{
    struct cw_exten *e;
    struct cw_switch *sw = NULL;
    char *data;
    const char *foundcontext = NULL;
    int status = 0;
    char *incstack[CW_PBX_MAX_STACK];
    char passdata[EXT_DATA_SIZE];
    int stacklen = 0;
    int res = -1;

    cw_mutex_lock(&conlock);

    e = pbx_find_extension(c, con, context, exten, priority, label, callerid, action, incstack, &stacklen, &status, &sw, &data, &foundcontext);

    if (e)
    {
        switch (action)
        {
        case HELPER_FINDLABEL:
            res = e->priority;
	    /* Fall through */
        case HELPER_CANMATCH:
        case HELPER_EXISTS:
        case HELPER_MATCHMORE:
            cw_mutex_unlock(&conlock);
            break;
        case HELPER_EXEC:
            cw_mutex_unlock(&conlock);
            if (c->context != context)
                cw_copy_string(c->context, context, sizeof(c->context));
            if (c->exten != exten)
                cw_copy_string(c->exten, exten, sizeof(c->exten));
            c->priority = priority;
            pbx_substitute_variables(c, &c->vars, e->data, passdata, sizeof(passdata));
            manager_event(EVENT_FLAG_CALL, "Newexten", 
                "Channel: %s\r\n"
                "Context: %s\r\n"
                "Extension: %s\r\n"
                "Priority: %d\r\n"
                "Application: %s\r\n"
                "AppData: %s\r\n"
                "Uniqueid: %s\r\n",
                c->name, c->context, c->exten, c->priority, e->app, passdata, c->uniqueid);
            res = cw_function_exec_str(c, e->apphash, e->app, passdata, NULL, 0);
	    break;
        default:
            cw_log(CW_LOG_WARNING, "Huh (%d)?\n", action);
	    break;
        }
    }
    else if (sw)
    {
        switch (action)
        {
        case HELPER_CANMATCH:
        case HELPER_EXISTS:
        case HELPER_MATCHMORE:
        case HELPER_FINDLABEL:
            cw_mutex_unlock(&conlock);
            break;
        case HELPER_EXEC:
            cw_mutex_unlock(&conlock);
            if (sw->exec)
                res = sw->exec(c, foundcontext ? foundcontext : context, exten, priority, callerid, data);
            else
                cw_log(CW_LOG_WARNING, "No execution engine for switch %s\n", sw->name);
            break;
        default:
            cw_log(CW_LOG_WARNING, "Huh (%d)?\n", action);
            break;
        }
    }
    else
    {
        cw_mutex_unlock(&conlock);
        switch (status)
        {
        case STATUS_NO_CONTEXT:
            if ((action != HELPER_EXISTS) && (action != HELPER_MATCHMORE))
                cw_log(CW_LOG_NOTICE, "Cannot find extension context '%s'\n", context);
            break;
        case STATUS_NO_EXTENSION:
            if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH) && (action != HELPER_MATCHMORE))
                cw_log(CW_LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, context);
            break;
        case STATUS_NO_PRIORITY:
            if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH) && (action != HELPER_MATCHMORE))
                cw_log(CW_LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, context);
            break;
        case STATUS_NO_LABEL:
            if (context)
                cw_log(CW_LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, context);
            break;
        default:
            cw_log(CW_LOG_DEBUG, "Shouldn't happen!\n");
        }
        
        res = ((action != HELPER_EXISTS) && (action != HELPER_CANMATCH) && (action != HELPER_MATCHMORE));
    }

    if (sw)
        cw_object_put(sw);

    return res;
}

/*! \brief  cw_hint_extension: Find hint for given extension in context */
static struct cw_exten *cw_hint_extension(struct cw_channel *c, const char *context, const char *exten)
{
    struct cw_exten *e;
    struct cw_switch *sw = NULL;
    char *data;
    const char *foundcontext = NULL;
    int status = 0;
    char *incstack[CW_PBX_MAX_STACK];
    int stacklen = 0;

    cw_mutex_lock(&conlock);

    e = pbx_find_extension(c, NULL, context, exten, PRIORITY_HINT, NULL, "", HELPER_EXISTS, incstack, &stacklen, &status, &sw, &data, &foundcontext);

    cw_mutex_unlock(&conlock);    

    if (sw)
        cw_object_put(sw);

    return e;
}

/*! \brief  cw_extensions_state2: Check state of extension by using hints */
static int cw_extension_state2(struct cw_exten *e)
{
    char hint[CW_MAX_EXTENSION] = "";    
    char *cur, *rest;
    int res = -1;
    int allunavailable = 1, allbusy = 1, allfree = 1;
    int busy = 0, inuse = 0, ring = 0;

    if (!e)
        return -1;

    cw_copy_string(hint, cw_get_extension_app(e), sizeof(hint));

    cur = hint;        /* On or more devices separated with a & character */
    do
    {
        rest = strchr(cur, '&');
        if (rest)
        {
            *rest = 0;
            rest++;
        }
    
        res = cw_device_state(cur);
        switch (res)
        {
        case CW_DEVICE_NOT_INUSE:
            allunavailable = 0;
            allbusy = 0;
            break;
        case CW_DEVICE_INUSE:
            inuse = 1;
            allunavailable = 0;
            allfree = 0;
            break;
        case CW_DEVICE_RINGING:
            ring = 1;
            allunavailable = 0;
            allfree = 0;
            break;
        case CW_DEVICE_BUSY:
            allunavailable = 0;
            allfree = 0;
            busy = 1;
            break;
        case CW_DEVICE_UNAVAILABLE:
        case CW_DEVICE_INVALID:
            allbusy = 0;
            allfree = 0;
            break;
        default:
            allunavailable = 0;
            allbusy = 0;
            allfree = 0;
        }
        cur = rest;
    }
    while (cur);

    if (!inuse && ring)
        return CW_EXTENSION_RINGING;
    if (inuse && ring)
        return (CW_EXTENSION_INUSE | CW_EXTENSION_RINGING);
    if (inuse)
        return CW_EXTENSION_INUSE;
    if (allfree)
        return CW_EXTENSION_NOT_INUSE;
    if (allbusy)        
        return CW_EXTENSION_BUSY;
    if (allunavailable)
        return CW_EXTENSION_UNAVAILABLE;
    if (busy) 
        return CW_EXTENSION_INUSE;
    
    return CW_EXTENSION_NOT_INUSE;
}

/*! \brief  cw_extension_state2str: Return extension_state as string */
const char *cw_extension_state2str(int extension_state)
{
    int i;

    for (i = 0;  (i < (sizeof(extension_states)/sizeof(extension_states[0])));  i++)
    {
        if (extension_states[i].extension_state == extension_state)
            return extension_states[i].text;
    }
    return "Unknown";    
}

/*! \brief  cw_extension_state: Check extension state for an extension by using hint */
int cw_extension_state(struct cw_channel *c, char *context, char *exten)
{
    struct cw_exten *e;

    e = cw_hint_extension(c, context, exten);    /* Do we have a hint for this extension ? */ 
    if (!e) 
        return -1;                /* No hint, return -1 */

    return cw_extension_state2(e);            /* Check all devices in the hint */
}

void cw_hint_state_changed(const char *device)
{
    struct cw_hint *hint;
    struct cw_state_cb *cblist;
    char buf[CW_MAX_EXTENSION];
    char *parse;
    char *cur;
    int state;

    cw_mutex_lock(&hintlock);

    for (hint = hints; hint; hint = hint->next)
    {
        cw_copy_string(buf, cw_get_extension_app(hint->exten), sizeof(buf));
        parse = buf;
        for (cur = strsep(&parse, "&"); cur; cur = strsep(&parse, "&"))
        {
            if (strcmp(cur, device))
                continue;

            /* Get device state for this hint */
            state = cw_extension_state2(hint->exten);
            
            if ((state == -1) || (state == hint->laststate))
                continue;

            /* Device state changed since last check - notify the watchers */
            
            /* For general callbacks */
            for (cblist = statecbs; cblist; cblist = cblist->next)
                cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
            
            /* For extension callbacks */
            for (cblist = hint->callbacks; cblist; cblist = cblist->next)
                cblist->callback(hint->exten->parent->name, hint->exten->exten, state, cblist->data);
            
            hint->laststate = state;
            break;
        }
    }

    cw_mutex_unlock(&hintlock);
}
            
/*! \brief  cw_extension_state_add: Add watcher for extension states */
int cw_extension_state_add(const char *context, const char *exten, 
                cw_state_cb_type callback, void *data)
{
    struct cw_hint *list;
    struct cw_state_cb *cblist;
    struct cw_exten *e;

    /* If there's no context and extension:  add callback to statecbs list */
    if (!context  &&  !exten)
    {
        cw_mutex_lock(&hintlock);

        cblist = statecbs;
        while (cblist)
        {
            if (cblist->callback == callback)
            {
                cblist->data = data;
                cw_mutex_unlock(&hintlock);
                return 0;
            }
            cblist = cblist->next;
        }
    
        /* Now insert the callback */
        if ((cblist = malloc(sizeof(struct cw_state_cb))) == NULL)
        {
            cw_mutex_unlock(&hintlock);
            return -1;
        }
        memset(cblist, 0, sizeof(struct cw_state_cb));
        cblist->id = 0;
        cblist->callback = callback;
        cblist->data = data;
    
        cblist->next = statecbs;
        statecbs = cblist;

        cw_mutex_unlock(&hintlock);
        return 0;
    }

    if (!context  ||  !exten)
        return -1;

    /* This callback type is for only one hint, so get the hint */
    e = cw_hint_extension(NULL, context, exten);    
    if (!e)
        return -1;

    /* Find the hint in the list of hints */
    cw_mutex_lock(&hintlock);
    list = hints;        

    while (list)
    {
        if (list->exten == e)
            break;        
        list = list->next;    
    }

    if (!list)
    {
        /* We have no hint, sorry */
        cw_mutex_unlock(&hintlock);
        return -1;
    }

    /* Now insert the callback in the callback list  */
    if ((cblist = malloc(sizeof(struct cw_state_cb))) == NULL)
    {
        cw_mutex_unlock(&hintlock);
        return -1;
    }
    memset(cblist, 0, sizeof(struct cw_state_cb));
    cblist->id = stateid++;        /* Unique ID for this callback */
    cblist->callback = callback;    /* Pointer to callback routine */
    cblist->data = data;        /* Data for the callback */

    cblist->next = list->callbacks;
    list->callbacks = cblist;

    cw_mutex_unlock(&hintlock);
    return cblist->id;
}

/*! \brief  cw_extension_state_del: Remove a watcher from the callback list */
int cw_extension_state_del(int id, cw_state_cb_type callback)
{
    struct cw_hint *list;
    struct cw_state_cb *cblist, *cbprev;

    if (!id && !callback)
        return -1;

    cw_mutex_lock(&hintlock);

    /* id is zero is a callback without extension */
    if (!id)
    {
        cbprev = NULL;
        cblist = statecbs;
        while (cblist)
        {
            if (cblist->callback == callback)
            {
                if (!cbprev)
                    statecbs = cblist->next;
                else
                    cbprev->next = cblist->next;

                free(cblist);

                cw_mutex_unlock(&hintlock);
                return 0;
            }
            cbprev = cblist;
            cblist = cblist->next;
        }

        cw_mutex_lock(&hintlock);
        return -1;
    }

    /* id greater than zero is a callback with extension */
    /* Find the callback based on ID */
    list = hints;
    while (list)
    {
        cblist = list->callbacks;
        cbprev = NULL;
        while (cblist)
        {
            if (cblist->id == id)
            {
                if (!cbprev)
                    list->callbacks = cblist->next;        
                else
                    cbprev->next = cblist->next;
                free(cblist);
        
                cw_mutex_unlock(&hintlock);
                return 0;        
            }        
            cbprev = cblist;                
            cblist = cblist->next;
        }
        list = list->next;
    }

    cw_mutex_unlock(&hintlock);
    return -1;
}

/*! \brief  cw_add_hint: Add hint to hint list, check initial extension state */
static int cw_add_hint(struct cw_exten *e)
{
    struct cw_hint *list;

    if (!e) 
        return -1;

    cw_mutex_lock(&hintlock);
    list = hints;        

    /* Search if hint exists, do nothing */
    while (list)
    {
        if (list->exten == e)
        {
            cw_mutex_unlock(&hintlock);
            if (option_debug > 1)
                cw_log(CW_LOG_DEBUG, "HINTS: Not re-adding existing hint %s: %s\n", cw_get_extension_name(e), cw_get_extension_app(e));
            return -1;
        }
        list = list->next;    
    }

    if (option_debug > 1)
        cw_log(CW_LOG_DEBUG, "HINTS: Adding hint %s: %s\n", cw_get_extension_name(e), cw_get_extension_app(e));

    if ((list = malloc(sizeof(struct cw_hint))) == NULL)
    {
        cw_mutex_unlock(&hintlock);
        if (option_debug > 1)
            cw_log(CW_LOG_DEBUG, "HINTS: Out of memory...\n");
        return -1;
    }
    /* Initialize and insert new item at the top */
    memset(list, 0, sizeof(struct cw_hint));
    list->exten = e;
    list->laststate = cw_extension_state2(e);
    list->next = hints;
    hints = list;

    cw_mutex_unlock(&hintlock);
    return 0;
}

/*! \brief  cw_change_hint: Change hint for an extension */
static int cw_change_hint(struct cw_exten *oe, struct cw_exten *ne)
{ 
    struct cw_hint *list;

    cw_mutex_lock(&hintlock);
    list = hints;

    while (list)
    {
        if (list->exten == oe)
        {
                list->exten = ne;
            cw_mutex_unlock(&hintlock);    
            return 0;
        }
        list = list->next;
    }
    cw_mutex_unlock(&hintlock);

    return -1;
}

/*! \brief  cw_remove_hint: Remove hint from extension */
static int cw_remove_hint(struct cw_exten *e)
{
    /* Cleanup the Notifys if hint is removed */
    struct cw_hint *list, *prev = NULL;
    struct cw_state_cb *cblist, *cbprev;

    if (!e) 
        return -1;

    cw_mutex_lock(&hintlock);

    list = hints;    
    while (list)
    {
        if (list->exten == e)
        {
            cbprev = NULL;
            cblist = list->callbacks;
            while (cblist)
            {
                /* Notify with -1 and remove all callbacks */
                cbprev = cblist;        
                cblist = cblist->next;
                cbprev->callback(list->exten->parent->name, list->exten->exten, CW_EXTENSION_DEACTIVATED, cbprev->data);
                free(cbprev);
                }
                list->callbacks = NULL;

                if (!prev)
                hints = list->next;
                else
                prev->next = list->next;
                free(list);
        
            cw_mutex_unlock(&hintlock);
            return 0;
        }
        prev = list;
        list = list->next;    
    }

    cw_mutex_unlock(&hintlock);
    return -1;
}


/*! \brief  cw_get_hint: Get hint for channel */
int cw_get_hint(char *hint, int hintsize, char *name, int namesize, struct cw_channel *c, const char *context, const char *exten)
{
    struct cw_exten *e;
    void *tmp;

    e = cw_hint_extension(c, context, exten);
    if (e)
    {
        if (hint) 
            cw_copy_string(hint, cw_get_extension_app(e), hintsize);
        if (name)
        {
            tmp = cw_get_extension_app_data(e);
            if (tmp)
                cw_copy_string(name, (char *) tmp, namesize);
        }
        return -1;
    }
    return 0;    
}

int cw_exists_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid) 
{
    return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_EXISTS);
}

int cw_findlabel_extension(struct cw_channel *c, const char *context, const char *exten, const char *label, const char *callerid) 
{
    return pbx_extension_helper(c, NULL, context, exten, 0, label, callerid, HELPER_FINDLABEL);
}

int cw_findlabel_extension2(struct cw_channel *c, struct cw_context *con, const char *exten, const char *label, const char *callerid) 
{
    return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, HELPER_FINDLABEL);
}

int cw_canmatch_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
    return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_CANMATCH);
}

int cw_matchmore_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
    return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_MATCHMORE);
}

int cw_exec_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid) 
{
    return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, HELPER_EXEC);
}

static int __cw_pbx_run(struct cw_channel *c)
{
    int firstpass = 1;
    int digit;
    char exten[256];
    int pos;
    int waittime;
    int res=0;
    int autoloopflag;

    /* A little initial setup here */
    if (c->pbx)
        cw_log(CW_LOG_WARNING, "%s already has PBX structure??\n", c->name);
    if ((c->pbx = malloc(sizeof(struct cw_pbx))) == NULL)
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        return -1;
    }
    if (c->amaflags)
    {
        if (!c->cdr)
        {
            c->cdr = cw_cdr_alloc();
            if (!c->cdr)
            {
                cw_log(CW_LOG_WARNING, "Unable to create Call Detail Record\n");
                free(c->pbx);
                return -1;
            }
            cw_cdr_init(c->cdr, c);
        }
    }
    memset(c->pbx, 0, sizeof(struct cw_pbx));
    /* Set reasonable defaults */
    c->pbx->rtimeout = 10;
    c->pbx->dtimeout = 5;

    autoloopflag = cw_test_flag(c, CW_FLAG_IN_AUTOLOOP);
    cw_set_flag(c, CW_FLAG_IN_AUTOLOOP);

    /* Start by trying whatever the channel is set to */
    if (!cw_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))
    {
        /* If not successful fall back to 's' */
        if (option_verbose > 1)
            cw_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", c->name, c->context, c->exten, c->priority);
        cw_copy_string(c->exten, "s", sizeof(c->exten));
        if (!cw_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))
        {
            /* JK02: And finally back to default if everything else failed */
            if (option_verbose > 1)
                cw_verbose( VERBOSE_PREFIX_2 "Starting %s at %s,%s,%d still failed so falling back to context 'default'\n", c->name, c->context, c->exten, c->priority);
            cw_copy_string(c->context, "default", sizeof(c->context));
        }
        c->priority = 1;
    }
    if (c->cdr  &&  !c->cdr->start.tv_sec  &&  !c->cdr->start.tv_usec)
        cw_cdr_start(c->cdr);
    for(;;)
    {
        pos = 0;
        digit = 0;
        while (cw_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))
        {
            memset(exten, 0, sizeof(exten));
            if ((res = cw_exec_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)))
            {
                /* Something bad happened, or a hangup has been requested. */
                if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
                    (res == '*') || (res == '#'))
                {
                    cw_log(CW_LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
                    memset(exten, 0, sizeof(exten));
                    pos = 0;
                    exten[pos++] = digit = res;
                    break;
                }
                switch (res)
                {
                case CW_PBX_KEEPALIVE:
                    if (option_debug)
                        cw_log(CW_LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
                    if (option_verbose > 1)
                        cw_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
                    goto out;
                    break;
                default:
                    if (option_debug)
                        cw_log(CW_LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
                    if (option_verbose > 1)
                        cw_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
                    if (c->_softhangup == CW_SOFTHANGUP_ASYNCGOTO)
                    {
                        c->_softhangup =0;
                        break;
                    }
                    /* atimeout */
                    if (c->_softhangup == CW_SOFTHANGUP_TIMEOUT)
                    {
                        break;
                    }

                    if (c->cdr)
                    {
                        cw_cdr_update(c);
                    }
                    goto out;
                }
            }
            if ((c->_softhangup == CW_SOFTHANGUP_TIMEOUT) && (cw_exists_extension(c,c->context,"T",1,c->cid.cid_num)))
            {
                cw_copy_string(c->exten, "T", sizeof(c->exten));
                /* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
                c->whentohangup = 0;
                c->priority = 0;
                c->_softhangup &= ~CW_SOFTHANGUP_TIMEOUT;
            }
            else if (c->_softhangup)
            {
                cw_log(CW_LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
                    c->exten, c->priority);
                goto out;
            }
            firstpass = 0;
            c->priority++;
        }
        if (!cw_exists_extension(c, c->context, c->exten, 1, c->cid.cid_num))
        {
            /* It's not a valid extension anymore */
            if (cw_exists_extension(c, c->context, "i", 1, c->cid.cid_num))
            {
                if (option_verbose > 2)
                    cw_verbose(VERBOSE_PREFIX_3 "Sent into invalid extension '%s' in context '%s' on %s\n", c->exten, c->context, c->name);
                pbx_builtin_setvar_helper(c, "INVALID_EXTEN", c->exten);
                cw_copy_string(c->exten, "i", sizeof(c->exten));
                c->priority = 1;
            }
            else
            {
                cw_log(CW_LOG_WARNING, "Channel '%s' sent into invalid extension '%s' in context '%s', but no invalid handler\n",
                    c->name, c->exten, c->context);
                goto out;
            }
        }
        else if (c->_softhangup == CW_SOFTHANGUP_TIMEOUT)
        {
            /* If we get this far with CW_SOFTHANGUP_TIMEOUT, then we know that the "T" extension is next. */
            c->_softhangup = 0;
        }
        else
        {
            /* Done, wait for an extension */
            waittime = 0;
            if (digit)
                waittime = c->pbx->dtimeout;
            else if (!autofallthrough)
                waittime = c->pbx->rtimeout;
            if (waittime)
            {
                while (cw_matchmore_extension(c, c->context, exten, 1, c->cid.cid_num))
                {
                    /* As long as we're willing to wait, and as long as it's not defined, 
                       keep reading digits until we can't possibly get a right answer anymore.  */
                    digit = cw_waitfordigit(c, waittime * 1000);
                    if (c->_softhangup == CW_SOFTHANGUP_ASYNCGOTO)
                    {
                        c->_softhangup = 0;
                    }
                    else
                    {
                        if (!digit)
                            /* No entry */
                            break;
                        if (digit < 0)
                            /* Error, maybe a  hangup */
                            goto out;
                        exten[pos++] = digit;
                        waittime = c->pbx->dtimeout;
                    }
                }
                if (cw_exists_extension(c, c->context, exten, 1, c->cid.cid_num)) {
                    /* Prepare the next cycle */
                    cw_copy_string(c->exten, exten, sizeof(c->exten));
                    c->priority = 1;
                }
                else
                {
                    /* No such extension */
                    if (!cw_strlen_zero(exten))
                    {
                        /* An invalid extension */
                        if (cw_exists_extension(c, c->context, "i", 1, c->cid.cid_num))
                        {
                            if (option_verbose > 2)
                                cw_verbose( VERBOSE_PREFIX_3 "Invalid extension '%s' in context '%s' on %s\n", exten, c->context, c->name);
                            pbx_builtin_setvar_helper(c, "INVALID_EXTEN", exten);
                            cw_copy_string(c->exten, "i", sizeof(c->exten));
                            c->priority = 1;
                        }
                        else
                        {
                            cw_log(CW_LOG_WARNING, "Invalid extension '%s', but no rule 'i' in context '%s'\n", exten, c->context);
                            goto out;
                        }
                    }
                    else
                    {
                        /* A simple timeout */
                        if (cw_exists_extension(c, c->context, "t", 1, c->cid.cid_num))
                        {
                            if (option_verbose > 2)
                                cw_verbose( VERBOSE_PREFIX_3 "Timeout on %s\n", c->name);
                            cw_copy_string(c->exten, "t", sizeof(c->exten));
                            c->priority = 1;
                        }
                        else
                        {
                            cw_log(CW_LOG_WARNING, "Timeout, but no rule 't' in context '%s'\n", c->context);
                            goto out;
                        }
                    }    
                }
                if (c->cdr)
                {
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_2 "CDR updated on %s\n",c->name);    
                    cw_cdr_update(c);
                }
            }
            else
            {
                struct cw_var_t *var;

                // this should really use c->hangupcause instead of dialstatus
                // let's go along with it for now but we should revisit it later
                
                var = pbx_builtin_getvar_helper(c, CW_KEYWORD_DIALSTATUS, "DIALSTATUS");

                if (option_verbose > 2)
                    cw_verbose(VERBOSE_PREFIX_2 "Auto fallthrough, channel '%s' status is '%s'\n", c->name, (var ? var->value : "UNKNOWN"));

                if (var && !strcmp(var->value, "BUSY")) {
                    cw_indicate(c, CW_CONTROL_BUSY);
                    if (c->_state != CW_STATE_UP)
                        cw_setstate(c, CW_STATE_BUSY);
		} else { /* CHANUNAVAIL, CONGESTION or UNKNOWN */
                    cw_indicate(c, CW_CONTROL_CONGESTION);
                    if (c->_state != CW_STATE_UP)
                        cw_setstate(c, CW_STATE_BUSY);
		}

		if (var)
			cw_object_put(var);

                cw_safe_sleep(c, 10000);
                goto out;
            }
        }
    }
    if (firstpass) 
        cw_log(CW_LOG_WARNING, "Don't know what to do with '%s'\n", c->name);
out:
    if ((res != CW_PBX_KEEPALIVE) && cw_exists_extension(c, c->context, "h", 1, c->cid.cid_num))
    {
		if (c->cdr && cw_end_cdr_before_h_exten)
			cw_cdr_end(c->cdr);

        c->exten[0] = 'h';
        c->exten[1] = '\0';
        c->priority = 1;
        while (cw_exists_extension(c, c->context, c->exten, c->priority, c->cid.cid_num))
        {
            if ((res = cw_exec_extension(c, c->context, c->exten, c->priority, c->cid.cid_num)))
            {
                /* Something bad happened, or a hangup has been requested. */
                if (option_debug)
                    cw_log(CW_LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
                if (option_verbose > 1)
                    cw_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
                break;
            }
            c->priority++;
        }
    }
    cw_set2_flag(c, autoloopflag, CW_FLAG_IN_AUTOLOOP);

    pbx_destroy(c->pbx);
    c->pbx = NULL;
    if (res != CW_PBX_KEEPALIVE)
        cw_hangup(c);
    return 0;
}

/* Returns 0 on success, non-zero if call limit was reached */
static int increase_call_count(const struct cw_channel *c)
{
    int failed = 0;
    double curloadavg;

    cw_mutex_lock(&maxcalllock);
    if (option_maxcalls)
    {
        if (countcalls >= option_maxcalls)
        {
            cw_log(CW_LOG_ERROR, "Maximum call limit of %d calls exceeded by '%s'!\n", option_maxcalls, c->name);
            failed = -1;
        }
    }
    if (option_maxload)
    {
        getloadavg(&curloadavg, 1);
        if (curloadavg >= option_maxload)
        {
            cw_log(CW_LOG_ERROR, "Maximum loadavg limit of %lf load exceeded by '%s' (currently %f)!\n", option_maxload, c->name, curloadavg);
            failed = -1;
        }
    }
    if (!failed)
        countcalls++;    
    cw_mutex_unlock(&maxcalllock);

    return failed;
}

static void decrease_call_count(void)
{
    cw_mutex_lock(&maxcalllock);
    if (countcalls > 0)
        countcalls--;
    cw_mutex_unlock(&maxcalllock);
}

static void *pbx_thread(void *data)
{
    /* NOTE:
       The launcher of this function _MUST_ increment 'countcalls'
       before invoking the function; it will be decremented when the
       PBX has finished running on the channel
     */
    struct cw_channel *c = data;

    __cw_pbx_run(c);
    decrease_call_count();
    cw_object_put(c);

    return NULL;
}

enum cw_pbx_result cw_pbx_start(struct cw_channel *c)
{
    pthread_t t;

    if (!c)
    {
        cw_log(CW_LOG_WARNING, "Asked to start thread on NULL channel?\n");
        return CW_PBX_FAILED;
    }
       
    if (increase_call_count(c))
        return CW_PBX_CALL_LIMIT;

    /* Start a new thread, and get something handling this channel. */
    if (cw_pthread_create(&t, &global_attr_detached, pbx_thread, cw_object_dup(c)))
    {
        cw_log(CW_LOG_WARNING, "Failed to create new channel thread\n");
        cw_object_put(c);
        return CW_PBX_FAILED;
    }

    return CW_PBX_SUCCESS;
}

enum cw_pbx_result cw_pbx_run(struct cw_channel *c)
{
    enum cw_pbx_result res = CW_PBX_SUCCESS;

    if (increase_call_count(c))
        return CW_PBX_CALL_LIMIT;

    res = __cw_pbx_run(c);
    decrease_call_count();

    return res;
}

int cw_active_calls(void)
{
    return countcalls;
}

int pbx_set_autofallthrough(int newval)
{
    int oldval;

    oldval = autofallthrough;
    if (oldval != newval)
        autofallthrough = newval;
    return oldval;
}

/*
 * This function locks contexts list by &conlist, search for the right context
 * structure, leave context list locked and call cw_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int cw_context_remove_include(const char *context, const char *include, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
        return -1;

    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_remove_include2(c, include, registrar);
	    break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes include, unlock context and
 * return.
 */
int cw_context_remove_include2(struct cw_context *con, const char *include, const char *registrar)
{
    struct cw_include *i, *pi = NULL;

    if (cw_mutex_lock(&con->lock))
        return -1;

    /* walk includes */
    i = con->includes;
    while (i)
    {
        /* find our include */
        if (!strcmp(i->name, include)
            && 
            (!registrar || !strcmp(i->registrar, registrar)))
        {
            /* remove from list */
            if (pi)
                pi->next = i->next;
            else
                con->includes = i->next;
            /* free include and return */
            free(i);
            cw_mutex_unlock(&con->lock);
            return 0;
        }
        pi = i;
        i = i->next;
    }

    /* we can't find the right include */
    cw_mutex_unlock(&con->lock);
    return -1;
}

/*
 * This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call cw_context_remove_switch2
 * which removes switch, unlock contexts list and return ...
 */
int cw_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
        return -1;

    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_remove_switch2(c, sw, data, registrar);
	    break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes switch, unlock context and
 * return.
 */
int cw_context_remove_switch2(struct cw_context *con, const char *sw, const char *data, const char *registrar)
{
    struct cw_sw *i, *pi = NULL;

    if (cw_mutex_lock(&con->lock))
        return -1;

    /* walk switches */
    i = con->alts;
    while (i)
    {
        /* find our switch */
        if (!strcmp(i->name, sw) && !strcmp(i->data, data)
            && 
            (!registrar || !strcmp(i->registrar, registrar)))
        {
            /* remove from list */
            if (pi)
                pi->next = i->next;
            else
                con->alts = i->next;
            /* free switch and return */
            free(i);
            cw_mutex_unlock(&con->lock);
            return 0;
        }
        pi = i;
        i = i->next;
    }

    /* we can't find the right switch */
    cw_mutex_unlock(&con->lock);
    return -1;
}

/*
 * This functions lock contexts list, search for the right context,
 * call cw_context_remove_extension2, unlock contexts list and return.
 * In this function we are using
 */
int cw_context_remove_extension(const char *context, const char *extension, int priority, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
        return -1;

    /* walk contexts ... */
    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        /* ... search for the right one ... */
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_remove_extension2(c, extension, priority, registrar);
            break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

/*
 * When do you want to call this function, make sure that &conlock is locked,
 * because some process can handle with your *con context before you lock
 * it.
 *
 * This functionc locks given context, search for the right extension and
 * fires out all peer in this extensions with given priority. If priority
 * is set to 0, all peers are removed. After that, unlock context and
 * return.
 */
int cw_context_remove_extension2(struct cw_context *con, const char *extension, int priority, const char *registrar)
{
    struct cw_exten *exten, *prev_exten = NULL;

    if (cw_mutex_lock(&con->lock))
        return -1;

    /* go through all extensions in context and search the right one ... */
    exten = con->root;
    while (exten)
    {
        /* look for right extension */
        if (!strcmp(exten->exten, extension)
            &&
            (!registrar || !strcmp(exten->registrar, registrar)))
        {
            struct cw_exten *peer;

            /* should we free all peers in this extension? (priority == 0)? */
            if (priority == 0)
            {
                /* remove this extension from context list */
                if (prev_exten)
                    prev_exten->next = exten->next;
                else
                    con->root = exten->next;

                /* fire out all peers */
                peer = exten; 
                while (peer)
                {
                    exten = peer->peer;
                    
                    if (!peer->priority == PRIORITY_HINT) 
                        cw_remove_hint(peer);

                    peer->datad(peer->data);
                    free(peer);

                    peer = exten;
                }

                cw_mutex_unlock(&con->lock);
                return 0;
            }
            else
            {
                /* remove only extension with exten->priority == priority */
                struct cw_exten *previous_peer = NULL;

                peer = exten;
                while (peer)
                {
                    /* is this our extension? */
                    if (peer->priority == priority
                        &&
                        (!registrar || !strcmp(peer->registrar, registrar)))
                    {
                        /* we are first priority extension? */
                        if (!previous_peer)
                        {
                            /* exists previous extension here? */
                            if (prev_exten)
                            {
                                /* yes, so we must change next pointer in
                                 * previous connection to next peer
                                 */
                                if (peer->peer)
                                {
                                    prev_exten->next = peer->peer;
                                    peer->peer->next = exten->next;
                                }
                                else
                                {
                                    prev_exten->next = exten->next;
                                }
                            }
                            else
                            {
                                /* no previous extension, we are first
                                 * extension, so change con->root ...
                                 */
                                if (peer->peer)
                                    con->root = peer->peer;
                                else
                                    con->root = exten->next; 
                            }
                        }
                        else
                        {
                            /* we are not first priority in extension */
                            previous_peer->peer = peer->peer;
                        }

                        /* now, free whole priority extension */
                        if (peer->priority==PRIORITY_HINT)
                            cw_remove_hint(peer);
                        peer->datad(peer->data);
                        free(peer);

                        cw_mutex_unlock(&con->lock);
                        return 0;
                    }
                    /* this is not right extension, skip to next peer */
                    previous_peer = peer;
                    peer = peer->peer;
                }

                cw_mutex_unlock(&con->lock);
                return -1;
            }
        }

        prev_exten = exten;
        exten = exten->next;
    }

    /* we can't find right extension */
    cw_mutex_unlock(&con->lock);
    return -1;
}


/*
 * Help for CLI commands ...
 */
static const char show_dialplan_help[] =
"Usage: show dialplan [exten@][context]\n"
"       Show dialplan\n";

static const char show_switches_help[] =
"Usage: show switches\n"
"       Show registered switches\n";

static const char show_hints_help[] =
"Usage: show hints\n"
"       Show registered hints\n";

static const char show_globals_help[] =
"Usage: show globals\n"
"       List current global dialplan variables and their values\n";

static const char set_global_help[] =
"Usage: set global <name> <value>\n"
"       Set global dialplan variable <name> to <value>\n";


/*
 * IMPLEMENTATION OF CLI FUNCTIONS IS IN THE SAME ORDER AS COMMANDS HELPS
 *
 */

/*! \brief  handle_show_hints: CLI support for listing registred dial plan hints */
static int handle_show_hints(int fd, int argc, char *argv[])
{
    struct cw_hint *hint;
    int num = 0;
    int watchers;
    struct cw_state_cb *watcher;

    if (!hints)
    {
        cw_cli(fd, "There are no registered dialplan hints\n");
        return RESULT_SUCCESS;
    }
    /* ... we have hints ... */
    cw_cli(fd, "\n    -== Registered CallWeaver Dial Plan Hints ==-\n");
    if (cw_mutex_lock(&hintlock))
    {
        cw_log(CW_LOG_ERROR, "Unable to lock hints\n");
        return -1;
    }
    hint = hints;
    while (hint)
    {
        watchers = 0;
        for (watcher = hint->callbacks; watcher; watcher = watcher->next)
            watchers++;
        cw_cli(fd, "   %-20.20s: %-20.20s  State:%-15.15s Watchers %2d\n",
            cw_get_extension_name(hint->exten), cw_get_extension_app(hint->exten),
            cw_extension_state2str(hint->laststate), watchers);
        num++;
        hint = hint->next;
    }
    cw_cli(fd, "----------------\n");
    cw_cli(fd, "- %d hints registered\n", num);
    cw_mutex_unlock(&hintlock);
    return RESULT_SUCCESS;
}


/*
 * 'show dialplan' CLI command implementation functions ...
 */
static void complete_show_dialplan_context(int fd, char *argv[], int lastarg, int lastarg_len)
{
    struct cw_context *c;

    if (lastarg == 2)
    {
        if (!cw_lock_contexts())
        {
            for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
	    {
                if (!strncasecmp(argv[2], cw_get_context_name(c), lastarg_len))
                    cw_cli(fd, "%s\n", cw_get_context_name(c));
	    }
            cw_unlock_contexts();
        }
        else
        {
            cw_log(CW_LOG_ERROR, "Unable to lock context list\n");
        }
    }
}

struct dialplan_counters
{
    int total_context;
    int total_exten;
    int total_prio;
    int context_existence;
    int extension_existence;
};

static int show_dialplan_helper(int fd, char *context, char *exten, struct dialplan_counters *dpc, struct cw_include *rinclude, int includecount, char *includes[])
{
    struct cw_context *c;
    int res=0, old_total_exten = dpc->total_exten;

    /* try to lock contexts */
    if (cw_lock_contexts())
    {
        cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
        return -1;
    }

    /* walk all contexts ... */
    for (c = cw_walk_contexts(NULL); c ; c = cw_walk_contexts(c))
    {
        /* show this context? */
        if (!context  ||  !strcmp(cw_get_context_name(c), context))
        {
            dpc->context_existence = 1;

            /* try to lock context before walking in ... */
            if (!cw_lock_context(c))
            {
                struct cw_exten *e;
                struct cw_include *i;
                struct cw_ignorepat *ip;
                struct cw_sw *sw;
                char buf[256], buf2[256];
                int context_info_printed = 0;

                /* are we looking for exten too? if yes, we print context
                 * if we our extension only
                 */
                if (!exten)
                {
                    dpc->total_context++;
                    cw_cli(fd, "[ Context '%s' (%#x) created by '%s' ]\n",
                        cw_get_context_name(c), c->hash, cw_get_context_registrar(c));
                    context_info_printed = 1;
                }

                /* walk extensions ... */
                for (e = cw_walk_context_extensions(c, NULL);  e;  e = cw_walk_context_extensions(c, e))
                {
                    struct cw_exten *p;
                    int prio;

                    /* looking for extension? is this our extension? */
                    if (exten
                        &&
                        !cw_extension_match(cw_get_extension_name(e), exten))
                    {
                        /* we are looking for extension and it's not our
                          * extension, so skip to next extension */
                        continue;
                    }

                    dpc->extension_existence = 1;

                    /* may we print context info? */    
                    if (!context_info_printed)
                    {
                        dpc->total_context++;
                        if (rinclude)
                        {
                            /* TODO Print more info about rinclude */
                            cw_cli(fd, "[ Included context '%s' (%#x) created by '%s' ]\n",
                                cw_get_context_name(c), c->hash,
                                cw_get_context_registrar(c));
                        }
                        else
                        {
                            cw_cli(fd, "[ Context '%s' (%#x) created by '%s' ]\n",
                                cw_get_context_name(c), c->hash,
                                cw_get_context_registrar(c));
                        }
                        context_info_printed = 1;
                    }
                    dpc->total_prio++;

                    /* write extension name and first peer */    
                    bzero(buf, sizeof(buf));        
                    snprintf(buf, sizeof(buf), "'%s' =>",
                        cw_get_extension_name(e));

                    prio = cw_get_extension_priority(e);
                    if (prio == PRIORITY_HINT)
                    {
                        snprintf(buf2, sizeof(buf2),
                            "hint: %s",
                            cw_get_extension_app(e));
                    }
                    else
                    {
                        snprintf(buf2, sizeof(buf2),
                            "%d. %s(%s)",
                            prio,
                            cw_get_extension_app(e),
                            (char *)cw_get_extension_app_data(e));
                    }

                    cw_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
                        cw_get_extension_registrar(e));

                    dpc->total_exten++;
                    /* walk next extension peers */
                    for (p = cw_walk_extension_priorities(e, e);  p;  p = cw_walk_extension_priorities(e, p))
                    {
                        dpc->total_prio++;
                        bzero((void *) buf2, sizeof(buf2));
                        bzero((void *) buf, sizeof(buf));
                        if (cw_get_extension_label(p))
                            snprintf(buf, sizeof(buf), "   [%s]", cw_get_extension_label(p));
                        prio = cw_get_extension_priority(p);
                        if (prio == PRIORITY_HINT)
                        {
                            snprintf(buf2, sizeof(buf2),
                                "hint: %s",
                                cw_get_extension_app(p));
                        }
                        else
                        {
                            snprintf(buf2, sizeof(buf2),
                                "%d. %s(%s)",
                                prio,
                                cw_get_extension_app(p),
                                (char *)cw_get_extension_app_data(p));
                        }

                        cw_cli(fd,"  %-17s %-45s [%s]\n",
                            buf, buf2,
                            cw_get_extension_registrar(p));
                    }
                }

                /* walk included and write info ... */
                for (i = cw_walk_context_includes(c, NULL);  i;  i = cw_walk_context_includes(c, i))
                {
                    bzero(buf, sizeof(buf));
                    snprintf(buf, sizeof(buf), "'%s'",
                        cw_get_include_name(i));
                    if (exten)
                    {
                        /* Check all includes for the requested extension */
                        if (includecount >= CW_PBX_MAX_STACK)
                        {
                            cw_log(CW_LOG_NOTICE, "Maximum include depth exceeded!\n");
                        }
                        else
                        {
                            int dupe=0;
                            int x;

                            for (x = 0;  x < includecount;  x++)
                            {
                                if (!strcasecmp(includes[x], cw_get_include_name(i)))
                                {
                                    dupe++;
                                    break;
                                }
                            }
                            if (!dupe)
                            {
                                includes[includecount] = (char *)cw_get_include_name(i);
                                show_dialplan_helper(fd, (char *)cw_get_include_name(i),
                                                    exten, dpc, i, includecount + 1, includes);
                            }
                            else
                            {
                                cw_log(CW_LOG_WARNING, "Avoiding circular include of %s within %s (%#x)\n",
                                         cw_get_include_name(i), context, c->hash);
                            }
                        }
                    }
                    else
                    {
                        cw_cli(fd, "  Include =>        %-45s [%s]\n",
                                 buf, cw_get_include_registrar(i));
                    }
                }

                /* walk ignore patterns and write info ... */
                for (ip = cw_walk_context_ignorepats(c, NULL);  ip;  ip = cw_walk_context_ignorepats(c, ip))
                {
                    const char *ipname = cw_get_ignorepat_name(ip);
                    char ignorepat[CW_MAX_EXTENSION];

                    snprintf(buf, sizeof(buf), "'%s'", ipname);
                    snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
                    if ((!exten)  ||  cw_extension_match(ignorepat, exten))
                    {
                        cw_cli(fd, "  Ignore pattern => %-45s [%s]\n",
                            buf, cw_get_ignorepat_registrar(ip));
                    }
                }
                if (!rinclude)
                {
                    for (sw = cw_walk_context_switches(c, NULL);  sw;  sw = cw_walk_context_switches(c, sw))
                    {
                        snprintf(buf, sizeof(buf), "'%s/%s'",
                            cw_get_switch_name(sw),
                            cw_get_switch_data(sw));
                        cw_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
                            buf, cw_get_switch_registrar(sw));    
                    }
                }
    
                cw_unlock_context(c);

                /* if we print something in context, make an empty line */
                if (context_info_printed) cw_cli(fd, "\r\n");
            }
        }
    }
    cw_unlock_contexts();

    if (dpc->total_exten == old_total_exten)
    {
        /* Nothing new under the sun */
        return -1;
    }
    return res;
}

static int handle_show_dialplan(int fd, int argc, char *argv[])
{
    char *exten = NULL, *context = NULL;
    /* Variables used for different counters */
    struct dialplan_counters counters;
    char *incstack[CW_PBX_MAX_STACK];
    memset(&counters, 0, sizeof(counters));

    if (argc != 2  &&  argc != 3) 
        return RESULT_SHOWUSAGE;

    /* we obtain [exten@]context? if yes, split them ... */
    if (argc == 3)
    {
        char *splitter = cw_strdupa(argv[2]);
        /* is there a '@' character? */
        if (strchr(argv[2], '@'))
        {
            /* yes, split into exten & context ... */
            exten   = strsep(&splitter, "@");
            context = splitter;

            /* check for length and change to NULL if cw_strlen_zero() */
            if (cw_strlen_zero(exten))
                exten = NULL;
            if (cw_strlen_zero(context))
                context = NULL;
            show_dialplan_helper(fd, context, exten, &counters, NULL, 0, incstack);
        }
        else
        {
            /* no '@' char, only context given */
            context = argv[2];
            if (cw_strlen_zero(context))
                context = NULL;
            show_dialplan_helper(fd, context, exten, &counters, NULL, 0, incstack);
        }
    }
    else
    {
        /* Show complete dial plan */
        show_dialplan_helper(fd, NULL, NULL, &counters, NULL, 0, incstack);
    }

    /* check for input failure and throw some error messages */
    if (context  &&  !counters.context_existence)
    {
        cw_cli(fd, "No such context '%s'\n", context);
        return RESULT_FAILURE;
    }

    if (exten  &&  !counters.extension_existence)
    {
        if (context)
            cw_cli(fd,
                     "No such extension %s in context %s\n",
                     exten,
                     context);
        else
            cw_cli(fd,
                     "No such extension '%s' extension in any context\n",
                     exten);
        return RESULT_FAILURE;
    }

    cw_cli(fd,"-= %d %s (%d %s) in %d %s. =-\n",
                counters.total_exten, counters.total_exten == 1 ? "extension" : "extensions",
                counters.total_prio, counters.total_prio == 1 ? "priority" : "priorities",
                counters.total_context, counters.total_context == 1 ? "context" : "contexts");

    /* everything ok */
    return RESULT_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct cw_clicmd pbx_cli[] = {
	{
		.cmda = { "show", "dialplan", NULL },
		.handler = handle_show_dialplan,
		.generator = complete_show_dialplan_context,
		.summary = "Show dialplan",
		.usage = show_dialplan_help,
	},
	{
		.cmda = { "show", "switches", NULL },
		.handler = handle_show_switches,
		.summary = "Show alternative switches",
		.usage = show_switches_help,
	},
	{
		.cmda = { "show", "hints", NULL },
		.handler = handle_show_hints,
		.summary = "Show dialplan hints",
		.usage = show_hints_help,
	},
	{
		.cmda = { "show", "globals", NULL },
		.handler = handle_show_globals,
		.summary = "Show global dialplan variables",
		.usage = show_globals_help,
	},
	{
		.cmda = { "set", "global", NULL },
		.handler = handle_set_global,
		.summary = "Set global dialplan variable",
		.usage = set_global_help,
	},
};


struct cw_context *cw_context_create(struct cw_context **extcontexts, const char *name, const char *registrar)
{
    struct cw_context *tmp, **local_contexts;
    unsigned int hash = cw_hash_string(name);
    int length;
    
    length = sizeof(struct cw_context);
    length += strlen(name) + 1;
    if (!extcontexts)
    {
        local_contexts = &contexts;
        cw_mutex_lock(&conlock);
    }
    else
    {
        local_contexts = extcontexts;
    }
    tmp = *local_contexts;
    while (tmp)
    {
        if (hash == tmp->hash && !strcmp(name, tmp->name))
        {
            cw_mutex_unlock(&conlock);
            cw_log(CW_LOG_WARNING, "Failed to register context '%s' because it is already in use\n", name);
            if (!extcontexts)
                cw_mutex_unlock(&conlock);
            return NULL;
        }
        tmp = tmp->next;
    }
    if ((tmp = malloc(length)))
    {
        memset(tmp, 0, length);
        cw_mutex_init(&tmp->lock);
        tmp->hash = hash;
        strcpy(tmp->name, name);
        tmp->root = NULL;
        tmp->registrar = registrar;
        tmp->next = *local_contexts;
        tmp->includes = NULL;
        tmp->ignorepats = NULL;
        *local_contexts = tmp;
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Registered context '%s' (%#x)\n", tmp->name, tmp->hash);
        else if (option_verbose > 2)
            cw_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s' (%#x)\n", tmp->name, tmp->hash);
    }
    else
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
    }
    
    if (!extcontexts)
        cw_mutex_unlock(&conlock);
    return tmp;
}

void __cw_context_destroy(struct cw_context *con, const char *registrar);

struct store_hint
{
    char *context;
    char *exten;
    struct cw_state_cb *callbacks;
    int laststate;
    CW_LIST_ENTRY(store_hint) list;
    char data[1];
};

CW_LIST_HEAD(store_hints, store_hint);

void cw_merge_contexts_and_delete(struct cw_context **extcontexts, const char *registrar)
{
    struct cw_context *tmp, *lasttmp = NULL;
    struct store_hints store;
    struct store_hint *this;
    struct cw_hint *hint;
    struct cw_exten *exten;
    int length;
    struct cw_state_cb *thiscb, *prevcb;

    /* preserve all watchers for hints associated with this registrar */
    CW_LIST_HEAD_INIT(&store);
    cw_mutex_lock(&hintlock);
    for (hint = hints;  hint;  hint = hint->next)
    {
        if (hint->callbacks  &&  !strcmp(registrar, hint->exten->parent->registrar))
        {
            length = strlen(hint->exten->exten) + strlen(hint->exten->parent->name) + 2 + sizeof(*this);
            if ((this = calloc(1, length)) == NULL)
            {
                cw_log(CW_LOG_WARNING, "Could not allocate memory to preserve hint\n");
                continue;
            }
            this->callbacks = hint->callbacks;
            hint->callbacks = NULL;
            this->laststate = hint->laststate;
            this->context = this->data;
            strcpy(this->data, hint->exten->parent->name);
            this->exten = this->data + strlen(this->context) + 1;
            strcpy(this->exten, hint->exten->exten);
            CW_LIST_INSERT_HEAD(&store, this, list);
        }
    }
    cw_mutex_unlock(&hintlock);

    tmp = *extcontexts;
    cw_mutex_lock(&conlock);
    if (registrar)
    {
        __cw_context_destroy(NULL,registrar);
        while (tmp)
        {
            lasttmp = tmp;
            tmp = tmp->next;
        }
    }
    else
    {
        while (tmp)
        {
            __cw_context_destroy(tmp,tmp->registrar);
            lasttmp = tmp;
            tmp = tmp->next;
        }
    }
    if (lasttmp)
    {
        lasttmp->next = contexts;
        contexts = *extcontexts;
        *extcontexts = NULL;
    }
    else 
    {
        cw_log(CW_LOG_WARNING, "Requested contexts could not be merged\n");
    }
    cw_mutex_unlock(&conlock);

    /* restore the watchers for hints that can be found; notify those that
       cannot be restored
    */
    while ((this = CW_LIST_REMOVE_HEAD(&store, list)))
    {
        exten = cw_hint_extension(NULL, this->context, this->exten);
        /* Find the hint in the list of hints */
        cw_mutex_lock(&hintlock);
        for (hint = hints;  hint;  hint = hint->next)
        {
            if (hint->exten == exten)
                break;
        }
        if (!exten  ||  !hint)
        {
            /* this hint has been removed, notify the watchers */
            prevcb = NULL;
            thiscb = this->callbacks;
            while (thiscb)
            {
                prevcb = thiscb;        
                thiscb = thiscb->next;
                prevcb->callback(this->context, this->exten, CW_EXTENSION_REMOVED, prevcb->data);
                free(prevcb);
            }
        }
        else
        {
            thiscb = this->callbacks;
            while (thiscb->next)
                thiscb = thiscb->next;
            thiscb->next = hint->callbacks;
            hint->callbacks = this->callbacks;
            hint->laststate = this->laststate;
        }
        cw_mutex_unlock(&hintlock);
        free(this);
    }
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int cw_context_add_include(const char *context, const char *include, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
    {
        errno = EBUSY;
        return -1;
    }

    errno = ENOENT;
    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        /* ... search for the right one ... */
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_add_include2(c, include, registrar);
	    break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

#define FIND_NEXT \
do { \
    c = info; \
    while(*c && (*c != ',')) c++; \
    if (*c) { *c = '\0'; c++; } else c = NULL; \
} while(0)

static void get_timerange(struct cw_timing *i, char *times)
{
    char *e;
    int x;
    int s1, s2;
    int e1, e2;
    /*    int cth, ctm; */

    /* start disabling all times, fill the fields with 0's, as they may contain garbage */
    memset(i->minmask, 0, sizeof(i->minmask));
    
    /* Star is all times */
    if (cw_strlen_zero(times)  ||  !strcmp(times, "*"))
    {
        for (x=0; x<24; x++)
            i->minmask[x] = (1 << 30) - 1;
        return;
    }
    /* Otherwise expect a range */
    e = strchr(times, '-');
    if (!e)
    {
        cw_log(CW_LOG_WARNING, "Time range is not valid. Assuming no restrictions based on time.\n");
        return;
    }
    *e = '\0';
    e++;
    while (*e  &&  !isdigit(*e)) 
        e++;
    if (!*e)
    {
        cw_log(CW_LOG_WARNING, "Invalid time range.  Assuming no restrictions based on time.\n");
        return;
    }
    if (sscanf(times, "%d:%d", &s1, &s2) != 2)
    {
        cw_log(CW_LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", times);
        return;
    }
    if (sscanf(e, "%d:%d", &e1, &e2) != 2)
    {
        cw_log(CW_LOG_WARNING, "%s isn't a time.  Assuming no restrictions based on time.\n", e);
        return;
    }

#if 1
    s1 = s1 * 30 + s2/2;
    if ((s1 < 0) || (s1 >= 24*30))
    {
        cw_log(CW_LOG_WARNING, "%s isn't a valid start time. Assuming no time.\n", times);
        return;
    }
    e1 = e1 * 30 + e2/2;
    if ((e1 < 0)  ||  (e1 >= 24*30))
    {
        cw_log(CW_LOG_WARNING, "%s isn't a valid end time. Assuming no time.\n", e);
        return;
    }
    /* Go through the time and enable each appropriate bit */
    for (x = s1;  x != e1;  x = (x + 1) % (24 * 30))
    {
        i->minmask[x/30] |= (1 << (x % 30));
    }
    /* Do the last one */
    i->minmask[x/30] |= (1 << (x % 30));
#else
    for (cth = 0;  cth < 24;  cth++)
    {
        /* Initialize masks to blank */
        i->minmask[cth] = 0;
        for (ctm = 0;  ctm < 30;  ctm++)
        {
            if (
            /* First hour with more than one hour */
                  (((cth == s1) && (ctm >= s2)) &&
                   ((cth < e1)))
            /* Only one hour */
            ||    (((cth == s1) && (ctm >= s2)) &&
                   ((cth == e1) && (ctm <= e2)))
            /* In between first and last hours (more than 2 hours) */
            ||    ((cth > s1) &&
                   (cth < e1))
            /* Last hour with more than one hour */
            ||    ((cth > s1) &&
                   ((cth == e1) && (ctm <= e2)))
            )
                i->minmask[cth] |= (1 << (ctm / 2));
        }
    }
#endif
    /* All done */
    return;
}

static char *days[] =
{
    "sun",
    "mon",
    "tue",
    "wed",
    "thu",
    "fri",
    "sat",
};

/*! \brief  get_dow: Get day of week */
static unsigned int get_dow(char *dow)
{
    char *c;
    /* The following line is coincidence, really! */
    int s, e, x;
    unsigned int mask;

    /* Check for all days */
    if (cw_strlen_zero(dow)  ||  !strcmp(dow, "*"))
        return (1 << 7) - 1;
    /* Get start and ending days */
    c = strchr(dow, '-');
    if (c)
    {
        *c = '\0';
        c++;
    }
    else
    {
        c = NULL;
    }
    /* Find the start */
    s = 0;
    while ((s < 7)  &&  strcasecmp(dow, days[s]))
        s++;
    if (s >= 7)
    {
        cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", dow);
        return 0;
    }
    if (c)
    {
        e = 0;
        while ((e < 7)  &&  strcasecmp(c, days[e]))
            e++;
        if (e >= 7)
        {
            cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", c);
            return 0;
        }
    }
    else
    {
        e = s;
    }
    mask = 0;
    for (x = s;  x != e;  x = (x + 1)%7)
        mask |= (1 << x);
    /* One last one */
    mask |= (1 << x);
    return mask;
}

static unsigned int get_day(char *day)
{
    char *c;
    /* The following line is coincidence, really! */
    int s, e, x;
    unsigned int mask;

    /* Check for all days */
    if (cw_strlen_zero(day)  ||  !strcmp(day, "*"))
    {
        mask = (1 << 30)  + ((1 << 30) - 1);
        return mask;
    }
    /* Get start and ending days */
    c = strchr(day, '-');
    if (c)
    {
        *c = '\0';
        c++;
    }
    /* Find the start */
    if (sscanf(day, "%d", &s) != 1)
    {
        cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", day);
        return 0;
    }
    if ((s < 1)  ||  (s > 31))
    {
        cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", day);
        return 0;
    }
    s--;
    if (c)
    {
        if (sscanf(c, "%d", &e) != 1)
        {
            cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", c);
            return 0;
        }
        if ((e < 1) || (e > 31))
        {
            cw_log(CW_LOG_WARNING, "Invalid day '%s', assuming none\n", c);
            return 0;
        }
        e--;
    }
    else
    {
        e = s;
    }
    mask = 0;
    for (x = s;  x != e;  x = (x + 1)%31)
        mask |= (1 << x);
    mask |= (1 << x);
    return mask;
}

static char *months[] =
{
    "jan",
    "feb",
    "mar",
    "apr",
    "may",
    "jun",
    "jul",
    "aug",
    "sep",
    "oct",
    "nov",
    "dec",
};

static unsigned int get_month(char *mon)
{
    char *c;
    /* The following line is coincidence, really! */
    int s, e, x;
    unsigned int mask;

    /* Check for all days */
    if (cw_strlen_zero(mon) || !strcmp(mon, "*")) 
        return (1 << 12) - 1;
    /* Get start and ending days */
    c = strchr(mon, '-');
    if (c)
    {
        *c = '\0';
        c++;
    }
    /* Find the start */
    s = 0;
    while((s < 12) && strcasecmp(mon, months[s]))
        s++;
    if (s >= 12)
    {
        cw_log(CW_LOG_WARNING, "Invalid month '%s', assuming none\n", mon);
        return 0;
    }
    if (c)
    {
        e = 0;
        while ((e < 12)  &&  strcasecmp(mon, months[e]))
            e++;
        if (e >= 12)
        {
            cw_log(CW_LOG_WARNING, "Invalid month '%s', assuming none\n", c);
            return 0;
        }
    }
    else
    {
        e = s;
    }
    mask = 0;
    for (x = s;  x != e;  x = (x + 1)%12)
    {
        mask |= (1 << x);
    }
    /* One last one */
    mask |= (1 << x);
    return mask;
}

int cw_build_timing(struct cw_timing *i, char *info_in)
{
    char info_save[256];
    char *info;
    char *c;

    /* Check for empty just in case */
    if (cw_strlen_zero(info_in))
        return 0;
    /* make a copy just in case we were passed a static string */
    cw_copy_string(info_save, info_in, sizeof(info_save));
    info = info_save;
    /* Assume everything except time */
    i->monthmask = (1 << 12) - 1;
    i->daymask = (1 << 30) - 1 + (1 << 30);
    i->dowmask = (1 << 7) - 1;
    /* Avoid using str tok */
    FIND_NEXT;
    /* Info has the time range, start with that */
    get_timerange(i, info);
    info = c;
    if (!info)
        return 1;
    FIND_NEXT;
    /* Now check for day of week */
    i->dowmask = get_dow(info);

    info = c;
    if (!info)
        return 1;
    FIND_NEXT;
    /* Now check for the day of the month */
    i->daymask = get_day(info);
    info = c;
    if (!info)
        return 1;
    FIND_NEXT;
    /* And finally go for the month */
    i->monthmask = get_month(info);

    return 1;
}

int cw_check_timing(struct cw_timing *i)
{
    struct tm tm;
    time_t t;

    time(&t);
    localtime_r(&t,&tm);

    /* If it's not the right month, return */
    if (!(i->monthmask & (1 << tm.tm_mon)))
    {
        return 0;
    }

    /* If it's not that time of the month.... */
    /* Warning, tm_mday has range 1..31! */
    if (!(i->daymask & (1 << (tm.tm_mday-1))))
        return 0;

    /* If it's not the right day of the week */
    if (!(i->dowmask & (1 << tm.tm_wday)))
        return 0;

    /* Sanity check the hour just to be safe */
    if ((tm.tm_hour < 0)  ||  (tm.tm_hour > 23))
    {
        cw_log(CW_LOG_WARNING, "Insane time...\n");
        return 0;
    }

    /* Now the tough part, we calculate if it fits
       in the right time based on min/hour */
    if (!(i->minmask[tm.tm_hour] & (1 << (tm.tm_min / 2))))
        return 0;

    /* If we got this far, then we're good */
    return 1;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int cw_context_add_include2(struct cw_context *con,
                              const char *value,
                              const char *registrar)
{
    struct cw_include *new_include;
    char *c;
    struct cw_include *i, *il = NULL; /* include, include_last */
    int length;
    char *p;
    
    length = sizeof(struct cw_include);
    length += 2 * (strlen(value) + 1);

    /* allocate new include structure ... */
    if (!(new_include = malloc(length)))
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        errno = ENOMEM;
        return -1;
    }
    
    /* ... fill in this structure ... */
    memset(new_include, 0, length);
    p = new_include->stuff;
    new_include->name = p;
    strcpy(new_include->name, value);
    p += strlen(value) + 1;
    new_include->rname = p;
    strcpy(new_include->rname, value);
    c = new_include->rname;
    /* Strip off timing info */
    while (*c  &&  (*c != ',')) 
        c++; 
    /* Process if it's there */
    if (*c)
    {
            new_include->hastime = cw_build_timing(&(new_include->timing), c+1);
        *c = '\0';
    }
    new_include->next      = NULL;
    new_include->registrar = registrar;

    /* ... try to lock this context ... */
    if (cw_mutex_lock(&con->lock))
    {
        free(new_include);
        errno = EBUSY;
        return -1;
    }

    /* ... go to last include and check if context is already included too... */
    i = con->includes;
    while (i)
    {
        if (!strcasecmp(i->name, new_include->name))
        {
            free(new_include);
            cw_mutex_unlock(&con->lock);
            errno = EEXIST;
            return -1;
        }
        il = i;
        i = i->next;
    }

    /* ... include new context into context list, unlock, return */
    if (il)
        il->next = new_include;
    else
        con->includes = new_include;
    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "Including context '%s' in context '%s'\n", new_include->name, cw_get_context_name(con)); 
    cw_mutex_unlock(&con->lock);

    return 0;
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int cw_context_add_switch(const char *context, const char *sw, const char *data, int eval, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;
    
    if (cw_lock_contexts())
    {
        errno = EBUSY;
        return -1;
    }

    errno = ENOENT;
    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        /* ... search for the right one ... */
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_add_switch2(c, sw, data, eval, registrar);
            break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int cw_context_add_switch2(struct cw_context *con, const char *value,
    const char *data, int eval, const char *registrar)
{
    struct cw_sw *new_sw;
    struct cw_sw *i, *il = NULL; /* sw, sw_last */
    int length;
    char *p;
    
    length = sizeof(struct cw_sw);
    length += strlen(value) + 1;
    if (data)
        length += strlen(data);
    length++;
    if (eval)
    {
        /* Create buffer for evaluation of variables */
        length += SWITCH_DATA_LENGTH;
        length++;
    }

    /* allocate new sw structure ... */
    if (!(new_sw = malloc(length)))
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        errno = ENOMEM;
        return -1;
    }
    
    /* ... fill in this structure ... */
    memset(new_sw, 0, length);
    p = new_sw->stuff;
    new_sw->name = p;
    strcpy(new_sw->name, value);
    p += strlen(value) + 1;
    new_sw->data = p;
    if (data)
    {
        strcpy(new_sw->data, data);
        p += strlen(data) + 1;
    }
    else
    {
        strcpy(new_sw->data, "");
        p++;
    }
    if (eval) 
        new_sw->tmpdata = p;
    new_sw->next      = NULL;
    new_sw->eval      = eval;
    new_sw->registrar = registrar;

    /* ... try to lock this context ... */
    if (cw_mutex_lock(&con->lock))
    {
        free(new_sw);
        errno = EBUSY;
        return -1;
    }

    /* ... go to last sw and check if context is already swd too... */
    i = con->alts;
    while (i)
    {
        if (!strcasecmp(i->name, new_sw->name) && !strcasecmp(i->data, new_sw->data))
        {
            free(new_sw);
            cw_mutex_unlock(&con->lock);
            errno = EEXIST;
            return -1;
        }
        il = i;
        i = i->next;
    }

    /* ... sw new context into context list, unlock, return */
    if (il)
        il->next = new_sw;
    else
        con->alts = new_sw;
    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "Including switch '%s/%s' in context '%s'\n", new_sw->name, new_sw->data, cw_get_context_name(con)); 
    cw_mutex_unlock(&con->lock);

    return 0;
}

/*
 * EBUSY  - can't lock
 * ENOENT - there is not context existence
 */
int cw_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
    {
        errno = EBUSY;
        return -1;
    }

    errno = ENOENT;
    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_context_remove_ignorepat2(c, ignorepat, registrar);
	    break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

int cw_context_remove_ignorepat2(struct cw_context *con, const char *ignorepat, const char *registrar)
{
    struct cw_ignorepat *ip, *ipl = NULL;

    if (cw_mutex_lock(&con->lock))
    {
        errno = EBUSY;
        return -1;
    }

    ip = con->ignorepats;
    while (ip)
    {
        if (!strcmp(ip->pattern, ignorepat)
            &&
            (!registrar || (registrar == ip->registrar)))
        {
            if (ipl)
            {
                ipl->next = ip->next;
                free(ip);
            }
            else
            {
                con->ignorepats = ip->next;
                free(ip);
            }
            cw_mutex_unlock(&con->lock);
            return 0;
        }
        ipl = ip;
        ip = ip->next;
    }

    cw_mutex_unlock(&con->lock);
    errno = EINVAL;
    return -1;
}

/*
 * EBUSY - can't lock
 * ENOENT - there is no existence of context
 */
int cw_context_add_ignorepat(const char *con, const char *value, const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(con);
    int ret = -1;

    if (cw_lock_contexts())
    {
        errno = EBUSY;
        return -1;
    }

    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        if (hash == c->hash && !strcmp(con, c->name))
        {
            ret = cw_context_add_ignorepat2(c, value, registrar);
	    break;
        } 
    }

    cw_unlock_contexts();
    return ret;
}

int cw_context_add_ignorepat2(struct cw_context *con, const char *value, const char *registrar)
{
    struct cw_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
    int length;

    length = sizeof(struct cw_ignorepat);
    length += strlen(value) + 1;
    if ((ignorepat = malloc(length)) == NULL)
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        errno = ENOMEM;
        return -1;
    }
    memset(ignorepat, 0, length);
    strcpy(ignorepat->pattern, value);
    ignorepat->next = NULL;
    ignorepat->registrar = registrar;
    cw_mutex_lock(&con->lock);
    ignorepatc = con->ignorepats;
    while (ignorepatc)
    {
        ignorepatl = ignorepatc;
        if (!strcasecmp(ignorepatc->pattern, value))
        {
            /* Already there */
            cw_mutex_unlock(&con->lock);
            errno = EEXIST;
            return -1;
        }
        ignorepatc = ignorepatc->next;
    }
    if (ignorepatl) 
        ignorepatl->next = ignorepat;
    else
        con->ignorepats = ignorepat;
    cw_mutex_unlock(&con->lock);
    return 0;
    
}

int cw_ignore_pattern(const char *context, const char *pattern)
{
    struct cw_context *con;
    struct cw_ignorepat *pat;

    con = cw_context_find(context);
    if (con)
    {
        pat = con->ignorepats;
        while (pat)
        {
            switch (cw_extension_pattern_match(pattern, pat->pattern))
            {
            case EXTENSION_MATCH_EXACT:
            case EXTENSION_MATCH_STRETCHABLE:
            case EXTENSION_MATCH_POSSIBLE:
                return 1;
            }
            pat = pat->next;
        }
    } 
    return 0;
}

/*
 * EBUSY   - can't lock
 * ENOENT  - no existence of context
 *
 */
int cw_add_extension(const char *context, int replace, const char *extension, int priority, const char *label, const char *callerid,
    const char *application, void *data, void (*datad)(void *), const char *registrar)
{
    struct cw_context *c;
    unsigned int hash = cw_hash_string(context);
    int ret = -1;

    if (cw_lock_contexts())
    {
        errno = EBUSY;
        return -1;
    }

    errno = ENOENT;
    for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c))
    {
        if (hash == c->hash && !strcmp(context, c->name))
        {
            ret = cw_add_extension2(c, replace, extension, priority, label, callerid,
                application, data, datad, registrar);
	    break;
        }
    }

    cw_unlock_contexts();
    return ret;
}

int cw_explicit_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority)
{
    if (!chan)
        return -1;

    if (!cw_strlen_zero(context))
        cw_copy_string(chan->context, context, sizeof(chan->context));
    if (!cw_strlen_zero(exten))
        cw_copy_string(chan->exten, exten, sizeof(chan->exten));
    if (priority > -1)
    {
        chan->priority = priority;
        /* see flag description in channel.h for explanation */
        if (cw_test_flag(chan, CW_FLAG_IN_AUTOLOOP))
            chan->priority--;
    }
    
    return 0;
}

int cw_async_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority)
{
    int res = 0;

    cw_channel_lock(chan);

    if (chan->pbx)
    {
        /* This channel is currently in the PBX */
        cw_explicit_goto_n(chan, context, exten, priority);
        cw_softhangup_nolock(chan, CW_SOFTHANGUP_ASYNCGOTO);
    }
    else
    {
        /* In order to do it when the channel doesn't really exist within
           the PBX, we have to make a new channel, masquerade, and start the PBX
           at the new location */
        struct cw_channel *tmpchan;
        
        tmpchan = cw_channel_alloc(0, "AsyncGoto/%s", chan->name);
        if (tmpchan)
        {
            cw_setstate(tmpchan, chan->_state);
            /* Make formats okay */
            tmpchan->readformat = chan->readformat;
            tmpchan->writeformat = chan->writeformat;
            /* Setup proper location */
            cw_explicit_goto_n(tmpchan,
                               (!cw_strlen_zero(context)) ? context : chan->context,
                               (!cw_strlen_zero(exten)) ? exten : chan->exten,
                               priority);

            /* Masquerade into temp channel */
            cw_channel_masquerade(tmpchan, chan);
        
            /* Grab the locks and get going */
            cw_channel_lock(tmpchan);
            cw_do_masquerade(tmpchan);
            cw_channel_unlock(tmpchan);
            /* Start the PBX going on our stolen channel */
            if (cw_pbx_start(tmpchan))
            {
                cw_log(CW_LOG_WARNING, "Unable to start PBX on %s\n", tmpchan->name);
                cw_hangup(tmpchan);
                res = -1;
            }
        }
        else
        {
            res = -1;
        }
    }
    cw_channel_unlock(chan);
    return res;
}

int cw_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority, int async, int ifexists)
{
    if (ifexists && cw_exists_extension(chan, context, exten, priority, chan->cid.cid_num) <= 0)
        return -1;

    if (async)
    	return cw_async_goto_n(chan, context, exten, priority);
    else
    	return cw_explicit_goto_n(chan, context, exten, priority);
}

int cw_goto(struct cw_channel *chan, const char *context, const char *exten, const char *priority, int async, int ifexists)
{
    int npriority;
    
    if (!chan || !priority || !*priority)
        return -1;

    if (!context || !*context)
	    context = chan->context;

    if (exten && !strcmp(exten, "BYEXTENSION")) {
        cw_log(CW_LOG_WARNING, "Use of BYEXTENSTION in Goto is deprecated. Use ${EXTEN} instead\n");
        exten = chan->exten;
    } else if (!exten || !*exten)
        exten = chan->exten;

    if (isdigit(*priority) || ((*priority == '+' || *priority == '-') && isdigit(priority[1]))) {
        switch (*priority) {
	    case '-':
		    npriority = chan->priority - atoi(priority+1);
		    break;
	    case '+':
		    npriority = chan->priority + atoi(priority+1);
		    break;
	    default:
		    npriority = atoi(priority);
		    break;
        }
    } else {
        if ((npriority = cw_findlabel_extension(chan, context, exten, priority, chan->cid.cid_num)) < 1) {
            if (!ifexists)
                cw_log(CW_LOG_WARNING, "Priority '%s' must be [+-]number, or a valid label\n", priority);
            return -1;
        }
        ifexists = 0;
    }

    return cw_goto_n(chan, context, exten, npriority, async, ifexists);
}

int cw_async_goto_by_name(const char *channame, const char *context, const char *exten, const char *priority)
{
    struct cw_channel *chan;
    int res = -1;

    chan = cw_get_channel_by_name_locked(channame);
    if (chan)
    {
        res = cw_goto(chan, context, exten, priority, 1, 1);
        cw_channel_unlock(chan);
        cw_object_put(chan);
    }
    return res;
}

static int ext_strncpy(char *dst, const char *src, int len)
{
    int count=0;

    while (*src  &&  (count < len - 1))
    {
        switch (*src)
        {
        case ' ':
            /*    otherwise exten => [a-b],1,... doesn't work */
            /*        case '-': */
            /* Ignore */
            break;
        default:
            *dst = *src;
            dst++;
        }
        src++;
        count++;
    }
    *dst = '\0';
    return count;
}

static void null_datad(void *foo)
{
}

/*
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
int cw_add_extension2(struct cw_context *con,
                        int replace, const char *extension, int priority, const char *label, const char *callerid,
                        const char *application, void *data, void (*datad)(void *),
                        const char *registrar)
{

#define LOG do {     if (option_debug) {\
        if (tmp->matchcid) { \
            cw_log(CW_LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
        } else { \
            cw_log(CW_LOG_DEBUG, "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
        } \
    } else if (option_verbose > 2) { \
        if (tmp->matchcid) { \
            cw_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d (CID match '%s')to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
        } else {  \
            cw_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
        } \
    } } while(0)

    /*
     * This is a fairly complex routine.  Different extensions are kept
     * in order by the extension number.  Then, extensions of different
     * priorities (same extension) are kept in a list, according to the
     * peer pointer.
     */
    struct cw_exten *tmp, *e, *el = NULL, *ep = NULL;
    int res;
    int length;
    char *p;
    unsigned int hash = cw_hash_string(extension);

    length = sizeof(struct cw_exten);
    length += strlen(extension) + 1;
    length += strlen(application) + 1;
    if (label)
        length += strlen(label) + 1;
    if (callerid)
        length += strlen(callerid) + 1;
    else
        length ++;

    /* Be optimistic:  Build the extension structure first */
    if (datad == NULL)
        datad = null_datad;
    if ((tmp = malloc(length)))
    {
        memset(tmp, 0, length);
        tmp->hash = hash;
        p = tmp->stuff;
        if (label)
        {
            tmp->label = p;
            strcpy(tmp->label, label);
            p += strlen(label) + 1;
        }
        tmp->exten = p;
        p += ext_strncpy(tmp->exten, extension, strlen(extension) + 1) + 1;
        tmp->priority = priority;
        tmp->cidmatch = p;
        if (callerid)
        {
            p += ext_strncpy(tmp->cidmatch, callerid, strlen(callerid) + 1) + 1;
            tmp->matchcid = 1;
        }
        else
        {
            tmp->cidmatch[0] = '\0';
            tmp->matchcid = 0;
            p++;
        }
        tmp->app = p;
        strcpy(tmp->app, application);
	tmp->apphash = cw_hash_string(application);
        tmp->parent = con;
        tmp->data = data;
        tmp->datad = datad;
        tmp->registrar = registrar;
        tmp->peer = NULL;
        tmp->next =  NULL;
    }
    else
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        errno = ENOMEM;
        return -1;
    }
    if (cw_mutex_lock(&con->lock))
    {
        free(tmp);
        /* And properly destroy the data */
        datad(data);
        cw_log(CW_LOG_WARNING, "Failed to lock context '%s' (%#x)\n", con->name, con->hash);
        errno = EBUSY;
        return -1;
    }
    e = con->root;
    while (e)
    {
        /* Make sure patterns are always last! */
        if ((e->exten[0] != '_') && (extension[0] == '_'))
            res = -1;
        else if ((e->exten[0] == '_') && (extension[0] != '_'))
            res = 1;
        else
            res= strcmp(e->exten, extension);
        if (!res)
        {
            if (!e->matchcid  &&  !tmp->matchcid)
                res = 0;
            else if (tmp->matchcid  &&  !e->matchcid)
                res = 1;
            else if (e->matchcid  &&  !tmp->matchcid)
                res = -1;
            else
                res = strcasecmp(e->cidmatch, tmp->cidmatch);
        }
        if (res == 0)
        {
            /* We have an exact match, now we find where we are
               and be sure there's no duplicates */
            while (e)
            {
                if (e->priority == tmp->priority)
                {
                    /* Can't have something exactly the same.  Is this a
                       replacement?  If so, replace, otherwise, bonk. */
                    if (replace)
                    {
                        if (ep)
                        {
                            /* We're in the peer list, insert ourselves */
                            ep->peer = tmp;
                            tmp->peer = e->peer;
                        }
                        else if (el)
                        {
                            /* We're the first extension. Take over e's functions */
                            el->next = tmp;
                            tmp->next = e->next;
                            tmp->peer = e->peer;
                        }
                        else
                        {
                            /* We're the very first extension.  */
                            con->root = tmp;
                            tmp->next = e->next;
                            tmp->peer = e->peer;
                        }
                        if (tmp->priority == PRIORITY_HINT)
                            cw_change_hint(e,tmp);
                        /* Destroy the old one */
                        e->datad(e->data);
                        free(e);
                        cw_mutex_unlock(&con->lock);
                        if (tmp->priority == PRIORITY_HINT)
                            cw_change_hint(e, tmp);
                        /* And immediately return success. */
                        LOG;
                        return 0;
                    }
                    else
                    {
                        cw_log(CW_LOG_WARNING, "Unable to register extension '%s', priority %d in '%s' (%#x), already in use\n",
                                 tmp->exten, tmp->priority, con->name, con->hash);
                        tmp->datad(tmp->data);
                        free(tmp);
                        cw_mutex_unlock(&con->lock);
                        errno = EEXIST;
                        return -1;
                    }
                }
                else if (e->priority > tmp->priority)
                {
                    /* Slip ourselves in just before e */
                    if (ep)
                    {
                        /* Easy enough, we're just in the peer list */
                        ep->peer = tmp;
                        tmp->peer = e;
                    }
                    else if (el)
                    {
                        /* We're the first extension in this peer list */
                        el->next = tmp;
                        tmp->next = e->next;
                        e->next = NULL;
                        tmp->peer = e;
                    }
                    else
                    {
                        /* We're the very first extension altogether */
                        tmp->next = con->root->next;
                        /* Con->root must always exist or we couldn't get here */
                        tmp->peer = con->root;
                        con->root = tmp;
                    }
                    cw_mutex_unlock(&con->lock);

                    /* And immediately return success. */
                    if (tmp->priority == PRIORITY_HINT)
                         cw_add_hint(tmp);
                    
                    LOG;
                    return 0;
                }
                ep = e;
                e = e->peer;
            }
            /* If we make it here, then it's time for us to go at the very end.
               ep *must* be defined or we couldn't have gotten here. */
            ep->peer = tmp;
            cw_mutex_unlock(&con->lock);
            if (tmp->priority == PRIORITY_HINT)
                cw_add_hint(tmp);
            
            /* And immediately return success. */
            LOG;
            return 0;
        }
        else if (res > 0)
        {
            /* Insert ourselves just before 'e'.  We're the first extension of
               this kind */
            tmp->next = e;
            if (el)
            {
                /* We're in the list somewhere */
                el->next = tmp;
            }
            else
            {
                /* We're at the top of the list */
                con->root = tmp;
            }
            cw_mutex_unlock(&con->lock);
            if (tmp->priority == PRIORITY_HINT)
                cw_add_hint(tmp);

            /* And immediately return success. */
            LOG;
            return 0;
        }            
            
        el = e;
        e = e->next;
    }
    /* If we fall all the way through to here, then we need to be on the end. */
    if (el)
        el->next = tmp;
    else
        con->root = tmp;
    cw_mutex_unlock(&con->lock);
    if (tmp->priority == PRIORITY_HINT)
        cw_add_hint(tmp);
    LOG;
    return 0;    
}

struct async_stat
{
    pthread_t p;
    struct cw_channel *chan;
    char context[CW_MAX_CONTEXT];
    char exten[CW_MAX_EXTENSION];
    int priority;
    int timeout;
    char app[CW_MAX_EXTENSION];
    char appdata[1024];
};

static void *async_wait(void *data) 
{
    struct async_stat *as = data;
    struct cw_channel *chan = as->chan;
    int timeout = as->timeout;
    int res;
    struct cw_frame *f;

    while (timeout  &&  (chan->_state != CW_STATE_UP))
    {
        res = cw_waitfor(chan, timeout);
        if (res < 1) 
            break;
        if (timeout > -1)
            timeout = res;
        f = cw_read(chan);
        if (!f)
            break;
        if (f->frametype == CW_FRAME_CONTROL)
        {
            if ((f->subclass == CW_CONTROL_BUSY)
                ||
                (f->subclass == CW_CONTROL_CONGESTION))
            {
                break;
            }
        }
        cw_fr_free(f);
    }
    if (chan->_state == CW_STATE_UP)
    {
        if (!cw_strlen_zero(as->app))
        {
	    cw_function_exec_str(chan, cw_hash_string(as->app), as->app, as->appdata, NULL, 0);
        }
        else
        {
            if (!cw_strlen_zero(as->context))
                cw_copy_string(chan->context, as->context, sizeof(chan->context));
            if (!cw_strlen_zero(as->exten))
                cw_copy_string(chan->exten, as->exten, sizeof(chan->exten));
            if (as->priority > 0)
                chan->priority = as->priority;
            /* Run the PBX */
            if (cw_pbx_run(chan))
            {
                cw_log(CW_LOG_ERROR, "Failed to start PBX on %s\n", chan->name);
            }
            else
            {
                /* PBX will have taken care of this */
                chan = NULL;
            }
        }
            
    }
    if (chan)
        cw_hangup(chan);

    cw_object_put(as->chan);
    free(as);
    return NULL;
}

/*! Function to update the cdr after a spool call fails.
 *
 *  This function updates the cdr for a failed spool call
 *  and takes the channel of the failed call as an argument.
 *
 * \param chan the channel for the failed call.
 */
int cw_pbx_outgoing_cdr_failed(void)
{
    /* allocate a channel */
    struct cw_channel *chan = cw_channel_alloc(0, NULL);

    if (!chan)
    {
        /* allocation of the channel failed, let some peeps know */
        cw_log(CW_LOG_WARNING, "Unable to allocate channel structure for CDR record\n");
        return -1;  /* failure */
    }

    chan->cdr = cw_cdr_alloc();   /* allocate a cdr for the channel */

    if (!chan->cdr)
    {
        /* allocation of the cdr failed */
        cw_log(CW_LOG_WARNING, "Unable to create Call Detail Record\n");
        cw_channel_free(chan);   /* free the channel */
        return -1;                /* return failure */
    }
    
    /* allocation of the cdr was successful */
    cw_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
    cw_cdr_start(chan->cdr);       /* record the start and stop time */
    cw_cdr_end(chan->cdr);
    cw_cdr_failed(chan->cdr);      /* set the status to failed */
    cw_cdr_detach(chan->cdr);      /* post and free the record */
    cw_channel_free(chan);         /* free the channel */
    
    return 0;  /* success */
}

int cw_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, struct cw_registry *vars, struct cw_channel **channel)
{
    struct cw_channel *chan;
    struct async_stat *as;
    int res = -1, cdr_res = -1;
    struct outgoing_helper oh;

    if (sync)
    {
        oh.context = context;
        oh.exten = exten;
        oh.priority = priority;
        oh.vars = vars;
        chan = __cw_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
        if (channel)
        {
            *channel = chan;
            if (chan)
                cw_channel_lock(chan);
        }
        if (chan)
        {
            if (chan->cdr)
            {
                /* check if the channel already has a cdr record, if not give it one */
                cw_log(CW_LOG_WARNING, "%s already has a call record??\n", chan->name);
            }
            else
            {
                chan->cdr = cw_cdr_alloc();   /* allocate a cdr for the channel */
                if (!chan->cdr)
                {
                    /* allocation of the cdr failed */
                    cw_log(CW_LOG_WARNING, "Unable to create Call Detail Record\n");
                    free(chan->pbx);
                    return -1;
                }
                /* allocation of the cdr was successful */
                cw_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
                cw_cdr_start(chan->cdr);
            }
            if (chan->_state == CW_STATE_UP)
            {
                res = 0;
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);

                if (sync > 1)
                {
                    if (channel)
                        cw_channel_unlock(chan);
                    if (cw_pbx_run(chan))
                    {
                        cw_log(CW_LOG_ERROR, "Unable to run PBX on %s\n", chan->name);
                        if (channel)
                            *channel = NULL;
                        cw_hangup(chan);
                        res = -1;
                    }
                }
                else
                {
                    if (cw_pbx_start(chan))
                    {
                        cw_log(CW_LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
                        if (channel)
                            *channel = NULL;
                        cw_hangup(chan);
                        res = -1;
                    } 
                }
            }
            else
            {
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", chan->name);

                if (chan->cdr)
                {
                    /* update the cdr */
                    /* here we update the status of the call, which sould be busy.
                     * if that fails then we set the status to failed */
                    if (cw_cdr_disposition(chan->cdr, chan->hangupcause))
                        cw_cdr_failed(chan->cdr);
                }
            
                if (channel)
                    *channel = NULL;
                cw_hangup(chan);
            }
        }

        if (res < 0)
        {
            /* the call failed for some reason */
            if (*reason == 0)
            {
                /* if the call failed (not busy or no answer)
                 * update the cdr with the failed message */
                cdr_res = cw_pbx_outgoing_cdr_failed();
                if (cdr_res != 0)
                    return cdr_res;
            }
            
            /* create a fake channel and execute the "failed" extension (if it exists) within the requested context */
            /* check if "failed" exists */
            if (cw_exists_extension(chan, context, "failed", 1, NULL))
            {
                chan = cw_channel_alloc(0, "OutgoingSpoolFailed");
                if (chan)
                {
                    if (!cw_strlen_zero(context))
                        cw_copy_string(chan->context, context, sizeof(chan->context));
                    cw_copy_string(chan->exten, "failed", sizeof(chan->exten));
                    chan->priority = 1;
                    cw_var_copy(&chan->vars, vars);
                    cw_pbx_run(chan);    
                }
                else
                {
                    cw_log(CW_LOG_WARNING, "Can't allocate the channel structure, skipping execution of extension 'failed'\n");
                }
            }
        }
    }
    else
    {
        if (!(as = calloc(1, sizeof(struct async_stat))))
        {
            cw_log(CW_LOG_ERROR, "Out of memory!\n");
            return -1;
        }
        chan = cw_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
        if (channel)
        {
            *channel = chan;
            if (chan)
                cw_channel_lock(chan);
        }
        if (!chan)
        {
            free(as);
            return -1;
        }
        as->chan = cw_object_dup(chan);
        cw_copy_string(as->context, context, sizeof(as->context));
        cw_copy_string(as->exten,  exten, sizeof(as->exten));
        as->priority = priority;
        as->timeout = timeout;
        cw_var_copy(&chan->vars, vars);
        if (cw_pthread_create(&as->p, &global_attr_detached, async_wait, as))
        {
            cw_log(CW_LOG_WARNING, "Failed to start async wait\n");
            if (channel)
                *channel = NULL;
            cw_hangup(chan);
	    cw_object_put(chan);
            free(as);
            return -1;
        }
        res = 0;
    }
    return res;
}

struct app_tmp
{
    char app[256];
    char data[256];
    struct cw_channel *chan;
    pthread_t t;
};

static void *cw_pbx_run_app(void *data)
{
    struct app_tmp *tmp = data;

    cw_function_exec_str(tmp->chan, cw_hash_string(tmp->app), tmp->app, tmp->data, NULL, 0);

    cw_hangup(tmp->chan);
    cw_object_put(tmp->chan);
    free(tmp);
    return NULL;
}

int cw_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, struct cw_registry *vars, struct cw_channel **locked_channel)
{
    struct cw_channel *chan;
    struct async_stat *as;
    struct app_tmp *tmp;
    int res = -1, cdr_res = -1;
    struct outgoing_helper oh;

    memset(&oh, 0, sizeof(oh));
    oh.vars = vars;

    if (locked_channel) 
        *locked_channel = NULL;
    if (cw_strlen_zero(app))
        return -1;
    if (sync)
    {
        chan = __cw_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name, &oh);
        if (chan)
        {
            if (chan->cdr)
            {
                /* check if the channel already has a cdr record, if not give it one */
                cw_log(CW_LOG_WARNING, "%s already has a call record??\n", chan->name);
            }
            else
            {
                chan->cdr = cw_cdr_alloc();   /* allocate a cdr for the channel */
                if (!chan->cdr)
                {
                    /* allocation of the cdr failed */
                    cw_log(CW_LOG_WARNING, "Unable to create Call Detail Record\n");
                    free(chan->pbx);
                    return -1;
                }
                /* allocation of the cdr was successful */
                cw_cdr_init(chan->cdr, chan);  /* initilize our channel's cdr */
                cw_cdr_start(chan->cdr);
            }
            cw_var_copy(&chan->vars, vars);
            if (chan->_state == CW_STATE_UP)
            {
                res = 0;
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", chan->name);
                if ((tmp = malloc(sizeof(struct app_tmp))))
                {
                    memset(tmp, 0, sizeof(struct app_tmp));
                    cw_copy_string(tmp->app, app, sizeof(tmp->app));
                    if (appdata)
                        cw_copy_string(tmp->data, appdata, sizeof(tmp->data));
                    tmp->chan = cw_object_dup(chan);
                    if (sync > 1)
                    {
                        if (locked_channel)
                            cw_channel_unlock(chan);
                        cw_pbx_run_app(tmp);
                    }
                    else
                    {
                        if (locked_channel) 
                            cw_channel_lock(chan);
                        if (cw_pthread_create(&tmp->t, &global_attr_detached, cw_pbx_run_app, tmp))
                        {
                            cw_log(CW_LOG_WARNING, "Unable to spawn execute thread on %s: %s\n", chan->name, strerror(errno));
                            if (locked_channel) 
                                cw_channel_unlock(chan);
                            cw_hangup(chan);
			    cw_object_put(chan);
                            free(tmp);
                            res = -1;
                        }
                        else
                        {
                            if (locked_channel) 
                                *locked_channel = chan;
                        }
                    }
                }
                else
                {
                    cw_log(CW_LOG_ERROR, "Out of memory :(\n");
                    res = -1;
                }
            }
            else
            {
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", chan->name);
                if (chan->cdr)
                {
                    /* update the cdr */
                    /* here we update the status of the call, which sould be busy.
                     * if that fails then we set the status to failed */
                    if (cw_cdr_disposition(chan->cdr, chan->hangupcause))
                        cw_cdr_failed(chan->cdr);
                }
                cw_hangup(chan);
            }
        }
        
        if (res < 0)
        {
            /* the call failed for some reason */
            if (*reason == 0)
            {
                /* if the call failed (not busy or no answer)
                 * update the cdr with the failed message */
                cdr_res = cw_pbx_outgoing_cdr_failed();
                if (cdr_res != 0)
                    return cdr_res;
            }
        }

    }
    else
    {
        if ((as = malloc(sizeof(struct async_stat))) == NULL)
        {
            cw_log(CW_LOG_ERROR, "Out of memory!\n");
            return -1;
        }
        memset(as, 0, sizeof(struct async_stat));
        chan = cw_request_and_dial(type, format, data, timeout, reason, cid_num, cid_name);
        if (!chan)
        {
            free(as);
            return -1;
        }
        as->chan = chan;
        cw_copy_string(as->app, app, sizeof(as->app));
        if (appdata)
            cw_copy_string(as->appdata,  appdata, sizeof(as->appdata));
        as->timeout = timeout;
        cw_var_copy(&chan->vars, vars);
        /* Start a new thread, and get something handling this channel. */
        if (locked_channel) 
            cw_channel_lock(chan);
        if (cw_pthread_create(&as->p, &global_attr_detached, async_wait, as))
        {
            cw_log(CW_LOG_WARNING, "Failed to start async wait\n");
            free(as);
            if (locked_channel) 
                cw_channel_unlock(chan);
            cw_hangup(chan);
            return -1;
        }
        if (locked_channel)
            *locked_channel = chan;
        res = 0;
    }
    return res;
}

static void destroy_exten(struct cw_exten *e)
{
    if (e->priority == PRIORITY_HINT)
        cw_remove_hint(e);

    if (e->datad)
        e->datad(e->data);
    free(e);
}

void __cw_context_destroy(struct cw_context *con, const char *registrar)
{
    struct cw_context *tmp, *tmpl=NULL;
    struct cw_include *tmpi, *tmpil= NULL;
    struct cw_sw *sw, *swl= NULL;
    struct cw_exten *e, *el, *en;
    struct cw_ignorepat *ipi, *ipl = NULL;

    cw_mutex_lock(&conlock);
    tmp = contexts;
    while (tmp)
    {
        if (((con  &&  tmp->hash == con->hash  &&  !strcmp(tmp->name, con->name))  ||  !con)
            &&
            (!registrar ||  !strcasecmp(registrar, tmp->registrar)))
        {
            /* Okay, let's lock the structure to be sure nobody else
               is searching through it. */
            if (cw_mutex_lock(&tmp->lock))
            {
                cw_log(CW_LOG_WARNING, "Unable to lock context lock\n");
                return;
            }
            if (tmpl)
                tmpl->next = tmp->next;
            else
                contexts = tmp->next;
            /* Okay, now we're safe to let it go -- in a sense, we were
               ready to let it go as soon as we locked it. */
            cw_mutex_unlock(&tmp->lock);
            for (tmpi = tmp->includes;  tmpi;  )
            {
                /* Free includes */
                tmpil = tmpi;
                tmpi = tmpi->next;
                free(tmpil);
            }
            for (ipi = tmp->ignorepats;  ipi;  )
            {
                /* Free ignorepats */
                ipl = ipi;
                ipi = ipi->next;
                free(ipl);
            }
            for (sw = tmp->alts;  sw;  )
            {
                /* Free switches */
                swl = sw;
                sw = sw->next;
                free(swl);
                swl = sw;
            }
            for (e = tmp->root;  e;  )
            {
                for (en = e->peer;  en;  )
                {
                    el = en;
                    en = en->peer;
                    destroy_exten(el);
                }
                el = e;
                e = e->next;
                destroy_exten(el);
            }
            cw_mutex_destroy(&tmp->lock);
            free(tmp);
            if (!con)
            {
                /* Might need to get another one -- restart */
                tmp = contexts;
                tmpl = NULL;
                tmpil = NULL;
                continue;
            }
            cw_mutex_unlock(&conlock);
            return;
        }
        tmpl = tmp;
        tmp = tmp->next;
    }
    cw_mutex_unlock(&conlock);
}

void cw_context_destroy(struct cw_context *con, const char *registrar)
{
    __cw_context_destroy(con,registrar);
}


struct pbx_builtin_serialize_variables_args {
	char **buf_p;
	size_t *size_p;
	int total;
};

static int pbx_builtin_serialize_variables_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct pbx_builtin_serialize_variables_args *args = data;

	args->total++;

	if (!cw_build_string(args->buf_p, args->size_p, "%s=%s\n", cw_var_name(var), var->value))
		return 0;

	cw_log(CW_LOG_ERROR, "Data Buffer Size Exceeded!\n");
	return 1;
}

int pbx_builtin_serialize_variables(struct cw_channel *chan, char *buf, size_t size)
{
	struct pbx_builtin_serialize_variables_args args = {
		.buf_p = &buf,
		.size_p = &size,
		.total = 0,
	};

	if (chan) {
		memset(buf, 0, size);
		cw_registry_iterate_ordered(&chan->vars, pbx_builtin_serialize_variables_one, &args);
	}

	return args.total;
}

struct cw_var_t *pbx_builtin_getvar_helper(struct cw_channel *chan, unsigned int hash, const char *name)
{
	struct cw_object *obj;

	if (name
	&& ((chan && (obj = cw_registry_find(&chan->vars, 1, hash, name)))
	|| (obj = cw_registry_find(&var_registry, 1, hash, name))))
		return container_of(obj, struct cw_var_t, obj);

	return NULL;
}

void pbx_builtin_pushvar_helper(struct cw_channel *chan, const char *name, const char *value)
{
	int err = 0;

	if (value) {
		if (option_verbose > 1 && chan)
			cw_verbose(VERBOSE_PREFIX_2 "Pushing global variable '%s' = '%s'\n", name, value);

		err = cw_var_assign((chan ? &chan->vars : &var_registry), name, value);

		if (err && chan)
			cw_softhangup_nolock(chan, CW_SOFTHANGUP_EXPLICIT);
	}
}


void pbx_builtin_setvar_helper(struct cw_channel *chan, const char *name, const char *value)
{
    struct cw_registry *reg = (chan ? &chan->vars : &var_registry);
    struct cw_var_t *var;
    unsigned int hash;
    int err = 0;

    if (option_verbose > 1 && !chan) {
        if (value)
            cw_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' = '%s'\n", name, value);
        else
            cw_verbose(VERBOSE_PREFIX_2 "Removing global variable '%s'\n", name);
    }

    if (value) {
        if (!(var = cw_var_new(name, value, 1)))
            err = 1;
        hash = var->hash;
    } else {
        hash = cw_hash_var_name(name);
	var = NULL;
    }

    if ((err || cw_registry_replace(reg, hash, name, (var ? &var->obj : NULL))) && chan)
        cw_softhangup_nolock(chan, CW_SOFTHANGUP_EXPLICIT);

    if (var)
        cw_object_put(var);
}


void pbx_builtin_clear_globals(void)
{
    cw_registry_flush(&var_registry);
}

int load_pbx(void)
{
    /* Initialize the PBX */
    if (option_verbose)
    {
        cw_verbose( "CallWeaver Core Initializing\n");
    }
    cw_function_registry_initialize();
    cw_cli_register_multiple(pbx_cli, arraysize(pbx_cli));

    return 0;
}

/*
 * Lock context list functions ...
 */
int cw_lock_contexts()
{
    return cw_mutex_lock(&conlock);
}

int cw_unlock_contexts()
{
    return cw_mutex_unlock(&conlock);
}

/*
 * Lock context ...
 */
int cw_lock_context(struct cw_context *con)
{
    return cw_mutex_lock(&con->lock);
}

int cw_unlock_context(struct cw_context *con)
{
    return cw_mutex_unlock(&con->lock);
}

/*
 * Name functions ...
 */
const char *cw_get_context_name(struct cw_context *con)
{
    return con ? con->name : NULL;
}

const char *cw_get_extension_name(struct cw_exten *exten)
{
    return exten ? exten->exten : NULL;
}

const char *cw_get_extension_label(struct cw_exten *exten)
{
    return exten ? exten->label : NULL;
}

const char *cw_get_include_name(struct cw_include *inc)
{
    return inc ? inc->name : NULL;
}

const char *cw_get_ignorepat_name(struct cw_ignorepat *ip)
{
    return ip ? ip->pattern : NULL;
}

int cw_get_extension_priority(struct cw_exten *exten)
{
    return exten ? exten->priority : -1;
}

/*
 * Registrar info functions ...
 */
const char *cw_get_context_registrar(struct cw_context *c)
{
    return c ? c->registrar : NULL;
}

const char *cw_get_extension_registrar(struct cw_exten *e)
{
    return e ? e->registrar : NULL;
}

const char *cw_get_include_registrar(struct cw_include *i)
{
    return i ? i->registrar : NULL;
}

const char *cw_get_ignorepat_registrar(struct cw_ignorepat *ip)
{
    return ip ? ip->registrar : NULL;
}

int cw_get_extension_matchcid(struct cw_exten *e)
{
    return e ? e->matchcid : 0;
}

const char *cw_get_extension_cidmatch(struct cw_exten *e)
{
    return e ? e->cidmatch : NULL;
}

const char *cw_get_extension_app(struct cw_exten *e)
{
    return e ? e->app : NULL;
}

void *cw_get_extension_app_data(struct cw_exten *e)
{
    return e ? e->data : NULL;
}

const char *cw_get_switch_name(struct cw_sw *sw)
{
    return sw ? sw->name : NULL;
}

const char *cw_get_switch_data(struct cw_sw *sw)
{
    return sw ? sw->data : NULL;
}

const char *cw_get_switch_registrar(struct cw_sw *sw)
{
    return sw ? sw->registrar : NULL;
}

/*
 * Walking functions ...
 */
struct cw_context *cw_walk_contexts(struct cw_context *con)
{
    if (!con)
        return contexts;
    else
        return con->next;
}

struct cw_exten *cw_walk_context_extensions(struct cw_context *con, struct cw_exten *exten)
{
    if (!exten)
        return con ? con->root : NULL;
    else
        return exten->next;
}

struct cw_sw *cw_walk_context_switches(struct cw_context *con, struct cw_sw *sw)
{
    if (!sw)
        return con ? con->alts : NULL;
    else
        return sw->next;
}

struct cw_exten *cw_walk_extension_priorities(struct cw_exten *exten, struct cw_exten *priority)
{
    if (!priority)
        return exten;
    else
        return priority->peer;
}

struct cw_include *cw_walk_context_includes(struct cw_context *con, struct cw_include *inc)
{
    if (!inc)
        return con ? con->includes : NULL;
    else
        return inc->next;
}

struct cw_ignorepat *cw_walk_context_ignorepats(struct cw_context *con, struct cw_ignorepat *ip)
{
    if (!ip)
        return con ? con->ignorepats : NULL;
    else
        return ip->next;
}

int cw_context_verify_includes(struct cw_context *con)
{
    struct cw_include *inc;
    int res = 0;

    for (inc = cw_walk_context_includes(con, NULL);  inc;  inc = cw_walk_context_includes(con, inc))
    {
        if (!cw_context_find(inc->rname))
        {
            res = -1;
            cw_log(CW_LOG_WARNING, "Attempt to include nonexistent context '%s' in context '%s' (%#x)\n",
                     cw_get_context_name(con), inc->rname, con->hash);
        }
    }
    return res;
}


int cw_parseable_goto(struct cw_channel *chan, const char *goto_string) 
{
	char *argv[3 + 1];
	char *context, *exten, *prio;
	int argc, ret;

	if (!goto_string || !(prio = cw_strdupa(goto_string))
	|| (argc = cw_separate_app_args(prio, ',', arraysize(argv), argv)) < 1 || argc > 3)
		return cw_function_syntax("Goto([[context, ]extension, ]priority)");

	prio = argv[argc - 1];
	exten = (argc > 1 ? argv[argc - 2] : NULL);
	context = (argc > 2 ? argv[0] : NULL);

	ret = cw_explicit_goto(chan, context, exten, prio);
	cw_cdr_update(chan);

	return ret;
}
