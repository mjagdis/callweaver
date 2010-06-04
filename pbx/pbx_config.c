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
static const char *config = "extensions.conf";
static const char *registrar = "pbx_config";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 0;
static int clearglobalvars_config = 0;

CW_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct cw_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static const char context_dont_include_help[] =
"Usage: dont include <context> in <context>\n"
"       Remove an included context from another context.\n";

static const char context_remove_extension_help[] =
"Usage: remove extension exten@context [priority]\n"
"       Remove an extension from a given context. If a priority\n"
"       is given, only that specific priority from the given extension\n"
"       will be removed.\n";

static const char context_add_include_help[] =
"Usage: include <context> in <context>\n"
"       Include a context in another context.\n";

static const char save_dialplan_help[] =
"Usage: save dialplan [/path/to/extension/file]\n"
"       Save dialplan created by pbx_config module.\n"
"\n"
"Example: save dialplan                 (/etc/callweaver/extensions.conf)\n"
"         save dialplan /home/markster  (/home/markster/extensions.conf)\n";

static const char context_add_extension_help[] =
"Usage: add extension <exten>,<priority>,<app>,<app-data> into <context>\n"
"       [replace]\n\n"
"       This command will add new extension into <context>. If there is an\n"
"       existence of extension with the same priority and last 'replace'\n"
"       arguments is given here we simply replace this extension.\n"
"\n"
"Example: add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
"         Now, you can dial 6123 and talk to Markster :)\n";

static const char context_add_ignorepat_help[] =
"Usage: add ignorepat <pattern> into <context>\n"
"       This command adds a new ignore pattern into context <context>\n"
"\n"
"Example: add ignorepat _3XX into local\n";

static const char context_remove_ignorepat_help[] =
"Usage: remove ignorepat <pattern> from <context>\n"
"       This command removes an ignore pattern from context <context>\n"
"\n"
"Example: remove ignorepat _3XX from local\n";

static const char reload_extensions_help[] =
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
static int handle_context_dont_include(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (!cw_context_remove_include(argv[4], argv[2], registrar)) {
		cw_dynstr_printf(ds_p, "We are not including '%s' in '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	cw_dynstr_printf(ds_p, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

static void complete_context_dont_include(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;
	struct cw_include *i;
	int done;

	if (lastarg == 2) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
						if (!strncmp(argv[2], cw_get_include_name(i), lastarg_len))
							cw_dynstr_printf(ds_p, "%s\n", cw_get_include_name(i));
					}

					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}

	/*
	 * 'in' completion ... (complete only if previous context is really
	 * included somewhere)
	 */
	else if (lastarg == 3 && !strncmp(argv[3], "in", lastarg_len)) {
		if (!cw_lock_contexts()) {
			done = 0;
			/* go through all contexts and check if is included ... */
			for (c = cw_walk_contexts(NULL); c && !done; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
						/* is it our context? */
						if (!strcmp(cw_get_include_name(i), argv[2])) {
							/* yes, it is, context is really included, so
							 * complete "in" command
							 */
							cw_dynstr_printf(ds_p, "in\n");
							done = 1;
							break;
						}
					}
					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
	}

	/*
	 * Context from which we are removing include ... 
	 */
	else if (lastarg == 4) {
		/* fourth word must be in */
		if (!strcmp(argv[3], "in") && !cw_lock_contexts()) {
			/* walk through all contexts ... */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					/* walk through all includes and check if it is our context */	
					for (i = cw_walk_context_includes(c, NULL); i; i = cw_walk_context_includes(c, i)) {
						/* is in this context included another on which we want to
						 * remove?
						 */
						if (!strcmp(argv[2], cw_get_include_name(i))) {
							/* yes, it's included, is matching our word too? */
							if (!strncmp(cw_get_context_name(c), argv[4], lastarg_len))
								cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c));
							break;
						}
					}	
					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}
}

/*
 * REMOVE EXTENSION command stuff
 */
static int handle_context_remove_extension(struct cw_dynstr *ds_p, int argc, char *argv[])
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
				cw_dynstr_printf(ds_p, "Invalid priority '%s'\n", argv[3]);
				return RESULT_FAILURE;
			}
		    }
		    removing_priority = atoi(argv[3]);
		} else
		    removing_priority = PRIORITY_HINT;

		if (removing_priority == 0) {
			cw_dynstr_printf(ds_p, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/*
	 * Format exten@context checking ...
	 */
	if (!(context = strchr(argv[2], (int)'@'))) {
		cw_dynstr_printf(ds_p, "First argument must be in exten@context format\n");
		return RESULT_FAILURE;
	}

	*context++ = '\0';
	exten = argv[2];
	if ((!strlen(exten)) || (!(strlen(context)))) {
		cw_dynstr_printf(ds_p, "Missing extension or context name in second argument '%s@%s'\n",
			exten == NULL ? "?" : exten, context == NULL ? "?" : context);
		return RESULT_FAILURE;
	}

	if (!cw_context_remove_extension(context, exten, removing_priority, registrar)) {
		if (!removing_priority)
			cw_dynstr_printf(ds_p, "Whole extension %s@%s removed\n",
				exten, context);
		else
			cw_dynstr_printf(ds_p, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		return RESULT_SUCCESS;
	}

	cw_dynstr_printf(ds_p, "Failed to remove extension %s@%s\n", exten, context);

	return RESULT_FAILURE;
}


static void complete_context_remove_extension(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;
	struct cw_exten *e;
	char *context = NULL, *exten = NULL, *delim = NULL;
	int context_len = 0, exten_len = 0;

	/*
	 * exten@context completion ... 
	 */
	if (lastarg == 2) {
		/* now, parse values from word = exten@context */
		if ((delim = strchr(argv[2], '@'))) {
			/* check for duplicates ... */
			if (delim != strrchr(argv[2], '@'))
				return;

			*delim = '\0';
			exten = argv[2];
			context = delim + 1;
			exten_len = delim - argv[2];
			context_len = lastarg_len - exten_len - 1;
		} else {
			exten = argv[2];
			exten_len = lastarg_len;
		}

		if (!cw_lock_contexts()) {
			/* find our context ... */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				/* our context? */
				if (!context || !strncmp(cw_get_context_name(c), context, context_len)) {
					/* try to complete extensions ... */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
						/* our extension? */
						if ((context && !strcmp(cw_get_extension_name(e), exten))
						|| (!context && !strncmp(cw_get_extension_name(e), exten, exten_len))) {
							if (exten)
								cw_dynstr_printf(ds_p, "%s@%s\n", cw_get_extension_name(e), cw_get_context_name(c));
						}
					}
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");

		if (delim)
			*delim = '@';
	}

	/*
	 * Complete priority ...
	 */
	else if (lastarg == 3) {
		/* wrong exten@context format? */
		if (!(delim = strchr(argv[2], '@')) || delim != strrchr(argv[2], '@'))
			return;

		/* check if there is exten and context too ... */
		*delim = '\0';
		if ((!strlen(argv[2])) || (!delim[1])) {
			*delim = '@';
			return;
		}

		exten = argv[2];
		context = delim + 1;

		if (!cw_lock_contexts()) {
			/* walk contexts */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strcmp(cw_get_context_name(c), context)) {
					/* walk extensions */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
						if (!strcmp(cw_get_extension_name(e), exten)) {
							struct cw_exten *priority;
							char buffer[10];
					
							for (priority = cw_walk_extension_priorities(e, NULL); priority; priority = cw_walk_extension_priorities(e, priority)) {
								snprintf(buffer, 10, "%d", cw_get_extension_priority(priority));
								if (!strncmp(argv[3], buffer, lastarg_len))
									cw_dynstr_printf(ds_p, "%s\n", buffer);
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

		*delim = '@';
	}
}

/*
 * Include context ...
 */
static int handle_context_add_include(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	/* third arg must be 'in' ... */
	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (cw_context_add_include(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_dynstr_printf(ds_p, "Out of memory for context addition\n"); break;

			case EBUSY:
				cw_dynstr_printf(ds_p, "Failed to lock context(s) list, please try again later\n"); break;

			case EEXIST:
				cw_dynstr_printf(ds_p, "Context '%s' already included in '%s' context\n",
					argv[1], argv[3]); break;

			case ENOENT:
			case EINVAL:
				cw_dynstr_printf(ds_p, "There is no existence of context '%s'\n",
					errno == ENOENT ? argv[4] : argv[2]); break;

			default:
				cw_dynstr_printf(ds_p, "Failed to include '%s' in '%s' context\n",
					argv[1], argv[3]); break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	cw_dynstr_printf(ds_p, "Context '%s' included in '%s' context\n",
		argv[2], argv[3]);

	return RESULT_SUCCESS;
}

static void complete_context_add_include(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c, *c2;

	if (lastarg == 1) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strncmp(cw_get_context_name(c), argv[1], lastarg_len))
					cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c));
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}

	/* complete 'in' only if context exist ... */
	else if (lastarg == 2 && !strncmp(argv[2], "in", lastarg_len)) {
		/* check for context existence ... */
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strcmp(argv[1], cw_get_context_name(c))) {
					cw_dynstr_printf(ds_p, "in\n");
					break;
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}

	/* serve context into which we include another context */
	else if (lastarg == 3 && !strcmp(argv[2], "in")) {
		if (!cw_lock_contexts()) {
			/* check for context existence ... */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strcmp(argv[1], cw_get_context_name(c))) {
					/* go through all contexts ... */
					for (c2 = cw_walk_contexts(NULL); c2; c2 = cw_walk_contexts(c2)) {
						/* must be different contexts ... */
						if (c2 != c && !strncmp(cw_get_context_name(c2), argv[3], lastarg_len))
							cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c2));
					}
					break;
				}
			}
			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");
	}
}

/*
 * 'save dialplan' CLI command implementation functions ...
 */
static int handle_save_dialplan(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char *filename;
	struct cw_context *c;
	struct cw_config *cfg;
	struct cw_variable *v;
	FILE *output;
	int context_header_written;
	int incomplete = 0; /* incomplete config write? */
	int res = RESULT_FAILURE;

	if (! (static_config && !write_protect_config)) {
		cw_dynstr_printf(ds_p, "I can't save dialplan now, see '%s' example file.\n", config);
		return RESULT_FAILURE;
	}

	if (argc != 2 && argc != 3) return RESULT_SHOWUSAGE;

	cw_mutex_lock(&save_dialplan_lock);

	/* have config path? */
	if (argc == 3) {
		/* is there extension.conf too? */
		if (strstr(argv[2], ".conf")) {
			filename = argv[2];
		} else {
			/* no, only directory path, check for last '/' occurence */
			filename = alloca(strlen(argv[2]) + 1 + strlen(config) + 1);
			sprintf(filename, "%s%s%s", argv[2], (*(argv[2] + strlen(argv[2]) -1) == '/' ? "" : "/"), config);
		}
	} else {
		/* no config file, default one */
		filename = alloca(strlen(cw_config[CW_CONFIG_DIR]) + 1 + strlen(config) + 1);
		sprintf(filename, "%s/%s", cw_config[CW_CONFIG_DIR], config);
	}

	cfg = cw_config_load("extensions.conf");

	cw_lock_contexts();

	/* create new file ... */
	if ((output = fopen(filename, "wt"))) {
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
						fprintf(output, "include => %s\n", cw_get_include_name(i));
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

						fprintf(output, "ignorepat => %s\n", cw_get_ignorepat_name(ip));
					}
					ip = cw_walk_context_ignorepats(c, ip);
				}

				cw_unlock_context(c);
			} else
				incomplete = 1;

			c = cw_walk_contexts(c);
		}

		fclose(output);
	} else
		cw_dynstr_printf(ds_p, "Failed to create file '%s'\n", filename);


	cw_unlock_contexts();
	cw_config_destroy(cfg);
	cw_mutex_unlock(&save_dialplan_lock);

	if (!incomplete) {
		cw_dynstr_printf(ds_p, "Dialplan successfully saved into '%s'\n", filename);
		res = RESULT_SUCCESS;
	} else
		cw_dynstr_printf(ds_p, "Saved dialplan is incomplete\n");

	return res;
}

/*
 * ADD EXTENSION command stuff
 */
static int handle_context_add_extension(struct cw_dynstr *ds_p, int argc, char *argv[])
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
				cw_dynstr_printf(ds_p, "'%s' is not a valid priority\n", prior);
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
		app_data = (char *)"";
	if (cw_add_extension(argv[4], argc == 6 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), free, registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_dynstr_printf(ds_p, "Out of free memory\n"); break;

			case EBUSY:
				cw_dynstr_printf(ds_p, "Failed to lock context(s) list, please try again later\n"); break;

			case ENOENT:
				cw_dynstr_printf(ds_p, "No existence of '%s' context\n", argv[4]); break;

			case EEXIST:
				cw_dynstr_printf(ds_p, "Extension %s@%s with priority %s already exists\n",
					exten, argv[4], prior); break;

			default:
				cw_dynstr_printf(ds_p, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[4]); break;
		}
		return RESULT_FAILURE;
	}

	if (argc == 6) 
		cw_dynstr_printf(ds_p, "Extension %s@%s (%s) replace by '%s,%s,%s,%s'\n",
			exten, argv[4], prior, exten, prior, app, app_data);
	else
		cw_dynstr_printf(ds_p, "Extension '%s,%s,%s,%s' added into '%s' context\n",
			exten, prior, app, app_data, argv[4]);

	return RESULT_SUCCESS;
}

/* add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static void complete_context_add_extension(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;

	/* complete 'into' word ... */
	if (lastarg == 3) {
		if (!strncmp(argv[3], "into", lastarg_len))
			cw_dynstr_printf(ds_p, "into\n");
	}

	/* complete context */
	else if (lastarg == 4) {
		/* try to lock contexts list ... */
		if (!cw_lock_contexts()) {
			/* walk through all contexts */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				/* matching context? */
				if (!strncmp(cw_get_context_name(c), argv[4], lastarg_len))
					cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c));
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
	}

	else if (lastarg == 5)
		if (!strncmp(argv[5], "replace", lastarg_len))
			cw_dynstr_printf(ds_p, "replace\n");
}

/*
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;

	if (cw_context_add_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				cw_dynstr_printf(ds_p, "Out of free memory\n"); break;

			case ENOENT:
				cw_dynstr_printf(ds_p, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EEXIST:
				cw_dynstr_printf(ds_p, "Ignore pattern '%s' already included in '%s' context\n",
					argv[2], argv[4]);
				break;

			case EBUSY:
				cw_dynstr_printf(ds_p, "Failed to lock context(s) list, please, try again later\n");
				break;

			default:
				cw_dynstr_printf(ds_p, "Failed to add ingore pattern '%s' into '%s' context\n",
					argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	cw_dynstr_printf(ds_p, "Ignore pattern '%s' added into '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static void complete_context_add_ignorepat(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;

	if (lastarg == 3) {
		if (!strncmp(argv[3], "into", lastarg_len))
			cw_dynstr_printf(ds_p, "into\n");
	}

	else if (lastarg == 4) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strncmp(cw_get_context_name(c), argv[4], lastarg_len)) {
					int serve_context = 1;

					if (!cw_lock_context(c)) {
						struct cw_ignorepat *ip;
						for (ip = cw_walk_context_ignorepats(c, NULL); ip && serve_context; ip = cw_walk_context_ignorepats(c, ip)) {
							if (!strcmp(cw_get_ignorepat_name(ip), argv[2]))
								serve_context = 0;
						}
						cw_unlock_context(c);
					}

					if (serve_context)
						cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c));
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock contexts list\n");
	}
}

static int handle_context_remove_ignorepat(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "from")) return RESULT_SHOWUSAGE;

	if (cw_context_remove_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case EBUSY:
				cw_dynstr_printf(ds_p, "Failed to lock context(s) list, please try again later\n");
				break;

			case ENOENT:
				cw_dynstr_printf(ds_p, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EINVAL:
				cw_dynstr_printf(ds_p, "There is no existence of '%s' ignore pattern in '%s' context\n",
					argv[2], argv[4]);
				break;

			default:
				cw_dynstr_printf(ds_p, "Failed to remove ignore pattern '%s' from '%s' context\n", argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	cw_dynstr_printf(ds_p, "Ignore pattern '%s' removed from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static int pbx_load_module(void);

static int handle_reload_extensions(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(ds_p);
	CW_UNUSED(argv);

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	pbx_load_module();
	return RESULT_SUCCESS;
}

static void complete_context_remove_ignorepat(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;

	if (lastarg == 2) {
		if (!cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					struct cw_ignorepat *ip;
			
					for (ip = cw_walk_context_ignorepats(c, NULL); ip; ip = cw_walk_context_ignorepats(c, ip)) {
						if (!strncmp(cw_get_ignorepat_name(ip), argv[2], lastarg_len))
							cw_dynstr_printf(ds_p, "%s\n", cw_get_ignorepat_name(ip));
					}

					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
	}
 
	else if (lastarg == 3) {
		if (!strncmp(argv[3], "from", lastarg_len))
			cw_dynstr_printf(ds_p, "from\n");
	}

	else if (lastarg == 4) {
		if (cw_lock_contexts()) {
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!cw_lock_context(c)) {
					struct cw_ignorepat *ip;

					for (ip = cw_walk_context_ignorepats(c, NULL); ip; ip = cw_walk_context_ignorepats(c, ip)) {
						if (!strcmp(cw_get_ignorepat_name(ip), argv[2]) && !strncmp(cw_get_context_name(c), argv[4], lastarg_len))
							cw_dynstr_printf(ds_p, "%s\n", cw_get_context_name(c));
					}

					cw_unlock_context(c);
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_WARNING, "Failed to lock contexts list\n");
	}
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
	.cmda = { "include", NULL },
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
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	struct cw_config *cfg;
	struct cw_variable *v;
	char *cxt, *ext, *pri, *appl, *data, *tc, *cidmatch;
	struct cw_context *con;
	char *end;
	char *label;
	int lastpri = -2;
	int res = -1;

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

		for (v = cw_variable_browse(cfg, "globals"); v; v = v->next) {
			pbx_substitute_variables(NULL, NULL, v->value, &ds);
			if (ds.error || cw_split_args(NULL, ds.data, "", '\0', NULL))
				goto err;
			pbx_builtin_setvar_helper(NULL, v->name, ds.data);
			cw_dynstr_reset(&ds);
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
						char *stringp;
						char *plus, *firstp, *firstc;
						int ipri = -2;

						tc = strdup(v->value);
						if(tc!=NULL){
							stringp = tc;
							ext = strsep(&stringp, ",");
							if (!ext)
								ext = (char *)"";
							pbx_substitute_variables(NULL, NULL, ext, &ds);
							if (!ds.error && !cw_split_args(NULL, ds.data, "", '\0', NULL)) {
								cidmatch = strchr(ds.data, '/');
								if (cidmatch) {
									*cidmatch = '\0';
									cidmatch++;
									cw_shrink_phone_number(cidmatch);
								}
								pri = strsep(&stringp, ",");
								if (!pri)
									pri = (char *)"";
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
										if ((ipri = cw_findlabel_extension2(NULL, con, ds.data, pri, cidmatch)) < 1) {
											cw_log(CW_LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
											ipri = 0;
										}
									}
								}
								appl = stringp;
								if (!appl)
									appl = (char *)"";
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
									data = (char *)"";
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
									data = (char *)"";
								appl = cw_skip_blanks(appl);
								if (ipri) {
									if (plus)
										ipri += atoi(plus);
									lastpri = ipri;
									if(!option_dontwarn) {
										if (!strcmp(ds.data, "_."))
											cw_log(CW_LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
									}
									if (cw_add_extension2(con, 0, ds.data, ipri, label, cidmatch, appl, strdup(data), FREE, registrar)) {
										cw_log(CW_LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
									}
								}
							}

							free(tc);
						} else
						    cw_log(CW_LOG_ERROR,"Error strdup returned NULL in %s\n",__PRETTY_FUNCTION__);
					} else if(!strcasecmp(v->name, "include")) {
						pbx_substitute_variables(NULL, NULL, v->value, &ds);
						if (!ds.error && !cw_split_args(NULL, ds.data, "", '\0', NULL)
						&& cw_context_add_include2(con, ds.data, registrar))
							cw_log(CW_LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
					} else if(!strcasecmp(v->name, "ignorepat")) {
						pbx_substitute_variables(NULL, NULL, v->value, &ds);
						if (!ds.error && !cw_split_args(NULL, ds.data, "", '\0', NULL)
						&& cw_context_add_ignorepat2(con, ds.data, registrar))
							cw_log(CW_LOG_WARNING, "Unable to include ignorepat '%s' in context '%s'\n", v->value, cxt);
					} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
						char *stringp;

						if (!strcasecmp(v->name, "switch")) {
							pbx_substitute_variables(NULL, NULL, v->value, &ds);
							if (!ds.error && cw_split_args(NULL, ds.data, "", '\0', NULL))
								ds.error = 1;
						} else
							cw_dynstr_printf(&ds, "%s", v->value);

						if (!ds.error) {
							stringp = ds.data;
							appl = strsep(&stringp, "/");
							data = strsep(&stringp, "");

							if (!data)
								data = (char *)"";
							if (cw_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar))
								cw_log(CW_LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
						}
					}

					cw_dynstr_reset(&ds);
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

	res = 0;

err:
	cw_dynstr_free(&ds);

	return res;
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
