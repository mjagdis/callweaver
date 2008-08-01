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
 * \brief Populate and remember extensions from static config file
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/pbx.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/cli.h"
#include "callweaver/phone_no_utils.h"

#ifdef __CW_DEBUG_MALLOC
static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

static const char dtext[] = "Text Extension Configuration";
static char *config = "extensions.conf";
static char *registrar = "pbx_config";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 0;
static int clearglobalvars_config = 0;

CW_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct cw_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static char context_dont_include_help[] =
"Usage: dont include <context> in <context>\n"
"       Remove an included context from another context.\n";

static char context_remove_extension_help[] =
"Usage: remove extension exten@context [priority]\n"
"       Remove an extension from a given context. If a priority\n"
"       is given, only that specific priority from the given extension\n"
"       will be removed.\n";

static char context_add_include_help[] =
"Usage: include <context> in <context>\n"
"       Include a context in another context.\n";

static char save_dialplan_help[] =
"Usage: save dialplan [/path/to/extension/file]\n"
"       Save dialplan created by pbx_config module.\n"
"\n"
"Example: save dialplan                 (/etc/callweaver/extensions.conf)\n"
"         save dialplan /home/markster  (/home/markster/extensions.conf)\n";

static char context_add_extension_help[] =
"Usage: add extension <exten>,<priority>,<app>,<app-data> into <context>\n"
"       [replace]\n\n"
"       This command will add new extension into <context>. If there is an\n"
"       existence of extension with the same priority and last 'replace'\n"
"       arguments is given here we simply replace this extension.\n"
"\n"
"Example: add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
"         Now, you can dial 6123 and talk to Markster :)\n";

static char context_add_ignorepat_help[] =
"Usage: add ignorepat <pattern> into <context>\n"
"       This command adds a new ignore pattern into context <context>\n"
"\n"
"Example: add ignorepat _3XX into local\n";

static char context_remove_ignorepat_help[] =
"Usage: remove ignorepat <pattern> from <context>\n"
"       This command removes an ignore pattern from context <context>\n"
"\n"
"Example: remove ignorepat _3XX from local\n";

static char reload_extensions_help[] =
"Usage: reload extensions.conf without reloading any other modules\n"
"       This command does not delete global variables unless\n"
"       clearglobalvars is set to yes in extensions.conf\n"
"\n"
"Example: extensions reload\n";


/*
 * Implementation of functions provided by this module
 */

/*
 * REMOVE INCLUDE command stuff
 */
static int handle_context_dont_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (!cw_context_remove_include(argv[4], argv[2], registrar)) {
		cw_cli(fd, "We are not including '%s' in '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	cw_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

static void complete_context_dont_include(int fd, char *line, int pos, char *word, int word_len)
{
	if (pos == 2) {
		struct cw_context *c;

		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				struct cw_include *i;

				if (!cw_lock_context(c)) {
					for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
						if (!strncmp(word, cw_get_include_name(i), word_len)) {
							struct cw_context *nc;
							int already_served = 0;

							/* check if this include is already served or not */
	
							/* go through all contexts again until we reach actual
							 * context or already_served = 1
							 */
							for (nc = cw_walk_contexts(NULL); nc && nc != c && !already_served; nc = cw_walk_contexts(nc)) {
								if (!cw_lock_context(nc)) {
									struct cw_include *ni;

									for (ni = cw_walk_context_includes(nc, NULL); ni && !already_served; ni = cw_walk_context_includes(nc, ni)) {
										if (!strcmp(cw_get_include_name(i), cw_get_include_name(ni)))
											already_served = 1;
									}	
							
									cw_unlock_context(nc);
								}
							}

							if (!already_served)
								cw_cli(fd, "%s\n", cw_get_include_name(i));
						}
					}

					cw_unlock_context(c);
				}
			}
		} else {
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
		}

		cw_unlock_contexts();
		return;
	}

	/*
	 * 'in' completion ... (complete only if previous context is really
	 * included somewhere)
	 */
	if (pos == 3) {
		struct cw_context *c;
		char *context, *dupline, *duplinet;

		/* take 'context' from line ... */
		if (!(dupline = strdup(line))) {
			cw_log(CW_LOG_ERROR, "Out of free memory\n");
			return;
		}

		duplinet = dupline;
		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");

		if (!context) {
			free(dupline);
			return;
		}

		if (cw_lock_contexts()) {
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return;
		}

		/* go through all contexts and check if is included ... */
		for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
			struct cw_include *i;
			if (!cw_lock_context(c)) {
				for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
					/* is it our context? */
					if (!strcmp(cw_get_include_name(i), context)) {
						/* yes, it is, context is really included, so
						 * complete "in" command
						 */
						cw_cli(fd, "in\n");
						break;
					}
				}
				cw_unlock_context(c);
			}
		}
		free(dupline);
		cw_unlock_contexts();
		return;
	}

	/*
	 * Context from which we are removing include ... 
	 */
	if (pos == 4) {
		struct cw_context *c;
		char *context, *dupline, *duplinet, *in;

		if (!(dupline = strdup(line))) {
			cw_log(CW_LOG_ERROR, "Out of free memory\n");
			return;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */

		if (!(context = strsep(&duplinet, " "))) {
			free(dupline);
			return;
		}

		/* third word must be in */
		in = strsep(&duplinet, " ");
		if (!in || strcmp(in, "in")) {
			free(dupline);
			return;
		}

		if (cw_lock_contexts()) {
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return;
		}

		/* walk through all contexts ... */
		for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
			struct cw_include *i;
			if (cw_lock_context(c))
				break;
	
			/* walk through all includes and check if it is our context */	
			for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
				/* is in this context included another on which we want to
				 * remove?
				 */
				if (!strcmp(context, cw_get_include_name(i))) {
					/* yes, it's included, is matching our word too? */
					if (!strncmp(cw_get_context_name(c), word, word_len))
						cw_cli(fd, "%s\n", cw_get_context_name(c));
					break;
				}
			}	
			cw_unlock_context(c);
		}

		free(dupline);
		cw_unlock_contexts();
		return;
	}

	return;
}

/*
 * REMOVE EXTENSION command stuff
 */
static int handle_context_remove_extension(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context;

	if (argc != 4 && argc != 3) return RESULT_SHOWUSAGE;

	/*
	 * Priority input checking ...
	 */
	if (argc == 4) {
		char *c = argv[3];

		/* check for digits in whole parameter for right priority ...
		 * why? because atoi (strtol) returns 0 if any characters in
		 * string and whole extension will be removed, it's not good
		 */
		if (strcmp("hint", c)) {
    		    while (*c != '\0') {
			if (!isdigit(*c++)) {
				cw_cli(fd, "Invalid priority '%s'\n", argv[3]);
				return RESULT_FAILURE;
			}
		    }
		    removing_priority = atoi(argv[3]);
		} else
		    removing_priority = PRIORITY_HINT;

		if (removing_priority == 0) {
			cw_cli(fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/*
	 * Format exten@context checking ...
	 */
	if (!(context = strchr(argv[2], (int)'@'))) {
		cw_cli(fd, "First argument must be in exten@context format\n");
		return RESULT_FAILURE;
	}

	*context++ = '\0';
	exten = argv[2];
	if ((!strlen(exten)) || (!(strlen(context)))) {
		cw_cli(fd, "Missing extension or context name in second argument '%s@%s'\n",
			exten == NULL ? "?" : exten, context == NULL ? "?" : context);
		return RESULT_FAILURE;
	}

	if (!cw_context_remove_extension(context, exten, removing_priority, registrar)) {
		if (!removing_priority)
			cw_cli(fd, "Whole extension %s@%s removed\n",
				exten, context);
		else
			cw_cli(fd, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		return RESULT_SUCCESS;
	}

	cw_cli(fd, "Failed to remove extension %s@%s\n", exten, context);

	return RESULT_FAILURE;
}


static void complete_context_remove_extension(int fd, char *line, int pos, char *word, int word_len)
{
	/*
	 * exten@context completion ... 
	 */
	if (pos == 2) {
		struct cw_context *c;
		struct cw_exten *e;
		char *context = NULL, *exten = NULL, *delim = NULL;

		/* now, parse values from word = exten@context */
		if ((delim = strchr(word, '@'))) {
			/* check for duplicates ... */
			if (delim != strrchr(word, '@'))
				return;

			*delim = '\0';
			exten = strdup(word);
			context = strdup(delim + 1);
			*delim = '@';
		} else {
			exten = strdup(word);
		}

		if (!cw_lock_contexts()) {
			/* find our context ... */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				/* our context? */
				if ( (!context || !strlen(context)) ||                            /* if no input, all contexts ... */
					 (context && !strncmp(cw_get_context_name(c), context, strlen(context))) ) {                  /* if input, compare ... */
					/* try to complete extensions ... */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
						/* our extension? */
						if ( (!exten || !strlen(exten)) ||                           /* if not input, all extensions ... */
							 (exten && !strncmp(cw_get_extension_name(e), exten, strlen(exten))) ) { /* if input, compare ... */
							/* If there is an extension then return
							 * exten@context.
							 */
							if (exten)
								cw_cli(fd, "%s@%s\n", cw_get_extension_name(e), cw_get_context_name(c));
						}
					}
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");

		free(exten);
		if (context)
			free(context);

		return;
	}

	/*
	 * Complete priority ...
	 */
	if (pos == 3) {
		char *delim, *exten, *context, *dupline, *duplinet, *ec;
		struct cw_context *c;

		dupline = strdup(line);
		if (!dupline)
			return;
		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'remove' */
		strsep(&duplinet, " "); /* skip 'extension */

		if (!(ec = strsep(&duplinet, " "))) {
			free(dupline);
			return;
		}

		/* wrong exten@context format? */
		if (!(delim = strchr(ec, '@')) || (strchr(ec, '@') != strrchr(ec, '@'))) {
			free(dupline);
			return;
		}

		/* check if there is exten and context too ... */
		*delim = '\0';
		if ((!strlen(ec)) || (!strlen(delim + 1))) {
			free(dupline);
			return;
		}

		exten = strdup(ec);
		context = strdup(delim + 1);
		free(dupline);

		if (!cw_lock_contexts()) {
			/* walk contexts */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strcmp(cw_get_context_name(c), context)) {
					struct cw_exten *e;

					/* walk extensions */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
						if (!strcmp(cw_get_extension_name(e), exten)) {
							struct cw_exten *priority;
							char buffer[10];
					
							for (priority = cw_walk_extension_priorities(e, NULL); priority; priority = cw_walk_extension_priorities(e, priority)) {
								snprintf(buffer, 10, "%u", cw_get_extension_priority(priority));
								if (!strncmp(word, buffer, word_len))
									cw_cli(fd, "%s\n", buffer);
							}

							break;
						}
					}

					break;
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");

		free(exten);
		free(context);
	}

	return; 
}

/*
 * Include context ...
 */
static int handle_context_add_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	/* third arg must be 'in' ... */
	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (cw_context_add_include(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_cli(fd, "Out of memory for context addition\n"); break;

			case EBUSY:
				cw_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case EEXIST:
				cw_cli(fd, "Context '%s' already included in '%s' context\n",
					argv[1], argv[3]); break;

			case ENOENT:
			case EINVAL:
				cw_cli(fd, "There is no existence of context '%s'\n",
					errno == ENOENT ? argv[4] : argv[2]); break;

			default:
				cw_cli(fd, "Failed to include '%s' in '%s' context\n",
					argv[1], argv[3]); break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	cw_cli(fd, "Context '%s' included in '%s' context\n",
		argv[2], argv[3]);

	return RESULT_SUCCESS;
}

static void complete_context_add_include(int fd, char *line, int pos, char *word, int word_len)
{
	struct cw_context *c;

	if (pos == 1) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if ((!strlen(word) || !strncmp(cw_get_context_name(c), word, word_len)))
					cw_cli(fd, "%s\n", cw_get_context_name(c));
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}

	/* complete 'in' only if context exist ... */
	if (pos == 2) {
		char *context, *dupline, *duplinet;

		/* parse context from line ... */
		if (!(dupline = strdup(line))) {
			cw_log(CW_LOG_ERROR, "Out of memory\n");
			return;
		}

		duplinet = dupline;

		strsep(&duplinet, " ");
		context = strsep(&duplinet, " ");
		if (context) {
			struct cw_context *c;

			/* check for context existence ... */
			if (!cw_lock_contexts()) {
				for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
					if (!strcmp(context, cw_get_context_name(c))) {
						cw_cli(fd, "into\n");
						break;
					}
				}

				cw_unlock_contexts();
			} else
				cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
		}	

		free(dupline);
		return;
	}

	/* serve context into which we include another context */
	if (pos == 3)
	{
		char *context, *dupline, *duplinet, *in;
		int context_existence = 0;

		if (!(dupline = strdup(line))) {
			cw_log(CW_LOG_ERROR, "Out of memory\n");
			return;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");
		in = strsep(&duplinet, " ");

		/* given some context and third word is in? */
		if (!strlen(context) || strcmp(in, "in")) {
			free(dupline);
			return;
		}

		if (cw_lock_contexts()) {
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return;
		}

		/* check for context existence ... */
		c = cw_walk_contexts(NULL);
		while (c && !context_existence) {
			if (!strcmp(context, cw_get_context_name(c))) {
				context_existence = 1;
				continue;
			}
			c = cw_walk_contexts(c);
		}

		if (!context_existence) {
			free(dupline);
			cw_unlock_contexts();
			return;
		}

		/* go through all contexts ... */
		for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
			/* must be different contexts ... */
			if (strcmp(context, cw_get_context_name(c))) {
				if (!cw_lock_context(c)) {
					struct cw_include *i;
					int included = 0;

					/* check for duplicate inclusion ... */
					for (i = cw_walk_context_includes(c, NULL); i && !included; i = cw_walk_context_includes(c, i)) {
						if (!strcmp(cw_get_include_name(i), context))
							included = 1;
					}

					cw_unlock_context(c);

					/* not included yet, so show possibility ... */
					if (!included && !strncmp(cw_get_context_name(c), word, word_len))
						cw_cli(fd, "%s\n", cw_get_context_name(c));
				}
			}
		}

		cw_unlock_contexts();
		free(dupline);
	}

	return;
}

/*
 * 'save dialplan' CLI command implementation functions ...
 */
static int handle_save_dialplan(int fd, int argc, char *argv[])
{
	char filename[256];
	struct cw_context *c;
	struct cw_config *cfg;
	struct cw_variable *v;
	int context_header_written;
	int incomplete = 0; /* incomplete config write? */
	FILE *output;

	if (! (static_config && !write_protect_config)) {
		cw_cli(fd,
			"I can't save dialplan now, see '%s' example file.\n",
			config);
		return RESULT_FAILURE;
	}

	if (argc != 2 && argc != 3) return RESULT_SHOWUSAGE;

	if (cw_mutex_lock(&save_dialplan_lock)) {
		cw_cli(fd,
			"Failed to lock dialplan saving (another proccess saving?)\n");
		return RESULT_FAILURE;
	}

	/* have config path? */
	if (argc == 3) {
		/* is there extension.conf too? */
		if (!strstr(argv[2], ".conf")) {
			/* no, only directory path, check for last '/' occurence */
			if (*(argv[2] + strlen(argv[2]) -1) == '/')
				snprintf(filename, sizeof(filename), "%s%s",
					argv[2], config);
			else
				/* without config extensions.conf, add it */
				snprintf(filename, sizeof(filename), "%s/%s",
					argv[2], config);
		} else
			/* there is an .conf */
			snprintf(filename, sizeof(filename), argv[2]);
	} else
		/* no config file, default one */
		snprintf(filename, sizeof(filename), "%s/%s",
			(char *)cw_config_CW_CONFIG_DIR, config);

	cfg = cw_config_load("extensions.conf");

	/* try to lock contexts list */
	if (cw_lock_contexts()) {
		cw_cli(fd, "Failed to lock contexts list\n");
		cw_mutex_unlock(&save_dialplan_lock);
		cw_config_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* create new file ... */
	if (!(output = fopen(filename, "wt"))) {
		cw_cli(fd, "Failed to create file '%s'\n",
			filename);
		cw_unlock_contexts();
		cw_mutex_unlock(&save_dialplan_lock);
		cw_config_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* fireout general info */
	fprintf(output, "[general]\nstatic=%s\nwriteprotect=%s\nautofallthrough=%s\nclearglobalvars=%s\npriorityjumping=%s\n\n",
		static_config ? "yes" : "no",
		write_protect_config ? "yes" : "no",
		autofallthrough_config ? "yes" : "no",
		clearglobalvars_config ? "yes" : "no",
		option_priority_jumping ? "yes" : "no");

	if ((v = cw_variable_browse(cfg, "globals"))) {
		fprintf(output, "[globals]\n");
		while(v) {
			fprintf(output, "%s => %s\n", v->name, v->value);
			v = v->next;
		}
		fprintf(output, "\n");
	}

	cw_config_destroy(cfg);
	
	/* walk all contexts */
	c = cw_walk_contexts(NULL);
	while (c) {
		context_header_written = 0;
	
		/* try to lock context and fireout all info */	
		if (!cw_lock_context(c)) {
			struct cw_exten *e, *last_written_e = NULL;
			struct cw_include *i;
			struct cw_ignorepat *ip;
			struct cw_sw *sw;

			/* registered by this module? */
			if (!strcmp(cw_get_context_registrar(c), registrar)) {
				fprintf(output, "[%s]\n", cw_get_context_name(c));
				context_header_written = 1;
			}

			/* walk extensions ... */
			e = cw_walk_context_extensions(c, NULL);
			while (e) {
				struct cw_exten *p;

				/* fireout priorities */
				p = cw_walk_extension_priorities(e, NULL);
				while (p) {
					if (!strcmp(cw_get_extension_registrar(p),
						registrar)) {
			
						/* make empty line between different extensions */	
						if (last_written_e != NULL &&
							strcmp(cw_get_extension_name(last_written_e),
								cw_get_extension_name(p)))
							fprintf(output, "\n");
						last_written_e = p;
				
						if (!context_header_written) {
							fprintf(output, "[%s]\n", cw_get_context_name(c));
							context_header_written = 1;
						}

						if (cw_get_extension_priority(p)!=PRIORITY_HINT) {
							char *tempdata;
							const char *el = cw_get_extension_label(p);
							char label[128] = "";

							tempdata = cw_get_extension_app_data(p);

							if (el && (snprintf(label, sizeof(label), "(%s)", el) != (strlen(el) + 2)))
								incomplete = 1; // error encountered or label is > 125 chars

							if (cw_get_extension_matchcid(p)) {
								fprintf(output, "exten => %s/%s,%d%s,%s(%s)\n",
								    cw_get_extension_name(p),
								    cw_get_extension_cidmatch(p),
								    cw_get_extension_priority(p),
								    label,
								    cw_get_extension_app(p),
								    tempdata);
							} else {
								fprintf(output, "exten => %s,%d%s,%s(%s)\n",
								    cw_get_extension_name(p),
								    cw_get_extension_priority(p),
								    label,
								    cw_get_extension_app(p),
								    tempdata);
							}
						} else {
							fprintf(output, "exten => %s,hint,%s\n",
							    cw_get_extension_name(p),
							    cw_get_extension_app(p));
						}
					}
					p = cw_walk_extension_priorities(e, p);
				}

				e = cw_walk_context_extensions(c, e);
			}

			/* written any extensions? ok, write space between exten & inc */
			if (last_written_e) fprintf(output, "\n");

			/* walk through includes */
			i = cw_walk_context_includes(c, NULL);
			while (i) {
				if (!strcmp(cw_get_include_registrar(i), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", cw_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "include => %s\n",
						cw_get_include_name(i));
				}
				i = cw_walk_context_includes(c, i);
			}

			if (cw_walk_context_includes(c, NULL))
				fprintf(output, "\n");

			/* walk through switches */
			sw = cw_walk_context_switches(c, NULL);
			while (sw) {
				if (!strcmp(cw_get_switch_registrar(sw), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", cw_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "switch => %s/%s\n",
						cw_get_switch_name(sw),
						cw_get_switch_data(sw));
				}
				sw = cw_walk_context_switches(c, sw);
			}

			if (cw_walk_context_switches(c, NULL))
				fprintf(output, "\n");

			/* fireout ignorepats ... */
			ip = cw_walk_context_ignorepats(c, NULL);
			while (ip) {
				if (!strcmp(cw_get_ignorepat_registrar(ip), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", cw_get_context_name(c));
						context_header_written = 1;
					}

					fprintf(output, "ignorepat => %s\n",
						cw_get_ignorepat_name(ip));
				}
				ip = cw_walk_context_ignorepats(c, ip);
			}

			cw_unlock_context(c);
		} else
			incomplete = 1;

		c = cw_walk_contexts(c);
	}	

	cw_unlock_contexts();
	cw_mutex_unlock(&save_dialplan_lock);
	fclose(output);

	if (incomplete) {
		cw_cli(fd, "Saved dialplan is incomplete\n");
		return RESULT_FAILURE;
	}

	cw_cli(fd, "Dialplan successfully saved into '%s'\n",
		filename);
	return RESULT_SUCCESS;
}

/*
 * ADD EXTENSION command stuff
 */
static int handle_context_add_extension(int fd, int argc, char *argv[])
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;

	/* check for arguments at first */
	if (argc != 5 && argc != 6) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;
	if (argc == 6) if (strcmp(argv[5], "replace")) return RESULT_SHOWUSAGE;

	whole_exten = argv[2];
	exten 		= strsep(&whole_exten,",");
	if (strchr(exten, '/')) {
		cidmatch = exten;
		strsep(&cidmatch,"/");
	} else {
		cidmatch = NULL;
	}
	prior       = strsep(&whole_exten,",");
	if (prior) {
    	if (!strcmp(prior, "hint")) {
			iprior = PRIORITY_HINT;
		} else {
			if (sscanf(prior, "%d", &iprior) != 1) {
				cw_cli(fd, "'%s' is not a valid priority\n", prior);
				prior = NULL;
			}
		}
	}
	app = whole_exten;
	if (app && (start = strchr(app, '(')) && (end = strrchr(app, ')'))) {
		*start = *end = '\0';
		app_data = start + 1;
	} else {
		if (app) {
			app_data = strchr(app, ',');
			if (app_data) {
				*app_data = '\0';
				app_data++;
			}
		} else	
			app_data = NULL;
	}

	if (!exten || !prior || !app || (!app_data && iprior != PRIORITY_HINT)) return RESULT_SHOWUSAGE;

	if (!app_data)
		app_data="";
	if (cw_add_extension(argv[4], argc == 6 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), free, registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_cli(fd, "Out of free memory\n"); break;

			case EBUSY:
				cw_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case ENOENT:
				cw_cli(fd, "No existence of '%s' context\n", argv[4]); break;

			case EEXIST:
				cw_cli(fd, "Extension %s@%s with priority %s already exists\n",
					exten, argv[4], prior); break;

			default:
				cw_cli(fd, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[4]); break;
		}
		return RESULT_FAILURE;
	}

	if (argc == 6) 
		cw_cli(fd, "Extension %s@%s (%s) replace by '%s,%s,%s,%s'\n",
			exten, argv[4], prior, exten, prior, app, app_data);
	else
		cw_cli(fd, "Extension '%s,%s,%s,%s' added into '%s' context\n",
			exten, prior, app, app_data, argv[4]);

	return RESULT_SUCCESS;
}

/* add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static void complete_context_add_extension(int fd, char *line, int pos, char *word, int word_len)
{
	/* complete 'into' word ... */
	if (pos == 3) {
		cw_cli(fd, "into\n");
		return;
	}

	/* complete context */
	if (pos == 4) {
		struct cw_context *c;

		/* try to lock contexts list ... */
		if (!cw_lock_contexts()) {
			/* walk through all contexts */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				/* matching context? */
				if (!strncmp(cw_get_context_name(c), word, word_len))
					cw_cli(fd, "%s\n", cw_get_context_name(c));
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");

		return;
	}

	if (pos == 5)
		cw_cli(fd, "replace\n");

	return;
}

/*
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;

	if (cw_context_add_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_cli(fd, "Out of free memory\n"); break;

			case ENOENT:
				cw_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EEXIST:
				cw_cli(fd, "Ignore pattern '%s' already included in '%s' context\n",
					argv[2], argv[4]);
				break;

			case EBUSY:
				cw_cli(fd, "Failed to lock context(s) list, please, try again later\n");
				break;

			default:
				cw_cli(fd, "Failed to add ingore pattern '%s' into '%s' context\n",
					argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	cw_cli(fd, "Ignore pattern '%s' added into '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static void complete_context_add_ignorepat(int fd, char *line, int pos, char *word, int word_len)
{
	if (pos == 3) {
		cw_cli(fd, "into\n");
		return;
	}

	if (pos == 4) {
		struct cw_context *c;
		char *dupline, *duplinet, *ignorepat = NULL;

		dupline = strdup(line);
		duplinet = dupline;

		if (duplinet) {
			strsep(&duplinet, " "); /* skip 'add' */
			strsep(&duplinet, " "); /* skip 'ignorepat' */
			ignorepat = strsep(&duplinet, " ");
		}

		if (!cw_lock_contexts()) {
			cw_log(CW_LOG_ERROR, "Failed to lock contexts list\n");

			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strncmp(cw_get_context_name(c), word, word_len)) {
					int serve_context = 1;

					if (ignorepat) {
						if (!cw_lock_context(c)) {
							struct cw_ignorepat *ip;
							ip = cw_walk_context_ignorepats(c, NULL);
							while (ip && serve_context) {
								if (!strcmp(cw_get_ignorepat_name(ip), ignorepat))
									serve_context = 0;
								ip = cw_walk_context_ignorepats(c, ip);
							}
							cw_unlock_context(c);
						}
					}

					if (serve_context)
						cw_cli(fd, "%s\n", cw_get_context_name(c));
				}
			}

			if (dupline)
				free(dupline);

			cw_unlock_contexts();
		}
	}

	return;
}

static int handle_context_remove_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "from")) return RESULT_SHOWUSAGE;

	if (cw_context_remove_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case EBUSY:
				cw_cli(fd, "Failed to lock context(s) list, please try again later\n");
				break;

			case ENOENT:
				cw_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EINVAL:
				cw_cli(fd, "There is no existence of '%s' ignore pattern in '%s' context\n",
					argv[2], argv[4]);
				break;

			default:
				cw_cli(fd, "Failed to remove ignore pattern '%s' from '%s' context\n", argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	cw_cli(fd, "Ignore pattern '%s' removed from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static int pbx_load_module(void);

static int handle_reload_extensions(int fd, int argc, char *argv[])
{
	if (argc!=2) return RESULT_SHOWUSAGE;
	pbx_load_module();
	return RESULT_SUCCESS;
}

static void complete_context_remove_ignorepat(int fd, char *line, int pos, char *word, int word_len)
{
	struct cw_context *c;

	if (pos == 2) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					struct cw_ignorepat *ip;
			
					for (ip = cw_walk_context_ignorepats(c, NULL); ip; ip = cw_walk_context_ignorepats(c, ip)) {
						if (!strncmp(cw_get_ignorepat_name(ip), word, word_len)) {
							struct cw_context *cw;
							int already_served = 0;

							for (cw = cw_walk_contexts(NULL); cw && cw != c && !already_served; cw = cw_walk_contexts(cw)) {
								if (!cw_lock_context(cw)) {
									struct cw_ignorepat *ipw;

									for (ipw = cw_walk_context_ignorepats(cw, NULL); ipw; ipw = cw_walk_context_ignorepats(cw, ipw)) {
										if (!strcmp(cw_get_ignorepat_name(ipw), cw_get_ignorepat_name(ip)))
											already_served = 1;
									}

									cw_unlock_context(cw);
								}
							}

							if (!already_served)
								cw_cli(fd, "%s\n", cw_get_ignorepat_name(ip));
						}
					}

					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");

		return;
	}
 
	if (pos == 3) {
		cw_cli(fd, "from\n");
		return;
	}

	if (pos == 4) {
		char *dupline, *duplinet, *ignorepat;

		dupline = strdup(line);
		if (!dupline) {
			cw_log(CW_LOG_ERROR, "Out of memory\n");
			return;
		}

		duplinet = dupline;
		strsep(&duplinet, " ");
		strsep(&duplinet, " ");
		ignorepat = strsep(&duplinet, " ");

		if (!ignorepat) {
			free(dupline);
			return;
		}

		if (cw_lock_contexts()) {
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return;
		}

		for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
			if (!cw_lock_context(c)) {
				struct cw_ignorepat *ip;

				for (ip = cw_walk_context_ignorepats(c, NULL); ip; ip = cw_walk_context_ignorepats(c, ip)) {
					if (!strcmp(cw_get_ignorepat_name(ip), ignorepat) && !strncmp(cw_get_context_name(c), word, word_len))
						cw_cli(fd, "%s\n", cw_get_context_name(c));
				}

				cw_unlock_context(c);
			}
		}

		free(dupline);
		cw_unlock_contexts();
	}

	return;
}

/*
 * CLI entries for commands provided by this module
 */
static struct cw_clicmd context_dont_include_cli = {
	.cmda = { "dont", "include", NULL },
	.handler = handle_context_dont_include,
	.generator = complete_context_dont_include,
	.summary = "Remove a specified include from context",
	.usage = context_dont_include_help,
};

static struct cw_clicmd context_remove_extension_cli = {
	.cmda = { "remove", "extension", NULL },
	.handler = handle_context_remove_extension,
	.generator = complete_context_remove_extension,
	.summary = "Remove a specified extension",
	.usage = context_remove_extension_help,
};

static struct cw_clicmd context_add_include_cli = {
	.cmda = { "include", "context", NULL },
	.handler = handle_context_add_include,
	.generator = complete_context_add_include,
	.summary = "Include context in other context",
	.usage = context_add_include_help,
};

static struct cw_clicmd save_dialplan_cli = {
	.cmda = { "save", "dialplan", NULL },
	.handler = handle_save_dialplan,
	.summary = "Save dialplan",
	.usage = save_dialplan_help,
};

static struct cw_clicmd context_add_extension_cli = {
	.cmda = { "add", "extension", NULL },
	.handler = handle_context_add_extension,
	.generator = complete_context_add_extension,
	.summary = "Add new extension into context",
	.usage = context_add_extension_help,
};

static struct cw_clicmd context_add_ignorepat_cli = {
	.cmda = { "add", "ignorepat", NULL },
	.handler = handle_context_add_ignorepat,
	.generator = complete_context_add_ignorepat,
	.summary = "Add new ignore pattern",
	.usage = context_add_ignorepat_help,
};

static struct cw_clicmd context_remove_ignorepat_cli = {
	.cmda = { "remove", "ignorepat", NULL },
	.handler = handle_context_remove_ignorepat,
	.generator = complete_context_remove_ignorepat,
	.summary = "Remove ignore pattern from context",
	.usage = context_remove_ignorepat_help,
};

static struct cw_clicmd reload_extensions_cli = {
	.cmda = { "extensions", "reload", NULL},
	.handler = handle_reload_extensions,
	.summary = "Reload extensions and *only* extensions",
	.usage = reload_extensions_help,
};

/*
 * Standard module functions ...
 */
static int unload_module(void)
{
	cw_cli_unregister(&context_add_extension_cli);
	if (static_config && !write_protect_config)
		cw_cli_unregister(&save_dialplan_cli);
	cw_cli_unregister(&context_add_include_cli);
	cw_cli_unregister(&context_dont_include_cli);
	cw_cli_unregister(&context_remove_extension_cli);
	cw_cli_unregister(&context_remove_ignorepat_cli);
	cw_cli_unregister(&context_add_ignorepat_cli);
	cw_cli_unregister(&reload_extensions_cli);
	cw_context_destroy(NULL, registrar);
	return 0;
}

static int pbx_load_module(void)
{
	char realvalue[4096];
	struct cw_config *cfg;
	struct cw_variable *v;
	char *cxt, *ext, *pri, *appl, *data, *tc, *cidmatch;
	struct cw_context *con;
	char *end;
	char *label;
	int lastpri = -2;

	cfg = cw_config_load(config);
	if (cfg) {
		/* Use existing config to populate the PBX table */
		static_config = cw_true(cw_variable_retrieve(cfg, "general",
							       "static"));
		write_protect_config = cw_true(cw_variable_retrieve(cfg, "general",
								      "writeprotect"));
		autofallthrough_config = cw_true(cw_variable_retrieve(cfg, "general",
									"autofallthrough"));
		clearglobalvars_config = cw_true(cw_variable_retrieve(cfg, "general", 
									"clearglobalvars"));
		option_priority_jumping = !cw_false(cw_variable_retrieve(cfg, "general",
									   "priorityjumping"));

		v = cw_variable_browse(cfg, "globals");
		while(v) {
			pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
			pbx_builtin_setvar_helper(NULL, v->name, realvalue);
			v = v->next;
		}
		cxt = cw_category_browse(cfg, NULL);
		while(cxt) {
			/* All categories but "general" or "globals" are considered contexts */
			if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals")) {
				cxt = cw_category_browse(cfg, cxt);
				continue;
			}
			if ((con=cw_context_create(&local_contexts,cxt, registrar))) {
				v = cw_variable_browse(cfg, cxt);
				while(v) {
					if (!strcasecmp(v->name, "exten")) {
						char *stringp=NULL;
						int ipri = -2;
						char realext[256]="";
						char *plus, *firstp, *firstc;
						tc = strdup(v->value);
						if(tc!=NULL){
							stringp=tc;
							ext = strsep(&stringp, ",");
							if (!ext)
								ext="";
							pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext));
							cidmatch = strchr(realext, '/');
							if (cidmatch) {
								*cidmatch = '\0';
								cidmatch++;
								cw_shrink_phone_number(cidmatch);
							}
							pri = strsep(&stringp, ",");
							if (!pri)
								pri="";
							label = strchr(pri, '(');
							if (label) {
								*label = '\0';
								label++;
								end = strchr(label, ')');
								if (end)
									*end = '\0';
								else
									cw_log(CW_LOG_WARNING, "Label missing trailing ')' at line %d\n", v->lineno);
							}
							plus = strchr(pri, '+');
							if (plus) {
								*plus = '\0';
								plus++;
							}
							if (!strcmp(pri,"hint"))
								ipri=PRIORITY_HINT;
							else if (!strcmp(pri, "next") || !strcmp(pri, "n")) {
								if (lastpri > -2)
									ipri = lastpri + 1;
								else
									cw_log(CW_LOG_WARNING, "Can't use 'next' priority on the first entry!\n");
							} else if (!strcmp(pri, "same") || !strcmp(pri, "s")) {
								if (lastpri > -2)
									ipri = lastpri;
								else
									cw_log(CW_LOG_WARNING, "Can't use 'same' priority on the first entry!\n");
							} else  {
								if (sscanf(pri, "%d", &ipri) != 1) {
									if ((ipri = cw_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
										cw_log(CW_LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
										ipri = 0;
									}
								}
							}
							appl = stringp;
							if (!appl)
								appl="";
							/* Find the first occurrence of either '(' or ',' */
							firstc = strchr(appl, ',');
							firstp = strchr(appl, '(');
							if (firstc && ((!firstp) || (firstc < firstp))) {
								/* comma found, no parenthesis */
								/* or both found, but comma found first */
								appl = strsep(&stringp, ",");
								data = stringp;
							} else if ((!firstc) && (!firstp)) {
								/* Neither found */
								data = "";
							} else {
								/* Final remaining case is parenthesis found first */
								appl = strsep(&stringp, "(");
								data = stringp;
								end = strrchr(data, ')');
								if ((end = strrchr(data, ')'))) {
									*end = '\0';
								} else {
									cw_log(CW_LOG_WARNING, "No closing parenthesis found? '%s(%s'\n", appl, data);
								}
							}

							if (!data)
								data="";
							appl = cw_skip_blanks(appl);
							if (ipri) {
								if (plus)
									ipri += atoi(plus);
								lastpri = ipri;
								if(!option_dontwarn) {
									if (!strcmp(realext, "_."))
										cw_log(CW_LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
								}
								if (cw_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), FREE, registrar)) {
									cw_log(CW_LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
								}
							}
							free(tc);
						} else 
						    cw_log(CW_LOG_ERROR,"Error strdup returned NULL in %s\n",__PRETTY_FUNCTION__);
					} else if(!strcasecmp(v->name, "include")) {
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						if (cw_context_add_include2(con, realvalue, registrar))
							cw_log(CW_LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
					} else if(!strcasecmp(v->name, "ignorepat")) {
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						if (cw_context_add_ignorepat2(con, realvalue, registrar))
							cw_log(CW_LOG_WARNING, "Unable to include ignorepat '%s' in context '%s'\n", v->value, cxt);
					} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
						char *stringp=NULL;
						if (!strcasecmp(v->name, "switch"))
							pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						else
							strncpy(realvalue, v->value, sizeof(realvalue) - 1);
						tc = realvalue;
						stringp=tc;
						appl = strsep(&stringp, "/");
						data = strsep(&stringp, "");
						if (!data)
							data = "";
						if (cw_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar))
							cw_log(CW_LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
					}
					v = v->next;
				}
			}
			cxt = cw_category_browse(cfg, cxt);
		}
		cw_config_destroy(cfg);
	}
	cw_merge_contexts_and_delete(&local_contexts,registrar);

	for (con = cw_walk_contexts(NULL); con; con = cw_walk_contexts(con))
		cw_context_verify_includes(con);

	pbx_set_autofallthrough(autofallthrough_config);

	return 0;
}

static int load_module(void)
{
	if (pbx_load_module()) return -1;
 
	cw_cli_register(&context_remove_extension_cli);
	cw_cli_register(&context_dont_include_cli);
	cw_cli_register(&context_add_include_cli);
	if (static_config && !write_protect_config)
		cw_cli_register(&save_dialplan_cli);
	cw_cli_register(&context_add_extension_cli);
	cw_cli_register(&context_add_ignorepat_cli);
	cw_cli_register(&context_remove_ignorepat_cli);
	cw_cli_register(&reload_extensions_cli);

	return 0;
}

static int reload_module(void)
{
	cw_context_destroy(NULL, registrar);
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
	pbx_load_module();
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, dtext)
