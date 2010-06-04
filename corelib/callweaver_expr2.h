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




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 224 "callweaver_expr2.y"
{
	struct val *val;
	struct cw_dynvals *args;
	int tok;
}
/* Line 1489 of yacc.c.  */
#line 193 "callweaver_expr2.h"
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


