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
 * \brief Options provided by main callweaver program
 */

#ifndef _CALLWEAVER_OPTIONS_H
#define _CALLWEAVER_OPTIONS_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/param.h>
#include <time.h>

#define CW_CACHE_DIR_LEN 512
#define CW_FILENAME_MAX	80

#ifdef __linux__
/* Opened by callweaver.c before privs are dropped */
extern int cw_cpu0_governor_fd;
#endif

extern CW_API_PUBLIC char hostname[MAXHOSTNAMELEN];
extern CW_API_PUBLIC int option_verbose;
extern CW_API_PUBLIC int option_debug;
extern CW_API_PUBLIC int option_nofork;
extern CW_API_PUBLIC int option_quiet;
extern CW_API_PUBLIC int option_console;
extern CW_API_PUBLIC int option_initcrypto;
extern CW_API_PUBLIC int option_nocolor;
extern CW_API_PUBLIC int option_remote;
extern CW_API_PUBLIC int option_reconnect;
extern CW_API_PUBLIC int fully_booted;
extern CW_API_PUBLIC int option_exec_includes;
extern CW_API_PUBLIC int option_cache_record_files;
extern CW_API_PUBLIC int option_transcode_slin;
extern CW_API_PUBLIC int option_maxcalls;
extern CW_API_PUBLIC double option_maxload;
extern CW_API_PUBLIC int option_dontwarn;
extern CW_API_PUBLIC int option_priority_jumping;
extern CW_API_PUBLIC int option_enableunsafeunload;
extern CW_API_PUBLIC char defaultlanguage[];
extern CW_API_PUBLIC struct timespec cw_startuptime;
extern CW_API_PUBLIC struct timespec cw_lastreloadtime;
extern CW_API_PUBLIC int cw_mainpid;
extern CW_API_PUBLIC char record_cache_dir[CW_CACHE_DIR_LEN];
extern CW_API_PUBLIC char debug_filename[CW_FILENAME_MAX];

#define VERBOSE_PREFIX_1 " "
#define VERBOSE_PREFIX_2 "  == "
#define VERBOSE_PREFIX_3 "    -- "
#define VERBOSE_PREFIX_4 "       > "  

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_OPTIONS_H */
