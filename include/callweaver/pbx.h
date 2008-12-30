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
 * \brief Core PBX routines and definitions.
 */

#ifndef _CALLWEAVER_PBX_H
#define _CALLWEAVER_PBX_H

#include "callweaver/sched.h"
#include "callweaver/channel.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"
#include "callweaver/function.h"
#include "callweaver/callweaver_hash.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


#define CW_PBX_KEEP    0
#define CW_PBX_REPLACE 1

/*! Max length of an application */
#define CW_MAX_APP	32

/*! Special return values from applications to the PBX */
#define CW_PBX_KEEPALIVE	10		/* Destroy the thread, but don't hang up the channel */
#define CW_PBX_NO_HANGUP_PEER       11

/*! Special Priority for an hint */
#define PRIORITY_HINT	-1

/*! Extension states */
enum cw_extension_states
{
	/*! Extension removed */
	CW_EXTENSION_REMOVED = -2,
	/*! Extension hint removed */
	CW_EXTENSION_DEACTIVATED = -1,
	/*! No device INUSE or BUSY  */
	CW_EXTENSION_NOT_INUSE = 0,
	/*! One or more devices INUSE */
	CW_EXTENSION_INUSE = 1 << 0,
	/*! All devices BUSY */
	CW_EXTENSION_BUSY = 1 << 1,
	/*! All devices UNAVAILABLE/UNREGISTERED */
	CW_EXTENSION_UNAVAILABLE = 1 << 2,
	/*! All devices RINGING */
	CW_EXTENSION_RINGING = 1 << 3,
};

enum cw_extension_match_conditions
{
    /*! The extension did not match. */
    EXTENSION_MATCH_FAILURE = 0,
    /*! The extension is an exact match for the pattern. */
    EXTENSION_MATCH_EXACT = 1,
    /*! The extension starts with an exact match for the pattern, but is longer than the pattern. */
    EXTENSION_MATCH_OVERLENGTH = 2,
    /*! The extension could match the pattern if more digits were added to the end, but is currently incomplete. */
    EXTENSION_MATCH_INCOMPLETE = 3,
    /*! The extension matches, but may be extended further as it matched a '.' or '~'. */
    EXTENSION_MATCH_STRETCHABLE = 4,
    /*! The extension is an 'optional' match for the pattern. i.e. the pattern has a '!' at the end. */
    EXTENSION_MATCH_POSSIBLE = 5
};

static const struct cfextension_states
{
	int extension_state;
	const char * const text;
} extension_states[] = {
	{ CW_EXTENSION_NOT_INUSE,                     "Idle" },
	{ CW_EXTENSION_INUSE,                         "InUse" },
	{ CW_EXTENSION_BUSY,                          "Busy" },
	{ CW_EXTENSION_UNAVAILABLE,                   "Unavailable" },
	{ CW_EXTENSION_RINGING,                       "Ringing" },
	{ CW_EXTENSION_INUSE | CW_EXTENSION_RINGING, "InUse&Ringing" }
};

struct cw_context;
struct cw_exten;     
struct cw_include;
struct cw_ignorepat;
struct cw_sw;
struct cw_func;

typedef int (*cw_state_cb_type)(char *context, char* id, enum cw_extension_states state, void *data);


struct cw_timing
{
	int hastime;				/* If time construct exists */
	unsigned int monthmask;			/* Mask for month */
	unsigned int daymask;			/* Mask for date */
	unsigned int dowmask;			/* Mask for day of week (mon-sun) */
	unsigned int minmask[24];		/* Mask for minute */
};

extern int cw_build_timing(struct cw_timing *i, char *info);
extern int cw_check_timing(struct cw_timing *i);

struct cw_pbx
{
        int dtimeout;                                   /* Timeout between digits (seconds) */
        int rtimeout;                                   /* Timeout for response
							   (seconds) */
};


/*! Interpret a condition variable
 *!
 * \param condition condition to interpret
 * Interpret a condition variable. This does _not_ evaluate teh condition.
 *     NULL and empty strings are false
 *     Non-empty strings are true
 *     Numbers are true if non-zero, false otherwise
 * Returns zero for false, non-zero for true
 */
extern int pbx_checkcondition(char *condition);


/*! Register a new context */
/*!
 * \param extcontexts pointer to the cw_context structure pointer
 * \param name name of the new context
 * \param registrar registrar of the context
 * This will first search for a context with your name.  If it exists already, it will not
 * create a new one.  If it does not exist, it will create a new one with the given name
 * and registrar.
 * It returns NULL on failure, and an cw_context structure on success
 */
struct cw_context *cw_context_create(struct cw_context **extcontexts, const char *name, const char *registrar);

/*! Merge the temporary contexts into a global contexts list and delete from the global list the ones that are being added */
/*!
 * \param extcontexts pointer to the cw_context structure pointer
 * \param registar of the context; if it's set the routine will delete all contexts that belong to that registrar; if NULL only the contexts that are specified in extcontexts
 */
void cw_merge_contexts_and_delete(struct cw_context **extcontexts, const char *registrar);

/*! Destroy a context (matches the specified context (or ANY context if NULL) */
/*!
 * \param con context to destroy
 * \param registrar who registered it
 * You can optionally leave out either parameter.  It will find it
 * based on either the cw_context or the registrar name.
 * Returns nothing
 */
void cw_context_destroy(struct cw_context *con, const char *registrar);

/*! Find a context */
/*!
 * \param name name of the context to find
 * Will search for the context with the given name.
 * Returns the cw_context on success, NULL on failure.
 */
struct cw_context *cw_context_find(const char *name);

enum cw_pbx_result
{
	CW_PBX_SUCCESS = 0,
	CW_PBX_FAILED = -1,
	CW_PBX_CALL_LIMIT = -2,
};

/*! Create a new thread and start the PBX (or whatever) */
/*!
 * \param c channel to start the pbx on
 * \return Zero on success, non-zero on failure
 */
enum cw_pbx_result cw_pbx_start(struct cw_channel *c);

/*! Execute the PBX in the current thread */
/*!
 * \param c channel to run the pbx on
 * \return Zero on success, non-zero on failure
 * This executes the PBX on a given channel. It allocates a new
 * PBX structure for the channel, and provides all PBX functionality.
 */
enum cw_pbx_result cw_pbx_run(struct cw_channel *c);

/*! 
 * \param context context to add the extension to
 * \param replace
 * \param extension extension to add
 * \param priority priority level of extension addition
 * \param callerid callerid of extension
 * \param application application to run on the extension with that priority level
 * \param data data to pass to the application
 * \param datad
 * \param registrar who registered the extension
 * Add and extension to an extension context.  
 * Callerid is a pattern to match CallerID, or NULL to match any callerid
 * Returns 0 on success, -1 on failure
 */
int cw_add_extension(const char *context, int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar);

/*! Add an extension to an extension context, this time with an cw_context *.  CallerID is a pattern to match on callerid, or NULL to not care about callerid */
/*! 
 * For details about the arguements, check cw_add_extension()
 */
int cw_add_extension2(struct cw_context *con,
				      int replace, const char *extension, int priority, const char *label, const char *callerid, 
					  const char *application, void *data, void (*datad)(void *),
					  const char *registrar);

/*! Uses hint and devicestate callback to get the state of an extension */
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to get state
 * Returns extension state !! = CW_EXTENSION_???
 */
int cw_extension_state(struct cw_channel *c, char *context, char *exten);

/*! Return string of the state of an extension */
/*!
 * \param extension_state is the numerical state delivered by cw_extension_state
 * Returns the state of an extension as string
 */
const char *cw_extension_state2str(int extension_state);

/*! Registers a state change callback */
/*!
 * \param context which context to look in
 * \param exten which extension to get state
 * \param callback callback to call if state changed
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
int cw_extension_state_add(const char *context, const char *exten, 
			    cw_state_cb_type callback, void *data);

/*! Deletes a registered state change callback by ID */
/*!
 * \param id of the callback to delete
 * Removes the callback from list of callbacks
 * Return 0 on success, -1 on failure
 */
int cw_extension_state_del(int id, cw_state_cb_type callback);

/*! If an extension exists, return non-zero */
/*!
 * \param hint buffer for hint
 * \param maxlen size of hint buffer
 * \param hint buffer for name portion of hint
 * \param maxlen size of name buffer
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * If an extension within the given context with the priority PRIORITY_HINT
 * is found a non zero value will be returned.
 * Otherwise, 0 is returned.
 */
int cw_get_hint(char *hint, int maxlen, char *name, int maxnamelen, struct cw_channel *c, const char *context, const char *exten);

/*! If an extension exists, return non-zero */
/*  work */
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param priority priority of the action within the extension
 * \param callerid callerid to search for
 * If an extension within the given context(or callerid) with the given priority is found a non zero value will be returned.
 * Otherwise, 0 is returned.
 */
int cw_exists_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid);

/*! If an extension exists, return non-zero */
/*  work */
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param labellabel of the action within the extension to match to priority
 * \param callerid callerid to search for
 * If an priority which matches given label in extension or -1 if not found.
\ */
int cw_findlabel_extension(struct cw_channel *c, const char *context, const char *exten, const char *label, const char *callerid);

int cw_findlabel_extension2(struct cw_channel *c, struct cw_context *con, const char *exten, const char *label, const char *callerid);

/*! Looks for a valid matching extension */
/*!
  \param c not really important
  \param context context to serach within
  \param exten extension to check
  \param priority priority of extension path
  \param callerid callerid of extension being searched for
   If "exten" *could be* a valid extension in this context with or without
   some more digits, return non-zero.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore
*/
int cw_canmatch_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid);

/*! Looks to see if adding anything to this extension might match something. (exists ^ canmatch) */
/*!
  \param c not really important
  \param context context to serach within
  \param exten extension to check
  \param priority priority of extension path
  \param callerid callerid of extension being searched for
   If "exten" *could match* a valid extension in this context with
   some more digits, return non-zero.  Does NOT return non-zero if this is
   an exact-match only.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore
*/
int cw_matchmore_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid);

const char *cw_extension_match_to_str(int match);

/*! Determine how well a given extension matches a given pattern (in NXX format) */
/*!
 * \param destinationm extension to check against the pattern.
 * \param pattern pattern to match
 * Checks the extent to which the given extension matches the given pattern.
 * \return one of the enum cw_extension_match_conditions values
 */
int cw_extension_pattern_match(const char *destination, const char *pattern);

/*! Execute an extension. */
/*!
  \param c channel to execute upon
  \param context which context extension is in
  \param exten extension to execute
  \param priority priority to execute within the given extension
   If it's not available, do whatever you should do for
   default extensions and halt the thread if necessary.  This function does not
   return, except on error.
*/
int cw_exec_extension(struct cw_channel *c, const char *context, const char *exten, int priority, const char *callerid);

/*! Add an include */
/*!
  \param context context to add include to
  \param include new include to add
  \param registrar who's registering it
   Adds an include taking a char * string as the context parameter
   Returns 0 on success, -1 on error
*/
int cw_context_add_include(const char *context, const char *include, const char *registrar);

/*! Add an include */
/*!
  \param con context to add the include to
  \param include include to add
  \param registrar who registered the context
   Adds an include taking a struct cw_context as the first parameter
   Returns 0 on success, -1 on failure
*/
int cw_context_add_include2(struct cw_context *con, const char *include, const char *registrar);

/*! Removes an include */
/*!
 * See add_include
 */
int cw_context_remove_include(const char *context, const char *include,const  char *registrar);
/*! Removes an include by an cw_context structure */
/*!
 * See add_include2
 */
int cw_context_remove_include2(struct cw_context *con, const char *include, const char *registrar);

/*! Verifies includes in an cw_contect structure */
/*!
 * \param con context in which to verify the includes
 * Returns 0 if no problems found, -1 if there were any missing context
 */
int cw_context_verify_includes(struct cw_context *con);
	  
/*! Add a switch */
/*!
 * \param context context to which to add the switch
 * \param sw switch to add
 * \param data data to pass to switch
 * \param eval whether to evaluate variables when running switch
 * \param registrar whoever registered the switch
 * This function registers a switch with the callweaver switch architecture
 * It returns 0 on success, -1 on failure
 */
int cw_context_add_switch(const char *context, const char *sw, const char *data, int eval, const char *registrar);
/*! Adds a switch (first param is a cw_context) */
/*!
 * See cw_context_add_switch()
 */
int cw_context_add_switch2(struct cw_context *con, const char *sw, const char *data, int eval, const char *registrar);

/*! Remove a switch */
/*!
 * Removes a switch with the given parameters
 * Returns 0 on success, -1 on failure
 */
int cw_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar);
int cw_context_remove_switch2(struct cw_context *con, const char *sw, const char *data, const char *registrar);

/*! Simply remove extension from context */
/*!
 * \param context context to remove extension from
 * \param extension which extension to remove
 * \param priority priority of extension to remove
 * \param registrar registrar of the extension
 * This function removes an extension from a given context.
 * Returns 0 on success, -1 on failure
 */
int cw_context_remove_extension(const char *context, const char *extension, int priority,
	const char *registrar);
int cw_context_remove_extension2(struct cw_context *con, const char *extension,
	int priority, const char *registrar);

/*! Add an ignorepat */
/*!
 * \param context which context to add the ignorpattern to
 * \param ignorpat ignorepattern to set up for the extension
 * \param registrar registrar of the ignore pattern
 * Adds an ignore pattern to a particular context.
 * Returns 0 on success, -1 on failure
 */
int cw_context_add_ignorepat(const char *context, const char *ignorepat, const char *registrar);
int cw_context_add_ignorepat2(struct cw_context *con, const char *ignorepat, const char *registrar);

/* Remove an ignorepat */
/*!
 * \param context context from which to remove the pattern
 * \param ignorepat the pattern to remove
 * \param registrar the registrar of the ignore pattern
 * This removes the given ignorepattern
 * Returns 0 on success, -1 on failure
 */
int cw_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar);
int cw_context_remove_ignorepat2(struct cw_context *con, const char *ignorepat, const char *registrar);

/*! Checks to see if a number should be ignored */
/*!
 * \param context context to search within
 * \param extension to check whether it should be ignored or not
 * Check if a number should be ignored with respect to dialtone cancellation.  
 * Returns 0 if the pattern should not be ignored, or non-zero if the pattern should be ignored 
 */
int cw_ignore_pattern(const char *context, const char *pattern);

/* Locking functions for outer modules, especially for completion functions */
/*! Locks the contexts */
/*! Locks the context list
 * Returns 0 on success, -1 on error
 */
int cw_lock_contexts(void);

/*! Unlocks contexts */
/*!
 * Returns 0 on success, -1 on failure
 */
int cw_unlock_contexts(void);

/*! Locks a given context */
/*!
 * \param con context to lock
 * Locks the context.
 * Returns 0 on success, -1 on failure
 */
int cw_lock_context(struct cw_context *con);
/*! Unlocks the given context */
/*!
 * \param con context to unlock
 * Unlocks the given context
 * Returns 0 on success, -1 on failure
 */
int cw_unlock_context(struct cw_context *con);


/* Synchronously or asynchronously make an outbound call and send it to a
   particular extension */
int cw_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, struct cw_registry *vars, struct cw_channel **locked_channel);

/* Synchronously or asynchronously make an outbound call and send it to a
   particular application with given extension */
int cw_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, struct cw_registry *vars, struct cw_channel **locked_channel);

/* Functions for returning values from structures */
const char *cw_get_context_name(struct cw_context *con);
const char *cw_get_extension_name(struct cw_exten *exten);
const char *cw_get_include_name(struct cw_include *include);
const char *cw_get_ignorepat_name(struct cw_ignorepat *ip);
const char *cw_get_switch_name(struct cw_sw *sw);
const char *cw_get_switch_data(struct cw_sw *sw);

/* Other extension stuff */
int cw_get_extension_priority(struct cw_exten *exten);
int cw_get_extension_matchcid(struct cw_exten *e);
const char *cw_get_extension_cidmatch(struct cw_exten *e);
const char *cw_get_extension_app(struct cw_exten *e);
const char *cw_get_extension_label(struct cw_exten *e);
void *cw_get_extension_app_data(struct cw_exten *e);

/* Registrar info functions ... */
const char *cw_get_context_registrar(struct cw_context *c);
const char *cw_get_extension_registrar(struct cw_exten *e);
const char *cw_get_include_registrar(struct cw_include *i);
const char *cw_get_ignorepat_registrar(struct cw_ignorepat *ip);
const char *cw_get_switch_registrar(struct cw_sw *sw);

/* Walking functions ... */
struct cw_context *cw_walk_contexts(struct cw_context *con);
struct cw_exten *cw_walk_context_extensions(struct cw_context *con,
	struct cw_exten *priority);
struct cw_exten *cw_walk_extension_priorities(struct cw_exten *exten,
	struct cw_exten *priority);
struct cw_include *cw_walk_context_includes(struct cw_context *con,
	struct cw_include *inc);
struct cw_ignorepat *cw_walk_context_ignorepats(struct cw_context *con,
	struct cw_ignorepat *ip);
struct cw_sw *cw_walk_context_switches(struct cw_context *con, struct cw_sw *sw);

int pbx_builtin_serialize_variables(struct cw_channel *chan, char *buf, size_t size);
extern struct cw_var_t *pbx_builtin_getvar_helper(struct cw_channel *chan, unsigned int hash, const char *name);
extern void pbx_builtin_pushvar_helper(struct cw_channel *chan, const char *name, const char *value);
extern void pbx_builtin_setvar_helper(struct cw_channel *chan, const char *name, const char *value);
extern void pbx_retrieve_variable(struct cw_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct cw_registry *var_reg);
extern void pbx_builtin_clear_globals(void);
extern int pbx_substitute_variables_helper(struct cw_channel *c,const char *cp1,char *cp2,int count);
extern int pbx_substitute_variables_varshead(struct cw_registry *vars, const char *cp1, char *cp2, int count);

int cw_extension_patmatch(const char *pattern, const char *data);

/* Set "autofallthrough" flag, if newval is <0, does not acutally set.  If
  set to 1, sets to auto fall through.  If newval set to 0, sets to no auto
  fall through (reads extension instead).  Returns previous value. */
extern int pbx_set_autofallthrough(int newval);
/* I can find neither parsable nor parseable at dictionary.com, but google gives me 169000 hits for parseable and only 49,800 for parsable */
int cw_parseable_goto(struct cw_channel *chan, const char *goto_string);
int cw_explicit_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority);
int cw_async_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority);
int cw_goto_n(struct cw_channel *chan, const char *context, const char *exten, int priority, int async, int ifexists);
int cw_goto(struct cw_channel *chan, const char *context, const char *exten, const char *priority, int async, int ifexists);
#define cw_explicit_goto(chan, context, exten, priority)    cw_goto((chan), (context), (exten), (priority), 0, 0)
#define cw_async_goto(chan, context, exten, priority)       cw_goto((chan), (context), (exten), (priority), 1, 0)
#define cw_goto_if_exists_n(chan, context, exten, priority) cw_goto_n((chan), (context), (exten), (priority), 0, 1)
#define cw_goto_if_exists(chan, context, exten, priority)   cw_goto((chan), (context), (exten), (priority), 0, 1)
int cw_async_goto_by_name(const char *chan, const char *context, const char *exten, const char *priority);


/*! \brief Logs a syntax error for a function
 *
 * \param syntax	the correct usage message
 *
 * \return -1. Propogating this back up the call chain will hang up the current call.
 */
extern int cw_function_syntax(const char *syntax);

/*! \brief Executes a function using an array of arguments
 *
 * Executes an application on a channel with the given arguments
 * and returns any result as a string in the given result buffer.
 *
 * \param chan		channel to execute on
 * \param hash		hash of the name of function to execute (from cw_hash_string())
 * \param name		name of function to execute
 * \param argc		the number of arguments
 * \param argv		an array of pointers to argument strings
 * \param result	where to write any result
 * \param result_max	how much space is available in out for a result
 *
 * \return 0 on success, -1 on failure
 */
extern int cw_function_exec(struct cw_channel *chan, unsigned int hash, const char *name, int argc, char **argv, char *result, size_t result_max);

/*! \brief Executes a function using an argument string
 *
 * Executes an application on a channel with the given arguments
 * and returns any result as a string in the given result buffer.
 * The argument string contains zero or more comma separated
 * arguments. Arguments may be quoted and/or contain backslash
 * escaped characters as allowed by cw_separate_app_args().
 * They may NOT contain variables or expressions that require
 * expansion - these should have been expanded prior to calling
 * cw_function_exec().
 *
 * \param chan		channel to execute on
 * \param hash		hash of the name of function to execute (from cw_hash_string())
 * \param name		name of function to execute
 * \param args		the argument string
 * \param result	where to write any result
 * \param result_max	how much space is available in out for a result
 *
 * \return 0 on success, -1 on failure
 */
extern int cw_function_exec_str(struct cw_channel *chan, unsigned int hash, const char *name, char *args, char *result, size_t result_max);


/* Number of active calls */
int cw_active_calls(void);
	
void cw_hint_state_changed(const char *device);

int pbx_checkcondition(char *condition);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_PBX_H */
