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
 * Written by Pace Willisson (pace@blitz.com)
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * And then overhauled twice by Steve Murphy (murf@e-tools.com)
 * to add double-quoted strings, allow mult. spaces, improve
 * error messages, and then to fold in a flex scanner for the 
 * yylex operation.
 *
 */

#include "callweaver.h"

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "callweaver/dynarray.h"
#include "callweaver/callweaver_expr.h"
#include "callweaver/logger.h"
#include "callweaver/pbx.h"


typedef void *yyscan_t;

#include "callweaver_expr2-common.h"


/* Arglists are stored as dynamic arrays of vals (struct val *).
 * This defines "struct cw_dynvals" and the cw_dynvals_* functions used to operate on it.
 */
CW_DYNARRAY_DECL(struct val *, vals)


#define YYPARSE_PARAM parseio
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner


static struct {
	const char *s;
	size_t l;
} math_name[] = {
#define S(text)	{ .s = text, .l = sizeof(text) - 1 }
	[TOK_ACOSH      - TOK_ACOSH] = S("ACOSH"),
	[TOK_ACOS       - TOK_ACOSH] = S("ACOS"),
	[TOK_ASINH      - TOK_ACOSH] = S("ASINH"),
	[TOK_ASIN       - TOK_ACOSH] = S("ASIN"),
	[TOK_ATAN2      - TOK_ACOSH] = S("ATAN2"),
	[TOK_ATANH      - TOK_ACOSH] = S("ATANH"),
	[TOK_ATAN       - TOK_ACOSH] = S("ATAN"),
	[TOK_CBRT       - TOK_ACOSH] = S("CBRT"),
	[TOK_CEIL       - TOK_ACOSH] = S("CEIL"),
	[TOK_COPYSIGN   - TOK_ACOSH] = S("COPYSIGN"),
	[TOK_COSH       - TOK_ACOSH] = S("COSH"),
	[TOK_COS        - TOK_ACOSH] = S("COS"),
	[TOK_ERFC       - TOK_ACOSH] = S("ERFC"),
	[TOK_ERF        - TOK_ACOSH] = S("ERF"),
	[TOK_EXP2       - TOK_ACOSH] = S("EXP2"),
	[TOK_EXP        - TOK_ACOSH] = S("EXP"),
	[TOK_EXPM1      - TOK_ACOSH] = S("EXPM1"),
	[TOK_FABS       - TOK_ACOSH] = S("FABS"),
	[TOK_FDIM       - TOK_ACOSH] = S("FDIM"),
	[TOK_FLOOR      - TOK_ACOSH] = S("FLOOR"),
	[TOK_FMA        - TOK_ACOSH] = S("FMA"),
	[TOK_FMAX       - TOK_ACOSH] = S("FMAX"),
	[TOK_FMIN       - TOK_ACOSH] = S("FMIN"),
	[TOK_FMOD       - TOK_ACOSH] = S("FMOD"),
	[TOK_HYPOT      - TOK_ACOSH] = S("HYPOT"),
	[TOK_LGAMMA     - TOK_ACOSH] = S("LGAMMA"),
	[TOK_LOG10      - TOK_ACOSH] = S("LOG10"),
	[TOK_LOG1P      - TOK_ACOSH] = S("LOG1P"),
	[TOK_LOG2       - TOK_ACOSH] = S("LOG2"),
	[TOK_LOGB       - TOK_ACOSH] = S("LOGB"),
	[TOK_LOG        - TOK_ACOSH] = S("LOG"),
	[TOK_NEARBYINT  - TOK_ACOSH] = S("NEARBYINT"),
	[TOK_NEXTAFTER  - TOK_ACOSH] = S("NEXTAFTER"),
	[TOK_NEXTTOWARD - TOK_ACOSH] = S("NEXTTOWARD"),
	[TOK_POW        - TOK_ACOSH] = S("POW"),
	[TOK_REMAINDER  - TOK_ACOSH] = S("REMAINDER"),
	[TOK_RINT       - TOK_ACOSH] = S("RINT"),
	[TOK_ROUND      - TOK_ACOSH] = S("ROUND"),
	[TOK_SINH       - TOK_ACOSH] = S("SINH"),
	[TOK_SIN        - TOK_ACOSH] = S("SIN"),
	[TOK_SQRT       - TOK_ACOSH] = S("SQRT"),
	[TOK_TANH       - TOK_ACOSH] = S("TANH"),
	[TOK_TAN        - TOK_ACOSH] = S("TAN"),
	[TOK_TGAMMA     - TOK_ACOSH] = S("TGAMMA"),
	[TOK_TRUNC      - TOK_ACOSH] = S("TRUNC"),
#undef S
};


typedef long double (* const math_f_t)(long double);
static math_f_t math_f_func[] = {
	[TOK_ACOSH      - TOK_ACOSH] = &acoshl,
	[TOK_ACOS       - TOK_ACOSH] = &acosl,
	[TOK_ASINH      - TOK_ACOSH] = &asinhl,
	[TOK_ASIN       - TOK_ACOSH] = &asinl,
	[TOK_ATANH      - TOK_ACOSH] = &atanhl,
	[TOK_ATAN       - TOK_ACOSH] = &atanl,
	[TOK_CBRT       - TOK_ACOSH] = &cbrtl,
	[TOK_CEIL       - TOK_ACOSH] = &ceill,
	[TOK_COSH       - TOK_ACOSH] = &coshl,
	[TOK_COS        - TOK_ACOSH] = &cosl,
	[TOK_ERFC       - TOK_ACOSH] = &erfcl,
	[TOK_ERF        - TOK_ACOSH] = &erfl,
	[TOK_EXP2       - TOK_ACOSH] = &exp2l,
	[TOK_EXP        - TOK_ACOSH] = &expl,
	[TOK_EXPM1      - TOK_ACOSH] = &expm1l,
	[TOK_FABS       - TOK_ACOSH] = &fabsl,
	[TOK_FLOOR      - TOK_ACOSH] = &floorl,
	[TOK_LGAMMA     - TOK_ACOSH] = &lgammal,
	[TOK_LOG10      - TOK_ACOSH] = &log10l,
	[TOK_LOG1P      - TOK_ACOSH] = &log1pl,
	[TOK_LOG2       - TOK_ACOSH] = &log2l,
	[TOK_LOGB       - TOK_ACOSH] = &logbl,
	[TOK_LOG        - TOK_ACOSH] = &logl,
	[TOK_NEARBYINT  - TOK_ACOSH] = &nearbyintl,
	[TOK_RINT       - TOK_ACOSH] = &rintl,
	[TOK_ROUND      - TOK_ACOSH] = &roundl,
	[TOK_SINH       - TOK_ACOSH] = &sinhl,
	[TOK_SIN        - TOK_ACOSH] = &sinl,
	[TOK_SQRT       - TOK_ACOSH] = &sqrtl,
	[TOK_TANH       - TOK_ACOSH] = &tanhl,
	[TOK_TAN        - TOK_ACOSH] = &tanl,
	[TOK_TGAMMA     - TOK_ACOSH] = &tgammal,
	[TOK_TRUNC      - TOK_ACOSH] = &truncl,
};

typedef long double (* const math_ff_t)(long double, long double);
static math_ff_t math_ff_func[] = {
	[TOK_ATAN2      - TOK_ATAN2] = &atan2l,
	[TOK_COPYSIGN   - TOK_ATAN2] = &copysignl,
	[TOK_FDIM       - TOK_ATAN2] = &fdiml,
	[TOK_FMAX       - TOK_ATAN2] = &fmaxl,
	[TOK_FMIN       - TOK_ATAN2] = &fminl,
	[TOK_FMOD       - TOK_ATAN2] = &fmodl,
	[TOK_HYPOT      - TOK_ATAN2] = &hypotl,
	[TOK_NEXTAFTER  - TOK_ATAN2] = &nextafterl,
	[TOK_NEXTTOWARD - TOK_ATAN2] = &nexttowardl,
	[TOK_POW        - TOK_ATAN2] = &powl,
	[TOK_REMAINDER  - TOK_ATAN2] = &remainderl,
};

typedef long double (* const math_fff_t)(long double, long double, long double);
static math_fff_t math_fff_func[] = {
	[TOK_FMA        - TOK_FMA  ] = &fmal,
};

 
static struct cw_dynvals *args_new(void);
static struct cw_dynvals *args_push_null(struct cw_dynvals *arglist);
static struct cw_dynvals *args_push_val(struct cw_dynvals *arglist, struct val *vp);
static void args_free(struct cw_dynvals *);
static int is_zero_or_null(struct val *);
static int isstring(struct val *);
static struct val *op_math_f(long double (* const op)(long double), struct val *a);
static struct val *op_math_ff(long double (* const op)(long double, long double), struct val *a, struct val *b);
static struct val *op_math_fff(long double (* const op)(long double, long double, long double), struct val *a, struct val *b, struct val *c);
static struct val *op_colon(struct val *, struct val *);
static struct val *op_eqtilde(struct val *, struct val *);
static struct val *op_div(struct val *, struct val *);
static struct val *op_eq(struct val *, struct val *);
static struct val *op_ge(struct val *, struct val *);
static struct val *op_gt(struct val *, struct val *);
static struct val *op_le(struct val *, struct val *);
static struct val *op_lt(struct val *, struct val *);
static struct val *op_minus(struct val *, struct val *);
static struct val *op_negate(struct val *);
static struct val *op_compl(struct val *);
static struct val *op_ne(struct val *, struct val *);
static struct val *op_plus(struct val *, struct val *);
static struct val *op_rem(struct val *, struct val *);
static struct val *op_times(struct val *, struct val *);
static int to_number(struct val *, int silent);
static const char *string_rep(struct val *, char buf[MAX_NUMBER_LEN]);

%}
 

%pure-parser
%locations


%union
{
	struct val *val;
	struct cw_dynvals *args;
	int tok;
}


%{
extern int cw_yylex(YYSTYPE *, YYLTYPE *, yyscan_t);
extern int cw_yyerror(const char *, YYLTYPE *, struct parse_io *);
#define cw_yyerror(x) cw_yyerror((x), &yyloc, parseio)
%}


%left ','
%left '?' TOK_COLONCOLON
%left TOK_OR
%left TOK_AND
%left TOK_EQ '>' '<' TOK_GE TOK_LE TOK_NE
%left '+' '-'
%left '*' '/' '%'
%right '!'
%left ':' TOK_EQTILDE
%left ')' '('


%type <val> start expr
%type <args> args args1

%type <tok> math_f
%token <tok> TOK_ACOSH /* This MUST be first */
%token <tok> TOK_ACOS TOK_ASINH TOK_ASIN TOK_ATANH TOK_ATAN TOK_CBRT TOK_CEIL
	TOK_COSH TOK_COS TOK_ERFC TOK_ERF TOK_EXP2 TOK_EXP TOK_EXPM1 TOK_FABS
	TOK_FLOOR TOK_LGAMMA TOK_LOG10 TOK_LOG1P TOK_LOG2 TOK_LOGB TOK_LOG
	TOK_NEARBYINT TOK_RINT TOK_ROUND TOK_SINH TOK_SIN TOK_SQRT TOK_TANH
	TOK_TAN TOK_TGAMMA TOK_TRUNC

%type <tok> math_ff
%token <tok> TOK_ATAN2 /* This MUST be first */
%token <tok> TOK_COPYSIGN TOK_FDIM TOK_FMAX TOK_FMIN TOK_FMOD TOK_HYPOT TOK_NEXTAFTER
	TOK_NEXTTOWARD TOK_POW TOK_REMAINDER

%type <tok> math_fff
%token <tok> TOK_FMA /* This MUST be first */
	/* Although there are currently no other 3-arg math functions */

%token <val> TOKEN


%destructor { free($$); } expr TOKEN
%destructor { args_free($$); } args args1

%%

start: expr	{
			struct parse_io *p = parseio;

			if (!(p->val = $1))
				YYABORT;
		}
	| /* empty */ {
			struct parse_io *p = parseio;

			if (!(p->val = cw_expr_make_str(CW_EXPR_string, "", 0)))
				YYABORT;
		}
	;

args:	/* empty */ {
			if (!($$ = args_new()))
				YYABORT;
		}
	| args1
	;

args1:	expr {
			if (!($$ = args_push_val(args_new(), $1)))
				YYABORT;
		}
	| args1 ',' expr %prec ')' {
			if (!($$ = args_push_val($1, $3)))
				YYABORT;
		}
	| args1 ',' %prec ')' {
			if (!($$ = args_push_null($1)))
				YYABORT;
		}
	| ',' expr %prec ')'	{
			if (!($$ = args_push_val(args_push_null(args_new()), $2)))
				YYABORT;
		}
	| ',' %prec ')'	{
			if (!($$ = args_push_null(args_new())))
				YYABORT;
		}
	;

math_f:
	TOK_ACOSH
	| TOK_ACOS
	| TOK_ASINH
	| TOK_ASIN
	| TOK_ATANH
	| TOK_ATAN
	| TOK_CBRT
	| TOK_CEIL
	| TOK_COSH
	| TOK_COS
	| TOK_ERFC
	| TOK_ERF
	| TOK_EXP2
	| TOK_EXP
	| TOK_EXPM1
	| TOK_FABS
	| TOK_FLOOR
	| TOK_LGAMMA
	| TOK_LOG10
	| TOK_LOG1P
	| TOK_LOG2
	| TOK_LOGB
	| TOK_LOG
	| TOK_NEARBYINT
	| TOK_RINT
	| TOK_ROUND
	| TOK_SINH
	| TOK_SIN
	| TOK_SQRT
	| TOK_TANH
	| TOK_TAN
	| TOK_TGAMMA
	| TOK_TRUNC
	;

math_ff:
	TOK_ATAN2
	| TOK_COPYSIGN
	| TOK_FDIM
	| TOK_FMAX
	| TOK_FMIN
	| TOK_FMOD
	| TOK_HYPOT
	| TOK_NEXTAFTER
	| TOK_NEXTTOWARD
	| TOK_POW
	| TOK_REMAINDER
	;

math_fff:
	TOK_FMA
	;

expr:	TOKEN '(' args ')'	{
			struct cw_dynstr ds = CW_DYNSTR_INIT;
			struct cw_dynstr result = CW_DYNSTR_INIT;
			char buf[MAX_NUMBER_LEN];
			const struct parse_io *p = parseio;
			const char *funcname;
			char **argv;
			int argc;
			int res = 1;

			if (!$1)
				YYABORT;

			if (!p->noexec) {
				$$ = NULL;

				if ((argv = malloc(sizeof(argv[0]) * ($3->used + 1)))) {
					for (argc = 0; argc < $3->used; argc++) {
						if ($3->data[argc]->type != CW_EXPR_number)
							argv[argc] = $3->data[argc]->u.s;
						else {
							argv[argc] = (char *)ds.used;
							cw_dynstr_printf(&ds, NUMBER_FORMAT "%c", $3->data[argc]->u.n, 0);
						}
					}

					if (!ds.error) {
						for (argc = 0; argc < $3->used; argc++) {
							if ($3->data[argc]->type == CW_EXPR_number)
								argv[argc] = &ds.data[(size_t)argv[argc]];
						}

						argv[argc] = NULL;

						funcname = string_rep($1, buf);

						if (!(res = (cw_function_exec(p->chan, cw_hash_string(funcname), funcname, argc, argv, &result) || result.error)))
							$$ = cw_expr_make_str(CW_EXPR_arbitrary_string, result.data, result.used);

						cw_dynstr_free(&result);
					}

					cw_dynstr_free(&ds);
					free(argv);
				}

				free($1);
			} else {
				$$ = $1;
				res = 0;
			}

			args_free($3);

			if (res)
				YYABORT;
		}
	| math_f '(' expr ')' {
			if (!($$ = op_math_f(math_f_func[$1 - TOK_ACOSH], $3))) YYABORT;
		}
	| math_f {
			if (!($$ = cw_expr_make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH].s, math_name[$1 - TOK_ACOSH].l))) YYABORT;
		}
	| math_ff '(' expr ',' expr ')' {
			if (!($$ = op_math_ff(math_ff_func[$1 - TOK_ATAN2], $3, $5))) YYABORT;
		}
	| math_ff {
			if (!($$ = cw_expr_make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH].s, math_name[$1 - TOK_ACOSH].l))) YYABORT;
		}
	| math_fff '(' expr ',' expr ',' expr ')' {
			if (!($$ = op_math_fff(math_fff_func[$1 - TOK_FMA], $3, $5, $7))) YYABORT;
		}
	| math_fff {
			if (!($$ = cw_expr_make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH].s, math_name[$1 - TOK_ACOSH].l))) YYABORT;
		}

	| TOKEN			{ if (!($$ = $1)) YYABORT; }
	| '(' expr ')'	{ if (!($$ = $2)) YYABORT; }

	| expr TOK_AND {
			struct parse_io *p = parseio;
			if (is_zero_or_null($1))
				p->noexec++;
		}
	expr	{
			struct parse_io *p = parseio;
			$$ = $4;
			if (is_zero_or_null($1)) {
				p->noexec--;
				$$ = $1;
				$1 = $4;
			}
			free($1);
		}

	| expr TOK_OR {
			struct parse_io *p = parseio;
			if (!is_zero_or_null($1))
				p->noexec++;
		}
	expr	{
			struct parse_io *p = parseio;
			$$ = $4;
			if (!is_zero_or_null($1)) {
				p->noexec--;
				$$ = $1;
				$1 = $4;
			}
			free($1);
		}

	| expr TOK_EQ expr	{ if (!($$ = op_eq($1, $3))) YYABORT; }
	| expr '>' expr	{ if (!($$ = op_gt($1, $3))) YYABORT; }
	| expr '<' expr	{ if (!($$ = op_lt($1, $3))) YYABORT; }
	| expr TOK_GE expr	{ if (!($$ = op_ge($1, $3))) YYABORT; }
	| expr TOK_LE expr	{ if (!($$ = op_le($1, $3))) YYABORT; }
	| expr TOK_NE expr	{ if (!($$ = op_ne($1, $3))) YYABORT; }

	| expr '+' expr	{ if (!($$ = op_plus($1, $3))) YYABORT; }
	| expr '-' expr	{ if (!($$ = op_minus($1, $3))) YYABORT; }
	| expr '*' expr	{ if (!($$ = op_times($1, $3))) YYABORT; }
	| expr '/' expr	{ if (!($$ = op_div($1, $3))) YYABORT; }
	| expr '%' expr	{ if (!($$ = op_rem($1, $3))) YYABORT; }

	| '-' expr %prec '!' {
			if (!($$ = op_negate($2))) YYABORT;
		}
	| '!' expr	{ if (!($$ = op_compl($2))) YYABORT; }

	| expr ':' expr	{ if (!($$ = op_colon($1, $3))) YYABORT; }
	| expr TOK_EQTILDE expr	{ if (!($$ = op_eqtilde($1, $3))) YYABORT; }

	| expr '?' {
			struct parse_io *p = parseio;
			if (is_zero_or_null($1))
				p->noexec++;
		}
	expr TOK_COLONCOLON {
			struct parse_io *p = parseio;
			if (is_zero_or_null($1))
				p->noexec--;
			else
				p->noexec++;
		}
	expr  {
			struct parse_io *p = parseio;
			$$ = $7;
			if (!is_zero_or_null($1)) {
				p->noexec--;
				$$ = $4;
				$4 = $7;
			}
			free($1);
			free($4);
		}
	;

%%


static int to_number(struct val *vp, int silent)
{
	char *end;
	long double n;
	int res = 0;

	if (vp) {
		switch (vp->type) {
			case CW_EXPR_arbitrary_string:
			case CW_EXPR_numeric_string:
				vp->type = CW_EXPR_string;
				errno = 0;
				n = strtold(vp->u.s, &end);
				if (end != vp->u.s && end[0] == '\0') {
					if (errno == ERANGE)
						cw_log(CW_LOG_WARNING, "Conversion of %s to number under/overflowed!\n", vp->u.s);
					free(vp->u.s);
					vp->type = CW_EXPR_number;
					vp->u.n = n;
					res = 1;
				}
				break;

			case CW_EXPR_number:
				res = 1;
				break;

			default:
				break;
		}

		if (!res && !silent)
			cw_log(CW_LOG_WARNING, "non-numeric argument: %s\n", vp->u.s);
	}

	return res;
}


static const char *string_rep(struct val *vp, char buf[MAX_NUMBER_LEN])
{
	char *ret = &vp->u.s[0];

	if (vp->type == CW_EXPR_number) {
		sprintf(buf, NUMBER_FORMAT, vp->u.n);
		ret = buf;
	}

	return ret;
}


/* return TRUE if this string is NOT a valid number */
static int isstring(struct val *vp)
{
	int ret = (vp->type == CW_EXPR_string);

	if (vp->type == CW_EXPR_arbitrary_string) {
		int i;

		vp->type = CW_EXPR_string;
		ret = 1;

		i = 0;
		if (vp->u.s[i] == '-' || vp->u.s[i] == '+') i++;
		if (isdigit(vp->u.s[i])) {
			/* [+-]?\d+(\.\d+)?[eE]-?\d+ */
			do { i++; } while (isdigit(vp->u.s[i]));
			if (vp->u.s[i] == '.')
				do { i++; } while (isdigit(vp->u.s[i]));
			if (vp->u.s[i] == 'e' || vp->u.s[i] == 'E') {
				i++;
				if (vp->u.s[i] == '-' || isdigit(vp->u.s[i]))
					do { i++; } while (isdigit(vp->u.s[i]));
			}

			if (!vp->u.s[i]) {
				vp->type = CW_EXPR_numeric_string;
				ret = 0;
			}
		} else {
			/* "nan", "NAN", "NaN", [+-]?inf(inity)? or [+-]?INF(INITY)? */
			if (((vp->u.s[0] == 'n' || vp->u.s[0] == 'N')
				&& (vp->u.s[1] == 'a' || (vp->u.s[1] == 'A' && vp->u.s[0] == 'N'))
				&& vp->u.s[2] == vp->u.s[0]
				&& !vp->u.s[3])
			|| (!strncmp(&vp->u.s[i], "inf", 3) && (!vp->u.s[i+3] || !strcmp(&vp->u.s[i+3], "inity")))
			|| (!strncmp(&vp->u.s[i], "INF", 3) && (!vp->u.s[i+3] || !strcmp(&vp->u.s[i+3], "INITY")))) {
				vp->type = CW_EXPR_numeric_string;
				ret = 0;
			}
		}
	}

	return ret;
}


static int is_zero_or_null(struct val *vp)
{
	int res;

	if (vp->type == CW_EXPR_number)
		res = (vp->u.n == 0.0L);
	else
		res = !vp->u.s
			|| vp->u.s[0] == '\0'
			|| (vp->u.s[0] == '0' && vp->u.s[1] == '\0')
			|| (vp->type != CW_EXPR_string && to_number(vp, 1) && vp->u.n == 0.0L);

	return res;
}


static const struct val null_arg = {
	.type = CW_EXPR_string,
	.u.s = { [0] = '\0' },
};


static struct cw_dynvals *args_new(void)
{
	struct cw_dynvals *arglist;

	if ((arglist = malloc(sizeof(struct cw_dynvals))))
		cw_dynvals_init(arglist, 1, CW_DYNARRAY_DEFAULT_CHUNK);

	return arglist;
}


static void
args_free(struct cw_dynvals *args)
{
	if (args) {
		int i;

		for (i = 0; i < args->used; i++)
			if (args->data[i] != &null_arg)
				free(args->data[i]);

		cw_dynvals_free(args);
		free(args);
	}
}


static struct cw_dynvals *args_push_null(struct cw_dynvals *arglist)
{
	if (arglist) {
		if (!cw_dynvals_need(arglist, 1))
			arglist->data[arglist->used++] = (struct val *)&null_arg;
		else {
			args_free(arglist);
			arglist = NULL;
		}
	}

	return arglist;
}


static struct cw_dynvals *args_push_val(struct cw_dynvals *arglist, struct val *vp)
{
	if (arglist) {
		if (!cw_dynvals_need(arglist, 1))
			arglist->data[arglist->used++] = vp;
		else {
			free(vp);
			args_free(arglist);
			arglist = NULL;
		}
	}

	return arglist;
}


static struct val *op_math_f(long double (*op)(long double), struct val *a)
{
	struct val *vp = NULL;

	if (to_number(a, 0)) {
		a->u.n = (*op)(a->u.n);
		vp = a;
	} else
		free(a);

	return vp;
}


static struct val *op_math_ff(long double (*op)(long double, long double), struct val *a, struct val *b)
{
	struct val *vp = NULL;

	if (to_number(a, 0) && to_number(b, 0)) {
		a->u.n = (*op)(a->u.n, b->u.n);
		vp = a;
	} else
		free(a);

	free(b);
	return vp;
}


static struct val *op_math_fff(long double (*op)(long double, long double, long double), struct val *a, struct val *b, struct val *c)
{
	struct val *vp = NULL;

	if (to_number(a, 0) && to_number(b, 0) && to_number(c, 0)) {
		a->u.n = (*op)(a->u.n, b->u.n, c->u.n);
		vp = a;
	} else
		free(a);

	free(b);
	free(c);
	return vp;
}


static struct val *op_eq(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) == 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n == b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_ne(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) != 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n != b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_gt(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) > 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n > b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_lt(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) < 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n < b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_ge(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) >= 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n >= b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_le(struct val *a, struct val *b)
{
	if (isstring(a) || isstring(b)) {
		char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
		a->u.n = (long double)(strcoll(string_rep(a, bufa), string_rep(b, bufb)) <= 0);
		a->type = CW_EXPR_number;
	} else {
		to_number(a, 0);
		to_number(b, 0);
		/* This should be the case. Unless isstring() is broken... */
		a->type = CW_EXPR_number;
		a->u.n = (long double)(a->u.n <= b->u.n);
	}

	free(b);
	return a;
}


static struct val *op_plus(struct val *a, struct val *b)
{
	if (!to_number(a, 1)) {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	if (to_number(b, 1))
		a->u.n += b->u.n;

	free(b);
	return a;
}


static struct val *op_minus(struct val *a, struct val *b)
{
	if (!to_number(a, 1)) {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	if (to_number(b, 1))
		a->u.n -= b->u.n;

	free(b);
	return a;
}


static struct val *op_negate(struct val *a)
{
	if (to_number(a, 1))
		a->u.n = -a->u.n;
	else {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	return a;
}


static struct val *op_compl(struct val *a)
{
	int v = is_zero_or_null(a);

	a->type = CW_EXPR_number;
	a->u.n = (long double)v;
	return a;
}


static struct val *op_times(struct val *a, struct val *b)
{
	if (to_number(a, 1) && to_number(b, 1))
		a->u.n *= b->u.n;
	else {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	free(b);
	return a;
}


static struct val *op_div(struct val *a, struct val *b)
{
	if (to_number(a, 1)) {
		if (to_number(b, 1) && b->u.n != 0.0L)
			a->u.n /= b->u.n;
		else
			a->u.n = INFINITY * a->u.n;
	} else {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	free(b);
	return a;
}


static struct val *op_rem(struct val *a, struct val *b)
{
	if (to_number(a, 1) && to_number(b, 1) && b->u.n != 0.0L)
		a->u.n = fmodl(a->u.n, b->u.n);
	else {
		a->type = CW_EXPR_number;
		a->u.n = 0.0L;
	}

	free(b);
	return a;
}
	

static struct val *op_colon(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
	const char *sa;
	struct val *v = NULL;
	int eval;

	if (!(eval = regcomp(&rp, string_rep(b, bufb), REG_EXTENDED))) {
		sa = string_rep(a, bufa);
		/* remember that patterns are anchored to the beginning of the line */
		if (regexec(&rp, sa, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0) {
			if (rm[1].rm_so >= 0)
				v = cw_expr_make_str(CW_EXPR_arbitrary_string, &sa[rm[1].rm_so], rm[1].rm_eo - rm[1].rm_so);
			else
				v = cw_expr_make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
		} else {
			if (rp.re_nsub == 0)
				v = cw_expr_make_number(0.0L);
			else
				v = cw_expr_make_str(CW_EXPR_string, "", 0);
		}

		regfree(&rp);
	} else {
		regerror(eval, &rp, errbuf, sizeof(errbuf));
		cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
		v = cw_expr_make_str(CW_EXPR_string, "", 0);
	}

	free(a);
	free(b);

	return v;
}
	

static struct val *op_eqtilde(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	char bufa[MAX_NUMBER_LEN], bufb[MAX_NUMBER_LEN];
	const char *sa;
	struct val *v = NULL;
	int eval;

	if (!(eval = regcomp(&rp, string_rep(b, bufb), REG_EXTENDED))) {
		sa = string_rep(a, bufa);
		/* remember that patterns are anchored to the beginning of the line */
		if (regexec(&rp, sa, (size_t)2, rm, 0) == 0) {
			if (rm[1].rm_so >= 0)
				v = cw_expr_make_str(CW_EXPR_arbitrary_string, &sa[rm[1].rm_so], rm[1].rm_eo - rm[1].rm_so);
			else
				v = cw_expr_make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
		} else {
			if (rp.re_nsub == 0)
				v = cw_expr_make_number(0.0L);
			else
				v = cw_expr_make_str(CW_EXPR_string, "", 0);
		}

		regfree(&rp);
	} else {
		regerror(eval, &rp, errbuf, sizeof(errbuf));
		cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
		v = cw_expr_make_str(CW_EXPR_string, "", 0);
	}

	free(a);
	free(b);

	return v;
}
