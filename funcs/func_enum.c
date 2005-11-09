/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005
 *
 * Oleksiy Krivoshey <oleksiyk@gmail.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Enum Functions
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL: svn+ssh://svn@svn.openpbx.org/openpbx/trunk/funcs/func_db.c $", "$Revision$")

#include "openpbx/module.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/utils.h"

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"

#include "openpbx/pbx.h"
#include "openpbx/options.h"

#include "openpbx/enum.h"

static char* synopsis = "Syntax: ENUMLOOKUP(number[,Method-type[,options|record#[,zone-suffix]]])\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *function_enum(struct opbx_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
       int res=0;
       char tech[80];
       char dest[80] = "";
       char *zone;
       char *options;
       struct localuser *u;
       char *params[4];
       char *p = data;
       char *s;
       int i = 0;


       if (!data || opbx_strlen_zero(data)) {
               opbx_log(LOG_WARNING, synopsis);
               return "";
       }

       do {
               if(i>3){
                       opbx_log(LOG_WARNING, synopsis);
                       return "";
               }
               params[i++] = p;
               p = strchr(p, '|');
               if(p){
                       *p = '\0';
                       p++;
               }
       } while(p);

       if(i < 1){
               opbx_log(LOG_WARNING, synopsis);
               return "";
       }

       if( (i > 1 && strlen(params[1]) == 0) || i < 2){
               opbx_copy_string(tech, "sip", sizeof(tech));
       } else {
               opbx_copy_string(tech, params[1], sizeof(tech));
       }

       if( (i > 3 && strlen(params[3]) == 0) || i<4){
               zone = "e164.arpa";
       } else {
               zone = params[3];
       }

       if( (i > 2 && strlen(params[2]) == 0) || i<3){
               options = "1";
       } else {
               options = params[2];
       }

       /* strip any '-' signs from number */
       p = params[0];
       /*
       while(*p == '+'){
               p++;
       }
       */
       s = p;
       i = 0;
       while(*p && *s){
               if(*s == '-'){
                       s++;
               } else {
                       p[i++] = *s++;
               }
       }
       p[i] = 0;

       LOCAL_USER_ACF_ADD(u);

       res = opbx_get_enum(chan, p, dest, sizeof(dest), tech, sizeof(tech), zone, options);

       LOCAL_USER_REMOVE(u);

       p = strchr(dest, ':');
       if(p && strncasecmp(tech, "ALL", sizeof(tech))) {
               opbx_copy_string(buf, p+1, sizeof(dest));
       } else {
               opbx_copy_string(buf, dest, sizeof(dest));
       }

       return buf;
}

static struct opbx_custom_function enum_function = {
       .name = "ENUMLOOKUP",
       .synopsis = "ENUMLOOKUP allows for general or specific querying of NAPTR records"
       " or counts of NAPTR types for ENUM or ENUM-like DNS pointers",
       .syntax = "ENUMLOOKUP(number[,Method-type[,options|record#[,zone-suffix]]])",
       .desc = "Option 'c' returns an integer count of the number of NAPTRs of a certain RR type.\n"
       "Combination of 'c' and Method-type of 'ALL' will return a count of all NAPTRs for the record.\n"
       "Defaults are: Method-type=sip, no options, record=1, zone-suffix=e164.arpa\n\n"
       "For more information, see README.enum",
       .read = function_enum,
};

static char *tdesc = "ENUMLOOKUP allows for general or specific querying of NAPTR records or counts of NAPTR types for ENUM or ENUM-like DNS pointers";

int unload_module(void)
{
       return opbx_custom_function_unregister(&enum_function);
}

int load_module(void)
{
       return opbx_custom_function_register(&enum_function);
}

char *description(void)
{
       return tdesc;
}

int usecount(void)
{
       return 0;
}

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
