/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Application convenience functions, designed to give consistent
 look and feel to CallWeaver apps.
 */

#ifndef _CALLWEAVER_APP_H
#define _CALLWEAVER_APP_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/linkedlists.h"
#include "callweaver/channel.h"

/* IVR stuff */

/* Callback function for IVR, returns 0 on completion, -1 on hangup or digit if
   interrupted */
typedef int (*cw_ivr_callback)(struct cw_channel *chan, char *option, void *cbdata);

typedef enum {
	CW_ACTION_UPONE,		/* adata is unused */
	CW_ACTION_EXIT,		/* adata is the return value for cw_ivr_menu_run if channel was not hungup */
	CW_ACTION_CALLBACK,	/* adata is an cw_ivr_callback */
	CW_ACTION_PLAYBACK,	/* adata is file to play */
	CW_ACTION_BACKGROUND,	/* adata is file to play */
	CW_ACTION_PLAYLIST,	/* adata is list of files, separated by ; to play */
	CW_ACTION_MENU,		/* adata is a pointer to an cw_ivr_menu */
	CW_ACTION_REPEAT,		/* adata is max # of repeats, cast to a pointer */
	CW_ACTION_RESTART,		/* adata is like repeat, but resets repeats to 0 */
	CW_ACTION_TRANSFER,	/* adata is a string with exten[@context] */
	CW_ACTION_WAITOPTION,	/* adata is a timeout, or 0 for defaults */
	CW_ACTION_NOOP,		/* adata is unused */
	CW_ACTION_BACKLIST,	/* adata is list of files separated by ; allows interruption */
} cw_ivr_action;

struct cw_ivr_option {
	char *option;
	cw_ivr_action action;
	void *adata;	
};

/* 
    Special "options" are: 
   "s" - "start here (one time greeting)"
   "g" - "greeting/instructions"
   "t" - "timeout"
   "h" - "hangup"
   "i" - "invalid selection"

*/

struct cw_ivr_menu {
	char *title;		/* Title of menu */
	unsigned int flags;	/* Flags */
	struct cw_ivr_option *options;	/* All options */
};

#define CW_IVR_FLAG_AUTORESTART (1 << 0)

struct cw_option {
	unsigned int flag;
	int arg_index;
};

extern CW_API_PUBLIC int cw_parseoptions(const struct cw_option *options, struct cw_flags *flags, char **args, char *optstr);

#define CW_DECLARE_OPTIONS(holder,args...) \
	static struct cw_option holder[128] = args

#define CW_IVR_DECLARE_MENU(holder,title,flags,foo...) \
	static struct cw_ivr_option __options_##holder[] = foo;\
	static struct cw_ivr_menu holder = { title, flags, __options_##holder }
	

/*! Plays a stream and gets DTMF data from a channel */
/*!
 * \param c Which channel one is interacting with
 * \param prompt File to pass to cw_streamfile (the one that you wish to play)
 * \param s The location where the DTMF data will be stored
 * \param maxlen Max Length of the data
 * \param timeout Timeout length waiting for data(in milliseconds).  Set to 0 for standard timeout(six seconds), or -1 for no time out.
 *
 *  This function was designed for application programmers for situations where they need 
 *  to play a message and then get some DTMF data in response to the message.  If a digit 
 *  is pressed during playback, it will immediately break out of the message and continue
 *  execution of your code.
 */
extern CW_API_PUBLIC int cw_app_getdata(struct cw_channel *c, const char *prompt, char *s, int maxlen, int timeout);

/* Full version with audiofd and controlfd.  NOTE: returns '2' on ctrlfd available, not '1' like other full functions */
extern CW_API_PUBLIC int cw_app_getdata_full(struct cw_channel *c, const char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd);


void CW_API_PUBLIC cw_install_t38_functions( int (*has_request_t38_func)(const struct cw_channel *chan) );

void CW_API_PUBLIC cw_uninstall_t38_functions(void);

int CW_API_PUBLIC cw_app_request_t38(const struct cw_channel *chan);


void CW_API_PUBLIC cw_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs));
  
void CW_API_PUBLIC cw_uninstall_vm_functions(void);

/*! Determine if a given mailbox has any voicemail */
int CW_API_PUBLIC cw_app_has_voicemail(const char *mailbox, const char *folder);

/*! Determine number of new/old messages in a mailbox */
int CW_API_PUBLIC cw_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs);

/*! Safely spawn an external program while closingn file descriptors */
extern CW_API_PUBLIC int cw_safe_system(const char *s);

/*! Send DTMF to chan (optionally entertain peer)   */
extern CW_API_PUBLIC int cw_dtmf_stream(struct cw_channel *chan, struct cw_channel *peer, char *digits, int between);

/*! Stream a file with fast forward, pause, reverse, restart. */
extern CW_API_PUBLIC int cw_control_streamfile(struct cw_channel *chan, const char *file, const char *fwd, const char *rev, const char *stop, const char *pause, const char *restart, int skipms);

/*! Play a stream and wait for a digit, returning the digit that was pressed */
extern CW_API_PUBLIC int cw_play_and_wait(struct cw_channel *chan, const char *fn);

/*! Record a file for a max amount of time (in seconds), in a given list of formats separated by ',', outputting the duration of the recording, and with a maximum */
/*   permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults. 
     calls cw_unlock_path() on 'path' if passed */
extern CW_API_PUBLIC int cw_play_and_record(struct cw_channel *chan, const char *playfile, const char *recordfile, int maxtime_sec, const char *fmt, int *duration, int silencethreshold, int maxsilence_ms, const char *path);

/*! Record a message and prepend the message to the given record file after playing the optional playfile (or a beep), storing the duration in 'duration' and with a maximum */
/*   permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults. */
extern CW_API_PUBLIC int cw_play_and_prepend(struct cw_channel *chan, char *playfile, char *recordfile, int maxtime_sec, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence_ms);

enum CW_LOCK_RESULT {
	CW_LOCK_SUCCESS = 0,
	CW_LOCK_TIMEOUT = -1,
	CW_LOCK_PATH_NOT_FOUND = -2,
	CW_LOCK_FAILURE = -3,
};

/*
 * \brief Lock a filesystem path.
 * \param path the path to be locked
 * \return one of CW_LOCK_RESULT values
 */
extern CW_API_PUBLIC enum CW_LOCK_RESULT cw_lock_path(const char *path);

/* Unlock a path */
extern CW_API_PUBLIC int cw_unlock_path(const char *path);

struct cw_group_info {
	struct cw_channel *chan;
	char *category;
	char *group;
	CW_LIST_ENTRY(cw_group_info) list;
};

/*! Split a group string into group and category, returning a default category if none is provided. */
extern CW_API_PUBLIC int cw_app_group_split_group(const char *data, char *group, int group_max, char *category, int category_max);

/*! Set the group for a channel, splitting the provided data into group and category, if specified. */
extern CW_API_PUBLIC int cw_app_group_set_channel(struct cw_channel *chan, const char *data);

/*! Get the current channel count of the specified group and category. */
extern CW_API_PUBLIC int cw_app_group_get_count(const char *group, const char *category);

/*! Get the current channel count of all groups that match the specified pattern and category. */
extern CW_API_PUBLIC int cw_app_group_match_get_count(const char *groupmatch, const char *category);

/*! Discard all group counting for a channel */
extern CW_API_PUBLIC int cw_app_group_discard(struct cw_channel *chan);

/*! Lock the group count list */
extern CW_API_PUBLIC int cw_app_group_list_lock(void);

/*! Get the head of the group count list */
extern CW_API_PUBLIC struct cw_group_info *cw_app_group_list_head(void);

/*! Unlock the group count list */
extern CW_API_PUBLIC int cw_app_group_list_unlock(void);


/*!
  \brief Separate a string into arguments in an array
  \param buf The string to be parsed (this must be a writable copy, as it will be modified)
  \param delim The character to be used to delimit arguments
  \param argv An array of 'char *' to be filled in with pointers to the found arguments
  \param max_args The number of elements in the array (i.e. the number of arguments you will accept)

  Note: if there are more arguments in the string than the array will hold, trailing
  arguments will be discarded

  \return The number of arguments found, or zero if the function arguments are not valid.
*/
extern CW_API_PUBLIC int cw_separate_app_args(char *buf, char delim, int max_args, char **argv);

/*! Present a dialtone and collect a certain length extension.  Returns 1 on valid extension entered, -1 on hangup, or 0 on invalid extension. Note that if 'collect' holds digits already, new digits will be appended, so be sure it's initialized properly */
extern CW_API_PUBLIC int cw_app_dtget(struct cw_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout);

/*! Allow to record message and have a review option */
extern CW_API_PUBLIC int cw_record_review(struct cw_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_APP_H */
