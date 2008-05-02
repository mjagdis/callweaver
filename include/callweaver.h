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

// #define RELEASE_TARBALL 1

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

#define DEFAULT_LANGUAGE "en"

#define CW_CONFIG_MAX_PATH 255

#ifndef RELEASE_TARBALL
#define CW_VERSION_INFO PACKAGE_STRING " SVN-" SVN_VERSION " built on " BUILD_HOSTNAME \
        ",  a " BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE
#else
#define CW_VERSION_INFO PACKAGE_STRING " built on " BUILD_HOSTNAME \
        ",  a " BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE
#endif


/* provided in callweaver.c */
extern int callweaver_main(int argc, char *argv[]);
extern char cw_config_CW_CONFIG_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_CONFIG_FILE[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_SPOOL_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_MONITOR_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_VAR_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_LOG_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_OGI_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_DB[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_DB_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_KEY_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_PID[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_SOCKET[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_RUN_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_CTL_PERMISSIONS[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_CTL_OWNER[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_CTL_GROUP[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_CTL[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_SYSTEM_NAME[20];
extern char cw_config_CW_SOUNDS_DIR[CW_CONFIG_MAX_PATH];
extern char cw_config_CW_ENABLE_UNSAFE_UNLOAD[20];

/* Provided by callweaver.c */
extern int cw_set_priority(int);
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
	static char __attribute__((unused)) __file_version[] = file ", " x;
#else /* LOW_MEMORY */
#  define CALLWEAVER_FILE_VERSION(file, x)
#endif

#if defined(__CW_DEBUG_MALLOC)  &&  !defined(_CALLWEAVER_CALLWEAVER_MM_H)
#include "callweaver/callweaver_mm.h"
#endif

#endif
