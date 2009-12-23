/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/atomic.h"
#include "callweaver/object.h"
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
#include "callweaver/features.h"
#include "callweaver/lock.h"
#include "callweaver/rtp.h"
#include "callweaver/utils.h"

#include "libltdl/ltdl.h"

/* For rl_filename_completion */
#include <readline/readline.h>


#define MODINFO_STATE_UNINITIALIZED	0
#define MODINFO_STATE_ACTIVE		1
#define MODINFO_STATE_UNMAP_ON_IDLE	2


/* We guarantee modules that their register, deregister and reconfigure functions
 * are serialized both with respect to themselves and to other modules. This lets
 * them to, for instance, do lock init/deinit without first requiring a lock to
 * serialize them.
 */
static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;


CW_MUTEX_DEFINE_STATIC(loader_mutex);
static pthread_key_t loader_err_key;

struct loader_err {
	char err[251];
};


MODULE_INFO(NULL, NULL, NULL, NULL, "Callweaver core")


static int cw_module_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_module *module_a = container_of(*objp_a, struct cw_module, obj);
	const struct cw_module *module_b = container_of(*objp_b, struct cw_module, obj);

	return strcmp(module_a->name, module_b->name);
}

static int module_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_module *mod = container_of(obj, struct cw_module, obj);

	return !strcmp(mod->name, pattern);
}

struct cw_registry module_registry = {
	.name = "Modules",
	.qsort_compare = cw_module_qsort_compare_by_name,
	.match = module_object_match,
};


static void module_release(struct cw_object *obj)
{
	struct cw_module *mod = container_of(obj, struct cw_module, obj);

	if (mod->modinfo) {
		if (mod->modinfo->state == MODINFO_STATE_UNMAP_ON_IDLE) {
			/* This was an explicit removal rather than a replacement by
			 * a load of a newer module so we need to log it.
			 */
			if (option_verbose)
				cw_verbose(VERBOSE_PREFIX_1 "Unloaded %s => (%s)\n", mod->name, mod->modinfo->description);
			cw_log(CW_LOG_NOTICE, "Unloaded %s => (%s)\n", mod->name, mod->modinfo->description);
		}

		if (mod->modinfo->release)
			mod->modinfo->release();

		cw_mutex_destroy(&mod->modinfo->localuser_lock);
	}

	if (mod->lib) {
		/* If the module exposes symbols that have since been linked
		 * from other objects it won't actually be unmapped and a
		 * subsequent load might _say_ it was loaded but it will
		 * actually be a resurrection of the existing module and
		 * statically initialised data won't be reset.
		 * Now we could futz around taking a copy of the data segment
		 * and restoring it on load but if the module doesn't get
		 * unmapped here there must be a reason why and that reason
		 * might mean the module is still in use and that trashing
		 * its data might be unexpected, unwelcome and fatal.
		 */
		lt_dlclose(mod->lib);
	}

	cw_object_destroy(mod);
	free(mod);
}


static int unload_module(const char *name, int hangup)
{
	struct cw_object *obj;
	int res = -1;

	pthread_mutex_lock(&modlock);

	if ((obj = cw_registry_find(&module_registry, 1, cw_hash_string(name), name))) {
		struct cw_module *mod = container_of(obj, struct cw_module, obj);

		if (option_verbose)
			cw_verbose(VERBOSE_PREFIX_1 "Deregistering %s => (%s)\n", mod->name, mod->modinfo->description);

		if (!(res = mod->modinfo->deregister())) {
			mod->modinfo->state = MODINFO_STATE_UNMAP_ON_IDLE;
			cw_registry_del(&module_registry, mod->reg_entry);

			if (hangup) {
				struct localuser *u;

				cw_mutex_lock(&mod->modinfo->localuser_lock);
				for (u = mod->modinfo->localusers; u; u = u->next)
					cw_softhangup(u->chan, CW_SOFTHANGUP_APPUNLOAD);
				cw_mutex_unlock(&mod->modinfo->localuser_lock);
			}

			res = 0;
		}

		cw_object_put(mod);
	}

	pthread_mutex_unlock(&modlock);

	return res;
}


struct module_generator_args {
	struct cw_dynstr **ds_p;
	const char *name;
	int name_len;
};

static int module_generator_one(struct cw_object *obj, void *data)
{
	struct cw_module *mod = container_of(obj, struct cw_module, obj);
	struct module_generator_args *args = data;

	if (!strncmp(args->name, mod->name, args->name_len))
		cw_dynstr_printf(args->ds_p, "%s\n", mod->name);

	return 0;
}

static void module_generator(struct cw_dynstr **ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct module_generator_args args = {
		.ds_p = ds_p,
		.name = argv[lastarg],
		.name_len = lastarg_len,
	};

	cw_registry_iterate(&module_registry, module_generator_one, &args);
}


struct module_reload_args {
	const char *name;
	int reloaded;
};

static int module_reconfigure(struct cw_object *obj, void *data)
{
	struct cw_module *mod = container_of(obj, struct cw_module, obj);
	struct module_reload_args *args = data;

	if (!args->name || !strcmp(args->name, mod->name)) {
		if (args->reloaded < 1)
			args->reloaded = 1;

		if (mod->modinfo->reconfig) {
			args->reloaded = 2;
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Reloading module '%s' (%s)\n", mod->name, mod->modinfo->description);
			mod->modinfo->reconfig();
		}
	}

	return 0;
}

int cw_module_reconfigure(const char *name)
{
	struct module_reload_args args = {
		.name = name,
		.reloaded = 0,
	};

	/* We'll do the logger and manager the favor of calling its reload here first */

	pthread_mutex_lock(&modlock);

	time(&cw_lastreloadtime);

	if (!name || !strcasecmp(name, "manager")) {
		manager_reload();
		args.reloaded = 2;
	}
	if (!name || !strcasecmp(name, "extconfig")) {
		read_config_maps();
		args.reloaded = 2;
	}
	if (!name || !strcasecmp(name, "cdr")) {
		cw_cdr_engine_reload();
		args.reloaded = 2;
	}
	if (!name || !strcasecmp(name, "enum")) {
		cw_enum_reload();
		args.reloaded = 2;
	}
	if (!name || !strcasecmp(name, "features")) {
		cw_features_reload();
		args.reloaded = 2;
	}
	if (!name || !strcasecmp(name, "rtp")) {
		cw_rtp_reload();
		args.reloaded = 2;
	}

	cw_registry_iterate(&module_registry, module_reconfigure, &args);

	pthread_mutex_unlock(&modlock);

	return args.reloaded;
}

static int module_load(const char *filename)
{
	struct cw_module *mod, *oldmod;
	struct cw_object *oldobj;
	struct modinfo *(*modinfo)(void);
	const char *p;
	unsigned int hash;
	int res;

#if 0
	/* This MUST be ok since we're called via an lt_dlforeach() search */
	if (*filename != '/')
		return -1;
#endif

	p = strrchr(filename, '/') + 1;
	res = strlen(p) + 1;

	if (!(mod = malloc(sizeof(*mod) + res))) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return -1;
	}

	memcpy(mod->name, p, res);

	res = -1;

	cw_object_init(mod, NULL, 1);
	mod->obj.release = module_release;
	mod->modinfo = NULL;
	mod->reg_entry = NULL;

	if (!(mod->lib = lt_dlopenext(filename))) {
		struct loader_err *local = pthread_getspecific(loader_err_key);
		cw_log(CW_LOG_ERROR, "Module '%s', error '%s'\n", mod->name, local->err);
		goto out_put_newmod;
	}

	modinfo = lt_dlsym(mod->lib, "get_modinfo");
	if (modinfo == NULL)
		modinfo = lt_dlsym(mod->lib, "_get_modinfo");
	if (modinfo == NULL) {
		cw_log(CW_LOG_ERROR, "No get_modinfo in module %s\n", mod->name);
		goto out_put_newmod;
	}

	oldmod = NULL;
	hash = cw_hash_string(mod->name);

	pthread_mutex_lock(&modlock);

	if ((oldobj = cw_registry_find(&module_registry, 1, hash, mod->name)))
		oldmod = container_of(oldobj, struct cw_module, obj);

	/* If it wasn't previously registered or lt_dlopenext() says it has mapped something
	 * different we can go ahead and plug it in.
	 */
	if (!oldmod || mod->lib != oldmod->lib) {
		mod->modinfo = (*modinfo)();
		mod->modinfo->self = mod;

		if (!(mod->reg_entry = cw_registry_add(&module_registry, hash, &mod->obj)))
			goto out_put_oldmod;

		if ((res = mod->modinfo->init())) {
			cw_log(CW_LOG_WARNING, "%s: register failed, returned %d\n", mod->name, res);
			mod->modinfo->deregister();
			cw_registry_del(&module_registry, mod->reg_entry);
			goto out_put_oldmod;
		}

		/* CAUTION: we may be resurrecting a module that had been deregistered and
		 * queued for removal when it went idle but that had not yet been removed.
		 */
		if (mod->modinfo->state == MODINFO_STATE_UNINITIALIZED)
			cw_mutex_init(&mod->modinfo->localuser_lock);

		if (!fully_booted) {
			if (option_verbose) {
				cw_verbose("[%s] => (%s)\n", mod->name, mod->modinfo->description);
			} else if (option_console || option_nofork)
				cw_log(CW_LOG_PROGRESS, ".");
		} else {
			if (option_verbose)
				cw_verbose(VERBOSE_PREFIX_1 "%s %s => (%s)\n",
					(mod->modinfo->state != MODINFO_STATE_UNINITIALIZED ? "Resurrected" : "Loaded"), mod->name, mod->modinfo->description);
			cw_log(CW_LOG_NOTICE, "%s %s => (%s)\n",
				(mod->modinfo->state != MODINFO_STATE_UNINITIALIZED ? "Resurrected" : "Loaded"), mod->name, mod->modinfo->description);
		}

		mod->modinfo->state = MODINFO_STATE_ACTIVE;

		if (oldmod) {
			if ((res = oldmod->modinfo->deregister()))
				cw_log(CW_LOG_WARNING, "%s: deregister of old instance failed, returned %d. Memory leaked.\n", oldmod->name, res);
			cw_registry_del(&module_registry, oldmod->reg_entry);
		}
	} else
		cw_log(CW_LOG_NOTICE, "%s: already loaded and current\n", mod->name);

	res = 0;

out_put_oldmod:
	pthread_mutex_unlock(&modlock);

	if (oldmod)
		cw_object_put(oldmod);

out_put_newmod:
	cw_object_put(mod);

	return res;
}


struct load_module_args {
	struct cw_config *cfg;
	const char *prefix;
	int prefix_len;
	int reload_ok;
	int found;
};

static int load_module(const char *filename, lt_ptr data)
{
	struct load_module_args *args = (struct load_module_args *)data;
	struct cw_object *obj = NULL;
	char *bname;

	/* We want _basenames_ that match the given prefix, completely if prefix_len is not set,
	 * otherwise with the given prefix.
	 */
	if ((bname = strrchr(filename, '/')) && (bname++, 1)
	&& (!args->prefix
		|| (args->prefix_len && !strncmp(bname, args->prefix, args->prefix_len))
		|| (!args->prefix_len && !strcmp(bname, args->prefix)))
	) {
		args->found++;

		if ((args->reload_ok || !(obj = cw_registry_find(&module_registry, 1, cw_hash_string(bname), bname)))) {
			int baselen = strlen(bname);
			struct cw_variable *v;

			/* If we were given a config check that this module is not barred from loading.
			 * N.B. The extension part of a module name in the config is ignored. Really we
			 * should generate a deprecated message.
			 */
			v = NULL;
			if (args->cfg) {
				for (v = cw_variable_browse(args->cfg, "modules");
					v && (strcasecmp(v->name, "noload") || strncasecmp(v->value, bname, baselen) || (v->value[baselen] && v->value[baselen] != '.'));
					v = v->next);
				if (option_verbose && v)
					cw_verbose(VERBOSE_PREFIX_1 "[skipping %s]\n", bname);
			}
			if (v == NULL)
				module_load(filename);
		}
	}

	if (obj)
		cw_object_put_obj(obj);

	return 0;
}

int load_modules(const int preload_only)
{
	static const char *loadorder[] = {
		"res_",
		"chan_",
		"pbx_",
		NULL,
	};
	struct loader_err loader_err;
	struct load_module_args args;
	struct cw_config *cfg;
	struct cw_variable *v;
	int i;

	loader_err.err[0] = '\0';
	pthread_setspecific(loader_err_key, &loader_err);

	if (option_verbose) {
		if (preload_only)
			cw_verbose("CallWeaver Dynamic Loader loading preload modules:\n");
		else
			cw_verbose("CallWeaver Dynamic Loader Starting:\n");
	}

	args.reload_ok = 0;
	args.found = 0;

	cfg = cw_config_load(CW_MODULE_CONFIG);
	if (cfg) {
		int doload;

		/* Load explicitly defined modules */
		args.cfg = NULL;
		for (v = cw_variable_browse(cfg, "modules"); v; v = v->next) {
			doload = 0;

			if (preload_only)
				doload = !strcasecmp(v->name, "preload");
			else
				doload = !strcasecmp(v->name, "load");

		       if (doload) {
				if (option_debug && !option_verbose)
					cw_log(CW_LOG_DEBUG, "Loading module %s\n", v->value);
				args.prefix = v->value;
				args.prefix_len = 0;
				lt_dlforeachfile(lt_dlgetsearchpath(), load_module, &args);
			}
		}
	}

	if (!preload_only) {
		if (!cfg || cw_true(cw_variable_retrieve(cfg, "modules", "autoload"))) {
			args.cfg = cfg;
			for (i = 0; i < arraysize(loadorder); i++) {
				if ((args.prefix = loadorder[i]))
					args.prefix_len = strlen(loadorder[i]);
				lt_dlforeachfile(lt_dlgetsearchpath(), load_module, &args);
			}
		}
	}

	cw_config_destroy(cfg);

	pthread_setspecific(loader_err_key, NULL);

	return 0;
}


struct handle_modlist_args {
	struct cw_dynstr **ds_p;
	int count;
	const char *like;
};

static int handle_modlist_one(struct cw_object *obj, void *data)
{
	struct cw_module *mod = container_of(obj, struct cw_module, obj);
	struct handle_modlist_args *args = data;

	if (!args->like || strcasestr(mod->name, args->like) ) {
		args->count++;
		cw_dynstr_printf(args->ds_p, "%-30s %-40.40s %10d %10d\n",
			mod->name, mod->modinfo->description, cw_object_refs(mod), mod->modinfo->localusecnt);
	}

	return 0;
}

static int handle_modlist(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	struct handle_modlist_args args = {
		.ds_p = ds_p,
		.count = 0,
		.like = NULL,
	};

	if (argc == 3)
		return RESULT_SHOWUSAGE;

	if (argc >= 4) {
		if (strcmp(argv[2], "like")) 
			return RESULT_SHOWUSAGE;
		args.like = argv[3];
	}

	cw_dynstr_printf(ds_p, "%-30s %-40.40s %10s %10s\n", "Module", "Description", "Refs", "Chan Usage");
	cw_registry_iterate_ordered(&module_registry, handle_modlist_one, &args);
	cw_dynstr_printf(ds_p, "%d modules loaded\n", args.count);

	return RESULT_SUCCESS;
}


static int handle_load(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	struct loader_err loader_err;
	struct load_module_args args;
	const char *path;

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	loader_err.err[0] = '\0';
	pthread_setspecific(loader_err_key, &loader_err);

	args.cfg = NULL;
	args.prefix = argv[1];
	args.prefix_len = 0;
	args.reload_ok = 1;
	args.found = 0;
	lt_dlforeachfile((path = lt_dlgetsearchpath()), load_module, &args);

	if (!args.found)
		cw_log(CW_LOG_NOTICE, "Module %s not found in %s\n", argv[1], path);

	pthread_setspecific(loader_err_key, NULL);

	return RESULT_SUCCESS;
}


struct complete_fn_args {
	struct cw_dynstr **ds_p;
	const char *word;
	int word_len;
};

static int complete_fn_one(const char *filename, lt_ptr data)
{
	struct complete_fn_args *args = (struct complete_fn_args *)data;
	char *bname;

	if ((bname = strrchr(filename, '/')) && (bname++, 1)
	&& !strncmp(bname, args->word, args->word_len))
		cw_dynstr_printf(args->ds_p, "%s\n", bname);

	return 0;
}

static void complete_fn(struct cw_dynstr **ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct complete_fn_args args;
	char *p;
	int i;

	if (lastarg == 1) {
		if (argv[lastarg][0] == '/') {
			i = 0;
			while ((p = (char *)rl_filename_completion_function(argv[lastarg], i++))) {
				cw_dynstr_printf(ds_p, "%s\n", p);
				free(p);
			}
		} else {
			args.ds_p = ds_p;
			args.word = argv[lastarg];
			args.word_len = lastarg_len;
			lt_dlforeachfile(lt_dlgetsearchpath(), complete_fn_one, &args);
		}
	}
}


static int handle_reconfigure(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	int x;

	if (argc < 1)
		return RESULT_SHOWUSAGE;

	if (argc > 1) { 
		for (x = 1; x < argc; x++) {
			int res = cw_module_reconfigure(argv[x]);
			switch (res) {
			case 0:
				cw_dynstr_printf(ds_p, "No such module '%s'\n", argv[x]);
				break;
			case 1:
				cw_dynstr_printf(ds_p, "Module '%s' does not support reconfigure\n", argv[x]);
				break;
			}
		}
	} else
		cw_module_reconfigure(NULL);

	return RESULT_SUCCESS;
}


static void reconfigure_module_generator(struct cw_dynstr **ds_p, char *argv[], int lastarg, int lastarg_len)
{
	static const char *core[] = {
		"extconfig",
		"cdr",
		"enum",
		"features",
		"rtp",
		"manager",
	};
	int i;

	for (i = 0; i < arraysize(core); i++) {
		if (!strncasecmp(argv[lastarg], core[i], lastarg_len))
			cw_dynstr_printf(ds_p, "%s\n", core[i]);
	}

	module_generator(ds_p, argv, lastarg, lastarg_len);
}


static int handle_unload(struct cw_dynstr **ds_p, int argc, char *argv[])
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
		else if (unload_module(argv[x], hangup)) {
			cw_dynstr_printf(ds_p, "Unable to unload module %s\n", argv[x]);
			return RESULT_FAILURE;
		}
	}

	return RESULT_SUCCESS;
}


static const char modlist_help[] =
"Usage: show modules [like keyword]\n"
"       Shows CallWeaver modules currently in use, and usage statistics.\n";

static const char load_help[] =
"Usage: load <module name>\n"
"       Loads the specified module into CallWeaver.\n"
"       If the module is already present but deregistered (see unload)\n"
"       its functionality will simply be reregistered. Note that since\n"
"       the module has not actually been unloaded and reloaded this\n"
"       may mean that internal state is NOT reset.\n";

static const char reconfigure_help[] =
"Usage: reconfigure [module ...]\n"
"       Reloads configuration files for all listed modules which support\n"
"       reloading, or for all supported modules if none are listed.\n";

static const char unload_help[] =
"Usage: unload [-h] <module name>\n"
"       Deregisters the functionality provided by the specified\n"
"       module from CallWeaver so that it will be removed as soon\n"
"       as it is no longer in use.\n"
"       The -h option requests that channels that are currently\n"
"       using or dependent on the module be hung up.\n"
"       It is always safe to call unload multiple times on a module.\n";


static struct cw_clicmd clicmds[] = {
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
		.cmda = { "reconfigure", NULL },
		.handler = handle_reconfigure,
		.generator = reconfigure_module_generator,
		.summary = "Reload configuration",
		.usage = reconfigure_help,
	},
	{
		.cmda = { "unload", NULL },
		.handler = handle_unload,
		.generator = module_generator,
		.summary = "Unload a dynamic module by name",
		.usage = unload_help,
	},

	/* DEPRECATED */
	{
		.cmda = { "reconfigure", NULL },
		.handler = handle_reconfigure,
		.generator = reconfigure_module_generator,
		.summary = "Reload configuration",
		.usage = reconfigure_help,
	},
};


static void loader_lock(void)
{
	cw_mutex_lock(&loader_mutex);
}

static void loader_unlock(void)
{
	cw_mutex_unlock(&loader_mutex);
}

static void loader_seterr(const char *err)
{
	struct loader_err *local = pthread_getspecific(loader_err_key);

	if (local) {
		if (err) {
			strncpy(local->err, err, sizeof(local->err) - 1);
			local->err[sizeof(local->err) - 1] = '\0';
		} else
			local->err[0] = '\0';
	}
}

static const char *loader_geterr(void)
{
	struct loader_err *local = pthread_getspecific(loader_err_key);

	if (!local)
		return NULL;
	return local->err;
}

void cw_loader_init(void)
{
	if (pthread_key_create(&loader_err_key, &free)) {
		perror("pthread_key_create");
		exit(1);
	}

	cw_registry_init(&module_registry, 256);
	lt_dlinit();
	lt_dlmutex_register(loader_lock, loader_unlock, loader_seterr, loader_geterr);
}

int cw_loader_cli_init(void)
{
	cw_cli_register_multiple(clicmds, arraysize(clicmds));
	return 0;
}
