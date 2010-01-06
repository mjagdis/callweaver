/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Andy Powell 
 *
 * Updated by Mark Spencer <markster@digium.com>
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
 * \brief Maths relatad dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/config.h"


static void *math_function;
static const char math_func_name[] = "MATH";
static const char math_func_synopsis[] = "Performs Mathematical Functions";
static const char math_func_syntax[] = "MATH(number1 op number2[, type_of_result])";
static const char math_func_desc[] =
	"Perform calculation on number 1 to number 2. Valid ops are: \n"
        "    +,-,/,*,%,<,>,>=,<=,==\n"
	"and behave as their C equivalents.\n"
	"<type_of_result> - wanted type of result:\n"
	"	f, float - float(default)\n"
	"	i, int - integer,\n"
	"	h, hex - hex,\n"
	"	c, char - char\n"
	"Example: Set(i=${MATH(123 % 16, int)}) - sets var i=11";


enum TypeOfFunctions
{
    ADDFUNCTION,
    DIVIDEFUNCTION,
    MULTIPLYFUNCTION,
    SUBTRACTFUNCTION,
    MODULUSFUNCTION,

    GTFUNCTION,
    LTFUNCTION,
    GTEFUNCTION,
    LTEFUNCTION,
    EQFUNCTION
};

enum TypeOfResult
{
    FLOAT_RESULT,
    INT_RESULT,
    HEX_RESULT,
    CHAR_RESULT
};


static int builtin_function_math(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	double fnum1;
	double fnum2;
	double ftmp = 0;
	char *op;
	char *mvalue1, *mvalue2 = NULL;
	int iaction = -1;
	int type_of_result = FLOAT_RESULT;

	CW_UNUSED(chan);

	if (argc != 2 || !argv[0][0] || !argv[1][0])
		return cw_function_syntax(math_func_syntax);

	if (buf) {
		if (!strcasecmp(argv[1],"float") || !strcasecmp(argv[1],"f"))
			type_of_result=FLOAT_RESULT;
		else if (!strcasecmp(argv[1],"int") || !strcasecmp(argv[1],"i"))
			type_of_result=INT_RESULT;
		else if (!strcasecmp(argv[1],"hex") || !strcasecmp(argv[1],"h"))
			type_of_result=HEX_RESULT;
		else if (!strcasecmp(argv[1],"char") || !strcasecmp(argv[1],"c"))
			type_of_result=CHAR_RESULT;
		else {
			cw_log(CW_LOG_ERROR, "Unknown type of result requested '%s'\n", argv[1]);
			return cw_function_syntax(math_func_syntax);
		}

		mvalue1 = argv[0];

		if ((op = strchr(mvalue1, '+'))) {
			iaction = ADDFUNCTION;
			*op = '\0';
		} else if ((op = strchr(mvalue1, '-'))) {
			iaction = SUBTRACTFUNCTION;
			*op = '\0';
		} else if ((op = strchr(mvalue1, '*'))) {
			iaction = MULTIPLYFUNCTION;
			*op = '\0';
		} else if ((op = strchr(mvalue1, '/'))) {
			iaction = DIVIDEFUNCTION;
			*op = '\0';
		} else if ((op = strchr(mvalue1, '%'))) {
			iaction = MODULUSFUNCTION;
			*op = '\0';
		} else if ((op = strchr(mvalue1, '>'))) {
			iaction = GTFUNCTION;
			*op = '\0';
			if (*(op+1) == '=') {
				*++op = '\0';
				iaction = GTEFUNCTION;
			}
		} else if ((op = strchr(mvalue1, '<'))) {
			iaction = LTFUNCTION;
			*op = '\0';
			if (*(op+1) == '=') {
				*++op = '\0';
				iaction = LTEFUNCTION;
			}
		} else if ((op = strchr(mvalue1, '='))) {
			iaction = GTFUNCTION;
			*op = '\0';
			if (*(op+1) == '=') {
				*++op = '\0';
				iaction = EQFUNCTION;
			} else
				op = NULL;
		} 
	
		if (op) 
			mvalue2 = op + 1;

		if (!mvalue1 || !mvalue2) {
			cw_log(CW_LOG_WARNING, "Supply all the parameters - just this once, please\n");
			return -1;
		}

		if (sscanf(mvalue1, "%lf", &fnum1) != 1) {
			cw_log(CW_LOG_WARNING, "'%s' is not a valid number\n", mvalue1);
			return -1;
		}

		if (sscanf(mvalue2, "%lf", &fnum2) != 1) {
			cw_log(CW_LOG_WARNING, "'%s' is not a valid number\n", mvalue2);
			return -1;
		}

		switch (iaction) {
		case ADDFUNCTION :
			ftmp = fnum1 + fnum2;
			break;
		case DIVIDEFUNCTION :
			if (fnum2 <= 0)
				ftmp = 0.0L; /* can't do a divide by 0 */
			else
				ftmp = (fnum1 / fnum2);
			break;
		case MULTIPLYFUNCTION :
			ftmp = (fnum1 * fnum2);
			break;
		case SUBTRACTFUNCTION :
			ftmp = (fnum1 - fnum2);
			break;
		case MODULUSFUNCTION :
		{
			int inum1 = fnum1;
			int inum2 = fnum2;
			
			ftmp = (inum1 % inum2);
		
			break;
		}
		case GTFUNCTION :
			cw_copy_string (buf, (fnum1 > fnum2)?"TRUE":"FALSE", len);
			break;
		case LTFUNCTION :
			cw_copy_string (buf, (fnum1 < fnum2)?"TRUE":"FALSE", len);
			break;
		case GTEFUNCTION :
			cw_copy_string (buf, (fnum1 >= fnum2)?"TRUE":"FALSE", len);
			break;
		case LTEFUNCTION :
			cw_copy_string (buf, (fnum1 <= fnum2)?"TRUE":"FALSE", len);
			break;					
		case EQFUNCTION :
			cw_copy_string (buf, (fnum1 == fnum2)?"TRUE":"FALSE", len);
			break;
		default :
			cw_log(CW_LOG_WARNING, "Something happened that neither of us should be proud of %d\n", iaction);
			return -1;
		}

		if (iaction < GTFUNCTION || iaction > EQFUNCTION) {
			if (type_of_result == FLOAT_RESULT)
				snprintf(buf, len, "%lf", ftmp);
			else if (type_of_result == INT_RESULT)
				snprintf(buf, len, "%i", (int) ftmp);
			else if (type_of_result == HEX_RESULT)
				snprintf(buf, len, "%x", (unsigned int) ftmp);
			else if (type_of_result == CHAR_RESULT)
				snprintf(buf, len, "%c", (unsigned char) ftmp);
		}
	}

	return 0;
}


static const char tdesc[] = "math functions";

static int unload_module(void)
{
        return cw_unregister_function(math_function);
}

static int load_module(void)
{
        math_function = cw_register_function(math_func_name, builtin_function_math, math_func_synopsis, math_func_syntax, math_func_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
