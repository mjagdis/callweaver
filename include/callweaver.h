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

#define OPBX_CONFIG_MAX_PATH 255

#ifndef RELEASE_TARBALL
#define OPBX_VERSION_INFO PACKAGE_STRING " SVN-" SVN_VERSION " built on " BUILD_HOSTNAME \
        ",  a " BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE
#else
#define OPBX_VERSION_INFO PACKAGE_STRING " built on " BUILD_HOSTNAME \
        ",  a " BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE
#endif


/* provided in callweaver.c */
extern int callweaver_main(int argc, char *argv[]);
extern char opbx_config_OPBX_CONFIG_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_CONFIG_FILE[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_SPOOL_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_MONITOR_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_VAR_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_LOG_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_OGI_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_DB[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_DB_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_KEY_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_PID[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_SOCKET[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_RUN_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_CTL_PERMISSIONS[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_CTL_OWNER[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_CTL_GROUP[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_CTL[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_SYSTEM_NAME[20];
extern char opbx_config_OPBX_SOUNDS_DIR[OPBX_CONFIG_MAX_PATH];
extern char opbx_config_OPBX_ENABLE_UNSAFE_UNLOAD[20];

/* Provided by callweaver.c */
extern int opbx_set_priority(int);
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
/* Provided by term.c */
extern int term_init(void);
/* Provided by db.c */
extern int opbxdb_init(void);
/* Provided by channel.c */
extern void opbx_channels_init(void);
/* Provided by dnsmgr.c */
extern int dnsmgr_init(void);
extern void dnsmgr_reload(void);


/*!
 * \brief Register/unregister a source code file with the core.
 * \param file the source file name
 * \param version the version string (typically a CVS revision keyword string)
 *
 * This macro will place a file-scope constructor and destructor into the
 * source of the module using it; this will cause the version of this file
 * to registered with the CallWeaver core (and unregistered) at the appropriate
 * times.
 *
 * Example:
 *
 * \code
 * CALLWEAVER_FILE_VERSION("\$HeadURL\$", "\$Revision\$")
 * \endcode
 *
 * \note The dollar signs above have been protected with backslashes to keep
 * CVS from modifying them in this file; under normal circumstances they would
 * not be present and CVS would expand the Revision keyword into the file's
 * revision number.
 */
#if defined(__GNUC__) && !defined(LOW_MEMORY)

#  include "callweaver/object.h"
#  include "callweaver/registry.h"
	struct opbx_file_version {
		struct opbx_object obj;
		struct opbx_registry_entry *reg_entry;
		char *file;
		char *version;
	};

	extern struct opbx_registry file_version_registry;

#  define CALLWEAVER_FILE_VERSION(scm_file, scm_version) \
	static struct opbx_file_version __file_version = { \
		.file = (scm_file), \
		.version = (scm_version), \
	}; \
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		opbx_object_init_obj(&__file_version.obj, NULL, 0); \
		__file_version.reg_entry = opbx_registry_add(&file_version_registry, &__file_version.obj); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		opbx_registry_del(&file_version_registry, __file_version.reg_entry); \
	}

#elif !defined(LOW_MEMORY) /* ! __GNUC__  && ! LOW_MEMORY*/
#  define CALLWEAVER_FILE_VERSION(file, x) static const char __file_version[] = x;
#else /* LOW_MEMORY */
#  define CALLWEAVER_FILE_VERSION(file, x)
#endif /* __GNUC__ */

#if defined(__OPBX_DEBUG_MALLOC)  &&  !defined(_CALLWEAVER_CALLWEAVER_MM_H)
#include "callweaver/callweaver_mm.h"
#endif

#endif
