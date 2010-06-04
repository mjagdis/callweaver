/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* Substitute the variable and function names.  */
#define yyparse cw_yyparse
#define yylex   cw_yylex
#define yyerror cw_yyerror
#define yylval  cw_yylval
#define yychar  cw_yychar
#define yydebug cw_yydebug
#define yynerrs cw_yynerrs
#define yylloc cw_yylloc

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_COMMA = 258,
     TOK_COLONCOLON = 259,
     TOK_COND = 260,
     TOK_OR = 261,
     TOK_AND = 262,
     TOK_NE = 263,
     TOK_LE = 264,
     TOK_GE = 265,
     TOK_LT = 266,
     TOK_GT = 267,
     TOK_EQ = 268,
     TOK_MINUS = 269,
     TOK_PLUS = 270,
     TOK_MOD = 271,
     TOK_DIV = 272,
     TOK_MULT = 273,
     TOK_COMPL = 274,
     TOK_EQTILDE = 275,
     TOK_COLON = 276,
     TOK_LP = 277,
     TOK_RP = 278,
     TOK_ACOSH = 279,
     TOK_ACOS = 280,
     TOK_ASINH = 281,
     TOK_ASIN = 282,
     TOK_ATANH = 283,
     TOK_ATAN = 284,
     TOK_CBRT = 285,
     TOK_CEIL = 286,
     TOK_COSH = 287,
     TOK_COS = 288,
     TOK_ERFC = 289,
     TOK_ERF = 290,
     TOK_EXP2 = 291,
     TOK_EXP = 292,
     TOK_EXPM1 = 293,
     TOK_FABS = 294,
     TOK_FLOOR = 295,
     TOK_LGAMMA = 296,
     TOK_LOG10 = 297,
     TOK_LOG1P = 298,
     TOK_LOG2 = 299,
     TOK_LOGB = 300,
     TOK_LOG = 301,
     TOK_NEARBYINT = 302,
     TOK_RINT = 303,
     TOK_ROUND = 304,
     TOK_SINH = 305,
     TOK_SIN = 306,
     TOK_SQRT = 307,
     TOK_TANH = 308,
     TOK_TAN = 309,
     TOK_TGAMMA = 310,
     TOK_TRUNC = 311,
     TOK_ATAN2 = 312,
     TOK_COPYSIGN = 313,
     TOK_FDIM = 314,
     TOK_FMAX = 315,
     TOK_FMIN = 316,
     TOK_FMOD = 317,
     TOK_HYPOT = 318,
     TOK_NEXTAFTER = 319,
     TOK_NEXTTOWARD = 320,
     TOK_POW = 321,
     TOK_REMAINDER = 322,
     TOK_FMA = 323,
     TOKEN = 324
   };
#endif
/* Tokens.  */
#define TOK_COMMA 258
#define TOK_COLONCOLON 259
#define TOK_COND 260
#define TOK_OR 261
#define TOK_AND 262
#define TOK_NE 263
#define TOK_LE 264
#define TOK_GE 265
#define TOK_LT 266
#define TOK_GT 267
#define TOK_EQ 268
#define TOK_MINUS 269
#define TOK_PLUS 270
#define TOK_MOD 271
#define TOK_DIV 272
#define TOK_MULT 273
#define TOK_COMPL 274
#define TOK_EQTILDE 275
#define TOK_COLON 276
#define TOK_LP 277
#define TOK_RP 278
#define TOK_ACOSH 279
#define TOK_ACOS 280
#define TOK_ASINH 281
#define TOK_ASIN 282
#define TOK_ATANH 283
#define TOK_ATAN 284
#define TOK_CBRT 285
#define TOK_CEIL 286
#define TOK_COSH 287
#define TOK_COS 288
#define TOK_ERFC 289
#define TOK_ERF 290
#define TOK_EXP2 291
#define TOK_EXP 292
#define TOK_EXPM1 293
#define TOK_FABS 294
#define TOK_FLOOR 295
#define TOK_LGAMMA 296
#define TOK_LOG10 297
#define TOK_LOG1P 298
#define TOK_LOG2 299
#define TOK_LOGB 300
#define TOK_LOG 301
#define TOK_NEARBYINT 302
#define TOK_RINT 303
#define TOK_ROUND 304
#define TOK_SINH 305
#define TOK_SIN 306
#define TOK_SQRT 307
#define TOK_TANH 308
#define TOK_TAN 309
#define TOK_TGAMMA 310
#define TOK_TRUNC 311
#define TOK_ATAN2 312
#define TOK_COPYSIGN 313
#define TOK_FDIM 314
#define TOK_FMAX 315
#define TOK_FMIN 316
#define TOK_FMOD 317
#define TOK_HYPOT 318
#define TOK_NEXTAFTER 319
#define TOK_NEXTTOWARD 320
#define TOK_POW 321
#define TOK_REMAINDER 322
#define TOK_FMA 323
#define TOKEN 324




/* Copy the first part of user declarations.  */
#line 1 "callweaver_expr2.y"

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
#define YYERROR_VERBOSE 1
extern char extra_error_message[4095];
extern int extra_error_message_supplied;


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


typedef long double (*math_f_t)(long double);
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

typedef long double (*math_ff_t)(long double, long double);
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

typedef long double (*math_fff_t)(long double, long double, long double);
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
static const char *string_rep(struct val *, char buf[MAX_NUMBER_LEN]);

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


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 225 "callweaver_expr2.y"
{
	struct val *val;
	struct cw_dynvals *args;
	int tok;
}
/* Line 187 of yacc.c.  */
#line 465 "callweaver_expr2.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */
#line 231 "callweaver_expr2.y"

extern int		cw_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));


/* Line 216 of yacc.c.  */
#line 493 "callweaver_expr2.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
	     && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE) + sizeof (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  59
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   362

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  8
/* YYNRULES -- Number of rules.  */
#define YYNRULES  82
/* YYNRULES -- Number of states.  */
#define YYNSTATES  118

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   324

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     5,     6,     7,     9,    11,    15,    18,
      21,    23,    25,    27,    29,    31,    33,    35,    37,    39,
      41,    43,    45,    47,    49,    51,    53,    55,    57,    59,
      61,    63,    65,    67,    69,    71,    73,    75,    77,    79,
      81,    83,    85,    87,    89,    91,    93,    95,    97,    99,
     101,   103,   105,   107,   109,   111,   113,   118,   123,   125,
     132,   134,   143,   145,   147,   151,   155,   159,   163,   167,
     171,   175,   179,   183,   187,   191,   194,   197,   201,   205,
     209,   213,   217
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      71,     0,    -1,    77,    -1,    -1,    -1,    73,    -1,    77,
      -1,    73,     3,    77,    -1,    73,     3,    -1,     3,    77,
      -1,     3,    -1,    24,    -1,    25,    -1,    26,    -1,    27,
      -1,    28,    -1,    29,    -1,    30,    -1,    31,    -1,    32,
      -1,    33,    -1,    34,    -1,    35,    -1,    36,    -1,    37,
      -1,    38,    -1,    39,    -1,    40,    -1,    41,    -1,    42,
      -1,    43,    -1,    44,    -1,    45,    -1,    46,    -1,    47,
      -1,    48,    -1,    49,    -1,    50,    -1,    51,    -1,    52,
      -1,    53,    -1,    54,    -1,    55,    -1,    56,    -1,    57,
      -1,    58,    -1,    59,    -1,    60,    -1,    61,    -1,    62,
      -1,    63,    -1,    64,    -1,    65,    -1,    66,    -1,    67,
      -1,    68,    -1,    69,    22,    72,    23,    -1,    74,    22,
      77,    23,    -1,    74,    -1,    75,    22,    77,     3,    77,
      23,    -1,    75,    -1,    76,    22,    77,     3,    77,     3,
      77,    23,    -1,    76,    -1,    69,    -1,    22,    77,    23,
      -1,    77,     6,    77,    -1,    77,     7,    77,    -1,    77,
      13,    77,    -1,    77,    12,    77,    -1,    77,    11,    77,
      -1,    77,    10,    77,    -1,    77,     9,    77,    -1,    77,
       8,    77,    -1,    77,    15,    77,    -1,    77,    14,    77,
      -1,    14,    77,    -1,    19,    77,    -1,    77,    18,    77,
      -1,    77,    17,    77,    -1,    77,    16,    77,    -1,    77,
      21,    77,    -1,    77,    20,    77,    -1,    77,     5,    77,
       4,    77,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   274,   274,   280,   288,   292,   295,   299,   303,   307,
     311,   318,   319,   320,   321,   322,   323,   324,   325,   326,
     327,   328,   329,   330,   331,   332,   333,   334,   335,   336,
     337,   338,   339,   340,   341,   342,   343,   344,   345,   346,
     347,   348,   349,   350,   354,   355,   356,   357,   358,   359,
     360,   361,   362,   363,   364,   368,   371,   419,   420,   421,
     422,   423,   424,   426,   427,   430,   433,   436,   439,   442,
     445,   448,   451,   454,   457,   460,   463,   466,   469,   472,
     475,   478,   481
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TOK_COMMA", "TOK_COLONCOLON",
  "TOK_COND", "TOK_OR", "TOK_AND", "TOK_NE", "TOK_LE", "TOK_GE", "TOK_LT",
  "TOK_GT", "TOK_EQ", "TOK_MINUS", "TOK_PLUS", "TOK_MOD", "TOK_DIV",
  "TOK_MULT", "TOK_COMPL", "TOK_EQTILDE", "TOK_COLON", "TOK_LP", "TOK_RP",
  "TOK_ACOSH", "TOK_ACOS", "TOK_ASINH", "TOK_ASIN", "TOK_ATANH",
  "TOK_ATAN", "TOK_CBRT", "TOK_CEIL", "TOK_COSH", "TOK_COS", "TOK_ERFC",
  "TOK_ERF", "TOK_EXP2", "TOK_EXP", "TOK_EXPM1", "TOK_FABS", "TOK_FLOOR",
  "TOK_LGAMMA", "TOK_LOG10", "TOK_LOG1P", "TOK_LOG2", "TOK_LOGB",
  "TOK_LOG", "TOK_NEARBYINT", "TOK_RINT", "TOK_ROUND", "TOK_SINH",
  "TOK_SIN", "TOK_SQRT", "TOK_TANH", "TOK_TAN", "TOK_TGAMMA", "TOK_TRUNC",
  "TOK_ATAN2", "TOK_COPYSIGN", "TOK_FDIM", "TOK_FMAX", "TOK_FMIN",
  "TOK_FMOD", "TOK_HYPOT", "TOK_NEXTAFTER", "TOK_NEXTTOWARD", "TOK_POW",
  "TOK_REMAINDER", "TOK_FMA", "TOKEN", "$accept", "start", "args", "args1",
  "math_f", "math_ff", "math_fff", "expr", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    70,    71,    71,    72,    72,    73,    73,    73,    73,
      73,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    75,    75,    75,    75,    75,    75,
      75,    75,    75,    75,    75,    76,    77,    77,    77,    77,
      77,    77,    77,    77,    77,    77,    77,    77,    77,    77,
      77,    77,    77,    77,    77,    77,    77,    77,    77,    77,
      77,    77,    77
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     0,     0,     1,     1,     3,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     4,     4,     1,     6,
       1,     8,     1,     1,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     2,     2,     3,     3,     3,
       3,     3,     5
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     0,     0,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    63,
       0,    58,    60,    62,     2,    75,    76,     0,     4,     1,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    64,
      10,     0,     5,     6,     0,     0,     0,     0,    65,    66,
      72,    71,    70,    69,    68,    67,    74,    73,    79,    78,
      77,    81,    80,     9,    56,     8,    57,     0,     0,     0,
       7,     0,     0,    82,    59,     0,     0,    61
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,    50,    81,    82,    51,    52,    53,    54
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -6
static const yytype_int16 yypact[] =
{
     147,   147,   147,   147,    -6,    -6,    -6,    -6,    -6,    -6,
      -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,
      -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,
      -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,
      -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -6,    -5,
      20,    14,    32,    36,   304,   142,   142,    -2,    91,    -6,
     147,   147,   147,   147,   147,   147,   147,   147,   147,   147,
     147,   147,   147,   147,   147,   147,   147,   147,   147,    -6,
     147,    16,    97,   304,    17,    75,   214,   287,   319,   333,
     341,   341,   341,   341,   341,   341,    81,    81,   142,   142,
     142,    -6,    -6,   304,    -6,   147,    -6,   147,   147,   147,
     304,   231,   250,    35,    -6,   147,   267,    -6
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
      -6,    -6,    -6,    -6,    -6,    -6,    -6,    -1
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      55,    56,    57,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    58,    77,    78,
      59,    79,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    60,    77,    78,   104,
     106,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    61,    77,    78,    83,    62,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   107,   103,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    80,    77,    78,    74,    75,    76,
     105,    77,    78,     0,   110,     1,   111,   112,   113,     0,
       2,     0,     0,     3,   116,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,     1,    77,    78,     0,     0,     2,     0,     0,     3,
       0,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,   108,     0,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,     0,    77,    78,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
       0,    77,    78,   115,   114,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,     0,
      77,    78,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,     0,    77,    78,     0,
     117,   109,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,     0,    77,    78,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,     0,    77,    78,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,     0,    77,
      78,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,     0,    77,    78,    72,    73,    74,    75,    76,
       0,    77,    78
};

static const yytype_int8 yycheck[] =
{
       1,     2,     3,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    22,    20,    21,
       0,    23,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    22,    20,    21,    23,
      23,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    22,    20,    21,    58,    22,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,     3,    80,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,     3,    20,    21,    16,    17,    18,
       3,    20,    21,    -1,   105,    14,   107,   108,   109,    -1,
      19,    -1,    -1,    22,   115,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    14,    20,    21,    -1,    -1,    19,    -1,    -1,    22,
      -1,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,     3,    -1,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    -1,    20,    21,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      -1,    20,    21,     3,    23,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    -1,
      20,    21,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    20,    21,    -1,
      23,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    20,    21,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    -1,    20,    21,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    -1,    20,
      21,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    -1,    20,    21,    14,    15,    16,    17,    18,
      -1,    20,    21
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    14,    19,    22,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      71,    74,    75,    76,    77,    77,    77,    77,    22,     0,
      22,    22,    22,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    20,    21,    23,
       3,    72,    73,    77,    77,    77,    77,    77,    77,    77,
      77,    77,    77,    77,    77,    77,    77,    77,    77,    77,
      77,    77,    77,    77,    23,     3,    23,     3,     3,     4,
      77,    77,    77,    77,    23,     3,    77,    23
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, &yylloc, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, &yylloc)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (yylocationp);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yylsp, yyrule)
    YYSTYPE *yyvsp;
    YYLTYPE *yylsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, yylsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, yylocationp)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
      case 69: /* "TOKEN" */
#line 269 "callweaver_expr2.y"
	{ free((yyvaluep->val)); };
#line 1561 "callweaver_expr2.c"
	break;
      case 72: /* "args" */
#line 270 "callweaver_expr2.y"
	{ args_free((yyvaluep->args)); };
#line 1566 "callweaver_expr2.c"
	break;
      case 73: /* "args1" */
#line 270 "callweaver_expr2.y"
	{ args_free((yyvaluep->args)); };
#line 1571 "callweaver_expr2.c"
	break;
      case 77: /* "expr" */
#line 269 "callweaver_expr2.y"
	{ free((yyvaluep->val)); };
#line 1576 "callweaver_expr2.c"
	break;

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  /* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the look-ahead symbol.  */
YYLTYPE yylloc;

  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[2];

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;
#if YYLTYPE_IS_TRIVIAL
  /* Initialize the default location before parsing starts.  */
  yylloc.first_line   = yylloc.last_line   = 1;
  yylloc.first_column = yylloc.last_column = 0;
#endif

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
	YYSTACK_RELOCATE (yyls);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;
  *++yylsp = yylloc;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location.  */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 274 "callweaver_expr2.y"
    {
			struct parse_io *p = parseio;

			if (!(p->val = (yyvsp[(1) - (1)].val)))
				YYABORT;
		;}
    break;

  case 3:
#line 280 "callweaver_expr2.y"
    {/* nothing */
			struct parse_io *p = parseio;

			if (!(p->val = cw_expr_make_str(CW_EXPR_string, "", 0)))
				YYABORT;
		;}
    break;

  case 4:
#line 288 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_new()))
				YYABORT;
		;}
    break;

  case 6:
#line 295 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val(args_new(), (yyvsp[(1) - (1)].val))))
				YYABORT;
		;}
    break;

  case 7:
#line 299 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val((yyvsp[(1) - (3)].args), (yyvsp[(3) - (3)].val))))
				YYABORT;
		;}
    break;

  case 8:
#line 303 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_null((yyvsp[(1) - (2)].args))))
				YYABORT;
		;}
    break;

  case 9:
#line 307 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val(args_push_null(args_new()), (yyvsp[(2) - (2)].val))))
				YYABORT;
		;}
    break;

  case 10:
#line 311 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_null(args_new())))
				YYABORT;
		;}
    break;

  case 11:
#line 318 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ACOSH; ;}
    break;

  case 12:
#line 319 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ACOS; ;}
    break;

  case 13:
#line 320 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ASINH; ;}
    break;

  case 14:
#line 321 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ASIN; ;}
    break;

  case 15:
#line 322 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ATANH; ;}
    break;

  case 16:
#line 323 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ATAN; ;}
    break;

  case 17:
#line 324 "callweaver_expr2.y"
    { (yyval.tok) = TOK_CBRT; ;}
    break;

  case 18:
#line 325 "callweaver_expr2.y"
    { (yyval.tok) = TOK_CEIL; ;}
    break;

  case 19:
#line 326 "callweaver_expr2.y"
    { (yyval.tok) = TOK_COSH; ;}
    break;

  case 20:
#line 327 "callweaver_expr2.y"
    { (yyval.tok) = TOK_COS; ;}
    break;

  case 21:
#line 328 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ERFC; ;}
    break;

  case 22:
#line 329 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ERF; ;}
    break;

  case 23:
#line 330 "callweaver_expr2.y"
    { (yyval.tok) = TOK_EXP2; ;}
    break;

  case 24:
#line 331 "callweaver_expr2.y"
    { (yyval.tok) = TOK_EXP; ;}
    break;

  case 25:
#line 332 "callweaver_expr2.y"
    { (yyval.tok) = TOK_EXPM1; ;}
    break;

  case 26:
#line 333 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FABS; ;}
    break;

  case 27:
#line 334 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FLOOR; ;}
    break;

  case 28:
#line 335 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LGAMMA; ;}
    break;

  case 29:
#line 336 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LOG10; ;}
    break;

  case 30:
#line 337 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LOG1P; ;}
    break;

  case 31:
#line 338 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LOG2; ;}
    break;

  case 32:
#line 339 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LOGB; ;}
    break;

  case 33:
#line 340 "callweaver_expr2.y"
    { (yyval.tok) = TOK_LOG; ;}
    break;

  case 34:
#line 341 "callweaver_expr2.y"
    { (yyval.tok) = TOK_NEARBYINT; ;}
    break;

  case 35:
#line 342 "callweaver_expr2.y"
    { (yyval.tok) = TOK_RINT; ;}
    break;

  case 36:
#line 343 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ROUND; ;}
    break;

  case 37:
#line 344 "callweaver_expr2.y"
    { (yyval.tok) = TOK_SINH; ;}
    break;

  case 38:
#line 345 "callweaver_expr2.y"
    { (yyval.tok) = TOK_SIN; ;}
    break;

  case 39:
#line 346 "callweaver_expr2.y"
    { (yyval.tok) = TOK_SQRT; ;}
    break;

  case 40:
#line 347 "callweaver_expr2.y"
    { (yyval.tok) = TOK_TANH; ;}
    break;

  case 41:
#line 348 "callweaver_expr2.y"
    { (yyval.tok) = TOK_TAN; ;}
    break;

  case 42:
#line 349 "callweaver_expr2.y"
    { (yyval.tok) = TOK_TGAMMA; ;}
    break;

  case 43:
#line 350 "callweaver_expr2.y"
    { (yyval.tok) = TOK_TRUNC; ;}
    break;

  case 44:
#line 354 "callweaver_expr2.y"
    { (yyval.tok) = TOK_ATAN2; ;}
    break;

  case 45:
#line 355 "callweaver_expr2.y"
    { (yyval.tok) = TOK_COPYSIGN; ;}
    break;

  case 46:
#line 356 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FDIM; ;}
    break;

  case 47:
#line 357 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FMAX; ;}
    break;

  case 48:
#line 358 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FMIN; ;}
    break;

  case 49:
#line 359 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FMOD; ;}
    break;

  case 50:
#line 360 "callweaver_expr2.y"
    { (yyval.tok) = TOK_HYPOT; ;}
    break;

  case 51:
#line 361 "callweaver_expr2.y"
    { (yyval.tok) = TOK_NEXTAFTER; ;}
    break;

  case 52:
#line 362 "callweaver_expr2.y"
    { (yyval.tok) = TOK_NEXTTOWARD; ;}
    break;

  case 53:
#line 363 "callweaver_expr2.y"
    { (yyval.tok) = TOK_POW; ;}
    break;

  case 54:
#line 364 "callweaver_expr2.y"
    { (yyval.tok) = TOK_REMAINDER; ;}
    break;

  case 55:
#line 368 "callweaver_expr2.y"
    { (yyval.tok) = TOK_FMA; ;}
    break;

  case 56:
#line 371 "callweaver_expr2.y"
    {
			struct cw_dynstr ds = CW_DYNSTR_INIT;
			struct cw_dynstr result = CW_DYNSTR_INIT;
			char buf[MAX_NUMBER_LEN];
			const struct parse_io *p = parseio;
			const char *funcname;
			char **argv;
			int argc;
			int res = 1;

			(yyval.val) = NULL;

			if ((argv = malloc(sizeof(argv[0]) * ((yyvsp[(3) - (4)].args)->used + 1)))) {
				for (argc = 0; argc < (yyvsp[(3) - (4)].args)->used; argc++) {
					if ((yyvsp[(3) - (4)].args)->data[argc]->type != CW_EXPR_number)
						argv[argc] = (yyvsp[(3) - (4)].args)->data[argc]->u.s;
					else {
						argv[argc] = (char *)ds.used;
						cw_dynstr_printf(&ds, NUMBER_FORMAT "%c", (yyvsp[(3) - (4)].args)->data[argc]->u.n, 0);
					}
				}

				if (!ds.error) {
					for (argc = 0; argc < (yyvsp[(3) - (4)].args)->used; argc++) {
						if ((yyvsp[(3) - (4)].args)->data[argc]->type == CW_EXPR_number)
							argv[argc] = &ds.data[(size_t)argv[argc]];
					}

					argv[argc] = NULL;

					funcname = string_rep((yyvsp[(1) - (4)].val), buf);

					if (!(res = (cw_function_exec(p->chan, cw_hash_string(funcname), funcname, argc, argv, &result) || result.error)))
						(yyval.val) = cw_expr_make_str(CW_EXPR_arbitrary_string, result.data, result.used);

					cw_dynstr_free(&result);
				}

				cw_dynstr_free(&ds);
				free(argv);
			}

			free((yyvsp[(1) - (4)].val));
			args_free((yyvsp[(3) - (4)].args));

			if (res)
				YYABORT;
		;}
    break;

  case 57:
#line 419 "callweaver_expr2.y"
    { if (!((yyval.val) = op_math_f(math_f_func[(yyvsp[(1) - (4)].tok) - TOK_ACOSH], (yyvsp[(3) - (4)].val)))) YYABORT; ;}
    break;

  case 58:
#line 420 "callweaver_expr2.y"
    { if (!((yyval.val) = cw_expr_make_str(CW_EXPR_string, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].s, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].l))) YYABORT; ;}
    break;

  case 59:
#line 421 "callweaver_expr2.y"
    { if (!((yyval.val) = op_math_ff(math_ff_func[(yyvsp[(1) - (6)].tok) - TOK_ATAN2], (yyvsp[(3) - (6)].val), (yyvsp[(5) - (6)].val)))) YYABORT; ;}
    break;

  case 60:
#line 422 "callweaver_expr2.y"
    { if (!((yyval.val) = cw_expr_make_str(CW_EXPR_string, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].s, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].l))) YYABORT; ;}
    break;

  case 61:
#line 423 "callweaver_expr2.y"
    { if (!((yyval.val) = op_math_fff(math_fff_func[(yyvsp[(1) - (8)].tok) - TOK_FMA], (yyvsp[(3) - (8)].val), (yyvsp[(5) - (8)].val), (yyvsp[(7) - (8)].val)))) YYABORT; ;}
    break;

  case 62:
#line 424 "callweaver_expr2.y"
    { if (!((yyval.val) = cw_expr_make_str(CW_EXPR_string, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].s, math_name[(yyvsp[(1) - (1)].tok) - TOK_ACOSH].l))) YYABORT; ;}
    break;

  case 63:
#line 426 "callweaver_expr2.y"
    { if (!((yyval.val) = (yyvsp[(1) - (1)].val))) YYABORT; ;}
    break;

  case 64:
#line 427 "callweaver_expr2.y"
    { if (!((yyval.val) = (yyvsp[(2) - (3)].val))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 65:
#line 430 "callweaver_expr2.y"
    { if (!((yyval.val) = op_or((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 66:
#line 433 "callweaver_expr2.y"
    { if (!((yyval.val) = op_and((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
                          (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 67:
#line 436 "callweaver_expr2.y"
    { if (!((yyval.val) = op_eq((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 68:
#line 439 "callweaver_expr2.y"
    { if (!((yyval.val) = op_gt((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 69:
#line 442 "callweaver_expr2.y"
    { if (!((yyval.val) = op_lt((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 70:
#line 445 "callweaver_expr2.y"
    { if (!((yyval.val) = op_ge((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 71:
#line 448 "callweaver_expr2.y"
    { if (!((yyval.val) = op_le((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 72:
#line 451 "callweaver_expr2.y"
    { if (!((yyval.val) = op_ne((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 73:
#line 454 "callweaver_expr2.y"
    { if (!((yyval.val) = op_plus((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 74:
#line 457 "callweaver_expr2.y"
    { if (!((yyval.val) = op_minus((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 75:
#line 460 "callweaver_expr2.y"
    { if (!((yyval.val) = op_negate((yyvsp[(2) - (2)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 76:
#line 463 "callweaver_expr2.y"
    { if (!((yyval.val) = op_compl((yyvsp[(2) - (2)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 77:
#line 466 "callweaver_expr2.y"
    { if (!((yyval.val) = op_times((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 78:
#line 469 "callweaver_expr2.y"
    { if (!((yyval.val) = op_div((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 79:
#line 472 "callweaver_expr2.y"
    { if (!((yyval.val) = op_rem((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 80:
#line 475 "callweaver_expr2.y"
    { if (!((yyval.val) = op_colon((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 81:
#line 478 "callweaver_expr2.y"
    { if (!((yyval.val) = op_eqtilde((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 82:
#line 481 "callweaver_expr2.y"
    { if (!((yyval.val) = op_cond ((yyvsp[(1) - (5)].val), (yyvsp[(3) - (5)].val), (yyvsp[(5) - (5)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (5)]).first_column; (yyloc).last_column = (yylsp[(3) - (5)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;


/* Line 1267 of yacc.c.  */
#line 2413 "callweaver_expr2.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }

  yyerror_range[0] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval, &yylloc);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  yyerror_range[0] = yylsp[1-yylen];
  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      yyerror_range[0] = *yylsp;
      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the look-ahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, (yyerror_range - 1), 2);
  *++yylsp = yyloc;

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, &yylloc);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 486 "callweaver_expr2.y"



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


static struct val *op_or(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a)) {
		r = b;
		b = a;
	}

	free(b);
	return r;
}


static struct val *op_and(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a) || is_zero_or_null(b)) {
		free(a);
		r = cw_expr_make_number(0.0L);
	}

	free(b);
	return r;
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


static struct val *op_cond(struct val *a, struct val *b, struct val *c)
{
	struct val *r = b;

	if (is_zero_or_null(a)) {
		r = c;
		c = b;
	}

	free(a);
	free(c);

	return r;
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

