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
 * \brief CallWeaver module definitions.
 *
 * This file contains the definitons for functions CallWeaver modules should
 * provide and some other module related functions.
 */

#ifndef _CALLWEAVER_MODULE_H
#define _CALLWEAVER_MODULE_H


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/lock.h"

struct module;


#define OPBX_MODULE_CONFIG "modules.conf" /*!< \brief Module configuration file */


/*!
 * \brief Get a reference to a module
 */
extern struct module *opbx_module_get(struct module *mod);

/*!
 * \brief Put a reference to a module
 */
extern void opbx_module_put(struct module *mod);

/*! 
 * \brief Load a module.
 * \param resource_name The filename of the module to load.
 *
 * This function is run by the PBX to load the modules.  It performs
 * all loading and initilization tasks.   Basically, to load a module, just
 * give it the name of the module and it will do the rest.
 *
 * \return Zero on success, -1 on error.
 */
int opbx_load_resource(const char *resource_name);

/*! 
 * \brief Unloads a module.
 * \param resource_name The name of the module to unload.
 * \param hangup        If true any channels using or depending on the module
 *                      will be hung up.
 *
 * Deregister the module's functionality so that it will be unloaded as soon
 * as it is no longer in use. Optionally hangup channels which are using or
 * depending on the module's presence. The module will not be unloaded as
 * long as channels are using it.
 *
 * \return Zero on success, -1 on error.
 */
int opbx_unload_resource(const char *resource_name, int hangup);


int opbx_loader_init(void);

int opbx_loader_exit(void);

/*! 
 * \brief Reload callweaver modules.
 * \param name the name of the module to reload
 *
 * This function reloads the specified module, or if no modules are specified,
 * it will reload all loaded modules.
 *
 * \note Modules are reloaded using their reload() functions, not unloading
 * them and loading them again.
 *
 * \return Zero if the specified module was not found, 1 if the module was
 * found but cannot be reloaded, -1 if a reload operation is already in
 * progress, and 2 if the specfied module was found and reloaded.
 */
int opbx_module_reload(const char *name);

/*! 
 * \brief Match modules names for the CallWeaver cli.
 * \param line Unused by this function, but this should be the line we are
 *        matching.
 * \param wordstem The partial name to match. 
 * \param pos The position the word we are completing is in.
 * \param state The possible match to return.
 * \param rpos The position we should be matching.  This should be the same as
 *        pos.
 * \param needsreload This should be 1 if we need to reload this module and 0
 *        otherwise.  This function will only return modules that are reloadble
 *        if this is 1.
 *
 * \return A possible completion of the partial match, or NULL if no matches
 * were found.
 */
char *opbx_module_helper(char *line, char *wordstem, int pos, int state, int rpos, int needsreload);


struct localuser;

struct modinfo {
	struct module *self;

	/*!
	 * \brief Initialize the module.
	 * 
	 * This function is called at module load time.  Put all code in here
	 * that needs to set up your module's hardware, software, registrations,
	 * etc.
	 *
	 * \return This function should return 0 on success and non-zero on failure.
	 */
	int (*init)(void);

	/*!
	 * \brief Reload the module's config
	 *
	 * This is called when the module is requested to reload its configuration.
	 * Note that this may be called while the module is in use and it is up
	 * to the module itself to decide what changes may be allowed to any
	 * particular moment.
	 */
	int (*reconfig)(void);

	/*! 
	 * \brief Deregister the module's services
	 *
	 * This is called when the module is _requested_ to be removed.  The module
	 * may not _actually_ be removed until later (and then only after a call to
	 * the release function, if any, below) if there are current references to it.
	 * Any registrations should be removed here. This will prevent anything else
	 * making use of this module however current users are permitted to continue
	 * until finished.
	 *
	 * \return Zero on success, or non-zero on error.
	 */
	int (*deregister)(void);

	/*! 
	 * \brief Cleanup all module structures, sockets, etc.
	 *
	 * This is called at just before the module is removed from the process.
	 * Memory allocations need to be freed and descriptors closed here.  Nothing else
	 * will do this for you!
	 *
	 * \return nothing
	 */
	void (*release)(void);

	/*! \brief Provides a description of the module. */
	const char *description;

	opbx_mutex_t localuser_lock;
	struct localuser *localusers;
	int localusecnt;
};

extern struct modinfo *get_modinfo(void);

#define MODULE_INFO(module_init, module_reconfig, module_deregister, module_release, module_description) \
	static __attribute__((unused)) struct modinfo __modinfo = { \
		.init = module_init, \
		.reconfig = module_reconfig, \
		.deregister = module_deregister, \
		.release = module_release, \
		.description = module_description, \
	}; \
	__attribute__((visibility("protected"))) struct modinfo *get_modinfo(void) \
		{ return &__modinfo; }


/* Local user routines keep track of which channels are using a given module
   resource.  They can help make removing modules safer, particularly if
   they're in use at the time they have been requested to be removed */

struct localuser {
	struct opbx_channel *chan;
	struct localuser *next;
};


/*! 
 * \brief Add a channel as a user of a module.
 * \param modinfo the modinfo of the module to add the user against
 * \param chan the channel that will be using the module
 * \param u pointer to a localuser struct to use
 *
 * Adds a channel to the list of users and increments the usecount.
 *
 * \return nothing
 */
static inline void opbx_module_user_add(struct modinfo *modinfo, struct opbx_channel *chan, struct localuser *u) {
	opbx_mutex_lock(&modinfo->localuser_lock);
	u->chan = chan;
	u->next = modinfo->localusers;
	modinfo->localusers = u;
	modinfo->localusecnt++;
	opbx_mutex_unlock(&modinfo->localuser_lock);
}
/*!
 * [DEPRECATED]
 * This macro adds a localuser to the list of users and increments the
 * usecount.  It expects a variable named \p chan of type \p opbx_channel in the
 * current scope.
 *
 * \note This function dynamically allocates memory.  If this operation fails
 * it will cause your function to return -1 to the caller.
 */
#define LOCAL_USER_ADD(u) { \
	if (!(u = malloc(sizeof(*u)))) { \
		opbx_log(OPBX_LOG_WARNING, "Out of memory\n"); \
		return -1; \
	} \
	opbx_module_user_add(get_modinfo(), chan, u); \
}

/*! 
 * \brief Remove a channel from a module's list of users.
 * \param modinfo the modinfo of the module to remove the user from
 * \param u pointer to the user's localuser struct
 *
 * Removes a channel from the list of users and decrements the
 * usecount.
 */
static inline void opbx_module_user_del(struct modinfo *modinfo, struct localuser *u) {
	struct localuser **uc;
	opbx_mutex_lock(&modinfo->localuser_lock);
	for (uc = &modinfo->localusers; *uc; uc = &(*uc)->next) {
		if (*uc == u) {
			*uc = (*uc)->next;
			break;
		}
	}
	modinfo->localusecnt--;
	opbx_mutex_unlock(&modinfo->localuser_lock);
}
/*!
 * [DEPRECATED]
 * This macro removes a localuser from the list of users and decrements the
 * usecount.
 */
#define LOCAL_USER_REMOVE(u) { \
	opbx_module_user_del(get_modinfo(), u); \
	free(u); \
}


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_MODULE_H */
