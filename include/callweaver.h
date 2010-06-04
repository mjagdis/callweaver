/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * General Definitions for CallWeaver top level program
 * 
 * Copyright (C) 1999-2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief CallWeaver main include file. File version handling, generic pbx functions.
*/
#if !defined(_CALLWEAVER_H)
#define _CALLWEAVER_H


#ifdef CW_API_IMPLEMENTATION
#  define CW_API_PUBLIC DLL_PUBLIC_EXPORT
#else
#  define CW_API_PUBLIC DLL_PUBLIC_IMPORT
#endif


#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

#define DEFAULT_LANGUAGE "en"

#define CW_CONFIG_MAX_PATH 255


/* provided in callweaver.c */
enum cw_config_name {
	CW_CONFIG_DIR,
	CW_SPOOL_DIR,
	CW_MONITOR_DIR,
	CW_VAR_DIR,
	CW_LOG_DIR,
	CW_OGI_DIR,
	CW_DB,
	CW_DB_DIR,
	CW_KEY_DIR,
	CW_PID,
	CW_SOCKET,
	CW_RUN_DIR,
	CW_RUN_USER,
	CW_RUN_GROUP,
	CW_CTL_PERMISSIONS,
	CW_CTL_GROUP,
	CW_CTL,
	CW_SYSTEM_NAME,
	CW_SOUNDS_DIR,
	CW_MOD_DIR,

	CW_CONFIG_DEPRECATED,
	CW_CONFIG_UNKNOWN,
};

extern CW_API_PUBLIC const char *cw_config[];

extern CW_API_PUBLIC enum cw_config_name cw_config_name2key(const char *name);

extern CW_API_PUBLIC int callweaver_main(int argc, char *argv[]);


/* Provided by version.c */
extern const char cw_version_string[];

/* Provided by module.c */
extern int load_modules(const int preload_only);
/* Provided by pbx.c */
extern int load_pbx(void);
/* Provided by logger.c */
extern int init_logger(void);
extern void close_logger(void);
/* Provided by frame.c */
extern int init_framer(void);
/* Provided by logger.c */
extern int reload_logger(int);
/* Provided by db.c */
extern int cwdb_init(void);
/* Provided by channel.c */
extern int cw_channels_init(void);


#if !defined(LOW_MEMORY)
#  define CALLWEAVER_FILE_VERSION(file, x) \
	static char __attribute__((used)) __file_version[] = file ", " x;
#else /* LOW_MEMORY */
#  define CALLWEAVER_FILE_VERSION(file, x)
#endif

#if defined(__CW_DEBUG_MALLOC)  &&  !defined(_CALLWEAVER_CALLWEAVER_MM_H)
#include "callweaver/callweaver_mm.h"
#endif

#endif
