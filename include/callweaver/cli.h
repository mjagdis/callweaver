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
 * \brief Standard Command Line Interface
 */

#ifndef _CALLWEAVER_CLI_H
#define _CALLWEAVER_CLI_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <stdarg.h>

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"


extern CW_API_PUBLIC void cw_cli(int fd, char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

#define RESULT_SUCCESS		0
#define RESULT_SHOWUSAGE	1
#define RESULT_FAILURE		2

#define CW_MAX_CMD_LEN 	16

#define CW_MAX_ARGS 64

#define CW_CLI_COMPLETE_EOF	"_EOF_"


/*! \brief A command line entry */ 
struct cw_clicmd {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	/*! Null terminated list of the words of the command */
	char *cmda[CW_MAX_CMD_LEN];
	/*! Handler for the command (fd for output, # of arguments, argument list).  Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(int fd, int argc, char *argv[]);
	/*! Summary of the command (< 60 characters) */
	const char *summary;
	/*! Detailed usage information */
	const char *usage;
	/*! Generate a list of possible completions for a given word */
	void (*generator)(int fd, char *argv[], int lastarg, int lastarg_len);
};


extern CW_API_PUBLIC struct cw_registry clicmd_registry;


#define cw_cli_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!cw_object_refs(__ptr)) \
		cw_object_init_obj(&__ptr->obj, CW_OBJECT_CURRENT_MODULE, 0); \
	__ptr->reg_entry = cw_registry_add(&clicmd_registry, 0, &__ptr->obj); \
	0; \
})
#define cw_cli_unregister(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr->reg_entry) \
		cw_registry_del(&clicmd_registry, __ptr->reg_entry); \
	0; \
})

#define cw_cli_register_multiple(array, count) do { \
	const typeof(&(array)[0]) __aptr = &(array)[0]; \
	int i, n = (count); \
	for (i = 0; i < n; i++) \
		cw_cli_register(&__aptr[i]); \
} while (0)

#define cw_cli_unregister_multiple(array, count) do { \
	const typeof(&(array)[0]) __aptr = &(array)[0]; \
	int i, n = (count); \
	for (i = 0; i < n; i++) \
		cw_cli_unregister(&__aptr[i]); \
} while (0)


/*! \brief Interprets a command 
 * Interpret a command s, sending output to fd
 * Returns 0 on succes, -1 on failure 
 */
extern CW_API_PUBLIC int cw_cli_command(int fd, char *s);

/*! \brief Readline madness 
 * Useful for readline, that's about it
 * Returns 0 on success, -1 on failure
 */
extern void cw_cli_generator(int fd, char *text);

extern void cw_cli_init(void);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_CLI_H */
