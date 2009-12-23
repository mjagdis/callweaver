/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005
 *
 * Oleksiy Krivoshey <oleksiyk@gmail.com>
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
 * \brief ENUM Functions
 * \arg See also cwENUM
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"

#include "callweaver/pbx.h"
#include "callweaver/options.h"

#include "callweaver/enum.h"

static void *enum_function;
static const char enum_func_name[] = "ENUMLOOKUP";
static const char enum_func_synopsis[] = "ENUMLOOKUP allows for general or specific querying of NAPTR records"
		" or counts of NAPTR types for ENUM or ENUM-like DNS pointers";
static const char enum_func_syntax[] = "ENUMLOOKUP(number[, Method-type[, options|record#[, zone-suffix]]])";
static const char enum_func_desc[] =
	"Option 'c' returns an integer count of the number of NAPTRs of a certain RR type.\n"
	"Option '*%d*' (e.g. result%d) returns an integer count of the matched NAPTRs and sets\n"
	"the results in variables (e.g. result1, result2, ...result<n>)\n"
	"Combination of 'c' and Method-type of 'ALL' will return a count of all NAPTRs for the record.\n"
	"Defaults are: Method-type=sip, no options, record=1, zone-suffix=e164.arpa\n\n"
	"For more information, see README.enum";

static void *txtcidname_function;
static const char txtcidname_func_name[] = "TXTCIDNAME";
static const char txtcidname_func_synopsis[] = "TXTCIDNAME looks up a caller name via DNS";
static const char txtcidname_func_syntax[] = "TXTCIDNAME(number)";
static const char txtcidname_func_desc[] = "This function looks up the given phone number in DNS to retrieve\n"
		"the caller id name.  The result will either be blank or be the value\n"
		"found in the TXT record in DNS.\n";


static int function_enum(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
       char tech[80];
       struct localuser *u;
       const char *suffix, *options;
       char *p, *s;

       if (argc < 1 || argc == arraysize(argv) || !argv[0][0])
               return cw_function_syntax(enum_func_syntax);

       /* A certain application from which CallWeaver was originally
	* derived changed argument parsing at some stage and, possibly
	* inadvertantly, made options and record# separate arguments.
	* They aren't. They're mutually exclusive. A record# is just
	* a numeric option. We support options and record# as either
	* the same or distinct arguments. This sucks but it avoids
	* unpleasantness for people upgrading to CallWeaver.
	*/
	if (argc >= 4) {
		if ((!argv[2][0] && isdigit(argv[3][0])) || (argv[2][0] && !argv[3][0])) {
			cw_log(CW_LOG_WARNING, "options and record# are the same argument!\n");
			if (!argv[2][0])
				argv[2] = argv[3];
			argv[3] = argv[4];
			argc--;
		}
       }

	if (buf) {
		cw_copy_string(tech, (argc < 1 || !argv[1][0] ? "sip" : argv[1]), sizeof(tech));

		options = (argc < 2 || !argv[2][0] ? "1" : argv[2]);

		suffix = (argc < 3 || !argv[3][0] ? "e164.arpa" : argv[3]);

		/* strip any '-' signs from number */
		for (s = p = argv[0]; *s; s++)
			if (*s != '-')
				*(p++) = *s;
		*p = '\0';

		LOCAL_USER_ADD(u);

		/* N.B. The oly reason cw_get_enum returns tech is to support
		 * the old (and deprecated) apps/app_enum which hardcodes a mapping
		 * from enum method to channel technology. With funcs/func_enum
		 * you're expected to do it yourself in the dialplan.
		 */
		cw_get_enum(chan, argv[0], buf, len, tech, sizeof(tech), suffix, options);

		LOCAL_USER_REMOVE(u);

		if ((p = strchr(buf, ':')) && strcasecmp(argv[1], "ALL"))
			cw_copy_string(buf, p+1, len);
	}

       return 0;
}

static int function_txtcidname(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	int res;
	char tech[80];
	char txt[256] = "";
	char dest[80];
	struct localuser *u;

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(txtcidname_func_syntax);

	if (buf) {
		LOCAL_USER_ADD(u);

		res = cw_get_txt(chan, argv[0], dest, sizeof(dest), tech, sizeof(tech), txt, sizeof(txt));

		if (!cw_strlen_zero(txt))
	        	cw_copy_string(buf, txt, len);

		LOCAL_USER_REMOVE(u);
	}

	return 0;
}


static const char tdesc[] = "ENUMLOOKUP allows for general or specific querying of NAPTR records or counts of NAPTR types for ENUM or ENUM-like DNS pointers";

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(enum_function);
	res |= cw_unregister_function(txtcidname_function);
	return res;
}

static int load_module(void)
{
	enum_function = cw_register_function(enum_func_name, function_enum, enum_func_synopsis, enum_func_syntax, enum_func_desc);
	txtcidname_function = cw_register_function(txtcidname_func_name, function_txtcidname, txtcidname_func_synopsis, txtcidname_func_syntax, txtcidname_func_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
