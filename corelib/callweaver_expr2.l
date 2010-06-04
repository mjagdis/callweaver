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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>

#if !defined(SOLARIS) && !defined(__CYGWIN__)
#include <err.h>
#else
#define quad_t int64_t
#endif

#include <errno.h>
#include <limits.h>
#include <regex.h>

#include "callweaver/logger.h"
#include "callweaver/callweaver_expr.h"
#include "callweaver/strings.h"

#include "callweaver_expr2-common.h"
#include "callweaver_expr2.h" /* the o/p of the bison on callweaver_expr2.y */

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
	do {		\
		if ((yylval_param->val = malloc(sizeof(struct val)))) { \
			yylval_param->val->type = CW_EXPR_string; \
			cw_split_args(NULL, yytext, "", '\0', NULL); \
			if (!(yylval_param->val->u.s = strdup(yytext))) { \
				free(yylval_param->val); \
				yylval_param->val = NULL; \
			} \
		} \
	} while (0)

#define SET_NUMERIC_STRING \
	do { \
		if ((yylval_param->val = malloc(sizeof(struct val)))) { \
			yylval_param->val->type = CW_EXPR_numeric_string; \
			if (!(yylval_param->val->u.s = strdup(yytext))) { \
				free(yylval_param->val); \
				yylval_param->val = NULL; \
			} \
		} \
	} while (0)

void cw_yyset_column(int column_no, yyscan_t yyscanner);
int cw_yyget_column(yyscan_t yyscanner);
static int curlycount = 0;
static char *expr2_token_subst(const char *mess);

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
%option bison-bridge
%option bison-locations
%option noyywrap
%x var trail

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

\$\{	{
		/* gather the contents of ${} expressions, with trailing stuff,
		 * into a single TOKEN.
		 * They are much more complex now than they used to be
		 */
		curlycount = 0;
		BEGIN(var);
		yymore();
	}

[ \t\r]		{}

[\n]		{/* what to do with eol */}

-?[0-9]+(\.[0-9]+)?([eE]-?[0-9]+)?	{
		SET_COLUMNS;
		SET_NUMERIC_STRING;
		return TOKEN;
	}

-?(inf(inity)?|INF(INITY)?)	{
		SET_COLUMNS;
		SET_NUMERIC_STRING;
		return TOKEN;
	}

nan|NAN|NaN	{
		SET_COLUMNS;
		SET_NUMERIC_STRING;
		return TOKEN;
	}

(\'[^']*\'|\"(\\\"|[^"])*\"|\\.|$[^{]|[a-zA-Z0-9.;_^#@\x80-\xff])+	{
		SET_COLUMNS;
		SET_STRING;
		return TOKEN;
	}


<var>[^{}]*\}	{
		curlycount--;
		if (curlycount < 0) {
			BEGIN(trail);
			yymore();
		} else {
			yymore();
		}
	}
	
<var>[^{}]*\{	{
		curlycount++;
		yymore();
	}
	

<trail>[^-\t\r \n$():?%/+=*<>!|&]*	{
		BEGIN(0);
		SET_COLUMNS;
		SET_STRING;
		return TOKEN;
	}
	
<trail>[-\t\r \n$():?%/+=*<>!|&]	{
		char c = yytext[yyleng-1];
		BEGIN(0);
		unput(c);
		SET_COLUMNS;
		SET_STRING;
		return TOKEN;
	}
	
<trail>\$\{	{
		curlycount = 0;
		BEGIN(var);
		yymore();
	}
	
<trail><<EOF>>	{
		BEGIN(0);
		SET_COLUMNS;
		SET_EOF;
		return TOKEN;
		/*actually, if an expr is only a variable ref, this could happen a LOT */
	}

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
	int return_value = 0;

	memset(&io, 0, sizeof(io));
	io.chan = chan;
	io.string = expr;  /* to pass to the error routine */

	cw_yylex_init(&io.scanner);

	cw_yy_scan_string(expr, io.scanner);

	if (!cw_yyparse((void *) &io)) {
		if (!io.val) {
			cw_dynstr_printf(result, "0");
			return_value = 1;
		} else {
			if (io.val->type == CW_EXPR_number) {
				return_value = cw_dynstr_printf(result, "%.18Lg", io.val->u.n);
			} else {
				cw_dynstr_printf(result, "%s", io.val->u.s);
				return_value = result->used;
				free(io.val->u.s);
			}
			free(io.val);
		}
	} else
		result->error = 1;

	cw_yylex_destroy(io.scanner);

	return return_value;
}


char extra_error_message[4095];
int extra_error_message_supplied = 0;
extern CW_API_PUBLIC void  cw_expr_register_extra_error_info(const char *message);
extern CW_API_PUBLIC void  cw_expr_clear_extra_error_info(void);

void  cw_expr_register_extra_error_info(const char *message)
{
       extra_error_message_supplied=1;
       strcpy(extra_error_message, message);
}

void  cw_expr_clear_extra_error_info(void)
{
       extra_error_message_supplied=0;
       extra_error_message[0] = 0;
}

static const char *expr2_token_equivs1[] =
{
	[TOKEN] = "TOKEN",
	[TOK_COND] = "TOK_COND",
	[TOK_COLONCOLON] = "TOK_COLONCOLON",
	[TOK_OR] = "TOK_OR",
	[TOK_AND] = "TOK_AND",
	[TOK_EQ] = "TOK_EQ",
	[TOK_GT] = "TOK_GT",
	[TOK_LT] = "TOK_LT",
	[TOK_GE] = "TOK_GE",
	[TOK_LE] = "TOK_LE",
	[TOK_NE] = "TOK_NE",
	[TOK_PLUS] = "TOK_PLUS",
	[TOK_MINUS] = "TOK_MINUS",
	[TOK_MULT] = "TOK_MULT",
	[TOK_DIV] = "TOK_DIV",
	[TOK_MOD] = "TOK_MOD",
	[TOK_COMPL] = "TOK_COMPL",
	[TOK_COLON] = "TOK_COLON",
	[TOK_EQTILDE] = "TOK_EQTILDE",
	[TOK_RP] = "TOK_RP",
	[TOK_LP] = "TOK_LP",
	[TOK_COMMA] = "TOK_COMMA",
};

static const char *expr2_token_equivs2[] =
{
	[TOKEN] = "<token>",
	[TOK_COND] = "?",
	[TOK_COLONCOLON] = "::",
	[TOK_OR] = "|",
	[TOK_AND] = "&",
	[TOK_EQ] = "=",
	[TOK_GT] = ">",
	[TOK_LT] = "<",
	[TOK_GE] = ">=",
	[TOK_LE] = "<=",
	[TOK_NE] = "!=",
	[TOK_PLUS] = "+",
	[TOK_MINUS] = "-",
	[TOK_MULT] = "*",
	[TOK_DIV] = "/",
	[TOK_MOD] = "%",
	[TOK_COMPL] = "!",
	[TOK_COLON] = ":",
	[TOK_EQTILDE] = "=~",
	[TOK_RP] = ")",
	[TOK_LP] = "(",
	[TOK_COMMA] = ",",
};


static char *expr2_token_subst(const char *mess)
{
	/* calc a length, malloc, fill, and return; yyerror had better free it! */
	int len=0,i;
	const char *t, *p;
	char *res, *s;
	int expr2_token_equivs_entries = sizeof(expr2_token_equivs1)/sizeof(char*);

	for (p = mess; *p; p++) {
		for (i = 0; i < expr2_token_equivs_entries; i++) {
			if (expr2_token_equivs1[i] && !strncmp(p, expr2_token_equivs1[i], strlen(expr2_token_equivs1[i]))) {
				len += strlen(expr2_token_equivs2[i]) + 2;
				p += strlen(expr2_token_equivs1[i]) - 1;
				break;
			}
		}
		len++;
	}
	res = malloc(len+1);
	res[0] = 0;
	s = res;
	for (p = mess; *p;) {
		int found = 0;
		for (i = 0; i < expr2_token_equivs_entries; i++) {
			if (expr2_token_equivs1[i] && !strncmp(p, expr2_token_equivs1[i], strlen(expr2_token_equivs1[i]))) {
				*s++ = '\'';
				for (t = expr2_token_equivs2[i]; *t;) {
					*s++ = *t++;
				}
				*s++ = '\'';
				p += strlen(expr2_token_equivs1[i]);
				found = 1;
				break;
			}
		}
		if (!found )
			*s++ = *p++;
	}
	*s = 0;
	return res;
}

int cw_yyerror (const char *s,  yyltype *loc, struct parse_io *parseio )
{	
	char spacebuf2[8000]; /* best safe than sorry */
	struct yyguts_t * yyg = (struct yyguts_t*)(parseio->scanner);
	char *s2 = expr2_token_subst((char *)s);
	int i=0;

	for(i=0;i< (int)(yytext - YY_CURRENT_BUFFER_LVALUE->yy_ch_buf);i++) spacebuf2[i] = ' ';  /* uh... assuming yyg is defined, then I can use the yycolumn macro,
																								which is the same thing as... get this:
													yyg->yy_buffer_stack[yyg->yy_buffer_stack_top]->yy_bs_column
													I was tempted to just use yy_buf_pos in the STATE, but..., well:
														a. the yy_buf_pos is the current position in the buffer, which
															may not relate to the entire string/buffer because of the
															buffering.
														b. but, analysis of the situation is that when you use the
															yy_scan_string func, it creates a single buffer the size of
															string, so the two would be the same... 
													so, in the end, the yycolumn macro is available, shorter, therefore easier. */
	spacebuf2[i++]='^';
	spacebuf2[i]= 0;

	cw_log(CW_LOG_WARNING, "%s%ssyntax error: %s; Input:\n%s\n%s\n",  
		(extra_error_message_supplied ? extra_error_message : ""),
		(extra_error_message_supplied ? " " : ""),
		s2, parseio->string,spacebuf2);

	free(s2);
	return(0);
}
