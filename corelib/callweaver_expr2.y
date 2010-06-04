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

 
static void		free_value __P((struct val *));
static int		is_zero_or_null __P((struct val *));
static int		isstring __P((struct val *));
static struct val	*make_number __P((long double));
static struct val	*make_str __P((const char *));
static struct val	*op_and __P((struct val *, struct val *));
static struct val	*op_colon __P((struct val *, struct val *));
static struct val	*op_eqtilde __P((struct val *, struct val *));
static struct val	*op_div __P((struct val *, struct val *));
static struct val	*op_eq __P((struct val *, struct val *));
static struct val	*op_ge __P((struct val *, struct val *));
static struct val	*op_gt __P((struct val *, struct val *));
static struct val	*op_le __P((struct val *, struct val *));
static struct val	*op_lt __P((struct val *, struct val *));
static struct val	*op_cond __P((struct val *, struct val *, struct val *));
static struct val	*op_minus __P((struct val *, struct val *));
static struct val	*op_negate __P((struct val *));
static struct val	*op_compl __P((struct val *));
static struct val	*op_ne __P((struct val *, struct val *));
static struct val	*op_or __P((struct val *, struct val *));
static struct val	*op_plus __P((struct val *, struct val *));
static struct val	*op_rem __P((struct val *, struct val *));
static struct val	*op_times __P((struct val *, struct val *));
static int		to_number __P((struct val *));
static int		to_string __P((struct val *));

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
}

%{
extern int		cw_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));
%}
%left <val> TOK_COND TOK_COLONCOLON
%left <val> TOK_OR
%left <val> TOK_AND
%left <val> TOK_EQ TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE
%left <val> TOK_PLUS TOK_MINUS
%left <val> TOK_MULT TOK_DIV TOK_MOD
%right <val> TOK_COMPL
%left <val> TOK_COLON TOK_EQTILDE
%left <val> TOK_RP TOK_LP


%token <val> TOKEN
%type <val> start expr


%destructor {  free_value($$); }  expr TOKEN

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

expr:	TOKEN   { if (!($$ = $1)) YYABORT; }
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

static struct val *make_str(const char *s)
{
	struct val *vp;

	if ((vp = malloc(sizeof(*vp)))) {
		if ((vp->u.s = strdup (s))) {
			vp->type = CW_EXPR_string;
			return vp;
		}
		free(vp);
	}

	cw_log(CW_LOG_ERROR, "Out of memory!\n");
	return NULL;
}


static void free_value(struct val *vp)
{	
	if (vp) {
		if (vp->type != CW_EXPR_number)
			free (vp->u.s);
		free(vp);
	}
}


static int to_number(struct val *vp)
{
	char *end;
	long double n;
	int res = 0;

	if (vp) {
		switch (vp->type) {
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

		if (!res && !extra_error_message_supplied)
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


static int isstring(struct val *vp)
{
	/* only TRUE if this string is not a valid number */
	return (vp->type == CW_EXPR_string);
}


static int is_zero_or_null(struct val *vp)
{
	int res;

	if (vp->type == CW_EXPR_number)
		res = (vp->u.n == 0.0L);
	else
		res = !vp->u.s
			|| vp->u.s[0] == '\0'
			|| (vp->type == CW_EXPR_numeric_string && to_number(vp) && vp->u.n == 0.0L);

	return res;
}


#undef cw_yyerror
#define cw_yyerror(x) cw_yyerror(x, YYLTYPE *yylloc, struct parse_io *parseio)

/* I put the cw_yyerror func in the flex input file,
   because it refers to the buffer state. Best to
   let it access the BUFFER stuff there and not trying
   define all the structs, macros etc. in this file! */


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
		to_number(a);
		to_number(b);
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
		to_number(a);
		to_number(b);
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
		to_number(a);
		to_number(b);
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
		to_number(a);
		to_number(b);
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
		to_number(a);
		to_number(b);
		r = make_number((long double)(a->u.n <= b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_cond(struct val *a, struct val *b, struct val *c)
{
	struct val *r = c;

	if ((a->type == CW_EXPR_string && a->u.s[0] && strcmp(a->u.s, "0") != 0)
	|| (a->type != CW_EXPR_string && to_number(a) && a->u.n)) {
		r = b;
		b = c;
	}

	free_value(a);
	free_value(b);

	return r;
}

static struct val * op_ne(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) != 0));
	} else {
		to_number(a);
		to_number(b);
		r = make_number((long double)(a->u.n != b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_plus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a)) {
		r = a->u.n;
		if (to_number(b))
			r += b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_minus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a)) {
		r = a->u.n;
		if (to_number(b))
			r -= b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_negate(struct val *a)
{
	long double r = 0.0L;

	if (to_number(a))
		r = -a->u.n;

	free_value(a);
	return make_number(r);
}

static struct val * op_compl(struct val *a)
{
	int v1 = 1;

	switch (a->type) {
		case CW_EXPR_number:
			if (a->u.n == 0.0L)
				v1 = 0;
			break;

		case CW_EXPR_numeric_string:
		case CW_EXPR_string:
			if (a->u.s == 0)
				v1 = 0;
			else {
				if (a->u.s[0] == 0
				|| (a->u.s[0] == '0' && a->u.s[1] == '\0'))
					v1 = 0;
			}
			break;
	}

	free_value(a);
	return make_number(!v1);
}

static struct val * op_times(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a) && to_number(b))
		r = a->u.n * b->u.n;

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_div(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a)) {
		if (to_number(b) && b->u.n != 0.0L)
			r = a->u.n / b->u.n;
		else
			r = INFINITY * (a->u.n >= 0 ? 1 : -1);
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}
	
static struct val * op_rem(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a) && to_number(b) && b->u.n != 0.0L)
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
	int eval;
	struct val *v = NULL;

	if (to_string(a) && to_string(b)) {
		if (!(eval = regcomp(&rp, b->u.s, REG_EXTENDED))) {
			/* remember that patterns are anchored to the beginning of the line */
			if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0) {
				if (rm[1].rm_so >= 0) {
					*(a->u.s + rm[1].rm_eo) = '\0';
					v = make_str (a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str("");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str("");
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
					v = make_str(a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str("");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str("");
		}
	}

	free_value(a);
	free_value(b);

	return v;
}
