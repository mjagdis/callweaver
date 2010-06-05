%{
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
 *
 *
 * This is a rewrite of the previous code:
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Dialplan Expression Lexical Scanner
 */

#include "callweaver.h"

#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver/callweaver_expr.h"
#include "callweaver/logger.h"
#include "callweaver/pbx.h"
#include "callweaver/strings.h"

#include "callweaver_expr2-common.h"
#include "callweaver_expr2.h" /* the o/p of the bison on callweaver_expr2.y */


struct val *cw_expr_make_str(enum valtype type, const char *s, size_t len)
{
	struct val *vp = NULL;
	size_t size;

	/* A value is always able to hold a number */
	size = offsetof(struct val, u.s) + len + 1;
	if ((vp = malloc(size >= sizeof(struct val) ? size : sizeof(struct val)))) {
		vp->type = type;
		memcpy(&vp->u.s[0], s, len);
		vp->u.s[len] = '\0';
	} else
		cw_log(CW_LOG_ERROR, "Out of memory!\n");

	return vp;
}


struct val *cw_expr_make_number(long double n)
{
	struct val *vp = NULL;

	if ((vp = malloc(sizeof(*vp)))) {
		vp->type = CW_EXPR_number;
		vp->u.n  = n;
	} else
		cw_log(CW_LOG_ERROR, "Out of memory!\n");

	return vp;

}


#ifndef YY_NO_INPUT
#  ifdef __cplusplus
    static int yyinput(yyscan_t yyscanner) __attribute__ ((__unused__));
#  else
    static int input(yyscan_t yyscanner) __attribute__ ((__unused__));
#  endif
#endif


#if defined(__GNUC__)
#  pragma GCC system_header
#elif defined __SUNPRO_CC
#  pragma disable_warn
#elif defined(_MSC_VER)
#  pragma warning(push, 1)
#endif

%}

%option prefix="cw_yy"
%option batch
%option outfile="callweaver_expr2f.c"
%option reentrant
%option extra-type="struct parse_io *"
%option bison-bridge
%option bison-locations
%option noyywrap

%%

\|		{ return TOK_OR; }
\&		{ return TOK_AND; }
\=		{ return TOK_EQ; }
\|\|		{ return TOK_OR; }
\&\&		{ return TOK_AND; }
\=\=		{ return TOK_EQ; }
\=~		{ return TOK_EQTILDE; }
\>		{ return '>'; }
\<		{ return '<'; }
\>\=		{ return TOK_GE; }
\<\=		{ return TOK_LE; }
\!\=		{ return TOK_NE; }
\+		{ return '+'; }
\-		{ return '-'; }
\*		{ return '*'; }
\/		{ return '/'; }
\%		{ return '%'; }
\?		{ return '?'; }
\!		{ return '!'; }
\:		{ return ':'; }
\:\:		{ return TOK_COLONCOLON; }
\(		{ return '('; }
\)		{ return ')'; }
\,		{ return ','; }

ACOSH		{ return (yylval->tok = TOK_ACOSH); }
ACOS		{ return (yylval->tok = TOK_ACOS); }
ASINH		{ return (yylval->tok = TOK_ASINH); }
ASIN		{ return (yylval->tok = TOK_ASIN); }
ATAN2		{ return (yylval->tok = TOK_ATAN2); }
ATAN		{ return (yylval->tok = TOK_ATAN); }
CBRT		{ return (yylval->tok = TOK_CBRT); }
CEIL		{ return (yylval->tok = TOK_CEIL); }
COPYSIGN	{ return (yylval->tok = TOK_COPYSIGN); }
COSH		{ return (yylval->tok = TOK_COSH); }
COS		{ return (yylval->tok = TOK_COS); }
ERFC		{ return (yylval->tok = TOK_ERFC); }
ERF		{ return (yylval->tok = TOK_ERF); }
EXP2		{ return (yylval->tok = TOK_EXP2); }
EXP		{ return (yylval->tok = TOK_EXP); }
EXPM1		{ return (yylval->tok = TOK_EXPM1); }
FABS		{ return (yylval->tok = TOK_FABS); }
FDIM		{ return (yylval->tok = TOK_FDIM); }
FLOOR		{ return (yylval->tok = TOK_FLOOR); }
FMA		{ return (yylval->tok = TOK_FMA); }
FMAX		{ return (yylval->tok = TOK_FMAX); }
FMIN		{ return (yylval->tok = TOK_FMIN); }
FMOD		{ return (yylval->tok = TOK_FMOD); }
HYPOT		{ return (yylval->tok = TOK_HYPOT); }
LGAMMA		{ return (yylval->tok = TOK_LGAMMA); }
LOG10		{ return (yylval->tok = TOK_LOG10); }
LOG1P		{ return (yylval->tok = TOK_LOG1P); }
LOG2		{ return (yylval->tok = TOK_LOG2); }
LOGB		{ return (yylval->tok = TOK_LOGB); }
LOG		{ return (yylval->tok = TOK_LOG); }
NEARBYINT	{ return (yylval->tok = TOK_NEARBYINT); }
NEXTAFTER	{ return (yylval->tok = TOK_NEXTAFTER); }
NEXTTOWARD	{ return (yylval->tok = TOK_NEXTTOWARD); }
POW		{ return (yylval->tok = TOK_POW); }
REMAINDER	{ return (yylval->tok = TOK_REMAINDER); }
RINT		{ return (yylval->tok = TOK_RINT); }
ROUND		{ return (yylval->tok = TOK_ROUND); }
SINH		{ return (yylval->tok = TOK_SINH); }
SIN		{ return (yylval->tok = TOK_SIN); }
SQRT		{ return (yylval->tok = TOK_SQRT); }
TANH		{ return (yylval->tok = TOK_TANH); }
TAN		{ return (yylval->tok = TOK_TAN); }
TGAMMA		{ return (yylval->tok = TOK_TGAMMA); }
TRUNC		{ return (yylval->tok = TOK_TRUNC); }

-?[0-9]+(\.[0-9]+)?([eE]-?[0-9]+)?	{
		yylval->val = cw_expr_make_number(strtold(yytext, NULL));
		return TOKEN;
	}

-?(inf(inity)?|INF(INITY)?)	{
		yylval->val = cw_expr_make_number(strtold(yytext, NULL));
		return TOKEN;
	}

nan|NAN|NaN	{
		yylval->val = cw_expr_make_number(strtold(yytext, NULL));
		return TOKEN;
	}

(\'[^']*\'|\"(\\\"|[^\"])*\"|\\.|[^'"\\ |&=~><!+\-*/%?:(),]+)+	{
		/* A string is a repeated sequence of '...', "...", \x or anything
		 * not an operator. Having figured out what it consists of we use
		 * cw_split_args to dequote it so there are no special characters
		 * and we have the string in its intended form.
		 */
		cw_split_args(NULL, yytext, "", '\0', NULL);
		yylval->val = cw_expr_make_str(CW_EXPR_string, yytext, yyleng);
		return TOKEN;
	}

.	{}


%%


extern int cw_yyparse(void *);


int cw_expr(struct cw_channel *chan, const char *expr, struct cw_dynstr *result)
{
	struct parse_io io;

	memset(&io, 0, sizeof(io));
	io.chan = chan;
	io.string = expr;

	cw_yylex_init_extra(&io, &io.scanner);

	cw_yy_scan_string(expr, io.scanner);

	if (!cw_yyparse((void *) &io)) {
		if (!io.val)
			cw_dynstr_printf(result, "0");
		else {
			if (io.val->type == CW_EXPR_number)
				cw_dynstr_printf(result, "%.18Lg", io.val->u.n);
			else
				cw_dynstr_printf(result, "%s", io.val->u.s);

			free(io.val);
		}
	} else
		result->error = 1;

	cw_yylex_destroy(io.scanner);

	return result->error;
}


int cw_yyerror(const char *s,  yyltype *loc, struct parse_io *parseio)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	struct yyguts_t *yyg = (struct yyguts_t*)(parseio->scanner);
	size_t n;

	CW_UNUSED(loc);

	n = strlen(s) + (size_t)(yytext - YY_CURRENT_BUFFER_LVALUE->yy_ch_buf);

	if (!cw_dynstr_need(&ds, n + 1)) {
		memset(ds.data, ' ', n);
		ds.data[n] = '\0';

		cw_log(CW_LOG_WARNING, "%s: %s\n  %s^ here\n",
			s, parseio->string, ds.data);
	}

	cw_dynstr_free(&ds);
	return 0;
}
