/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Language related dialplan functions
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "openpbx.h"

/* OPENPBX_FILE_VERSION(__FILE__, "$Revision$") */

#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/logger.h"
#include "openpbx/utils.h"
#include "openpbx/app.h"

static char *builtin_function_language_read(struct opbx_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	opbx_copy_string(buf, chan->language, len);

	return buf;
}

static void builtin_function_language_write(struct opbx_channel *chan, char *cmd, char *data, const char *value) 
{
	if (value)
		opbx_copy_string(chan->language, value, sizeof(chan->language));
}

#ifndef BUILTIN_FUNC
static
#endif
struct opbx_custom_function language_function = {
	.name = "LANGUAGE",
	.synopsis = "Gets or sets the channel's language.",
	.syntax = "LANGUAGE()",
	.desc = "Gets or sets the channel language.  This information is used for the\n"
	"syntax in generation of numbers, and to choose a natural language file\n"
	"when available.  For example, if language is set to 'fr' and the file\n"
	"'demo-congrats' is requested to be played, if the file\n"
	"'fr/demo-congrats' exists, then it will play that file, and if not\n"
	"will play the normal 'demo-congrats'.  For some language codes,\n"
	"changing the language also changes the syntax of some OpenPBX\n"
	"functions, like SayNumber.\n",
	.read = builtin_function_language_read,
	.write = builtin_function_language_write,
};

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
