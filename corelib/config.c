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
 * \brief Configuration File Parser
 *
 * Includes the CallWeaver Realtime API - ARA
 * See README.realtime
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#if defined(HAVE_GLOB_H)
#include <glob.h>
#if defined(__CYGWIN__)
#define GLOB_ABORTED GLOB_ABEND
#endif
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/channel.h"
#include "callweaver/app.h"

#define MAX_NESTED_COMMENTS 128
#define COMMENT_START ";--"
#define COMMENT_END "--;"
#define COMMENT_META ';'
#define COMMENT_TAG '-'

static const char *extconfig_conf = "extconfig.conf";

static struct cw_config_map {
	struct cw_config_map *next;
	char *name;
	char *driver;
	char *database;
	char *table;
	char stuff[0];
} *config_maps = NULL;

CW_MUTEX_DEFINE_STATIC(config_lock);


static int cw_config_engine_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_config_engine *config_engine_a = container_of(*objp_a, struct cw_config_engine, obj);
	const struct cw_config_engine *config_engine_b = container_of(*objp_b, struct cw_config_engine, obj);

	return strcmp(config_engine_a->name, config_engine_b->name);
}

static int config_engine_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_config_engine *ce = container_of(obj, struct cw_config_engine, obj);
	return strcasecmp(ce->name, pattern);
}

struct cw_registry config_engine_registry = {
	.name = "Config Engine",
	.qsort_compare = cw_config_engine_qsort_compare_by_name,
	.match = config_engine_object_match,
};

#define MAX_INCLUDE_LEVEL 10

struct cw_category {
	char name[80];
	int ignored;			/* do not let user of the config see this category */
	struct cw_variable *root;
	struct cw_variable *last;
	struct cw_category *next;
};

struct cw_config {
	struct cw_category *root;
	struct cw_category *last;
	struct cw_category *current;
	struct cw_category *last_browse;		/* used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
};

struct cw_variable *cw_variable_new(const char *name, const char *value) 
{
	struct cw_variable *variable;
	int name_len, value_len, length;

	name_len = strlen(name) + 1;
	value_len = strlen(value) + 1;
	length = sizeof(struct cw_variable) + name_len + value_len;

	if ((variable = malloc(length))) {
		memset(variable, 0, length);
		variable->value = variable->name + name_len;
		memcpy(variable->name, name, name_len);
		memcpy(variable->value, value, value_len);
	}

	return variable;
}

void cw_variable_append(struct cw_category *category, struct cw_variable *variable)
{
	if (category->last)
		category->last->next = variable;
	else
		category->root = variable;
	category->last = variable;
}

void cw_variables_destroy(struct cw_variable *v)
{
	struct cw_variable *vn;

	while(v) {
		vn = v;
		v = v->next;
		free(vn);
	}
}

struct cw_variable *cw_variable_browse(const struct cw_config *config, const char *category)
{
	struct cw_category *cat = NULL;

	if (category && config->last_browse && (config->last_browse->name == category))
		cat = config->last_browse;
	else
		cat = cw_category_get(config, category);

	if (cat)
		return cat->root;
	else
		return NULL;
}

char *cw_variable_retrieve(const struct cw_config *config, const char *category, const char *variable)
{
	struct cw_variable *v;

	if (category) {
		for (v = cw_variable_browse(config, category); v; v = v->next) {
			if (!strcasecmp(variable, v->name))
				return v->value;
		}
	} else {
		struct cw_category *cat;

		for (cat = config->root; cat; cat = cat->next)
			for (v = cat->root; v; v = v->next)
				if (!strcasecmp(variable, v->name))
					return v->value;
	}

	return NULL;
}

static struct cw_variable *variable_clone(const struct cw_variable *old)
{
	struct cw_variable *new = cw_variable_new(old->name, old->value);

	if (new)
		new->lineno = old->lineno;

	return new;
}
 
static void move_variables(struct cw_category *old, struct cw_category *new)
{
	struct cw_variable *var;
	struct cw_variable *next;

	next = old->root;
	old->root = NULL;
	for (var = next; var; var = next) {
		next = var->next;
		var->next = NULL;
		cw_variable_append(new, var);
	}
}

struct cw_category *cw_category_new(const char *name) 
{
	struct cw_category *category;

	category = malloc(sizeof(struct cw_category));
	if (category) {
		memset(category, 0, sizeof(struct cw_category));
		cw_copy_string(category->name, name, sizeof(category->name));
	}

	return category;
}

static struct cw_category *category_get(const struct cw_config *config, const char *category_name, int ignored)
{
	struct cw_category *cat;

	for (cat = config->root; cat; cat = cat->next) {
		if (cat->name == category_name && (ignored || !cat->ignored))
			return cat;
	}

	for (cat = config->root; cat; cat = cat->next) {
		if (!strcasecmp(cat->name, category_name) && (ignored || !cat->ignored))
			return cat;
	}

	return NULL;
}

struct cw_category *cw_category_get(const struct cw_config *config, const char *category_name)
{
	return category_get(config, category_name, 0);
}

int cw_category_exist(const struct cw_config *config, const char *category_name)
{
	return !!cw_category_get(config, category_name);
}

void cw_category_append(struct cw_config *config, struct cw_category *category)
{
	if (config->last)
		config->last->next = category;
	else
		config->root = category;
	config->last = category;
	config->current = category;
}

void cw_category_destroy(struct cw_category *cat)
{
	cw_variables_destroy(cat->root);
	free(cat);
}

static struct cw_category *next_available_category(struct cw_category *cat)
{
	for (; cat && cat->ignored; cat = cat->next);

	return cat;
}

char *cw_category_browse(struct cw_config *config, const char *prev)
{	
	struct cw_category *cat = NULL;

	if (prev && config->last_browse && (config->last_browse->name == prev))
		cat = config->last_browse->next;
	else if (!prev && config->root)
			cat = config->root;
	else if (prev) {
		for (cat = config->root; cat; cat = cat->next) {
			if (cat->name == prev) {
				cat = cat->next;
				break;
			}
		}
		if (!cat) {
			for (cat = config->root; cat; cat = cat->next) {
				if (!strcasecmp(cat->name, prev)) {
					cat = cat->next;
					break;
				}
			}
		}
	}
	
	if (cat)
		cat = next_available_category(cat);

	config->last_browse = cat;
	if (cat)
		return cat->name;
	else
		return NULL;
}

struct cw_variable *cw_category_detach_variables(struct cw_category *cat)
{
	struct cw_variable *v;

	v = cat->root;
	cat->root = NULL;

	return v;
}

void cw_category_rename(struct cw_category *cat, const char *name)
{
	cw_copy_string(cat->name, name, sizeof(cat->name));
}

static void inherit_category(struct cw_category *new, const struct cw_category *base)
{
	struct cw_variable *var;

	for (var = base->root; var; var = var->next) {
		struct cw_variable *v;
		
		v = variable_clone(var);
		if (v)
			cw_variable_append(new, v);
	}
}

struct cw_config *cw_config_new(void) 
{
	struct cw_config *config;

	config = malloc(sizeof(*config));
	if (config) {
		memset(config, 0, sizeof(*config));
		config->max_include_level = MAX_INCLUDE_LEVEL;
	}

	return config;
}

void cw_config_destroy(struct cw_config *cfg)
{
	struct cw_category *cat, *catn;

	if (!cfg)
		return;

	cat = cfg->root;
	while(cat) {
		cw_variables_destroy(cat->root);
		catn = cat;
		cat = cat->next;
		free(catn);
	}
	free(cfg);
}

struct cw_category *cw_config_get_current_category(const struct cw_config *cfg)
{
	return cfg->current;
}

void cw_config_set_current_category(struct cw_config *cfg, const struct cw_category *cat)
{
	/* cast below is just to silence compiler warning about dropping "const" */
	cfg->current = (struct cw_category *) cat;
}

static int process_text_line(struct cw_config *cfg, struct cw_category **cat, char *buf, int lineno, const char *configfile)
{
	char *c;
	char *cur = buf;
	struct cw_variable *v;
	char cmd[512], exec_file[512];
	int do_exec, do_include;

	/* Actually parse the entry */
	if (cur[0] == '[') {
		struct cw_category *newcat = NULL;
		char *catname;

		/* A category header */
		c = strchr(cur, ']');
		if (!c) {
			cw_log(CW_LOG_WARNING, "parse error: no closing ']', line %d of %s\n", lineno, configfile);
			return -1;
		}
		*c++ = '\0';
		cur++;
 		if (*c++ != '(')
 			c = NULL;
		catname = cur;
		*cat = newcat = cw_category_new(catname);
		if (!newcat) {
			cw_log(CW_LOG_WARNING, "Out of memory, line %d of %s\n", lineno, configfile);
			return -1;
		}
 		/* If there are options or categories to inherit from, process them now */
 		if (c) {
 			if (!(cur = strchr(c, ')'))) {
 				cw_log(CW_LOG_WARNING, "parse error: no closing ')', line %d of %s\n", lineno, configfile);
 				return -1;
 			}
 			*cur = '\0';
 			while ((cur = strsep(&c, ","))) {
				if (!strcasecmp(cur, "!")) {
					(*cat)->ignored = 1;
				} else if (!strcasecmp(cur, "+")) {
					*cat = category_get(cfg, catname, 1);
					if (!*cat) {
						cw_config_destroy(cfg);
						if (newcat)
							cw_category_destroy(newcat);
						cw_log(CW_LOG_WARNING, "Category addition requested, but category '%s' does not exist, line %d of %s\n", catname, lineno, configfile);
						return -1;
					}
					if (newcat) {
						move_variables(newcat, *cat);
						cw_category_destroy(newcat);
						newcat = NULL;
					}
				} else {
					struct cw_category *base;
 				
					base = category_get(cfg, cur, 1);
					if (!base) {
						cw_log(CW_LOG_WARNING, "Inheritance requested, but category '%s' does not exist, line %d of %s\n", cur, lineno, configfile);
						return -1;
					}
					inherit_category(*cat, base);
				}
 			}
 		}
		if (newcat)
			cw_category_append(cfg, *cat);
	} else if (cur[0] == '#') {
		/* A directive */
		cur++;
		c = cur;
		while(*c && !isblank(*c)) c++;
		if (*c) {
			*c = '\0';
			/* Find real argument */
			c = cw_skip_blanks(c + 1);
			if (!*c)
				c = NULL;
		} else 
			c = NULL;
		do_include = !strcasecmp(cur, "include");
		if(!do_include)
			do_exec = !strcasecmp(cur, "exec");
		else
			do_exec = 0;
		if (do_exec && !option_exec_includes) {
			cw_log(CW_LOG_WARNING, "Cannot perform #exec unless execincludes option is enabled in callweaver.conf (options section)!\n");
			do_exec = 0;
		}
		if (do_include || do_exec) {
			if (c) {
				/* Strip off leading and trailing "'s and <>'s */
				while((*c == '<') || (*c == '>') || (*c == '\"')) c++;
				/* Get rid of leading mess */
				cur = c;
				while (!cw_strlen_zero(cur)) {
					c = cur + strlen(cur) - 1;
					if ((*c == '>') || (*c == '<') || (*c == '\"'))
						*c = '\0';
					else
						break;
				}
				/* #exec </path/to/executable>
				   We create a tmp file, then we #include it, then we delete it. */
				if (do_exec) { 
					snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%ld.%ld", time(NULL), (long)pthread_self());
					snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file);
					cw_safe_system(cmd);
					cur = exec_file;
				} else
					exec_file[0] = '\0';
				/* A #include */
				do_include = cw_config_internal_load(cur, cfg) ? 1 : 0;
				if(!cw_strlen_zero(exec_file))
					unlink(exec_file);
				if(!do_include)
					return 0;

			} else {
				cw_log(CW_LOG_WARNING, "Directive '#%s' needs an argument (%s) at line %d of %s\n", 
						do_exec ? "exec" : "include",
						do_exec ? "/path/to/executable" : "filename",
						lineno,
						configfile);
			}
		}
		else 
			cw_log(CW_LOG_WARNING, "Unknown directive '%s' at line %d of %s\n", cur, lineno, configfile);
	} else {
		/* Just a line (variable = value) */
		if (!*cat) {
			cw_log(CW_LOG_WARNING,
				"parse error: No category context for line %d of %s\n", lineno, configfile);
			return -1;
		}
		c = strchr(cur, '=');
		if (c) {
			*c = 0;
			c++;
			/* Ignore > in => */
			if (*c== '>')
				c++;
			v = cw_variable_new(cw_strip(cur), cw_strip(c));
			if (v) {
				v->lineno = lineno;
				/* Put and reset comments */
				cw_variable_append(*cat, v);
			} else {
				cw_log(CW_LOG_WARNING, "Out of memory, line %d\n", lineno);
				return -1;
			}
		} else {
			cw_log(CW_LOG_WARNING, "No '=' (equal sign) in line %d of %s\n", lineno, configfile);
		}

	}
	return 0;
}

static struct cw_config *config_text_file_load(const char *filename, struct cw_config *cfg)
{
	char buf[8192];
	const char *fn;
	char *new_buf, *comment_p, *process_buf;
	FILE *f;
	int lineno=0;
	int comment = 0, nest[MAX_NESTED_COMMENTS];
	struct cw_category *cat = NULL;
	int count = 0;
	struct stat statbuf;

	cat = cw_config_get_current_category(cfg);

	if (filename[0] == '/') {
		fn = filename;
	} else {
		fn = alloca(strlen(cw_config[CW_CONFIG_DIR]) + 1 + strlen(filename) + 1);
		sprintf((char *)fn, "%s/%s", cw_config[CW_CONFIG_DIR], filename);
	}

#if defined(HAVE_GLOB_H)
	{
		int glob_ret;
		glob_t globbuf;
		globbuf.gl_offs = 0;	/* initialize it to silence gcc */
#if defined(SOLARIS)  ||  !defined(GLOB_NOMAGIC)  ||  !defined(GLOB_BRACE)
		glob_ret = glob(fn, GLOB_NOCHECK, NULL, &globbuf);
#else
		glob_ret = glob(fn, GLOB_NOMAGIC|GLOB_BRACE, NULL, &globbuf);
#endif
		if (glob_ret == GLOB_NOSPACE)
			cw_log(CW_LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Not enough memory\n", fn);
		else if (glob_ret  == GLOB_ABORTED)
			cw_log(CW_LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Read error\n", fn);
		else  {
			/* loop over expanded files */
			int i;
			for (i=0; i<globbuf.gl_pathc; i++) {
				fn = globbuf.gl_pathv[i];
#endif
	do {
		if (stat(fn, &statbuf)) {
			cw_log(CW_LOG_WARNING, "Cannot stat() '%s', ignoring\n", fn);
			continue;
		}
		if (!S_ISREG(statbuf.st_mode)) {
			cw_log(CW_LOG_WARNING, "'%s' is not a regular file, ignoring\n", fn);
			continue;
		}
		if ((option_verbose > 3) && !option_debug) {
			cw_verbose(VERBOSE_PREFIX_2 "Parsing '%s': ", fn);
			fflush(stdout);
		}
		if (!(f = fopen(fn, "r"))) {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "No file to parse: %s\n", fn);
			if (option_verbose > 1)
				cw_verbose( "Not found (%s)\n", strerror(errno));
			continue;
		}
		count++;
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Parsing %s - Found\n", fn);
		if (option_verbose > 3)
			cw_verbose( "Parsing %s - Found\n", fn);
		while(!feof(f)) {
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
				new_buf = buf;
				if (comment)
					process_buf = NULL;
				else
					process_buf = buf;
				while ((comment_p = strchr(new_buf, COMMENT_META))) {
					if ((comment_p > new_buf) && (*(comment_p-1) == '\\')) {
						/* Yuck, gotta memmove */
						memmove(comment_p - 1, comment_p, strlen(comment_p) + 1);
						new_buf = comment_p;
					} else if(comment_p[1] == COMMENT_TAG && comment_p[2] == COMMENT_TAG && (comment_p[3] != '-')) {
						/* Meta-Comment start detected ";--" */
						if (comment < MAX_NESTED_COMMENTS) {
							*comment_p = '\0';
							new_buf = comment_p + 3;
							comment++;
							nest[comment-1] = lineno;
						} else {
							cw_log(CW_LOG_ERROR, "Maximum nest limit of %d reached.\n", MAX_NESTED_COMMENTS);
						}
					} else if ((comment_p >= new_buf + 2) &&
						   (*(comment_p - 1) == COMMENT_TAG) &&
						   (*(comment_p - 2) == COMMENT_TAG)) {
						/* Meta-Comment end detected */
						comment--;
						new_buf = comment_p + 1;
						if (!comment) {
							/* Back to non-comment now */
							if (process_buf) {
								/* Actually have to move what's left over the top, then continue */
								char *oldptr;
								oldptr = process_buf + strlen(process_buf);
								memmove(oldptr, new_buf, strlen(new_buf) + 1);
								new_buf = oldptr;
							} else
								process_buf = new_buf;
						}
					} else {
						if (!comment) {
							/* If ; is found, and we are not nested in a comment, 
							   we immediately stop all comment processing */
							*comment_p = '\0'; 
							new_buf = comment_p;
						} else
							new_buf = comment_p + 1;
					}
				}
				if (process_buf) {
					char *p = cw_strip(process_buf);
					if (!cw_strlen_zero(p)) {
						if (process_text_line(cfg, &cat, p, lineno, filename)) {
							cfg = NULL;
							break;
						}
					}
				}
			}
		}
		fclose(f);		
	} while(0);
	if (comment) {
		cw_log(CW_LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment]);
	}
#if defined(HAVE_GLOB_H)
					if (!cfg)
						break;
				}
				globfree(&globbuf);
			}
		}
#endif
	if (count == 0)
		return NULL;

	return cfg;
}

static void clear_config_maps(void) 
{
	struct cw_config_map *map;

	cw_mutex_lock(&config_lock);

	while (config_maps) {
		map = config_maps;
		config_maps = config_maps->next;
		free(map);
	}
		
	cw_mutex_unlock(&config_lock);
}

static int append_mapping(const char *name, const char *driver, const char *database, const char *table)
{
	struct cw_config_map *map;
	int length;

	length = sizeof(*map);
	length += strlen(name) + 1;
	length += strlen(driver) + 1;
	length += strlen(database) + 1;
	if (table)
		length += strlen(table) + 1;
	map = malloc(length);

	if (!map)
		return -1;

	memset(map, 0, length);
	map->name = map->stuff;
	strcpy(map->name, name);
	map->driver = map->name + strlen(map->name) + 1;
	strcpy(map->driver, driver);
	map->database = map->driver + strlen(map->driver) + 1;
	strcpy(map->database, database);
	if (table) {
		map->table = map->database + strlen(map->database) + 1;
		strcpy(map->table, table);
	}
	map->next = config_maps;

	if (option_verbose > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Binding %s to %s/%s/%s\n",
			    map->name, map->driver, map->database, map->table ? map->table : map->name);

	config_maps = map;
	return 0;
}

void read_config_maps(void) 
{
	struct cw_config *config, *configtmp;
	struct cw_variable *v;
	char *driver, *table, *database, *stringp;

	clear_config_maps();

	configtmp = cw_config_new();
	configtmp->max_include_level = 1;
	config = cw_config_internal_load(extconfig_conf, configtmp);
	if (!config) {
		cw_config_destroy(configtmp);
		return;
	}

	for (v = cw_variable_browse(config, "settings"); v; v = v->next) {
		stringp = v->value;
		driver = strsep(&stringp, ",");
		database = strsep(&stringp, ",");
		table = strsep(&stringp, ",");
			
		if (!strcmp(v->name, extconfig_conf)) {
			cw_log(CW_LOG_WARNING, "Cannot bind '%s'!\n", extconfig_conf);
			continue;
		}

		if (!strcmp(v->name, "callweaver.conf")) {
			cw_log(CW_LOG_WARNING, "Cannot bind 'callweaver.conf'!\n");
			continue;
		}

		if (!strcmp(v->name, "logger.conf")) {
			cw_log(CW_LOG_WARNING, "Cannot bind 'logger.conf'!\n");
			continue;
		}

		if (!driver || !database)
			continue;
		if (!strcasecmp(v->name, "sipfriends")) {
			cw_log(CW_LOG_WARNING, "The 'sipfriends' table is obsolete, update your config to use sipusers and sippeers, though they can point to the same table.\n");
			append_mapping("sipusers", driver, database, table ? table : "sipfriends");
			append_mapping("sippeers", driver, database, table ? table : "sipfriends");
		} else if (!strcasecmp(v->name, "iaxfriends")) {
			cw_log(CW_LOG_WARNING, "The 'iaxfriends' table is obsolete, update your config to use iaxusers and iaxpeers, though they can point to the same table.\n");
			append_mapping("iaxusers", driver, database, table ? table : "iaxfriends");
			append_mapping("iaxpeers", driver, database, table ? table : "iaxfriends");
		} else 
			append_mapping(v->name, driver, database, table);
	}
		
	cw_config_destroy(config);
}

/*--- find_engine: Find realtime engine for realtime family */
static struct cw_config_engine *find_engine(const char *family, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct cw_config_map *map;
	struct cw_config_engine *eng;
	struct cw_object *obj;

	cw_mutex_lock(&config_lock);

	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(family, map->name)) {
			if (database)
				cw_copy_string(database, map->database, dbsiz);
			if (table)
				cw_copy_string(table, map->table ? map->table : family, tabsiz);
			break;
		}
	}

	cw_mutex_unlock(&config_lock);

	/* Check if the required driver (engine) exist */
	eng = NULL;
	if (map) {
		obj = cw_registry_find(&config_engine_registry, 0, 0, map->driver);
		if (obj)
			eng = container_of(obj, struct cw_config_engine, obj);
		else
			cw_log(CW_LOG_WARNING, "Realtime mapping for '%s' requires engine '%s', but the engine is not available\n", map->name, map->driver);
	}

	return eng;
}


struct cw_config *cw_config_internal_load(const char *filename, struct cw_config *cfg)
{
	char db[256];
	char table[256];
	struct cw_config_engine *eng;
	struct cw_config *result;

	if (cfg->include_level == cfg->max_include_level) {
		cw_log(CW_LOG_WARNING, "Maximum Include level (%d) exceeded\n", cfg->max_include_level);
		return NULL;
	}

	cfg->include_level++;

	eng = NULL;
	if (strcmp(filename, extconfig_conf) && strcmp(filename, "callweaver.conf")) {
		eng = find_engine(filename, db, sizeof(db), table, sizeof(table));
		if (eng && !eng->load_func) {
			cw_object_put(eng);
			eng = NULL;
		}

		if (!eng) {
			eng = find_engine("global", db, sizeof(db), table, sizeof(table));
			if (eng && !eng->load_func) {
				cw_object_put(eng);
				eng = NULL;
			}
		}
	}

	if (eng) {
		result = eng->load_func(db, table, filename, cfg);
		cw_object_put(eng);
	} else
		result = config_text_file_load(filename, cfg);

	if (result)
		result->include_level--;

	return result;
}

struct cw_config *cw_config_load(const char *filename)
{
	struct cw_config *cfg;
	struct cw_config *result;

	cfg = cw_config_new();
	if (!cfg)
		return NULL;

	result = cw_config_internal_load(filename, cfg);
	if (!result)
		cw_config_destroy(cfg);

	return result;
}

struct cw_variable *cw_load_realtime(const char *family, ...)
{
	struct cw_config_engine *eng;
	char db[256]="";
	char table[256]="";
	struct cw_variable *res=NULL;
	va_list ap;

	va_start(ap, family);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng) {
		if (eng->realtime_func) 
			res = eng->realtime_func(db, table, ap);
		cw_object_put(eng);
	}
	va_end(ap);

	return res;
}

/*--- cw_check_realtime: Check if realtime engine is configured for family */
int cw_check_realtime(const char *family)
{
	struct cw_config_engine *eng;

	eng = find_engine(family, NULL, 0, NULL, 0);
	if (eng) {
		cw_object_put(eng);
		return 1;
	}
	return 0;

}

struct cw_config *cw_load_realtime_multientry(const char *family, ...)
{
	struct cw_config_engine *eng;
	char db[256]="";
	char table[256]="";
	struct cw_config *res=NULL;
	va_list ap;

	va_start(ap, family);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng) {
		if (eng->realtime_multi_func) 
			res = eng->realtime_multi_func(db, table, ap);
		cw_object_put(eng);
	}
	va_end(ap);

	return res;
}

int cw_update_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	struct cw_config_engine *eng;
	int res = -1;
	char db[256]="";
	char table[256]="";
	va_list ap;

	va_start(ap, lookup);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng) {
		if (eng->update_func) 
			res = eng->update_func(db, table, keyfield, lookup, ap);
		cw_object_put(eng);
	}
	va_end(ap);

	return res;
}

static int config_engine_print(struct cw_object *obj, void *data)
{
	struct cw_config_engine *eng = container_of(obj, struct cw_config_engine, obj);
	struct cw_dynstr *ds_p = data;
	struct cw_config_map *map;

	cw_dynstr_printf(ds_p, "Config Engine: %s\n", eng->name);
	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(map->driver, eng->name)) {
			cw_dynstr_printf(ds_p, "===> %s (db=%s, table=%s)\n", map->name, map->database,
				map->table ? map->table : map->name);
		}
	}
	cw_dynstr_printf(ds_p, "\n");
	return 0;
}

static int config_command(struct cw_dynstr *ds_p, int argc, char **argv)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_registry_iterate_ordered(&config_engine_registry, config_engine_print, ds_p);
	return 0;
}

static const char show_config_help[] =
	"Usage: show config mappings\n"
	"	Shows the filenames to config engines.\n";

static struct cw_clicmd config_command_struct = {
	.cmda = { "show", "config", "mappings", NULL },
	.handler = config_command,
	.summary = "Show Config mappings (file names to config engines)",
	.usage = show_config_help,
};

int register_config_cli(void)
{
	return cw_cli_register(&config_command_struct);
}
