%{
/*
 * CallWeaver -- An open source telephony toolkit.
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


#define SET_COLUMNS \
	do { \
		yylloc_param->first_column = (int)(yyg->yytext_r - YY_CURRENT_BUFFER_LVALUE->yy_ch_buf); \
		yylloc_param->last_column += yyleng - 1; \
		yylloc_param->first_line = yylloc_param->last_line = 1; \
	} while (0)

#define SET_EOF	\
	do { \
		if ((yylval_param->val = malloc(sizeof(struct val)))) { \
			yylval_param->val->type = CW_EXPR_string; \
			yylval_param->val->u.s = strdup(yytext); \
		} \
	} while (0)

#define SET_STRING \
	do { \
		cw_split_args(NULL, yytext, "", '\0', NULL); \
		yylval_param->val = cw_expr_make_str(CW_EXPR_string, yytext, yyleng); \
	} while (0)

#define SET_NUMERIC\
	do { \
		yylval_param->val = cw_expr_make_number(strtold(yytext, NULL)); \
	} while (0)


void cw_yyset_column(int column_no, yyscan_t yyscanner);
int cw_yyget_column(yyscan_t yyscanner);


#ifndef YY_NO_INPUT
#  ifdef __cplusplus
    static int yyinput (yyscan_t yyscanner) __attribute__ (( unused ));
#  else
    static int input  (yyscan_t yyscanner) __attribute__ (( unused ));
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

\|	{ SET_COLUMNS; return TOK_OR;}
\&	{ SET_COLUMNS; return TOK_AND;}
\=	{ SET_COLUMNS; return TOK_EQ;}
\|\|	{ SET_COLUMNS; return TOK_OR;}
\&\&	{ SET_COLUMNS; return TOK_AND;}
\=\=	{ SET_COLUMNS; return TOK_EQ;}
\=~	{ SET_COLUMNS; return TOK_EQTILDE;}
\>	{ SET_COLUMNS; return TOK_GT;}
\<	{ SET_COLUMNS; return TOK_LT;}
\>\=	{ SET_COLUMNS; return TOK_GE;}
\<\=	{ SET_COLUMNS; return TOK_LE;}
\!\=	{ SET_COLUMNS; return TOK_NE;}
\+	{ SET_COLUMNS; return TOK_PLUS;}
\-	{ SET_COLUMNS; return TOK_MINUS;}
\*	{ SET_COLUMNS; return TOK_MULT;}
\/	{ SET_COLUMNS; return TOK_DIV;}
\%	{ SET_COLUMNS; return TOK_MOD;}
\?	{ SET_COLUMNS; return TOK_COND;}
\!	{ SET_COLUMNS; return TOK_COMPL;}
\:	{ SET_COLUMNS; return TOK_COLON;}
\:\:	{ SET_COLUMNS; return TOK_COLONCOLON;}
\(	{ SET_COLUMNS; return TOK_LP;}
\)	{ SET_COLUMNS; return TOK_RP;}
\,	{ SET_COLUMNS; return TOK_COMMA;}

ACOSH	{ SET_COLUMNS; return TOK_ACOSH; }
ACOS	{ SET_COLUMNS; return TOK_ACOS; }
ASINH	{ SET_COLUMNS; return TOK_ASINH; }
ASIN	{ SET_COLUMNS; return TOK_ASIN; }
ATAN2	{ SET_COLUMNS; return TOK_ATAN2; }
ATAN	{ SET_COLUMNS; return TOK_ATAN; }
CBRT	{ SET_COLUMNS; return TOK_CBRT; }
CEIL	{ SET_COLUMNS; return TOK_CEIL; }
COPYSIGN	{ SET_COLUMNS; return TOK_COPYSIGN; }
COSH	{ SET_COLUMNS; return TOK_COSH; }
COS	{ SET_COLUMNS; return TOK_COS; }
ERFC	{ SET_COLUMNS; return TOK_ERFC; }
ERF	{ SET_COLUMNS; return TOK_ERF; }
EXP2	{ SET_COLUMNS; return TOK_EXP2; }
EXP	{ SET_COLUMNS; return TOK_EXP; }
EXPM1	{ SET_COLUMNS; return TOK_EXPM1; }
FABS	{ SET_COLUMNS; return TOK_FABS; }
FDIM	{ SET_COLUMNS; return TOK_FDIM; }
FLOOR	{ SET_COLUMNS; return TOK_FLOOR; }
FMA	{ SET_COLUMNS; return TOK_FMA; }
FMAX	{ SET_COLUMNS; return TOK_FMAX; }
FMIN	{ SET_COLUMNS; return TOK_FMIN; }
FMOD	{ SET_COLUMNS; return TOK_FMOD; }
HYPOT	{ SET_COLUMNS; return TOK_HYPOT; }
LGAMMA	{ SET_COLUMNS; return TOK_LGAMMA; }
LOG10	{ SET_COLUMNS; return TOK_LOG10; }
LOG1P	{ SET_COLUMNS; return TOK_LOG1P; }
LOG2	{ SET_COLUMNS; return TOK_LOG2; }
LOGB	{ SET_COLUMNS; return TOK_LOGB; }
LOG	{ SET_COLUMNS; return TOK_LOG; }
NEARBYINT	{ SET_COLUMNS; return TOK_NEARBYINT; }
NEXTAFTER	{ SET_COLUMNS; return TOK_NEXTAFTER; }
NEXTTOWARD	{ SET_COLUMNS; return TOK_NEXTTOWARD; }
POW	{ SET_COLUMNS; return TOK_POW; }
REMAINDER	{ SET_COLUMNS; return TOK_REMAINDER; }
RINT	{ SET_COLUMNS; return TOK_RINT; }
ROUND	{ SET_COLUMNS; return TOK_ROUND; }
SINH	{ SET_COLUMNS; return TOK_SINH; }
SIN	{ SET_COLUMNS; return TOK_SIN; }
SQRT	{ SET_COLUMNS; return TOK_SQRT; }
TANH	{ SET_COLUMNS; return TOK_TANH; }
TAN	{ SET_COLUMNS; return TOK_TAN; }
TGAMMA	{ SET_COLUMNS; return TOK_TGAMMA; }
TRUNC	{ SET_COLUMNS; return TOK_TRUNC; }

-?[0-9]+(\.[0-9]+)?([eE]-?[0-9]+)?	{
		SET_COLUMNS;
		SET_NUMERIC;
		return TOKEN;
	}

-?(inf(inity)?|INF(INITY)?)	{
		SET_COLUMNS;
		SET_NUMERIC;
		return TOKEN;
	}

nan|NAN|NaN	{
		SET_COLUMNS;
		SET_NUMERIC;
		return TOKEN;
	}

(\'[^']*\'|\"(\\\"|[^"])*\"|\\.|[a-zA-Z0-9.;_^#@\x80-\xff])+	{
		SET_COLUMNS;
		SET_STRING;
		return TOKEN;
	}

.	{}


%%

/* I'm putting the interface routine to the whole parse here in the flexer input file
   mainly because of all the flexer initialization that has to be done. Shouldn't matter
   where it is, as long as it's somewhere. I didn't want to define a prototype for the
   cw_yy_scan_string in the .y file, because then, I'd have to define YY_BUFFER_STATE there...
	UGH! that would be inappropriate. */

int cw_yyparse(void *); /* need to/should define this prototype for the call to yyparse */
int cw_yyerror(const char *, YYLTYPE *, struct parse_io *); /* likewise */

int cw_expr(struct cw_channel *chan, const char *expr, struct cw_dynstr *result)
{
	struct parse_io io;

	memset(&io, 0, sizeof(io));
	io.chan = chan;
	io.string = expr;  /* to pass to the error routine */

	cw_yylex_init_extra(&io, &io.scanner);

	cw_yy_scan_string(expr, io.scanner);

	if (!cw_yyparse((void *) &io)) {
		if (!io.val) {
			cw_dynstr_printf(result, "0");
		} else {
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


char extra_error_message[4095];
int extra_error_message_supplied = 0;
extern CW_API_PUBLIC void cw_expr_register_extra_error_info(const char *message);
extern CW_API_PUBLIC void cw_expr_clear_extra_error_info(void);

void cw_expr_register_extra_error_info(const char *message)
{
	extra_error_message_supplied = 1;
	strcpy(extra_error_message, message);
}

void cw_expr_clear_extra_error_info(void)
{
	extra_error_message_supplied = 0;
	extra_error_message[0] = 0;
}


int cw_yyerror(const char *s,  yyltype *loc, struct parse_io *parseio)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	struct yyguts_t * yyg = (struct yyguts_t*)(parseio->scanner);
	size_t n;

	CW_UNUSED(loc);

	n = (size_t)(yytext - YY_CURRENT_BUFFER_LVALUE->yy_ch_buf);

	if (!cw_dynstr_need(&ds, n + 2)) {
		memset(ds.data, ' ', n);
		ds.data[n] = '^';
		ds.data[n + 1] = 0;

		cw_log(CW_LOG_WARNING, "%s%s%s; Input:\n%s\n%s\n",
			(extra_error_message_supplied ? extra_error_message : ""),
			(extra_error_message_supplied ? " " : ""),
			s, parseio->string, ds.data);
	}

	cw_dynstr_free(&ds);
	return(0);
}
