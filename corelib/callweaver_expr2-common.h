/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
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

#include "callweaver/channel.h"


/* printf format to use when converting a number (long double) to text */
#define NUMBER_FORMAT	"%.18Lg"

/* Maximum length that a number printed with NUMBER_FORMAT can ever be */
#define MAX_NUMBER_LEN	32


enum valtype {
	CW_EXPR_number, CW_EXPR_string, CW_EXPR_numeric_string, CW_EXPR_arbitrary_string
};

struct val {
	enum valtype type;
	union {
		long double n;
		char s[1];
	} u;
};

struct parse_io
{
	yyscan_t scanner;
	struct cw_channel *chan;
	struct val *val;
	const char *string;
	int noexec;
};


extern struct val *cw_expr_make_str(enum valtype type, const char *s, size_t len);
extern struct val *cw_expr_make_number(long double n);
