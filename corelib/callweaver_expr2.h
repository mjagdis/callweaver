/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

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

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_COLONCOLON = 258,
     TOK_OR = 259,
     TOK_AND = 260,
     TOK_NE = 261,
     TOK_LE = 262,
     TOK_GE = 263,
     TOK_EQ = 264,
     TOK_EQTILDE = 265,
     TOK_ACOSH = 266,
     TOK_ACOS = 267,
     TOK_ASINH = 268,
     TOK_ASIN = 269,
     TOK_ATANH = 270,
     TOK_ATAN = 271,
     TOK_CBRT = 272,
     TOK_CEIL = 273,
     TOK_COSH = 274,
     TOK_COS = 275,
     TOK_ERFC = 276,
     TOK_ERF = 277,
     TOK_EXP2 = 278,
     TOK_EXP = 279,
     TOK_EXPM1 = 280,
     TOK_FABS = 281,
     TOK_FLOOR = 282,
     TOK_LGAMMA = 283,
     TOK_LOG10 = 284,
     TOK_LOG1P = 285,
     TOK_LOG2 = 286,
     TOK_LOGB = 287,
     TOK_LOG = 288,
     TOK_NEARBYINT = 289,
     TOK_RINT = 290,
     TOK_ROUND = 291,
     TOK_SINH = 292,
     TOK_SIN = 293,
     TOK_SQRT = 294,
     TOK_TANH = 295,
     TOK_TAN = 296,
     TOK_TGAMMA = 297,
     TOK_TRUNC = 298,
     TOK_ATAN2 = 299,
     TOK_COPYSIGN = 300,
     TOK_FDIM = 301,
     TOK_FMAX = 302,
     TOK_FMIN = 303,
     TOK_FMOD = 304,
     TOK_HYPOT = 305,
     TOK_NEXTAFTER = 306,
     TOK_NEXTTOWARD = 307,
     TOK_POW = 308,
     TOK_REMAINDER = 309,
     TOK_FMA = 310,
     TOKEN = 311
   };
#endif
/* Tokens.  */
#define TOK_COLONCOLON 258
#define TOK_OR 259
#define TOK_AND 260
#define TOK_NE 261
#define TOK_LE 262
#define TOK_GE 263
#define TOK_EQ 264
#define TOK_EQTILDE 265
#define TOK_ACOSH 266
#define TOK_ACOS 267
#define TOK_ASINH 268
#define TOK_ASIN 269
#define TOK_ATANH 270
#define TOK_ATAN 271
#define TOK_CBRT 272
#define TOK_CEIL 273
#define TOK_COSH 274
#define TOK_COS 275
#define TOK_ERFC 276
#define TOK_ERF 277
#define TOK_EXP2 278
#define TOK_EXP 279
#define TOK_EXPM1 280
#define TOK_FABS 281
#define TOK_FLOOR 282
#define TOK_LGAMMA 283
#define TOK_LOG10 284
#define TOK_LOG1P 285
#define TOK_LOG2 286
#define TOK_LOGB 287
#define TOK_LOG 288
#define TOK_NEARBYINT 289
#define TOK_RINT 290
#define TOK_ROUND 291
#define TOK_SINH 292
#define TOK_SIN 293
#define TOK_SQRT 294
#define TOK_TANH 295
#define TOK_TAN 296
#define TOK_TGAMMA 297
#define TOK_TRUNC 298
#define TOK_ATAN2 299
#define TOK_COPYSIGN 300
#define TOK_FDIM 301
#define TOK_FMAX 302
#define TOK_FMIN 303
#define TOK_FMOD 304
#define TOK_HYPOT 305
#define TOK_NEXTAFTER 306
#define TOK_NEXTTOWARD 307
#define TOK_POW 308
#define TOK_REMAINDER 309
#define TOK_FMA 310
#define TOKEN 311




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 216 "callweaver_expr2.y"
{
	struct val *val;
	struct cw_dynvals *args;
	int tok;
}
/* Line 1489 of yacc.c.  */
#line 167 "callweaver_expr2.h"
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


