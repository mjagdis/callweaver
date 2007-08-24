/* A Bison parser, made by GNU Bison 1.875.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* If NAME_PREFIX is specified substitute the variables and functions
   names.  */
#define yyparse ael_yyparse
#define yylex   ael_yylex
#define yyerror ael_yyerror
#define yylval  ael_yylval
#define yychar  ael_yychar
#define yydebug ael_yydebug
#define yynerrs ael_yynerrs
#define yylloc ael_yylloc

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     KW_CONTEXT = 258,
     LC = 259,
     RC = 260,
     LP = 261,
     RP = 262,
     SEMI = 263,
     EQ = 264,
     COMMA = 265,
     COLON = 266,
     AMPER = 267,
     BAR = 268,
     AT = 269,
     KW_PROC = 270,
     KW_GLOBALS = 271,
     KW_IGNOREPAT = 272,
     KW_SWITCH = 273,
     KW_IF = 274,
     KW_IFTIME = 275,
     KW_ELSE = 276,
     KW_RANDOM = 277,
     KW_ABSTRACT = 278,
     EXTENMARK = 279,
     KW_GOTO = 280,
     KW_JUMP = 281,
     KW_RETURN = 282,
     KW_BREAK = 283,
     KW_CONTINUE = 284,
     KW_REGEXTEN = 285,
     KW_HINT = 286,
     KW_FOR = 287,
     KW_WHILE = 288,
     KW_CASE = 289,
     KW_PATTERN = 290,
     KW_DEFAULT = 291,
     KW_CATCH = 292,
     KW_SWITCHES = 293,
     KW_ESWITCHES = 294,
     KW_INCLUDES = 295,
     word = 296
   };
#endif
#define KW_CONTEXT 258
#define LC 259
#define RC 260
#define LP 261
#define RP 262
#define SEMI 263
#define EQ 264
#define COMMA 265
#define COLON 266
#define AMPER 267
#define BAR 268
#define AT 269
#define KW_PROC 270
#define KW_GLOBALS 271
#define KW_IGNOREPAT 272
#define KW_SWITCH 273
#define KW_IF 274
#define KW_IFTIME 275
#define KW_ELSE 276
#define KW_RANDOM 277
#define KW_ABSTRACT 278
#define EXTENMARK 279
#define KW_GOTO 280
#define KW_JUMP 281
#define KW_RETURN 282
#define KW_BREAK 283
#define KW_CONTINUE 284
#define KW_REGEXTEN 285
#define KW_HINT 286
#define KW_FOR 287
#define KW_WHILE 288
#define KW_CASE 289
#define KW_PATTERN 290
#define KW_DEFAULT 291
#define KW_CATCH 292
#define KW_SWITCHES 293
#define KW_ESWITCHES 294
#define KW_INCLUDES 295
#define word 296




/* Copy the first part of user declarations.  */
#line 1 "ael/ael.y"

/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Steve Murphy <murf@parsetree.com>
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
 * \brief Bison Grammar description of AEL2.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

#include "callweaver/logger.h"

#include "ael_structs.h"

static pval * linku1(pval *head, pval *tail);

void reset_parencount(yyscan_t yyscanner);
void reset_semicount(yyscan_t yyscanner);
void reset_argcount(yyscan_t yyscanner );

#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1

extern char *my_file;
#ifdef AAL_ARGCHECK
int ael_is_funcname(char *name);
#endif
static char *ael_token_subst(char *mess);



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 53 "ael/ael.y"
typedef union YYSTYPE {
	int	intval;		/* integer value, typically flags */
	char	*str;		/* strings */
	struct pval *pval;	/* full objects */
} YYSTYPE;
/* Line 191 of yacc.c.  */
#line 223 "ael/ael_tab.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined (YYLTYPE) && ! defined (YYLTYPE_IS_DECLARED)
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
#line 59 "ael/ael.y"

	/* declaring these AFTER the union makes things a lot simpler! */
void yyerror(YYLTYPE *locp, struct parse_io *parseio, char const *s);
int ael_yylex (YYSTYPE * yylval_param, YYLTYPE * yylloc_param , void * yyscanner);

/* create a new object with start-end marker */
static pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column);

/* create a new object with start-end marker, simplified interface.
 * Must be declared here because YYLTYPE is not known before
 */
static pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last);

/* another frontend for npval, this time for a string */
static pval *nword(char *string, YYLTYPE *pos);

/* update end position of an object, return the object */
static pval *update_last(pval *, YYLTYPE *);


/* Line 214 of yacc.c.  */
#line 267 "ael/ael_tab.c"

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYLTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
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
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  14
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   302

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  53
/* YYNRULES -- Number of rules. */
#define YYNRULES  130
/* YYNRULES -- Number of states. */
#define YYNSTATES  266

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   296

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
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
      35,    36,    37,    38,    39,    40,    41
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     5,     7,    10,    13,    15,    17,    19,
      21,    23,    25,    32,    34,    35,    44,    49,    50,    53,
      56,    57,    63,    64,    66,    70,    73,    74,    77,    80,
      82,    84,    86,    88,    90,    92,    95,    97,   102,   106,
     111,   119,   128,   129,   132,   135,   141,   143,   151,   159,
     160,   165,   168,   171,   176,   178,   181,   183,   186,   190,
     192,   195,   199,   205,   209,   211,   215,   219,   222,   223,
     224,   225,   238,   242,   244,   248,   251,   254,   255,   261,
     264,   267,   270,   274,   276,   279,   280,   282,   286,   290,
     296,   302,   308,   314,   315,   318,   321,   326,   327,   333,
     337,   338,   342,   346,   349,   351,   352,   354,   355,   359,
     360,   363,   368,   372,   377,   378,   381,   383,   389,   394,
     399,   400,   404,   407,   409,   413,   417,   420,   424,   427,
     432
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    49,    -1,    50,    -1,     8,
      -1,    41,    -1,    36,    -1,    48,     3,    46,     4,    55,
       5,    -1,    23,    -1,    -1,    15,    41,     6,    54,     7,
       4,    87,     5,    -1,    16,     4,    51,     5,    -1,    -1,
      52,    51,    -1,    51,     1,    -1,    -1,    41,     9,    53,
      41,     8,    -1,    -1,    41,    -1,    54,    10,    41,    -1,
      54,     1,    -1,    -1,    56,    55,    -1,    55,     1,    -1,
      58,    -1,    94,    -1,    89,    -1,    90,    -1,    57,    -1,
      52,    -1,    41,     1,    -1,     8,    -1,    17,    24,    41,
       8,    -1,    41,    24,    69,    -1,    30,    41,    24,    69,
      -1,    31,     6,    66,     7,    41,    24,    69,    -1,    30,
      31,     6,    66,     7,    41,    24,    69,    -1,    -1,    69,
      59,    -1,    59,     1,    -1,    66,    11,    66,    11,    66,
      -1,    41,    -1,    60,    13,    66,    13,    66,    13,    66,
      -1,    60,    10,    66,    10,    66,    10,    66,    -1,    -1,
       6,    63,    65,     7,    -1,    19,    62,    -1,    22,    62,
      -1,    20,     6,    61,     7,    -1,    41,    -1,    41,    41,
      -1,    41,    -1,    41,    41,    -1,    41,    41,    41,    -1,
      41,    -1,    41,    41,    -1,    67,    11,    41,    -1,    18,
      62,     4,    85,     5,    -1,     4,    59,     5,    -1,    52,
      -1,    25,    75,     8,    -1,    26,    77,     8,    -1,    41,
      11,    -1,    -1,    -1,    -1,    32,     6,    70,    41,     8,
      71,    41,     8,    72,    41,     7,    69,    -1,    33,    62,
      69,    -1,    68,    -1,    12,    78,     8,    -1,    82,     8,
      -1,    41,     8,    -1,    -1,    82,     9,    73,    41,     8,
      -1,    28,     8,    -1,    27,     8,    -1,    29,     8,    -1,
      64,    69,    74,    -1,     8,    -1,    21,    69,    -1,    -1,
      67,    -1,    67,    13,    67,    -1,    67,    10,    67,    -1,
      67,    13,    67,    13,    67,    -1,    67,    10,    67,    10,
      67,    -1,    36,    13,    67,    13,    67,    -1,    36,    10,
      67,    10,    67,    -1,    -1,    10,    41,    -1,    67,    76,
      -1,    67,    76,    14,    46,    -1,    -1,    41,     6,    79,
      84,     7,    -1,    41,     6,     7,    -1,    -1,    41,     6,
      81,    -1,    80,    84,     7,    -1,    80,     7,    -1,    41,
      -1,    -1,    65,    -1,    -1,    84,    10,    83,    -1,    -1,
      86,    85,    -1,    34,    41,    11,    59,    -1,    36,    11,
      59,    -1,    35,    41,    11,    59,    -1,    -1,    88,    87,
      -1,    69,    -1,    37,    41,     4,    59,     5,    -1,    38,
       4,    91,     5,    -1,    39,     4,    91,     5,    -1,    -1,
      41,     8,    91,    -1,    91,     1,    -1,    46,    -1,    46,
      13,    61,    -1,    46,    10,    61,    -1,    92,     8,    -1,
      93,    92,     8,    -1,    93,     1,    -1,    40,     4,    93,
       5,    -1,    40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   184,   184,   187,   188,   189,   192,   193,   194,   195,
     198,   199,   202,   210,   211,   214,   219,   224,   225,   226,
     229,   229,   236,   237,   238,   239,   242,   243,   244,   247,
     248,   249,   250,   251,   252,   253,   254,   257,   262,   266,
     271,   276,   286,   287,   288,   294,   299,   303,   308,   316,
     316,   320,   323,   326,   337,   338,   345,   346,   351,   359,
     360,   364,   370,   379,   382,   383,   386,   389,   392,   393,
     394,   392,   400,   404,   405,   406,   407,   410,   410,   443,
     444,   445,   446,   450,   453,   454,   457,   458,   461,   464,
     468,   472,   476,   482,   483,   487,   490,   496,   496,   501,
     509,   509,   520,   527,   530,   531,   534,   535,   538,   541,
     542,   545,   549,   553,   559,   560,   563,   564,   570,   575,
     580,   581,   582,   585,   586,   590,   597,   598,   599,   602,
     605
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "KW_CONTEXT", "LC", "RC", "LP", "RP", 
  "SEMI", "EQ", "COMMA", "COLON", "AMPER", "BAR", "AT", "KW_PROC", 
  "KW_GLOBALS", "KW_IGNOREPAT", "KW_SWITCH", "KW_IF", "KW_IFTIME", 
  "KW_ELSE", "KW_RANDOM", "KW_ABSTRACT", "EXTENMARK", "KW_GOTO", 
  "KW_JUMP", "KW_RETURN", "KW_BREAK", "KW_CONTINUE", "KW_REGEXTEN", 
  "KW_HINT", "KW_FOR", "KW_WHILE", "KW_CASE", "KW_PATTERN", "KW_DEFAULT", 
  "KW_CATCH", "KW_SWITCHES", "KW_ESWITCHES", "KW_INCLUDES", "word", 
  "$accept", "file", "objects", "object", "context_name", "context", 
  "opt_abstract", "proc", "globals", "global_statements", "assignment", 
  "@1", "arglist", "elements", "element", "ignorepat", "extension", 
  "statements", "timerange", "timespec", "test_expr", "@2", 
  "if_like_head", "word_list", "word3_list", "goto_word", 
  "switch_statement", "statement", "@3", "@4", "@5", "@6", "opt_else", 
  "target", "opt_pri", "jumptarget", "proc_call", "@7", 
  "application_call_head", "@8", "application_call", "opt_word", 
  "eval_arglist", "case_statements", "case_statement", "proc_statements", 
  "proc_statement", "switches", "eswitches", "switchlist", 
  "included_entry", "includeslist", "includes", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    42,    43,    44,    44,    44,    45,    45,    45,    45,
      46,    46,    47,    48,    48,    49,    50,    51,    51,    51,
      53,    52,    54,    54,    54,    54,    55,    55,    55,    56,
      56,    56,    56,    56,    56,    56,    56,    57,    58,    58,
      58,    58,    59,    59,    59,    60,    60,    61,    61,    63,
      62,    64,    64,    64,    65,    65,    66,    66,    66,    67,
      67,    67,    68,    69,    69,    69,    69,    69,    70,    71,
      72,    69,    69,    69,    69,    69,    69,    73,    69,    69,
      69,    69,    69,    69,    74,    74,    75,    75,    75,    75,
      75,    75,    75,    76,    76,    77,    77,    79,    78,    78,
      81,    80,    82,    82,    83,    83,    84,    84,    84,    85,
      85,    86,    86,    86,    87,    87,    88,    88,    89,    90,
      91,    91,    91,    92,    92,    92,    93,    93,    93,    94,
      94
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     6,     1,     0,     8,     4,     0,     2,     2,
       0,     5,     0,     1,     3,     2,     0,     2,     2,     1,
       1,     1,     1,     1,     1,     2,     1,     4,     3,     4,
       7,     8,     0,     2,     2,     5,     1,     7,     7,     0,
       4,     2,     2,     4,     1,     2,     1,     2,     3,     1,
       2,     3,     5,     3,     1,     3,     3,     2,     0,     0,
       0,    12,     3,     1,     3,     2,     2,     0,     5,     2,
       2,     2,     3,     1,     2,     0,     1,     3,     3,     5,
       5,     5,     5,     0,     2,     2,     4,     0,     5,     3,
       0,     3,     3,     2,     1,     0,     1,     0,     3,     0,
       2,     4,     3,     4,     0,     2,     1,     5,     4,     4,
       0,     3,     2,     1,     3,     3,     2,     3,     2,     4,
       3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      14,     9,     0,     0,    13,     0,     0,     3,     6,     0,
       7,     8,     0,    17,     1,     5,     4,     0,    22,     0,
       0,    17,    11,    10,     0,    23,     0,    20,    19,    16,
       0,    26,    25,     0,     0,     0,    36,     0,     0,     0,
       0,     0,     0,     0,    34,     0,    26,    33,    29,    31,
      32,    30,   114,    24,     0,     0,     0,     0,     0,   120,
     120,     0,    35,     0,    28,    12,     0,    42,    83,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    64,     0,    73,   116,   107,     0,     0,
     114,    21,     0,     0,     0,    56,     0,     0,     0,     0,
     130,   123,     0,     0,    38,     0,    42,     0,     0,    49,
       0,    51,     0,    52,     0,    59,    86,     0,    93,     0,
      80,    79,    81,    68,     0,     0,   100,    76,    67,    85,
     103,    54,   106,     0,    75,    77,    15,   115,    37,     0,
      39,    57,     0,   120,   122,   118,   119,     0,     0,   126,
     128,   129,     0,    44,    63,     0,    97,    74,     0,   109,
      46,     0,     0,     0,     0,     0,    60,     0,     0,     0,
      65,     0,    95,    66,     0,    72,    42,   101,     0,    82,
      55,   102,   105,     0,     0,    58,     0,     0,   125,   124,
     127,    99,   107,     0,     0,     0,     0,     0,   109,     0,
       0,    53,     0,     0,     0,    88,    61,    87,    94,     0,
       0,     0,    84,   104,   108,     0,     0,     0,     0,    50,
       0,     0,    42,    62,   110,     0,     0,     0,     0,     0,
       0,     0,    96,    69,   117,    78,     0,    40,    98,    42,
      42,     0,     0,     0,     0,    92,    91,    90,    89,     0,
      41,     0,     0,     0,     0,    45,     0,     0,     0,    70,
      48,    47,     0,     0,     0,    71
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,     5,     6,     7,   101,     8,     9,    10,    11,    20,
      83,    35,    26,    45,    46,    47,    48,   105,   161,   162,
     110,   158,    84,   132,   163,   116,    85,   106,   174,   249,
     262,   183,   179,   117,   172,   119,   108,   192,    87,   177,
      88,   214,   133,   197,   198,    89,    90,    49,    50,    98,
     102,   103,    51
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -103
static const short yypact[] =
{
      99,  -103,    -7,    45,  -103,    56,   108,  -103,  -103,    65,
    -103,  -103,    74,    22,  -103,  -103,  -103,   -10,    71,   110,
      86,    22,  -103,  -103,   166,  -103,    12,  -103,  -103,  -103,
     149,   136,  -103,   185,    88,   130,  -103,   172,   -23,   196,
     199,   200,   201,    11,  -103,   150,   136,  -103,  -103,  -103,
    -103,  -103,    25,  -103,   198,   169,   202,   183,   170,   173,
     173,    19,  -103,    57,  -103,  -103,   159,    57,  -103,   174,
     203,   203,   207,   203,    23,   175,   209,   210,   211,   214,
     203,   181,   119,  -103,    57,  -103,  -103,     7,     8,   216,
      25,  -103,   215,   170,    57,   184,   217,   218,   167,   168,
    -103,    60,   219,     5,  -103,   177,    57,   222,   221,  -103,
     226,  -103,   190,  -103,    68,   191,   180,   225,    17,   227,
    -103,  -103,  -103,  -103,    57,   230,  -103,  -103,  -103,   220,
    -103,   195,  -103,   111,  -103,  -103,  -103,  -103,  -103,   231,
    -103,   204,   205,   173,  -103,  -103,  -103,   190,   190,  -103,
    -103,  -103,   229,  -103,  -103,    66,   232,  -103,   206,   127,
      -2,   139,   233,   237,   175,   175,  -103,   175,   208,   175,
    -103,   212,   228,  -103,   213,  -103,    57,  -103,    57,  -103,
    -103,  -103,   223,   224,   234,  -103,   235,   178,  -103,  -103,
    -103,  -103,   206,   236,   238,   239,   240,   245,   127,   170,
     170,  -103,   170,    94,    83,   125,  -103,   186,  -103,   -10,
     244,   187,  -103,  -103,  -103,   247,   242,    57,   188,  -103,
     246,   249,    57,  -103,  -103,   248,   243,   250,   175,   175,
     175,   175,  -103,  -103,  -103,  -103,    57,  -103,  -103,    57,
      57,    98,   170,   170,   170,   251,   251,   251,   251,   241,
    -103,   105,   112,   253,   254,  -103,   260,   170,   170,  -103,
    -103,  -103,   252,   262,    57,  -103
};

/* YYPGOTO[NTERM-NUM].  */
static const short yypgoto[] =
{
    -103,  -103,  -103,   264,   -15,  -103,  -103,  -103,  -103,   255,
      -6,  -103,  -103,   256,  -103,  -103,  -103,  -102,  -103,    33,
     -50,  -103,  -103,   113,   -57,   -72,  -103,   -52,  -103,  -103,
    -103,  -103,  -103,  -103,  -103,  -103,  -103,  -103,  -103,  -103,
    -103,  -103,    52,    75,  -103,   182,  -103,  -103,  -103,   -55,
     171,  -103,  -103
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -122
static const short yytable[] =
{
      86,    96,    24,   118,   155,    99,   150,    21,    56,   -56,
     151,   104,    62,    32,   130,    21,   134,   135,    57,    33,
      27,   111,    34,   113,   100,    44,    22,   171,   168,    67,
     124,    23,   129,    68,    12,    63,   139,    69,    86,   141,
      44,    22,   140,    70,    71,    72,    23,    73,   131,    13,
      74,    75,    76,    77,    78,    22,    14,    79,    80,   114,
      23,    67,    81,    19,   115,    68,    82,   153,    17,    69,
     147,   -43,   175,   148,   211,    70,    71,    72,   164,    73,
      18,   165,    74,    75,    76,    77,    78,    28,   187,    79,
      80,    29,   203,   204,   168,   205,   229,   207,    82,   153,
     -43,   -43,   -43,  -112,   228,   168,   153,     1,    -2,    15,
    -111,   -14,    25,   153,     2,     3,     1,  -113,   181,    27,
     241,   182,     4,     2,     3,   126,   212,   127,    27,    53,
     128,     4,  -112,  -112,  -112,   230,   168,   251,   252,  -111,
    -111,  -111,   225,   226,    36,   227,  -113,  -113,  -113,   199,
      28,    64,   200,    37,   -18,    65,   245,   246,   247,   248,
      64,   194,   195,   196,   -27,   237,    38,    39,   144,   144,
      31,    54,   145,   146,    40,    41,    42,    43,   153,   144,
     188,   189,   154,  -121,   250,   253,   254,   255,   153,    52,
     167,   168,   234,   169,   232,   238,    55,   168,   182,   231,
     260,   261,    58,    59,    60,    61,    91,    94,    93,   109,
      92,    95,   265,   112,    97,   107,   115,   120,   121,   122,
     123,   136,   125,   138,   142,   141,   143,   149,   156,   157,
     159,   160,   166,   170,   176,   173,   180,   190,   184,   191,
     201,   178,   209,   219,   218,   185,   186,   131,   202,   206,
     223,   222,   233,   208,   210,   235,   243,   239,   242,   217,
     240,   244,   168,   257,   213,   215,   236,   258,   259,   264,
      16,   193,   137,   224,   152,   216,    30,     0,     0,   220,
     221,     0,   256,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   263,     0,     0,     0,     0,     0,     0,
       0,     0,    66
};

static const short yycheck[] =
{
      52,    58,    17,    75,   106,    60,     1,    13,    31,    11,
       5,    63,     1,     1,     7,    21,     8,     9,    41,     7,
       9,    71,    10,    73,     5,    31,    36,    10,    11,     4,
      80,    41,    84,     8,    41,    24,    93,    12,    90,    41,
      46,    36,    94,    18,    19,    20,    41,    22,    41,     4,
      25,    26,    27,    28,    29,    36,     0,    32,    33,    36,
      41,     4,    37,    41,    41,     8,    41,     1,     3,    12,
      10,     5,   124,    13,   176,    18,    19,    20,    10,    22,
       6,    13,    25,    26,    27,    28,    29,     1,   143,    32,
      33,     5,   164,   165,    11,   167,    13,   169,    41,     1,
      34,    35,    36,     5,    10,    11,     1,     8,     0,     1,
       5,     3,    41,     1,    15,    16,     8,     5,     7,     9,
     222,    10,    23,    15,    16,     6,   178,     8,     9,    41,
      11,    23,    34,    35,    36,    10,    11,   239,   240,    34,
      35,    36,   199,   200,     8,   202,    34,    35,    36,    10,
       1,     1,    13,    17,     5,     5,   228,   229,   230,   231,
       1,    34,    35,    36,     5,   217,    30,    31,     1,     1,
       4,    41,     5,     5,    38,    39,    40,    41,     1,     1,
     147,   148,     5,     5,   236,   242,   243,   244,     1,     4,
      10,    11,     5,    13,   209,     7,    24,    11,    10,    13,
     257,   258,     6,     4,     4,     4,     8,    24,     6,     6,
      41,    41,   264,     6,    41,    41,    41,     8,     8,     8,
       6,     5,    41,     8,     7,    41,     8,     8,     6,     8,
       4,    41,    41,     8,     4,     8,    41,     8,     7,     7,
       7,    21,    14,     7,   192,    41,    41,    41,    11,    41,
       5,    11,     8,    41,    41,     8,    13,    11,    10,    24,
      11,    11,    11,    10,    41,    41,    24,    13,     8,     7,
       6,   158,    90,   198,   103,    41,    21,    -1,    -1,    41,
      41,    -1,    41,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    41,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    46
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     8,    15,    16,    23,    43,    44,    45,    47,    48,
      49,    50,    41,     4,     0,     1,    45,     3,     6,    41,
      51,    52,    36,    41,    46,    41,    54,     9,     1,     5,
      51,     4,     1,     7,    10,    53,     8,    17,    30,    31,
      38,    39,    40,    41,    52,    55,    56,    57,    58,    89,
      90,    94,     4,    41,    41,    24,    31,    41,     6,     4,
       4,     4,     1,    24,     1,     5,    55,     4,     8,    12,
      18,    19,    20,    22,    25,    26,    27,    28,    29,    32,
      33,    37,    41,    52,    64,    68,    69,    80,    82,    87,
      88,     8,    41,     6,    24,    41,    66,    41,    91,    91,
       5,    46,    92,    93,    69,    59,    69,    41,    78,     6,
      62,    62,     6,    62,    36,    41,    67,    75,    67,    77,
       8,     8,     8,     6,    62,    41,     6,     8,    11,    69,
       7,    41,    65,    84,     8,     9,     5,    87,     8,    66,
      69,    41,     7,     8,     1,     5,     5,    10,    13,     8,
       1,     5,    92,     1,     5,    59,     6,     8,    63,     4,
      41,    60,    61,    66,    10,    13,    41,    10,    11,    13,
       8,    10,    76,     8,    70,    69,     4,    81,    21,    74,
      41,     7,    10,    73,     7,    41,    41,    91,    61,    61,
       8,     7,    79,    65,    34,    35,    36,    85,    86,    10,
      13,     7,    11,    67,    67,    67,    41,    67,    41,    14,
      41,    59,    69,    41,    83,    41,    41,    24,    84,     7,
      41,    41,    11,     5,    85,    66,    66,    66,    10,    13,
      10,    13,    46,     8,     5,     8,    24,    69,     7,    11,
      11,    59,    10,    13,    11,    67,    67,    67,    67,    71,
      69,    59,    59,    66,    66,    66,    41,    10,    13,     8,
      66,    66,    72,    41,     7,    69
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrlab1

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
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror (&yylloc, parseio, "syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
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
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value, Location);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
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
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yytype, yyvaluep, yylocationp)
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  switch (yytype)
    {
      case 41: /* word */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1098 "ael/ael_tab.c"
        break;
      case 44: /* objects */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1106 "ael/ael_tab.c"
        break;
      case 45: /* object */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1114 "ael/ael_tab.c"
        break;
      case 46: /* context_name */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1119 "ael/ael_tab.c"
        break;
      case 47: /* context */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1127 "ael/ael_tab.c"
        break;
      case 49: /* proc */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1135 "ael/ael_tab.c"
        break;
      case 50: /* globals */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1143 "ael/ael_tab.c"
        break;
      case 51: /* global_statements */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1151 "ael/ael_tab.c"
        break;
      case 52: /* assignment */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1159 "ael/ael_tab.c"
        break;
      case 54: /* arglist */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1167 "ael/ael_tab.c"
        break;
      case 55: /* elements */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1175 "ael/ael_tab.c"
        break;
      case 56: /* element */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1183 "ael/ael_tab.c"
        break;
      case 57: /* ignorepat */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1191 "ael/ael_tab.c"
        break;
      case 58: /* extension */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1199 "ael/ael_tab.c"
        break;
      case 59: /* statements */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1207 "ael/ael_tab.c"
        break;
      case 60: /* timerange */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1212 "ael/ael_tab.c"
        break;
      case 61: /* timespec */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1220 "ael/ael_tab.c"
        break;
      case 62: /* test_expr */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1225 "ael/ael_tab.c"
        break;
      case 64: /* if_like_head */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1233 "ael/ael_tab.c"
        break;
      case 65: /* word_list */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1238 "ael/ael_tab.c"
        break;
      case 66: /* word3_list */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1243 "ael/ael_tab.c"
        break;
      case 67: /* goto_word */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1248 "ael/ael_tab.c"
        break;
      case 68: /* switch_statement */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1256 "ael/ael_tab.c"
        break;
      case 69: /* statement */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1264 "ael/ael_tab.c"
        break;
      case 74: /* opt_else */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1272 "ael/ael_tab.c"
        break;
      case 75: /* target */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1280 "ael/ael_tab.c"
        break;
      case 76: /* opt_pri */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1285 "ael/ael_tab.c"
        break;
      case 77: /* jumptarget */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1293 "ael/ael_tab.c"
        break;
      case 78: /* proc_call */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1301 "ael/ael_tab.c"
        break;
      case 80: /* application_call_head */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1309 "ael/ael_tab.c"
        break;
      case 82: /* application_call */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1317 "ael/ael_tab.c"
        break;
      case 83: /* opt_word */
#line 176 "ael/ael.y"
        { free(yyvaluep->str);};
#line 1322 "ael/ael_tab.c"
        break;
      case 84: /* eval_arglist */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1330 "ael/ael_tab.c"
        break;
      case 85: /* case_statements */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1338 "ael/ael_tab.c"
        break;
      case 86: /* case_statement */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1346 "ael/ael_tab.c"
        break;
      case 87: /* proc_statements */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1354 "ael/ael_tab.c"
        break;
      case 88: /* proc_statement */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1362 "ael/ael_tab.c"
        break;
      case 89: /* switches */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1370 "ael/ael_tab.c"
        break;
      case 90: /* eswitches */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1378 "ael/ael_tab.c"
        break;
      case 91: /* switchlist */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1386 "ael/ael_tab.c"
        break;
      case 92: /* included_entry */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1394 "ael/ael_tab.c"
        break;
      case 93: /* includeslist */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1402 "ael/ael_tab.c"
        break;
      case 94: /* includes */
#line 163 "ael/ael.y"
        {
		destroy_pval(yyvaluep->pval);
		prev_word=0;
	};
#line 1410 "ael/ael_tab.c"
        break;

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (struct parse_io *parseio);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (struct parse_io *parseio)
#else
int
yyparse (parseio)
    struct parse_io *parseio;
#endif
#endif
{
  /* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc;

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  YYLTYPE *yylerrsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

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
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
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
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
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

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
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
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
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

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
  *++yylsp = yylloc;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
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

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 184 "ael/ael.y"
    { yyval.pval = parseio->pval = yyvsp[0].pval; }
    break;

  case 3:
#line 187 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 4:
#line 188 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 5:
#line 189 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 6:
#line 192 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 7:
#line 193 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 8:
#line 194 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 9:
#line 195 "ael/ael.y"
    {yyval.pval=0;/* allow older docs to be read */}
    break;

  case 10:
#line 198 "ael/ael.y"
    { yyval.str = yyvsp[0].str; }
    break;

  case 11:
#line 199 "ael/ael.y"
    { yyval.str = strdup("default"); }
    break;

  case 12:
#line 202 "ael/ael.y"
    {
		yyval.pval = npval2(PV_CONTEXT, &yylsp[-5], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-3].str;
		yyval.pval->u2.statements = yyvsp[-1].pval;
		yyval.pval->u3.abstract = yyvsp[-5].intval; }
    break;

  case 13:
#line 210 "ael/ael.y"
    { yyval.intval = 1; }
    break;

  case 14:
#line 211 "ael/ael.y"
    { yyval.intval = 0; }
    break;

  case 15:
#line 214 "ael/ael.y"
    {
		yyval.pval = npval2(PV_PROC, &yylsp[-7], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-6].str; yyval.pval->u2.arglist = yyvsp[-4].pval; yyval.pval->u3.proc_statements = yyvsp[-1].pval; }
    break;

  case 16:
#line 219 "ael/ael.y"
    {
		yyval.pval = npval2(PV_GLOBALS, &yylsp[-3], &yylsp[0]);
		yyval.pval->u1.statements = yyvsp[-1].pval;}
    break;

  case 17:
#line 224 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 18:
#line 225 "ael/ael.y"
    {yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 19:
#line 226 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 20:
#line 229 "ael/ael.y"
    { reset_semicount(parseio->scanner); }
    break;

  case 21:
#line 229 "ael/ael.y"
    {
		yyval.pval = npval2(PV_VARDEC, &yylsp[-4], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-4].str;
		yyval.pval->u2.val = yyvsp[-1].str; }
    break;

  case 22:
#line 236 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 23:
#line 237 "ael/ael.y"
    { yyval.pval = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 24:
#line 238 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-2].pval, nword(yyvsp[0].str, &yylsp[0])); }
    break;

  case 25:
#line 239 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 26:
#line 242 "ael/ael.y"
    {yyval.pval=0;}
    break;

  case 27:
#line 243 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 28:
#line 244 "ael/ael.y"
    { yyval.pval=yyvsp[-1].pval;}
    break;

  case 29:
#line 247 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 30:
#line 248 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 31:
#line 249 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 32:
#line 250 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 33:
#line 251 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 34:
#line 252 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 35:
#line 253 "ael/ael.y"
    {free(yyvsp[-1].str); yyval.pval=0;}
    break;

  case 36:
#line 254 "ael/ael.y"
    {yyval.pval=0;/* allow older docs to be read */}
    break;

  case 37:
#line 257 "ael/ael.y"
    {
		yyval.pval = npval2(PV_IGNOREPAT, &yylsp[-3], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-1].str;}
    break;

  case 38:
#line 262 "ael/ael.y"
    {
		yyval.pval = npval2(PV_EXTENSION, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval; }
    break;

  case 39:
#line 266 "ael/ael.y"
    {
		yyval.pval = npval2(PV_EXTENSION, &yylsp[-3], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval;
		yyval.pval->u4.regexten=1;}
    break;

  case 40:
#line 271 "ael/ael.y"
    {
		yyval.pval = npval2(PV_EXTENSION, &yylsp[-6], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval;
		yyval.pval->u3.hints = yyvsp[-4].str;}
    break;

  case 41:
#line 276 "ael/ael.y"
    {
		yyval.pval = npval2(PV_EXTENSION, &yylsp[-7], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval;
		yyval.pval->u4.regexten=1;
		yyval.pval->u3.hints = yyvsp[-4].str;}
    break;

  case 42:
#line 286 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 43:
#line 287 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 44:
#line 288 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 45:
#line 294 "ael/ael.y"
    {
		asprintf(&yyval.str, "%s:%s:%s", yyvsp[-4].str, yyvsp[-2].str, yyvsp[0].str);
		free(yyvsp[-4].str);
		free(yyvsp[-2].str);
		free(yyvsp[0].str); }
    break;

  case 46:
#line 299 "ael/ael.y"
    { yyval.str = yyvsp[0].str; }
    break;

  case 47:
#line 303 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-6].str, &yylsp[-6]);
		yyval.pval->next = nword(yyvsp[-4].str, &yylsp[-4]);
		yyval.pval->next->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 48:
#line 308 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-6].str, &yylsp[-6]);
		yyval.pval->next = nword(yyvsp[-4].str, &yylsp[-4]);
		yyval.pval->next->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 49:
#line 316 "ael/ael.y"
    { reset_parencount(parseio->scanner); }
    break;

  case 50:
#line 316 "ael/ael.y"
    { yyval.str = yyvsp[-1].str; }
    break;

  case 51:
#line 320 "ael/ael.y"
    {
		yyval.pval= npval2(PV_IF, &yylsp[-1], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[0].str; }
    break;

  case 52:
#line 323 "ael/ael.y"
    {
		yyval.pval = npval2(PV_RANDOM, &yylsp[-1], &yylsp[0]);
		yyval.pval->u1.str=yyvsp[0].str;}
    break;

  case 53:
#line 326 "ael/ael.y"
    {
		yyval.pval = npval2(PV_IFTIME, &yylsp[-3], &yylsp[0]);
		yyval.pval->u1.list = yyvsp[-1].pval;
		prev_word = 0; }
    break;

  case 54:
#line 337 "ael/ael.y"
    { yyval.str = yyvsp[0].str;}
    break;

  case 55:
#line 338 "ael/ael.y"
    {
		asprintf(&(yyval.str), "%s%s", yyvsp[-1].str, yyvsp[0].str);
		free(yyvsp[-1].str);
		free(yyvsp[0].str);
		prev_word = yyval.str;}
    break;

  case 56:
#line 345 "ael/ael.y"
    { yyval.str = yyvsp[0].str;}
    break;

  case 57:
#line 346 "ael/ael.y"
    {
		asprintf(&(yyval.str), "%s%s", yyvsp[-1].str, yyvsp[0].str);
		free(yyvsp[-1].str);
		free(yyvsp[0].str);
		prev_word = yyval.str;}
    break;

  case 58:
#line 351 "ael/ael.y"
    {
		asprintf(&(yyval.str), "%s%s%s", yyvsp[-2].str, yyvsp[-1].str, yyvsp[0].str);
		free(yyvsp[-2].str);
		free(yyvsp[-1].str);
		free(yyvsp[0].str);
		prev_word=yyval.str;}
    break;

  case 59:
#line 359 "ael/ael.y"
    { yyval.str = yyvsp[0].str;}
    break;

  case 60:
#line 360 "ael/ael.y"
    {
		asprintf(&(yyval.str), "%s%s", yyvsp[-1].str, yyvsp[0].str);
		free(yyvsp[-1].str);
		free(yyvsp[0].str);}
    break;

  case 61:
#line 364 "ael/ael.y"
    {
		asprintf(&(yyval.str), "%s:%s", yyvsp[-2].str, yyvsp[0].str);
		free(yyvsp[-2].str);
		free(yyvsp[0].str);}
    break;

  case 62:
#line 370 "ael/ael.y"
    {
		yyval.pval = npval2(PV_SWITCH, &yylsp[-4], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-3].str;
		yyval.pval->u2.statements = yyvsp[-1].pval;}
    break;

  case 63:
#line 379 "ael/ael.y"
    {
		yyval.pval = npval2(PV_STATEMENTBLOCK, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.list = yyvsp[-1].pval; }
    break;

  case 64:
#line 382 "ael/ael.y"
    { yyval.pval = yyvsp[0].pval; }
    break;

  case 65:
#line 383 "ael/ael.y"
    {
		yyval.pval = npval2(PV_GOTO, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.list = yyvsp[-1].pval;}
    break;

  case 66:
#line 386 "ael/ael.y"
    {
		yyval.pval = npval2(PV_GOTO, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.list = yyvsp[-1].pval;}
    break;

  case 67:
#line 389 "ael/ael.y"
    {
		yyval.pval = npval2(PV_LABEL, &yylsp[-1], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-1].str; }
    break;

  case 68:
#line 392 "ael/ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 69:
#line 393 "ael/ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 70:
#line 394 "ael/ael.y"
    {reset_parencount(parseio->scanner);}
    break;

  case 71:
#line 394 "ael/ael.y"
    { /* XXX word_list maybe ? */
		yyval.pval = npval2(PV_FOR, &yylsp[-11], &yylsp[0]);
		yyval.pval->u1.for_init = yyvsp[-8].str;
		yyval.pval->u2.for_test=yyvsp[-5].str;
		yyval.pval->u3.for_inc = yyvsp[-2].str;
		yyval.pval->u4.for_statements = yyvsp[0].pval;}
    break;

  case 72:
#line 400 "ael/ael.y"
    {
		yyval.pval = npval2(PV_WHILE, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-1].str;
		yyval.pval->u2.statements = yyvsp[0].pval; }
    break;

  case 73:
#line 404 "ael/ael.y"
    { yyval.pval = yyvsp[0].pval; }
    break;

  case 74:
#line 405 "ael/ael.y"
    { yyval.pval = update_last(yyvsp[-1].pval, &yylsp[-1]); }
    break;

  case 75:
#line 406 "ael/ael.y"
    { yyval.pval = update_last(yyvsp[-1].pval, &yylsp[0]); }
    break;

  case 76:
#line 407 "ael/ael.y"
    {
		yyval.pval= npval2(PV_APPLICATION_CALL, &yylsp[-1], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-1].str;}
    break;

  case 77:
#line 410 "ael/ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 78:
#line 410 "ael/ael.y"
    {
		char *bufx;
		int tot=0;
		pval *pptr;
		yyval.pval = npval2(PV_VARDEC, &yylsp[-4], &yylsp[0]);
		yyval.pval->u2.val=yyvsp[-1].str;
		/* rebuild the original string-- this is not an app call, it's an unwrapped vardec, with a func call on the LHS */
		/* string to big to fit in the buffer? */
		tot+=strlen(yyvsp[-4].pval->u1.str);
		for(pptr=yyvsp[-4].pval->u2.arglist;pptr;pptr=pptr->next) {
			tot+=strlen(pptr->u1.str);
			tot++; /* for a sep like a comma */
		}
		tot+=4; /* for safety */
		bufx = calloc(1, tot);
		strcpy(bufx,yyvsp[-4].pval->u1.str);
		strcat(bufx,"(");
		/* XXX need to advance the pointer or the loop is very inefficient */
		for (pptr=yyvsp[-4].pval->u2.arglist;pptr;pptr=pptr->next) {
			if ( pptr != yyvsp[-4].pval->u2.arglist )
				strcat(bufx,",");
			strcat(bufx,pptr->u1.str);
		}
		strcat(bufx,")");
#ifdef AAL_ARGCHECK
		if ( !ael_is_funcname(yyvsp[-4].pval->u1.str) )
			opbx_log(OPBX_LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Function call? The name %s is not in my internal list of function names\n",
				my_file, yylsp[-4].first_line, yylsp[-4].first_column, yylsp[-4].last_column, yyvsp[-4].pval->u1.str);
#endif
		yyval.pval->u1.str = bufx;
		destroy_pval(yyvsp[-4].pval); /* the app call it is not, get rid of that chain */
		prev_word = 0;
	}
    break;

  case 79:
#line 443 "ael/ael.y"
    { yyval.pval = npval2(PV_BREAK, &yylsp[-1], &yylsp[0]); }
    break;

  case 80:
#line 444 "ael/ael.y"
    { yyval.pval = npval2(PV_RETURN, &yylsp[-1], &yylsp[0]); }
    break;

  case 81:
#line 445 "ael/ael.y"
    { yyval.pval = npval2(PV_CONTINUE, &yylsp[-1], &yylsp[0]); }
    break;

  case 82:
#line 446 "ael/ael.y"
    {
		yyval.pval = update_last(yyvsp[-2].pval, &yylsp[-1]);
		yyval.pval->u2.statements = yyvsp[-1].pval;
		yyval.pval->u3.else_statements = yyvsp[0].pval;}
    break;

  case 83:
#line 450 "ael/ael.y"
    { yyval.pval=0; }
    break;

  case 84:
#line 453 "ael/ael.y"
    { yyval.pval = yyvsp[0].pval; }
    break;

  case 85:
#line 454 "ael/ael.y"
    { yyval.pval = NULL ; }
    break;

  case 86:
#line 457 "ael/ael.y"
    { yyval.pval = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 87:
#line 458 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 88:
#line 461 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 89:
#line 464 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-4].str, &yylsp[-4]);
		yyval.pval->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 90:
#line 468 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-4].str, &yylsp[-4]);
		yyval.pval->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 91:
#line 472 "ael/ael.y"
    {
		yyval.pval = nword(strdup("default"), &yylsp[-4]);
		yyval.pval->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 92:
#line 476 "ael/ael.y"
    {
		yyval.pval = nword(strdup("default"), &yylsp[-4]);
		yyval.pval->next = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->next->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 93:
#line 482 "ael/ael.y"
    { yyval.str = strdup("1"); }
    break;

  case 94:
#line 483 "ael/ael.y"
    { yyval.str = yyvsp[0].str; }
    break;

  case 95:
#line 487 "ael/ael.y"
    {			/* ext[, pri] default 1 */
		yyval.pval = nword(yyvsp[-1].str, &yylsp[-1]);
		yyval.pval->next = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 96:
#line 490 "ael/ael.y"
    {	/* context, ext, pri */
		yyval.pval = nword(yyvsp[0].str, &yylsp[0]);
		yyval.pval->next = nword(yyvsp[-3].str, &yylsp[-3]);
		yyval.pval->next->next = nword(yyvsp[-2].str, &yylsp[-2]); }
    break;

  case 97:
#line 496 "ael/ael.y"
    {reset_argcount(parseio->scanner);}
    break;

  case 98:
#line 496 "ael/ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		yyval.pval = npval2(PV_PROC_CALL, &yylsp[-4], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-4].str;
		yyval.pval->u2.arglist = yyvsp[-1].pval;}
    break;

  case 99:
#line 501 "ael/ael.y"
    {
		yyval.pval= npval2(PV_PROC_CALL, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-2].str; }
    break;

  case 100:
#line 509 "ael/ael.y"
    {reset_argcount(parseio->scanner);}
    break;

  case 101:
#line 509 "ael/ael.y"
    {
		if (strcasecmp(yyvsp[-2].str,"goto") == 0) {
			yyval.pval = npval2(PV_GOTO, &yylsp[-2], &yylsp[-1]);
			free(yyvsp[-2].str); /* won't be using this */
			opbx_log(OPBX_LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, yylsp[-2].first_line, yylsp[-2].first_column, yylsp[-2].last_column );
		} else {
			yyval.pval= npval2(PV_APPLICATION_CALL, &yylsp[-2], &yylsp[-1]);
			yyval.pval->u1.str = yyvsp[-2].str;
		} }
    break;

  case 102:
#line 520 "ael/ael.y"
    {
		yyval.pval = update_last(yyvsp[-2].pval, &yylsp[0]);
 		if( yyval.pval->type == PV_GOTO )
			yyval.pval->u1.list = yyvsp[-1].pval;
	 	else
			yyval.pval->u2.arglist = yyvsp[-1].pval;
	}
    break;

  case 103:
#line 527 "ael/ael.y"
    { yyval.pval = update_last(yyvsp[-1].pval, &yylsp[0]); }
    break;

  case 104:
#line 530 "ael/ael.y"
    { yyval.str = yyvsp[0].str; }
    break;

  case 105:
#line 531 "ael/ael.y"
    { yyval.str = strdup(""); }
    break;

  case 106:
#line 534 "ael/ael.y"
    { yyval.pval = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 107:
#line 535 "ael/ael.y"
    {
		yyval.pval= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		yyval.pval->u1.str = strdup(""); }
    break;

  case 108:
#line 538 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-2].pval, nword(yyvsp[0].str, &yylsp[0])); }
    break;

  case 109:
#line 541 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 110:
#line 542 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 111:
#line 545 "ael/ael.y"
    {
		yyval.pval = npval2(PV_CASE, &yylsp[-3], &yylsp[-1]); /* XXX 3 or 4 ? */
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval;}
    break;

  case 112:
#line 549 "ael/ael.y"
    {
		yyval.pval = npval2(PV_DEFAULT, &yylsp[-2], &yylsp[0]);
		yyval.pval->u1.str = NULL;
		yyval.pval->u2.statements = yyvsp[0].pval;}
    break;

  case 113:
#line 553 "ael/ael.y"
    {
		yyval.pval = npval2(PV_PATTERN, &yylsp[-3], &yylsp[0]); /* XXX@3 or @4 ? */
		yyval.pval->u1.str = yyvsp[-2].str;
		yyval.pval->u2.statements = yyvsp[0].pval;}
    break;

  case 114:
#line 559 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 115:
#line 560 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-1].pval, yyvsp[0].pval); }
    break;

  case 116:
#line 563 "ael/ael.y"
    {yyval.pval=yyvsp[0].pval;}
    break;

  case 117:
#line 564 "ael/ael.y"
    {
		yyval.pval = npval2(PV_CATCH, &yylsp[-4], &yylsp[0]);
		yyval.pval->u1.str = yyvsp[-3].str;
		yyval.pval->u2.statements = yyvsp[-1].pval;}
    break;

  case 118:
#line 570 "ael/ael.y"
    {
		yyval.pval = npval2(PV_SWITCHES, &yylsp[-3], &yylsp[-2]);
		yyval.pval->u1.list = yyvsp[-1].pval; }
    break;

  case 119:
#line 575 "ael/ael.y"
    {
		yyval.pval = npval2(PV_ESWITCHES, &yylsp[-3], &yylsp[-2]);
		yyval.pval->u1.list = yyvsp[-1].pval; }
    break;

  case 120:
#line 580 "ael/ael.y"
    { yyval.pval = NULL; }
    break;

  case 121:
#line 581 "ael/ael.y"
    { yyval.pval = linku1(nword(yyvsp[-2].str, &yylsp[-2]), yyvsp[0].pval); }
    break;

  case 122:
#line 582 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 123:
#line 585 "ael/ael.y"
    { yyval.pval = nword(yyvsp[0].str, &yylsp[0]); }
    break;

  case 124:
#line 586 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->u2.arglist = yyvsp[0].pval;
		prev_word=0; /* XXX sure ? */ }
    break;

  case 125:
#line 590 "ael/ael.y"
    {
		yyval.pval = nword(yyvsp[-2].str, &yylsp[-2]);
		yyval.pval->u2.arglist = yyvsp[0].pval;
		prev_word=0; /* XXX sure ? */ }
    break;

  case 126:
#line 597 "ael/ael.y"
    { yyval.pval = yyvsp[-1].pval; }
    break;

  case 127:
#line 598 "ael/ael.y"
    { yyval.pval = linku1(yyvsp[-2].pval, yyvsp[-1].pval); }
    break;

  case 128:
#line 599 "ael/ael.y"
    {yyval.pval=yyvsp[-1].pval;}
    break;

  case 129:
#line 602 "ael/ael.y"
    {
		yyval.pval = npval2(PV_INCLUDES, &yylsp[-3], &yylsp[0]);
		yyval.pval->u1.list = yyvsp[-1].pval;}
    break;

  case 130:
#line 605 "ael/ael.y"
    {
		yyval.pval = npval2(PV_INCLUDES, &yylsp[-2], &yylsp[0]);}
    break;


    }

/* Line 991 of yacc.c.  */
#line 2559 "ael/ael_tab.c"

  yyvsp -= yylen;
  yyssp -= yylen;
  yylsp -= yylen;

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
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (&yylloc, parseio, yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror (&yylloc, parseio, "syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror (&yylloc, parseio, "syntax error");
    }

  yylerrsp = yylsp;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp, yylsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval, &yylloc);
      yychar = YYEMPTY;
      *++yylerrsp = yylloc;
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab2;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:

  /* Suppress GCC warning that yyerrlab1 is unused when no action
     invokes YYERROR.  */
#if defined (__GNUC_MINOR__) && 2093 <= (__GNUC__ * 1000 + __GNUC_MINOR__) \
    && !defined __cplusplus
  __attribute__ ((__unused__))
#endif

  yylerrsp = yylsp;
  *++yylerrsp = yyloc;
  goto yyerrlab2;


/*---------------------------------------------------------------.
| yyerrlab2 -- pop states until the error token can be shifted.  |
`---------------------------------------------------------------*/
yyerrlab2:
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

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp, yylsp);
      yyvsp--;
      yystate = *--yyssp;
      yylsp--;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;
  YYLLOC_DEFAULT (yyloc, yylsp, (yylerrsp - yylsp));
  *++yylsp = yyloc;

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
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror (&yylloc, parseio, "parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 610 "ael/ael.y"


static char *token_equivs1[] =
{
	"AMPER",
	"AT",
	"BAR",
	"COLON",
	"COMMA",
	"EQ",
	"EXTENMARK",
	"KW_BREAK",
	"KW_CASE",
	"KW_CATCH",
	"KW_CONTEXT",
	"KW_CONTINUE",
	"KW_DEFAULT",
	"KW_ELSE",
	"KW_ESWITCHES",
	"KW_FOR",
	"KW_GLOBALS",
	"KW_GOTO",
	"KW_HINT",
	"KW_IFTIME",
	"KW_IF",
	"KW_IGNOREPAT",
	"KW_INCLUDES"
	"KW_JUMP",
	"KW_PROC",
	"KW_PATTERN",
	"KW_REGEXTEN",
	"KW_RETURN",
	"KW_SWITCHES",
	"KW_SWITCH",
	"KW_WHILE",
	"LC",
	"LP",
	"RC",
	"RP",
	"SEMI",
};

static char *token_equivs2[] =
{
	"&",
	"@",
	"|",
	":",
	",",
	"=",
	"=>",
	"break",
	"case",
	"catch",
	"context",
	"continue",
	"default",
	"else",
	"eswitches",
	"for",
	"globals",
	"goto",
	"hint",
	"ifTime",
	"if",
	"ignorepat",
	"includes"
	"jump",
	"proc",
	"pattern",
	"regexten",
	"return",
	"switches",
	"switch",
	"while",
	"{",
	"(",
	"}",
	")",
	";",
};


static char *ael_token_subst(char *mess)
{
	/* calc a length, malloc, fill, and return; yyerror had better free it! */
	int len=0,i;
	char *p;
	char *res, *s,*t;
	int token_equivs_entries = sizeof(token_equivs1)/sizeof(char*);

	for (p=mess; *p; p++) {
		for (i=0; i<token_equivs_entries; i++) {
			if ( strncmp(p,token_equivs1[i],strlen(token_equivs1[i])) == 0 )
			{
				len+=strlen(token_equivs2[i])+2;
				p += strlen(token_equivs1[i])-1;
				break;
			}
		}
		len++;
	}
	res = calloc(1, len+1);
	res[0] = 0;
	s = res;
	for (p=mess; *p;) {
		int found = 0;
		for (i=0; i<token_equivs_entries; i++) {
			if ( strncmp(p,token_equivs1[i],strlen(token_equivs1[i])) == 0 ) {
				*s++ = '\'';
				for (t=token_equivs2[i]; *t;) {
					*s++ = *t++;
				}
				*s++ = '\'';
				p += strlen(token_equivs1[i]);
				found = 1;
				break;
			}
		}
		if( !found )
			*s++ = *p++;
	}
	*s++ = 0;
	return res;
}

void yyerror(YYLTYPE *locp, struct parse_io *parseio,  char const *s)
{
	char *s2 = ael_token_subst((char *)s);
	if (locp->first_line == locp->last_line) {
		opbx_log(OPBX_LOG_ERROR, "==== File: %s, Line %d, Cols: %d-%d: Error: %s\n", my_file, locp->first_line, locp->first_column, locp->last_column, s2);
	} else {
		opbx_log(OPBX_LOG_ERROR, "==== File: %s, Line %d Col %d  to Line %d Col %d: Error: %s\n", my_file, locp->first_line, locp->first_column, locp->last_line, locp->last_column, s2);
	}
	free(s2);
	parseio->syntax_error_count++;
}

static struct pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column)
{
	pval *z = calloc(1, sizeof(struct pval));
	z->type = type;
	z->startline = first_line;
	z->endline = last_line;
	z->startcol = first_column;
	z->endcol = last_column;
	z->filename = strdup(my_file);
	return z;
}

static struct pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last)
{
	return npval(type, first->first_line, last->last_line,
			first->first_column, last->last_column);
}

static struct pval *update_last(pval *obj, YYLTYPE *last)
{
	obj->endline = last->last_line;
	obj->endcol = last->last_column;
	return obj;
}

/* frontend for npval to create a PV_WORD string from the given token */
static pval *nword(char *string, YYLTYPE *pos)
{
	pval *p = npval2(PV_WORD, pos, pos);
	if (p)
		p->u1.str = string;
	return p;
}

/* append second element to the list in the first one */
static pval * linku1(pval *head, pval *tail)
{
	if (!head)
		return tail;
	if (tail) {
		if (!head->next) {
			head->next = tail;
		} else {
			head->u1_last->next = tail;
		}
		head->u1_last = tail;
	}
	return head;
}

