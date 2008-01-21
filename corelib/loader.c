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
 * \brief Module Loader
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/atomic.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"
#include "callweaver/cli.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/manager.h"
#include "callweaver/cdr.h"
#include "callweaver/enum.h"
#include "callweaver/lock.h"
#include "callweaver/rtp.h"
#include "callweaver/utils.h"

#include "libltdl/ltdl.h"

/* For rl_filename_completion */
#include <readline/readline.h>


static struct modinfo core_modinfo = {
	.self = NULL,
};

struct modinfo *get_modinfo(void)
{
	return &core_modinfo;
}


struct module {
	atomic_t refs;
	struct modinfo *modinfo;
	void *lib;
	struct module *next;
	char resource[0];
};

OPBX_MUTEX_DEFINE_STATIC(module_lock);
static struct module *module_list = NULL;
static int modlistver = 0;

OPBX_MUTEX_DEFINE_STATIC(reloadlock);


struct module *opbx_module_get(struct module *mod)
{
	if (mod)
		atomic_inc(&mod->refs);
	return mod;
}

static void opbx_module_release(struct module *mod)
{
	struct module **m;
	int n;

	/* If it is still in the module list it's possible that
	 * someone else can grab a reference before we removed it.
	 * If that happens their put will do the close and free
	 * rather than ours. We just need to recheck the ref count
	 * _while_holding_the_module_list_lock_ to know if we
	 * are responsible or not.
	 */
	n = 0;
	opbx_mutex_lock(&module_lock);
	for (m = &module_list; *m; m = &(*m)->next) {
		if (*m == mod) {
			if (atomic_read(&mod->refs)) {
				opbx_mutex_unlock(&module_lock);
				return;
			}

			*m = mod->next;

			if (mod->modinfo && mod->modinfo->release)
				mod->modinfo->release();

			opbx_mutex_destroy(&mod->modinfo->localuser_lock);
			atomic_destroy(&mod->refs);
			lt_dlclose(mod->lib);
			opbx_mutex_unlock(&module_lock);
			free(mod);
			if (option_verbose)
				opbx_verbose(VERBOSE_PREFIX_1 "Module %s closed and unloaded\n", mod->resource);
			break;
		}
	}
}

void opbx_module_put(struct module *mod)
{
	if (mod && atomic_dec_and_test(&mod->refs))
		opbx_module_release(mod);
}


int opbx_unload_resource(const char *resource_name, int hangup)
{
	struct module *mod;
	int res = -1;

	opbx_mutex_lock(&module_lock);

	for (mod = module_list; mod; mod = mod->next) {
		if (!strcasecmp(mod->resource, resource_name)) {
			opbx_module_get(mod);
			res = mod->modinfo->deregister();
			opbx_mutex_unlock(&module_lock);
			if (hangup) {
				struct localuser *u;
				opbx_mutex_lock(&mod->modinfo->localuser_lock);
				for (u = mod->modinfo->localusers; u; u = u->next)
					opbx_softhangup(u->chan, OPBX_SOFTHANGUP_APPUNLOAD);
				opbx_mutex_unlock(&mod->modinfo->localuser_lock);
			}
			opbx_module_put(mod);
			return 0;
		}
	}

	opbx_mutex_unlock(&module_lock);
	return -1;
}

static char *module_generator(char *line, char *word, int pos, int state)
{
	struct module *m;
	int which = 0;
	char *ret = NULL;

	opbx_mutex_lock(&module_lock);

	m = module_list;
	while (m) {
		if (!strncasecmp(word, m->resource, strlen(word))) {
			if (++which > state) {
				ret = strdup(m->resource);
				break;
			}
		}
		m = m->next;
	}

	opbx_mutex_unlock(&module_lock);
	return ret;
}

int opbx_module_reload(const char *name)
{
	struct module *m;
	int reloaded = 0;
	int oldversion;
	int (*reload)(void);
	/* We'll do the logger and manager the favor of calling its reload here first */

	if (opbx_mutex_trylock(&reloadlock)) {
		opbx_verbose("The previous reload command didn't finish yet\n");
		return -1;
	}
	if (!name || !strcasecmp(name, "extconfig")) {
		read_config_maps();
		reloaded = 2;
	}
	if (!name || !strcasecmp(name, "cdr")) {
		opbx_cdr_engine_reload();
		reloaded = 2;
	}
	if (!name || !strcasecmp(name, "enum")) {
		opbx_enum_reload();
		reloaded = 2;
	}
	if (!name || !strcasecmp(name, "rtp")) {
		opbx_rtp_reload();
		reloaded = 2;
	}
	time(&opbx_lastreloadtime);

	opbx_mutex_lock(&module_lock);
	oldversion = modlistver;
	m = module_list;
	while(m) {
		if (!name || !strcasecmp(name, m->resource)) {
			if (reloaded < 1)
				reloaded = 1;
			reload = m->modinfo->reconfig;
			opbx_mutex_unlock(&module_lock);
			if (reload) {
				reloaded = 2;
				if (option_verbose > 2) 
					opbx_verbose(VERBOSE_PREFIX_3 "Reloading module '%s' (%s)\n", m->resource, m->modinfo->description);
				reload();
			}
			opbx_mutex_lock(&module_lock);
			if (oldversion != modlistver)
				break;
		}
		m = m->next;
	}
	opbx_mutex_unlock(&module_lock);
	opbx_mutex_unlock(&reloadlock);
	return reloaded;
}

int opbx_load_resource(const char *resource_name)
{
	struct module *newmod, *mod, **m;
	struct modinfo *(*modinfo)(void);
	int res;

	res = strlen(resource_name) + 1;

	if (!(newmod = mod = malloc(sizeof(struct module) + res))) {
		opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
		return -1;
	}

	memcpy(mod->resource, resource_name, res);

	opbx_mutex_lock(&module_lock);

	mod->lib = lt_dlopen(resource_name);
	if (!mod->lib) {
		opbx_mutex_unlock(&module_lock);
		opbx_log(OPBX_LOG_ERROR, "%s\n", lt_dlerror());
		free(mod);
		return -1;
	}

	modinfo = lt_dlsym(mod->lib, "get_modinfo");
	if (modinfo == NULL)
		modinfo = lt_dlsym(mod->lib, "_get_modinfo");
	if (modinfo == NULL) {
		lt_dlclose(mod->lib);
		opbx_mutex_unlock(&module_lock);
		opbx_log(OPBX_LOG_ERROR, "No get_modinfo in module %s\n", resource_name);
		free(mod);
		return -1;
	}

	mod->modinfo = (*modinfo)();

	/* Add to the modules list in alphabetic order
	 * This used to append to the list so that "reloads will be issued in the
	 * same order modules were loaded" but that makes no sense since modules
	 * can be unloaded and loaded dynamically
	 */
	res = -1;
	for (m = &module_list; *m; m = &(*m)->next) {
		res = strcasecmp(resource_name, (*m)->resource);
		if (res <= 0)
			break;
	}
	if (res) {
		/* Start with one ref - that's us */
		atomic_set(&mod->refs, 1);
		mod->modinfo->self = mod;
		opbx_mutex_init(&mod->modinfo->localuser_lock);
		mod->next = *m;
		*m = mod;
	} else {
		lt_dlclose(mod->lib);
		free(mod);
		mod = opbx_module_get(*m);
	}

	/* The init has to happen within the lock otherwise we have races
	 * between simultaneous loads/unloads of the same module
	 */
	res = mod->modinfo->init();

	modlistver++;
	opbx_mutex_unlock(&module_lock);

	if (!fully_booted) {
		if (option_verbose) {
			opbx_verbose("[%s] => (%s)\n", resource_name, mod->modinfo->description);
		} else if (option_console || option_nofork)
			opbx_verbose( ".");
	} else {
		if (option_verbose)
			opbx_verbose(VERBOSE_PREFIX_1 "%s %s => (%s)\n", (mod == newmod ? "Loaded" : "Reregistered"), resource_name, mod->modinfo->description);
	}

	if (res) {
		opbx_log(OPBX_LOG_WARNING, "%s: register failed, returned %d\n", resource_name, res);
		opbx_unload_resource(resource_name, 0);
		return -1;
	}

	/* Drop our reference. If the module didn't register any capabilities
	 * this will be the last reference so the module will be removed.
	 * Otherwise there has to be one or more unregisters plus everything
	 * with a reference has to release it.
	 */
	opbx_module_put(mod);
	return 0;
}

static int opbx_resource_exists(const char *resource)
{
	struct module *m;
	if (opbx_mutex_lock(&module_lock))
		opbx_log(OPBX_LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(resource, m->resource))
			break;
		m = m->next;
	}
	opbx_mutex_unlock(&module_lock);
	if (m)
		return -1;
	else
		return 0;
}

static const char *loadorder[] =
{
	"res_",
	"chan_",
	"pbx_",
	NULL,
};

struct load_modules_one_args {
	struct opbx_config *cfg;
	int prefix;
	int prefix_len;
};

static int load_modules_one(const char *filename, lt_ptr data)
{
	char soname[256+1];
	struct load_modules_one_args *args = (struct load_modules_one_args *)data;
	char *basename;

	/* Sadly the names produced by lt_dlforeachfile cannot be used as
	 * arguments to lt_dlopen so we have to fix them up.
	 * We want: basenames that start with the given prefix, with ".so" added
	 * and which are not already loaded
	 */
	if ((basename = strrchr(filename, '/')) && (basename++, 1)
	&& (!loadorder[args->prefix] || !strncasecmp(basename, loadorder[args->prefix], args->prefix_len))
	&& (snprintf(soname, sizeof(soname), "%s.so", basename), !opbx_resource_exists(soname))) {
		struct opbx_variable *v;

		/* It's a shared library -- Just be sure we're allowed to load it -- kinda
		   an inefficient way to do it, but oh well. */
		v = NULL;
		if (args->cfg) {
			for (v = opbx_variable_browse(args->cfg, "modules");
				v && (strcasecmp(v->name, "noload") || strcasecmp(v->value, soname));
				v = v->next);
			if (option_verbose && v)
				opbx_verbose( VERBOSE_PREFIX_1 "[skipping %s]\n", basename);
		}
		if (!v)
			opbx_load_resource(soname);
	}

	return 0;
}

int load_modules(const int preload_only)
{
	struct opbx_config *cfg;
	struct opbx_variable *v;

	if (option_verbose) {
		if (preload_only)
			opbx_verbose("CallWeaver Dynamic Loader loading preload modules:\n");
		else
			opbx_verbose("CallWeaver Dynamic Loader Starting:\n");
	}

	cfg = opbx_config_load(OPBX_MODULE_CONFIG);
	if (cfg) {
		int doload;

		/* Load explicitly defined modules */
		for (v = opbx_variable_browse(cfg, "modules"); v; v = v->next) {
			doload = 0;

			if (preload_only)
				doload = !strcasecmp(v->name, "preload");
			else
				doload = !strcasecmp(v->name, "load");

		       if (doload) {
				if (option_debug && !option_verbose)
					opbx_log(OPBX_LOG_DEBUG, "Loading module %s\n", v->value);
				if (opbx_load_resource(v->value)) {
					opbx_log(OPBX_LOG_WARNING, "Loading module %s failed!\n", v->value);
					opbx_config_destroy(cfg);
					return -1;
				}
			}
		}
	}

	if (preload_only) {
		opbx_config_destroy(cfg);
		return 0;
	}

	if (!cfg || opbx_true(opbx_variable_retrieve(cfg, "modules", "autoload"))) {
		struct load_modules_one_args args;
		args.cfg = cfg;
		for (args.prefix = 0; args.prefix < arraysize(loadorder); args.prefix++) {
			if (loadorder[args.prefix])
				args.prefix_len = strlen(loadorder[args.prefix]);
			lt_dlforeachfile(lt_dlgetsearchpath(), load_modules_one, &args);
		}
	} 
	opbx_config_destroy(cfg);
	return 0;
}


static int handle_modlist(int fd, int argc, char *argv[])
{
	char *like = NULL;
	struct module *m;
	int count;

	if (argc == 3)
		return RESULT_SHOWUSAGE;
	else if (argc >= 4) {
		if (strcmp(argv[2], "like")) 
			return RESULT_SHOWUSAGE;
		like = argv[3];
	}

	opbx_cli(fd, "%-30s %-40.40s %10s %10s\n", "Module", "Description", "Refs", "Chan Usage");

	opbx_mutex_trylock(&module_lock);

	count = 0;
	for (m = module_list; m; m = m->next) {
		if (!like || strcasestr(m->resource, like) ) {
			count++;
			opbx_cli(fd, "%-30s %-40.40s %10d %10d\n",
				m->resource, m->modinfo->description, atomic_read(&m->refs), m->modinfo->localusecnt);
		}
	}
	opbx_cli(fd, "%d modules loaded\n", count);

	opbx_mutex_unlock(&module_lock);
	return RESULT_SUCCESS;
}


static int handle_load(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;

	if (opbx_load_resource(argv[1])) {
		opbx_cli(fd, "Unable to load module %s\n", argv[1]);
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}


struct complete_fn_args {
	const char *word;
	char *ret;
	int wordlen;
	int state;
};

static int complete_fn_one(const char *filename, lt_ptr data)
{
	struct complete_fn_args *args = (struct complete_fn_args *)data;
	char *basename;

	if ((basename = strrchr(filename, '/')) && (basename++, 1)
	&& !strncmp(basename, args->word, args->wordlen)) {
		if (!args->state) {
			int l = strlen(basename);
			if ((args->ret = malloc(l + 3 + 1))) {
				memcpy(args->ret, basename, l);
				memcpy(args->ret + l, ".so\000", 4);
				return 1;
			}
			opbx_log(OPBX_LOG_ERROR, "Out of memory!\n");
			return 1;
		}
		args->state--;
	}
	return 0;
}

static char *complete_fn(char *line, char *word, int pos, int state)
{
	char filename[256];
	struct complete_fn_args args = {
		.ret = NULL,
	};

	if (pos == 1) {
		if (word[0] == '/') {
			opbx_copy_string(filename, word, sizeof(filename));
			args.ret = (char*)rl_filename_completion_function(filename, state);
			if (args.ret)
				args.ret = strdup(args.ret);
		} else {
			args.state = state;
			args.word = word;
			args.wordlen = strlen(word);
			lt_dlforeachfile(lt_dlgetsearchpath(), complete_fn_one, &args);
		}
	}
	return args.ret;
}


static int handle_reload(int fd, int argc, char *argv[])
{
	int x;

	if (argc < 1)
		return RESULT_SHOWUSAGE;

	if (argc > 1) { 
		for (x = 1; x < argc; x++) {
			int res = opbx_module_reload(argv[x]);
			switch (res) {
			case 0:
				opbx_cli(fd, "No such module '%s'\n", argv[x]);
				break;
			case 1:
				opbx_cli(fd, "Module '%s' does not support reload\n", argv[x]);
				break;
			}
		}
	} else
		opbx_module_reload(NULL);

	return RESULT_SUCCESS;
}


static char *reload_module_generator(char *line, char *word, int pos, int state)
{
	static char *core[] = {
		"extconfig",
		"manager",
		"enum",
		"rtp",
	};
	char *ret = NULL;
	int i, l;

	l = strlen(word);

	for (i = 0; i < arraysize(core); i++) {
		if (!strncasecmp(word, core[i], l)) {
			if (state-- == 0) {
				ret = strdup(core[i]);
				break;
			}
		}
	}

	if (!ret)
		ret = module_generator(line, word, pos, state - arraysize(core));

	return ret;
}


static int handle_unload(int fd, int argc, char *argv[])
{
	int x;
	int hangup = 0;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	for (x = 1; x < argc; x++) {
		if (argv[x][0] == '-') {
			switch (argv[x][1]) {
			case 'h':
				hangup = 1;
				break;
			default:
				return RESULT_SHOWUSAGE;
			}
		} else if (x !=  argc - 1) 
			return RESULT_SHOWUSAGE;
		else if (opbx_unload_resource(argv[x], hangup)) {
			opbx_cli(fd, "Unable to unload resource %s\n", argv[x]);
			return RESULT_FAILURE;
		}
	}

	return RESULT_SUCCESS;
}


static char modlist_help[] =
"Usage: show modules [like keyword]\n"
"       Shows CallWeaver modules currently in use, and usage statistics.\n";

static char load_help[] = 
"Usage: load <module name>\n"
"       Loads the specified module into CallWeaver.\n"
"       If the module is already present but deregistered (see unload)\n"
"       its functionality will simply be reregistered. Note that since\n"
"       the module has not actually been unloaded and reloaded this\n"
"       may mean that internal state is NOT reset.\n";

static char reload_help[] = 
"Usage: reload [module ...]\n"
"       Reloads configuration files for all listed modules which support\n"
"       reloading, or for all supported modules if none are listed.\n";

static char unload_help[] = 
"Usage: unload [-h] <module name>\n"
"       Deregisters the functionality provided by the specified\n"
"       module from CallWeaver so that it will be removed as soon\n"
"       as it is no longer in use.\n"
"       The -h option requests that channels that are currently\n"
"       using or dependent on the module be hung up.\n"
"       It is always safe to call unload multiple times on a module.\n";


static struct opbx_clicmd clicmds[] = {
	{
		.cmda = { "show", "modules", NULL },
		.handler = handle_modlist,
		.summary = "List modules and info",
		.usage = modlist_help,
	},
	{
		.cmda = { "show", "modules", "like", NULL },
		.handler = handle_modlist,
		.generator = module_generator,
		.summary = "List modules and info",
		.usage = modlist_help,
	},
	{
		.cmda = { "load", NULL },
		.handler = handle_load,
		.generator = complete_fn,
		.summary = "Load a dynamic module by name",
		.usage = load_help,
	},
	{
		.cmda = { "reload", NULL },
		.handler = handle_reload,
		.generator = reload_module_generator,
		.summary = "Reload configuration",
		.usage = reload_help,
	},
	{
		.cmda = { "unload", NULL },
		.handler = handle_unload,
		.generator = module_generator,
		.summary = "Unload a dynamic module by name",
		.usage = unload_help,
	},
};


OPBX_MUTEX_DEFINE_STATIC(loader_mutex);
static pthread_key_t loader_err_key;
struct loader_err {
	int have_err;
	char err[251];
};

static void loader_lock(void)
{
	opbx_mutex_lock(&loader_mutex);
}

static void loader_unlock(void)
{
	opbx_mutex_unlock(&loader_mutex);
}

static void loader_seterr(const char *err)
{
	struct loader_err *local = pthread_getspecific(loader_err_key);
	if (local) {
		if ((local->have_err = (err ? 1 : 0))) {
			strncpy(local->err, err, sizeof(local->err) - 1);
			local->err[sizeof(local->err) - 1] = '\0';
		}
	}
}

static const char *loader_geterr(void)
{
	struct loader_err *local = pthread_getspecific(loader_err_key);

	if (!local || !local->have_err)
		return NULL;
	local->have_err = 0;
	return local->err;
}


void opbx_loader_init(void)
{
	if (pthread_key_create(&loader_err_key, &free)) {
		perror("pthread_key_create");
		exit(1);
	}
	pthread_setspecific(loader_err_key, calloc(1, sizeof(struct loader_err)));

	lt_dlinit();
	lt_dlmutex_register(loader_lock, loader_unlock, loader_seterr, loader_geterr);
}


int opbx_loader_cli_init(void)
{
	opbx_cli_register_multiple(clicmds, arraysize(clicmds));
	return 0;
}
