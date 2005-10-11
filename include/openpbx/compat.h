/*
 * OpenPBX -- A telephony toolkit for Linux.
 *
 * General Definitions for OpenPBX top level program
 * 
 * Copyright (C) 1999-2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _COMPAT_H
#define _COMPAT_H

#ifdef SOLARIS
#define __BEGIN_DECLS
#define __END_DECLS

#ifndef __P
#define __P(p) p
#endif

#include "openpbx/confdefs.h"

char* strsep(char** str, const char* delims);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
#endif /* SOLARIS */

#ifdef __CYGWIN__
#define _WIN32_WINNT 0x0500
#endif /* __CYGWIN__ */

#ifdef __CYGWIN__
typedef unsigned long long uint64_t;
#endif

#endif
