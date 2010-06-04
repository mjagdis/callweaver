%{
/* Written by Pace Willisson (pace@blitz.com) 
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

#include "callweaver/callweaver_expr.h"
#include "callweaver/logger.h"
#include "callweaver/pbx.h"


typedef void *yyscan_t;

#include "callweaver_expr2-common.h"


#define YYPARSE_PARAM parseio
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1
extern char extra_error_message[4095];
extern int extra_error_message_supplied;


static const char *math_name[] = {
	[TOK_ACOSH      - TOK_ACOSH] = "ACOSH",
	[TOK_ACOS       - TOK_ACOSH] = "ACOS",
	[TOK_ASINH      - TOK_ACOSH] = "ASINH",
	[TOK_ASIN       - TOK_ACOSH] = "ASIN",
	[TOK_ATAN2      - TOK_ACOSH] = "ATAN2",
	[TOK_ATANH      - TOK_ACOSH] = "ATANH",
	[TOK_ATAN       - TOK_ACOSH] = "ATAN",
	[TOK_CBRT       - TOK_ACOSH] = "CBRT",
	[TOK_CEIL       - TOK_ACOSH] = "CEIL",
	[TOK_COPYSIGN   - TOK_ACOSH] = "COPYSIGN",
	[TOK_COSH       - TOK_ACOSH] = "COSH",
	[TOK_COS        - TOK_ACOSH] = "COS",
	[TOK_ERFC       - TOK_ACOSH] = "ERFC",
	[TOK_ERF        - TOK_ACOSH] = "ERF",
	[TOK_EXP2       - TOK_ACOSH] = "EXP2",
	[TOK_EXP        - TOK_ACOSH] = "EXP",
	[TOK_EXPM1      - TOK_ACOSH] = "EXPM1",
	[TOK_FABS       - TOK_ACOSH] = "FABS",
	[TOK_FDIM       - TOK_ACOSH] = "FDIM",
	[TOK_FLOOR      - TOK_ACOSH] = "FLOOR",
	[TOK_FMA        - TOK_ACOSH] = "FMA",
	[TOK_FMAX       - TOK_ACOSH] = "FMAX",
	[TOK_FMIN       - TOK_ACOSH] = "FMIN",
	[TOK_FMOD       - TOK_ACOSH] = "FMOD",
	[TOK_HYPOT      - TOK_ACOSH] = "HYPOT",
	[TOK_LGAMMA     - TOK_ACOSH] = "LGAMMA",
	[TOK_LOG10      - TOK_ACOSH] = "LOG10",
	[TOK_LOG1P      - TOK_ACOSH] = "LOG1P",
	[TOK_LOG2       - TOK_ACOSH] = "LOG2",
	[TOK_LOGB       - TOK_ACOSH] = "LOGB",
	[TOK_LOG        - TOK_ACOSH] = "LOG",
	[TOK_NEARBYINT  - TOK_ACOSH] = "NEARBYINT",
	[TOK_NEXTAFTER  - TOK_ACOSH] = "NEXTAFTER",
	[TOK_NEXTTOWARD - TOK_ACOSH] = "NEXTTOWARD",
	[TOK_POW        - TOK_ACOSH] = "POW",
	[TOK_REMAINDER  - TOK_ACOSH] = "REMAINDER",
	[TOK_RINT       - TOK_ACOSH] = "RINT",
	[TOK_ROUND      - TOK_ACOSH] = "ROUND",
	[TOK_SINH       - TOK_ACOSH] = "SINH",
	[TOK_SIN        - TOK_ACOSH] = "SIN",
	[TOK_SQRT       - TOK_ACOSH] = "SQRT",
	[TOK_TANH       - TOK_ACOSH] = "TANH",
	[TOK_TAN        - TOK_ACOSH] = "TAN",
	[TOK_TGAMMA     - TOK_ACOSH] = "TGAMMA",
	[TOK_TRUNC      - TOK_ACOSH] = "TRUNC",
};


static void *math_func[] = {
	[TOK_ACOSH      - TOK_ACOSH] = &acoshl,
	[TOK_ACOS       - TOK_ACOSH] = &acosl,
	[TOK_ASINH      - TOK_ACOSH] = &asinhl,
	[TOK_ASIN       - TOK_ACOSH] = &asinl,
	[TOK_ATAN2      - TOK_ACOSH] = &atan2l,
	[TOK_ATANH      - TOK_ACOSH] = &atanhl,
	[TOK_ATAN       - TOK_ACOSH] = &atanl,
	[TOK_CBRT       - TOK_ACOSH] = &cbrtl,
	[TOK_CEIL       - TOK_ACOSH] = &ceill,
	[TOK_COPYSIGN   - TOK_ACOSH] = &copysignl,
	[TOK_COSH       - TOK_ACOSH] = &coshl,
	[TOK_COS        - TOK_ACOSH] = &cosl,
	[TOK_ERFC       - TOK_ACOSH] = &erfcl,
	[TOK_ERF        - TOK_ACOSH] = &erfl,
	[TOK_EXP2       - TOK_ACOSH] = &exp2l,
	[TOK_EXP        - TOK_ACOSH] = &expl,
	[TOK_EXPM1      - TOK_ACOSH] = &expm1l,
	[TOK_FABS       - TOK_ACOSH] = &fabsl,
	[TOK_FDIM       - TOK_ACOSH] = &fdiml,
	[TOK_FLOOR      - TOK_ACOSH] = &floorl,
	[TOK_FMA        - TOK_ACOSH] = &fmal,
	[TOK_FMAX       - TOK_ACOSH] = &fmaxl,
	[TOK_FMIN       - TOK_ACOSH] = &fminl,
	[TOK_FMOD       - TOK_ACOSH] = &fmodl,
	[TOK_HYPOT      - TOK_ACOSH] = &hypotl,
	[TOK_LGAMMA     - TOK_ACOSH] = &lgammal,
	[TOK_LOG10      - TOK_ACOSH] = &log10l,
	[TOK_LOG1P      - TOK_ACOSH] = &log1pl,
	[TOK_LOG2       - TOK_ACOSH] = &log2l,
	[TOK_LOGB       - TOK_ACOSH] = &logbl,
	[TOK_LOG        - TOK_ACOSH] = &logl,
	[TOK_NEARBYINT  - TOK_ACOSH] = &nearbyintl,
	[TOK_NEXTAFTER  - TOK_ACOSH] = &nextafterl,
	[TOK_NEXTTOWARD - TOK_ACOSH] = &nexttowardl,
	[TOK_POW        - TOK_ACOSH] = &powl,
	[TOK_REMAINDER  - TOK_ACOSH] = &remainderl,
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

 
static struct cw_dynargs *args_new(void);
static struct cw_dynargs *args_push_null(struct cw_dynargs *arglist);
static struct cw_dynargs *args_push_val(struct cw_dynargs *arglist, struct val *vp);
static void free_args(struct cw_dynargs *);
static void free_value(struct val *);
static int is_zero_or_null(struct val *);
static int isstring(struct val *);
static struct val *make_number(long double);
static struct val *make_str(enum valtype type, const char *);
static struct val *op_math_f(long double (*op)(long double), struct val *a);
static struct val *op_math_ff(long double (*op)(long double, long double), struct val *a, struct val *b);
static struct val *op_math_fff(long double (*op)(long double, long double, long double), struct val *a, struct val *b, struct val *c);
static struct val *op_and(struct val *, struct val *);
static struct val *op_colon(struct val *, struct val *);
static struct val *op_eqtilde(struct val *, struct val *);
static struct val *op_div(struct val *, struct val *);
static struct val *op_eq(struct val *, struct val *);
static struct val *op_ge(struct val *, struct val *);
static struct val *op_gt(struct val *, struct val *);
static struct val *op_le(struct val *, struct val *);
static struct val *op_lt(struct val *, struct val *);
static struct val *op_cond(struct val *, struct val *, struct val *);
static struct val *op_minus(struct val *, struct val *);
static struct val *op_negate(struct val *);
static struct val *op_compl(struct val *);
static struct val *op_ne(struct val *, struct val *);
static struct val *op_or(struct val *, struct val *);
static struct val *op_plus(struct val *, struct val *);
static struct val *op_rem(struct val *, struct val *);
static struct val *op_times(struct val *, struct val *);
static int to_number(struct val *, int silent);
static int to_string(struct val *);

/* uh, if I want to predeclare yylex with a YYLTYPE, I have to predeclare the yyltype... sigh */
typedef struct yyltype
{
  int first_line;
  int first_column;

  int last_line;
  int last_column;
} yyltype;

# define YYLTYPE yyltype
# define YYLTYPE_IS_TRIVIAL 1

/* we will get warning about no prototype for yylex! But we can't
   define it here, we have no definition yet for YYSTYPE. */

int		cw_yyerror(const char *,YYLTYPE *, struct parse_io *);
 
/* I wanted to add args to the yyerror routine, so I could print out
   some useful info about the error. Not as easy as it looks, but it
   is possible. */
#define cw_yyerror(x) cw_yyerror(x,&yyloc,parseio)
%}
 
%pure-parser
%locations
/* %debug  for when you are having big problems */

/* %name-prefix="cw_yy" */

%union
{
	struct val *val;
	struct cw_dynargs *args;
	int tok;
}

%{
extern int		cw_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));
%}
%left <val> TOK_COMMA
%left <val> TOK_COND TOK_COLONCOLON
%left <val> TOK_OR
%left <val> TOK_AND
%left <val> TOK_EQ TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE
%left <val> TOK_PLUS TOK_MINUS
%left <val> TOK_MULT TOK_DIV TOK_MOD
%right <val> TOK_COMPL
%left <val> TOK_COLON TOK_EQTILDE
%left <val> TOK_RP TOK_LP


%type <val> start expr
%type <args> args args1

%type <tok> math_f math_ff math_fff

%token <tok> TOK_ACOSH /* This MUST be first */
%token <tok> TOK_ACOS TOK_ASINH TOK_ASIN TOK_ATAN2 TOK_ATANH TOK_ATAN TOK_CBRT
	TOK_CEIL TOK_COPYSIGN TOK_COSH TOK_COS TOK_ERFC TOK_ERF TOK_EXP2 TOK_EXP TOK_EXPM1
	TOK_FABS TOK_FDIM TOK_FLOOR TOK_FMA TOK_FMAX TOK_FMIN TOK_FMOD TOK_HYPOT TOK_LGAMMA
	TOK_LOG10 TOK_LOG1P TOK_LOG2 TOK_LOGB TOK_LOG TOK_NEARBYINT TOK_NEXTAFTER TOK_NEXTTOWARD
	TOK_POW TOK_REMAINDER TOK_RINT TOK_ROUND TOK_SINH TOK_SIN TOK_SQRT TOK_TANH TOK_TAN
	TOK_TGAMMA TOK_TRUNC

%token <val> TOKEN


%destructor { free_value($$); } expr TOKEN
%destructor { free_args($$); } args args1

%%

start: expr	{
			struct parse_io *p = parseio;

			if ((p->val = malloc(sizeof(*p->val))))
				memcpy(p->val, $1, sizeof(*p->val));
			free($1);
			if (!p->val)
				YYABORT;
		}
	|	{/* nothing */
			struct parse_io *p = parseio;

			if ((p->val = malloc(sizeof(*p->val)))) {
				p->val->type = CW_EXPR_string;
				p->val->u.s = strdup("");
			} else
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
	| args1 TOK_COMMA expr %prec TOK_RP {
			if (!($$ = args_push_val($1, $3)))
				YYABORT;
		}
	| args1 TOK_COMMA %prec TOK_RP {
			if (!($$ = args_push_null($1)))
				YYABORT;
		}
	| TOK_COMMA expr %prec TOK_RP	{
			if (!($$ = args_push_val(args_push_null(args_new()), $2)))
				YYABORT;
		}
	| TOK_COMMA %prec TOK_RP	{
			if (!($$ = args_push_null(args_new())))
				YYABORT;
		}
	;

math_f:
	TOK_ACOSH		{ $$ = TOK_ACOSH; }
	| TOK_ACOS		{ $$ = TOK_ACOS; }
	| TOK_ASINH		{ $$ = TOK_ASINH; }
	| TOK_ASIN		{ $$ = TOK_ASIN; }
	| TOK_ATANH		{ $$ = TOK_ATANH; }
	| TOK_ATAN		{ $$ = TOK_ATAN; }
	| TOK_CBRT		{ $$ = TOK_CBRT; }
	| TOK_CEIL		{ $$ = TOK_CEIL; }
	| TOK_COSH		{ $$ = TOK_COSH; }
	| TOK_COS		{ $$ = TOK_COS; }
	| TOK_ERFC		{ $$ = TOK_ERFC; }
	| TOK_ERF		{ $$ = TOK_ERF; }
	| TOK_EXP2		{ $$ = TOK_EXP2; }
	| TOK_EXP		{ $$ = TOK_EXP; }
	| TOK_EXPM1		{ $$ = TOK_EXPM1; }
	| TOK_FABS		{ $$ = TOK_FABS; }
	| TOK_FLOOR		{ $$ = TOK_FLOOR; }
	| TOK_LGAMMA		{ $$ = TOK_LGAMMA; }
	| TOK_LOG10		{ $$ = TOK_LOG10; }
	| TOK_LOG1P		{ $$ = TOK_LOG1P; }
	| TOK_LOG2		{ $$ = TOK_LOG2; }
	| TOK_LOGB		{ $$ = TOK_LOGB; }
	| TOK_LOG		{ $$ = TOK_LOG; }
	| TOK_NEARBYINT		{ $$ = TOK_NEARBYINT; }
	| TOK_RINT		{ $$ = TOK_RINT; }
	| TOK_ROUND		{ $$ = TOK_ROUND; }
	| TOK_SINH		{ $$ = TOK_SINH; }
	| TOK_SIN		{ $$ = TOK_SIN; }
	| TOK_SQRT		{ $$ = TOK_SQRT; }
	| TOK_TANH		{ $$ = TOK_TANH; }
	| TOK_TAN		{ $$ = TOK_TAN; }
	| TOK_TGAMMA		{ $$ = TOK_TGAMMA; }
	| TOK_TRUNC		{ $$ = TOK_TRUNC; }
	;

math_ff:
	TOK_ATAN2		{ $$ = TOK_ATAN2; }
	| TOK_COPYSIGN		{ $$ = TOK_COPYSIGN; }
	| TOK_FDIM		{ $$ = TOK_FDIM; }
	| TOK_FMAX		{ $$ = TOK_FMAX; }
	| TOK_FMIN		{ $$ = TOK_FMIN; }
	| TOK_FMOD		{ $$ = TOK_FMOD; }
	| TOK_HYPOT		{ $$ = TOK_HYPOT; }
	| TOK_NEXTAFTER		{ $$ = TOK_NEXTAFTER; }
	| TOK_NEXTTOWARD	{ $$ = TOK_NEXTTOWARD; }
	| TOK_POW		{ $$ = TOK_POW; }
	| TOK_REMAINDER		{ $$ = TOK_REMAINDER; }
	;

math_fff:
	TOK_FMA		{ $$ = TOK_FMA; }
	;

expr:	TOKEN TOK_LP args TOK_RP	{
			int res = 1;

			$$ = NULL;
			if (!cw_dynargs_need($3, 1) && to_string($1)) {
				const struct parse_io *p = parseio;
				struct cw_dynstr result = CW_DYNSTR_INIT;

				$3->data[$3->used] = NULL;

				if (!(res = (cw_function_exec(p->chan, cw_hash_string($1->u.s), $1->u.s, $3->used, &$3->data[0], &result) || result.error))) {
					free($1->u.s);
					if (!(res = (!($1->u.s = cw_dynstr_steal(&result)) && !($1->u.s = strdup(""))))) {
						$1->type = CW_EXPR_arbitrary_string;
						$$ = $1;
						$1 = NULL;
					}
				}
				cw_dynstr_free(&result);
			}

			free_value($1);
			free_args($3);

			if (res)
				YYABORT;
		}
	| math_f TOK_LP expr TOK_RP	{ if (!($$ = op_math_f(math_func[$1 - TOK_ACOSH], $3))) YYABORT; }
	| math_f			{ if (!($$ = make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH]))) YYABORT; }
	| math_ff TOK_LP expr TOK_COMMA expr TOK_RP	{ if (!($$ = op_math_ff(math_func[$1 - TOK_ACOSH], $3, $5))) YYABORT; }
	| math_ff			{ if (!($$ = make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH]))) YYABORT; }
	| math_fff TOK_LP expr TOK_COMMA expr TOK_COMMA expr TOK_RP	{ if (!($$ = op_math_fff(math_func[$1 - TOK_ACOSH], $3, $5, $7))) YYABORT; }
	| math_fff			{ if (!($$ = make_str(CW_EXPR_string, math_name[$1 - TOK_ACOSH]))) YYABORT; }

	| TOKEN   { if (!($$ = $1)) YYABORT; }
	| TOK_LP expr TOK_RP { if (!($$ = $2)) YYABORT;
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;}
	| expr TOK_OR expr { if (!($$ = op_or ($1, $3))) YYABORT;
                         @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_AND expr { if (!($$ = op_and ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
                          @$.first_line=0; @$.last_line=0;}
	| expr TOK_EQ expr { if (!($$ = op_eq ($1, $3))) YYABORT;
	                     @$.first_column = @1.first_column; @$.last_column = @3.last_column;
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_GT expr { if (!($$ = op_gt ($1, $3))) YYABORT;
                         @$.first_column = @1.first_column; @$.last_column = @3.last_column;
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_LT expr { if (!($$ = op_lt ($1, $3))) YYABORT;
	                     @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_GE expr  { if (!($$ = op_ge ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_LE expr  { if (!($$ = op_le ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_NE expr  { if (!($$ = op_ne ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_PLUS expr { if (!($$ = op_plus ($1, $3))) YYABORT;
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;}
	| expr TOK_MINUS expr { if (!($$ = op_minus ($1, $3))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| TOK_MINUS expr %prec TOK_COMPL { if (!($$ = op_negate ($2))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @2.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| TOK_COMPL expr   { if (!($$ = op_compl ($2))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @2.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_MULT expr { if (!($$ = op_times ($1, $3))) YYABORT;
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;}
	| expr TOK_DIV expr { if (!($$ = op_div ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_MOD expr { if (!($$ = op_rem ($1, $3))) YYABORT;
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_COLON expr { if (!($$ = op_colon ($1, $3))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_EQTILDE expr { if (!($$ = op_eqtilde ($1, $3))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_COND expr TOK_COLONCOLON expr  { if (!($$ = op_cond ($1, $3, $5))) YYABORT;
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	;

%%

static struct val *make_number(long double n)
{
	struct val *vp = NULL;

	if ((vp = malloc(sizeof(*vp)))) {
		vp->type = CW_EXPR_number;
		vp->u.n  = n;
	} else
		cw_log(CW_LOG_ERROR, "Out of memory!\n");

	return vp;

}

static struct val *make_str(enum valtype type, const char *s)
{
	struct val *vp;

	if ((vp = malloc(sizeof(*vp)))) {
		if ((vp->u.s = strdup(s))) {
			vp->type = type;
			return vp;
		}
		free(vp);
	}

	cw_log(CW_LOG_ERROR, "Out of memory!\n");
	return NULL;
}


static void
free_args(struct cw_dynargs *args)
{
	if (args) {
		int i;

		for (i = 0; i < args->used; i++)
			free(args->data[i]);

		cw_dynargs_free(args);
		free(args);
	}
}


static void free_value(struct val *vp)
{	
	if (vp) {
		if (vp->type != CW_EXPR_number)
			free(vp->u.s);
		free(vp);
	}
}


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

		if (!res && !silent && !extra_error_message_supplied)
			cw_log(CW_LOG_WARNING, "non-numeric argument: %s\n", vp->u.s);
	}

	return res;
}


static int to_string(struct val *vp)
{
	if (vp->type == CW_EXPR_number) {
		if ((vp->u.s = malloc(32))) {
			sprintf(vp->u.s, "%.18Lg", vp->u.n);
			vp->type = CW_EXPR_numeric_string;
		} else
			cw_log(CW_LOG_WARNING,"Out of memory!\n");
	}

	return vp->u.s != NULL;
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


static struct cw_dynargs *args_new(void)
{
	struct cw_dynargs *arglist;

	if ((arglist = malloc(sizeof(struct cw_dynargs))))
		cw_dynargs_init(arglist, 1, CW_DYNARRAY_DEFAULT_CHUNK);

	return arglist;
}


static struct cw_dynargs *args_push_null(struct cw_dynargs *arglist)
{
	if (arglist) {
		if (cw_dynargs_need(arglist, 1) || !(arglist->data[arglist->used++] = strdup(""))) {
			free_args(arglist);
			arglist = NULL;
		}
	}

	return arglist;
}


static struct cw_dynargs *args_push_val(struct cw_dynargs *arglist, struct val *vp)
{
	if (arglist) {
		if (!cw_dynargs_need(arglist, 1) && to_string(vp)) {
			arglist->data[arglist->used++] = vp->u.s;
			vp->u.s = NULL;
		} else {
			free_args(arglist);
			arglist = NULL;
		}
	}

	free_value(vp);
	return arglist;
}


#undef cw_yyerror
#define cw_yyerror(x) cw_yyerror(x, YYLTYPE *yylloc, struct parse_io *parseio)

/* I put the cw_yyerror func in the flex input file,
   because it refers to the buffer state. Best to
   let it access the BUFFER stuff there and not trying
   define all the structs, macros etc. in this file! */


static struct val *op_math_f(long double (*op)(long double), struct val *a)
{
	struct val *vp = NULL;

	if (to_number(a, 0)) {
		a->u.n = (*op)(a->u.n);
		vp = a;
	} else
		free_value(a);

	return vp;
}


static struct val *op_math_ff(long double (*op)(long double, long double), struct val *a, struct val *b)
{
	struct val *vp = NULL;

	if (to_number(a, 0) && to_number(b, 0)) {
		a->u.n = (*op)(a->u.n, b->u.n);
		vp = a;
	} else
		free_value(a);

	free_value(b);
	return vp;
}


static struct val *op_math_fff(long double (*op)(long double, long double, long double), struct val *a, struct val *b, struct val *c)
{
	struct val *vp = NULL;

	if (to_number(a, 0) && to_number(b, 0) && to_number(c, 0)) {
		a->u.n = (*op)(a->u.n, b->u.n, c->u.n);
		vp = a;
	} else
		free_value(a);

	free_value(b);
	free_value(c);
	return vp;
}


static struct val * op_or(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a)) {
		r = b;
		b = a;
	}

	free_value(b);
	return r;
}
		
static struct val * op_and(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a) || is_zero_or_null(b)) {
		free_value(a);
		r = make_number(0.0L);
	}

	free_value(b);
	return r;
}

static struct val * op_eq(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) == 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n == b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_gt(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string (b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) > 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n > b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_lt(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) < 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n < b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_ge(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) >= 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n >= b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_le(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) <= 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n <= b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_cond(struct val *a, struct val *b, struct val *c)
{
	struct val *r = b;

	if (is_zero_or_null(a)) {
		r = c;
		c = b;
	}

	free_value(a);
	free_value(c);

	return r;
}

static struct val * op_ne(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) != 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n != b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_plus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		r = a->u.n;
		if (to_number(b, 1))
			r += b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_minus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		r = a->u.n;
		if (to_number(b, 1))
			r -= b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_negate(struct val *a)
{
	long double r = 0.0L;

	if (to_number(a, 1))
		r = -a->u.n;

	free_value(a);
	return make_number(r);
}

static struct val * op_compl(struct val *a)
{
	struct val *v;

	v = make_number(is_zero_or_null(a));
	free_value(a);
	return v;
}

static struct val * op_times(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1) && to_number(b, 1))
		r = a->u.n * b->u.n;

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_div(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		if (to_number(b, 1) && b->u.n != 0.0L)
			r = a->u.n / b->u.n;
		else
			r = INFINITY * a->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}
	
static struct val * op_rem(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1) && to_number(b, 1) && b->u.n != 0.0L)
		r = fmodl(a->u.n, b->u.n);

	free_value(a);
	free_value(b);

	return make_number(r);
}
	

static struct val * op_colon(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	struct val *v = NULL;
	int eval;

	if (to_string(a) && to_string(b)) {
		if (!(eval = regcomp(&rp, b->u.s, REG_EXTENDED))) {
			/* remember that patterns are anchored to the beginning of the line */
			if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0) {
				if (rm[1].rm_so >= 0) {
					*(a->u.s + rm[1].rm_eo) = '\0';
					v = make_str(CW_EXPR_arbitrary_string, a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str(CW_EXPR_string, "");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str(CW_EXPR_string, "");
		}
	}

	free_value(a);
	free_value(b);

	return v;
}
	

static struct val * op_eqtilde(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v = NULL;

	if (to_string(a) && to_string(b)) {
		if (!(eval = regcomp(&rp, b->u.s, REG_EXTENDED))) {
			/* remember that patterns are anchored to the beginning of the line */
			if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 ) {
				if (rm[1].rm_so >= 0) {
					*(a->u.s + rm[1].rm_eo) = '\0';
					v = make_str(CW_EXPR_arbitrary_string, a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str(CW_EXPR_string, "");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str(CW_EXPR_string, "");
		}
	}

	free_value(a);
	free_value(b);

	return v;
}
