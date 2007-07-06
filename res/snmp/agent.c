/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * Ported to CallWeaver by Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/indications.h"
#include "callweaver/pbx.h"

/* Colission between Net-SNMP and CallWeaver */
#define unload_module opbx_unload_module
#include "callweaver/module.h"
#undef unload_module

#include "agent.h"

/* Helper functions in Net-SNMP, header file not installed by default */
int header_generic(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **);
int header_simple_table(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **, int);
int register_sysORTable(oid *, size_t, const char *);
int unregister_sysORTable(oid *, size_t);

/* Not defined in header files */
extern char opbx_config_OPBX_SOCKET[];

/* Forward declaration */
static void init_callweaver_mib(void);

/*
 * Anchor for all the CallWeaver MIB values
 */
static oid callweaver_oid[] = { 1, 3, 6, 1, 4, 1, 22736, 1 };

/*
 * MIB values -- these correspond to values in the CallWeaver MIB,
 * and MUST be kept in sync with the MIB for things to work as
 * expected.
 */
#define OPBXVERSION 1
#define OPBXVERSTRING 1
#define OPBXVERTAG 2

#define OPBXCONFIGURATION 2
#define OPBXCONFUPTIME 1
#define OPBXCONFRELOADTIME 2
#define OPBXCONFPID 3
#define OPBXCONFSOCKET 4

#define OPBXMODULES 3
#define OPBXMODCOUNT 1

#define OPBXINDICATIONS 4
#define OPBXINDCOUNT 1
#define OPBXINDCURRENT 2

#define OPBXINDTABLE 3
#define OPBXINDINDEX 1
#define OPBXINDCOUNTRY 2
#define OPBXINDALIAS 3
#define OPBXINDDESCRIPTION 4

#define OPBXCHANNELS 5
#define OPBXCHANCOUNT 1

#define OPBXCHANTABLE 2
#define OPBXCHANINDEX 1
#define OPBXCHANNAME 2
#define OPBXCHANLANGUAGE 3
#define OPBXCHANTYPE 4
#define OPBXCHANMUSICCLASS 5
#define OPBXCHANBRIDGE 6
#define OPBXCHANMASQ 7
#define OPBXCHANMASQR 8
#define OPBXCHANWHENHANGUP 9
#define OPBXCHANAPP 10
#define OPBXCHANDATA 11
#define OPBXCHANCONTEXT 12
#define OPBXCHANPROCCONTEXT 13
#define OPBXCHANPROCEXTEN 14
#define OPBXCHANPROCPRI 15
#define OPBXCHANEXTEN 16
#define OPBXCHANPRI 17
#define OPBXCHANACCOUNTCODE 18
#define OPBXCHANFORWARDTO 19
#define OPBXCHANUNIQUEID 20
#define OPBXCHANCALLGROUP 21
#define OPBXCHANPICKUPGROUP 22
#define OPBXCHANSTATE 23
#define OPBXCHANMUTED 24
#define OPBXCHANRINGS 25
#define OPBXCHANCIDDNID 26
#define OPBXCHANCIDNUM 27
#define OPBXCHANCIDNAME 28
#define OPBXCHANCIDANI 29
#define OPBXCHANCIDRDNIS 30
#define OPBXCHANCIDPRES 31
#define OPBXCHANCIDANI2 32
#define OPBXCHANCIDTON 33
#define OPBXCHANCIDTNS 34
#define OPBXCHANAMAFLAGS 35
#define OPBXCHANADSI 36
#define OPBXCHANTONEZONE 37
#define OPBXCHANHANGUPCAUSE 38
#define OPBXCHANVARIABLES 39
#define OPBXCHANFLAGS 40
#define OPBXCHANTRANSFERCAP 41

#define OPBXCHANTYPECOUNT 3

#define OPBXCHANTYPETABLE 4
#define OPBXCHANTYPEINDEX 1
#define OPBXCHANTYPENAME 2
#define OPBXCHANTYPEDESC 3
#define OPBXCHANTYPEDEVSTATE 4
#define OPBXCHANTYPEINDICATIONS 5
#define OPBXCHANTYPETRANSFER 6
#define OPBXCHANTYPECHANNELS 7

void *agent_thread(void *arg)
{
    opbx_verbose(VERBOSE_PREFIX_2 "Starting %sAgent\n", res_snmp_agentx_subagent ? "Sub" : "");

    snmp_enable_stderrlog();

    if (res_snmp_agentx_subagent)
        netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
                               NETSNMP_DS_AGENT_ROLE,
                               1);

    init_agent("CallWeaver");

    init_callweaver_mib();

    init_snmp("CallWeaver");

    if (!res_snmp_agentx_subagent)
        init_master_agent();

    while (res_snmp_dont_stop)
        agent_check_and_process(1);

    snmp_shutdown("CallWeaver");

    opbx_verbose(VERBOSE_PREFIX_2 "Terminating %sAgent\n",
                 res_snmp_agentx_subagent ? "Sub" : "");

    return NULL;
}

static u_char *
opbx_var_channels(struct variable *vp, oid *name, size_t *length,
                  int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case OPBXCHANCOUNT:
        long_ret = opbx_active_channels();
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_channels_table(struct variable *vp, oid *name, size_t *length,
                                       int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    static u_char bits_ret[2];
    static char string_ret[256];
    struct opbx_channel *chan, *bridge;
    struct timeval tval;
    u_char *ret;
    int i, bit;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, opbx_active_channels()))
        return NULL;

    i = name[*length - 1] - 1;
    for (chan = opbx_channel_walk_locked(NULL);
         chan  &&  i;
         chan = opbx_channel_walk_locked(chan), i--)
    {
        opbx_channel_unlock(chan);
    }
    if (chan == NULL)
        return NULL;
    *var_len = sizeof(long_ret);

    switch (vp->magic)
    {
    case OPBXCHANINDEX:
        long_ret = name[*length - 1];
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANNAME:
        if (!opbx_strlen_zero(chan->name))
        {
            strncpy(string_ret, chan->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANLANGUAGE:
        if (!opbx_strlen_zero(chan->language))
        {
            strncpy(string_ret, chan->language, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANTYPE:
        strncpy(string_ret, chan->tech->type, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *)string_ret;
        break;
    case OPBXCHANMUSICCLASS:
        if (!opbx_strlen_zero(chan->musicclass))
        {
            strncpy(string_ret, chan->musicclass, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANBRIDGE:
        if ((bridge = opbx_bridged_channel(chan)) != NULL)
        {
            strncpy(string_ret, bridge->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANMASQ:
        if (chan->masq  &&  !opbx_strlen_zero(chan->masq->name))
        {
            strncpy(string_ret, chan->masq->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANMASQR:
        if (chan->masqr  &&  !opbx_strlen_zero(chan->masqr->name))
        {
            strncpy(string_ret, chan->masqr->name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANWHENHANGUP:
        if (chan->whentohangup)
        {
            gettimeofday(&tval, NULL);
            long_ret = difftime(chan->whentohangup, tval.tv_sec) * 100 - tval.tv_usec / 10000;
            ret = (u_char *) &long_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANAPP:
        if (chan->appl)
        {
            strncpy(string_ret, chan->appl, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
            ret = NULL;
        break;
    case OPBXCHANDATA:
        opbx_log(LOG_WARNING, "OPBXCHANDATA doesn't exist anymore\n");
        ret = NULL;
        break;
    case OPBXCHANCONTEXT:
        strncpy(string_ret, chan->context, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case OPBXCHANPROCCONTEXT:
        strncpy(string_ret, chan->proc_context, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case OPBXCHANPROCEXTEN:
        strncpy(string_ret, chan->proc_exten, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case OPBXCHANPROCPRI:
        long_ret = chan->proc_priority;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANEXTEN:
        strncpy(string_ret, chan->exten, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case OPBXCHANPRI:
        long_ret = chan->priority;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANACCOUNTCODE:
        if (!opbx_strlen_zero(chan->accountcode))
        {
            strncpy(string_ret, chan->accountcode, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANFORWARDTO:
        if (!opbx_strlen_zero(chan->call_forward))
        {
            strncpy(string_ret, chan->call_forward, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANUNIQUEID:
        strncpy(string_ret, chan->uniqueid, sizeof(string_ret));
        string_ret[sizeof(string_ret) - 1] = '\0';
        *var_len = strlen(string_ret);
        ret = (u_char *) string_ret;
        break;
    case OPBXCHANCALLGROUP:
        long_ret = chan->callgroup;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANPICKUPGROUP:
        long_ret = chan->pickupgroup;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANSTATE:
        long_ret = chan->_state & 0xffff;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANMUTED:
        long_ret = chan->_state & OPBX_STATE_MUTE  ?  1  :  2;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANRINGS:
        long_ret = chan->rings;
        ret = (u_char *) &long_ret;
        break;
    case OPBXCHANCIDDNID:
        if (chan->cid.cid_dnid)
        {
            strncpy(string_ret, chan->cid.cid_dnid, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *) string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANCIDNUM:
        if (chan->cid.cid_num)
        {
            strncpy(string_ret, chan->cid.cid_num, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANCIDNAME:
        if (chan->cid.cid_name)
        {
            strncpy(string_ret, chan->cid.cid_name, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANCIDANI:
        if (chan->cid.cid_ani)
        {
            strncpy(string_ret, chan->cid.cid_ani, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANCIDRDNIS:
        if (chan->cid.cid_rdnis)
        {
            strncpy(string_ret, chan->cid.cid_rdnis, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANCIDPRES:
        long_ret = chan->cid.cid_pres;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANCIDANI2:
        long_ret = chan->cid.cid_ani2;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANCIDTON:
        long_ret = chan->cid.cid_ton;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANCIDTNS:
        long_ret = chan->cid.cid_tns;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANAMAFLAGS:
        long_ret = chan->amaflags;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANADSI:
        long_ret = chan->adsicpe;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANTONEZONE:
        if (chan->zone)
        {
            strncpy(string_ret, chan->zone->country, sizeof(string_ret));
            string_ret[sizeof(string_ret) - 1] = '\0';
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANHANGUPCAUSE:
        long_ret = chan->hangupcause;
        ret = (u_char *)&long_ret;
        break;
    case OPBXCHANVARIABLES:
        if (pbx_builtin_serialize_variables(chan, string_ret, sizeof(string_ret)))
        {
            *var_len = strlen(string_ret);
            ret = (u_char *)string_ret;
        }
        else
        {
            ret = NULL;
        }
        break;
    case OPBXCHANFLAGS:
        bits_ret[0] = 0;
        for (bit = 0;  bit < 8;  bit++)
            bits_ret[0] |= ((chan->flags & (1 << bit)) >> bit) << (7 - bit);
        bits_ret[1] = 0;
        for (bit = 0;  bit < 8;  bit++)
            bits_ret[1] |= (((chan->flags >> 8) & (1 << bit)) >> bit) << (7 - bit);
        *var_len = 2;
        ret = bits_ret;
        break;
    case OPBXCHANTRANSFERCAP:
        long_ret = chan->transfercapability;
        ret = (u_char *)&long_ret;
        break;
    default:
        ret = NULL;
        break;
    }
    opbx_channel_unlock(chan);
    return ret;
}

static u_char *opbx_var_channel_types(struct variable *vp, oid *name, size_t *length,
                                      int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct opbx_variable *channel_types, *next;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case OPBXCHANTYPECOUNT:
        long_ret = 0;
        for (channel_types = next = opbx_channeltype_list();  next;  next = next->next)
            long_ret++;
        opbx_variables_destroy(channel_types);
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_channel_types_table(struct variable *vp, oid *name, size_t *length,
        int exact, size_t *var_len, WriteMethod **write_method)
{
    const struct opbx_channel_tech *tech = NULL;
    struct opbx_variable *channel_types, *next;
    static unsigned long long_ret;
    struct opbx_channel *chan;
    u_long i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
        return NULL;

    channel_types = opbx_channeltype_list();
    for (i = 1, next = channel_types;  next  &&  i != name[*length - 1];  next = next->next, i++)
        ;
    if (next != NULL)
        tech = opbx_get_channel_tech(next->name);
    opbx_variables_destroy(channel_types);
    if (next == NULL  ||  tech == NULL)
        return NULL;

    switch (vp->magic)
    {
    case OPBXCHANTYPEINDEX:
        long_ret = name[*length - 1];
        return (u_char *) &long_ret;
    case OPBXCHANTYPENAME:
        *var_len = strlen(tech->type);
        return (u_char *) tech->type;
    case OPBXCHANTYPEDESC:
        *var_len = strlen(tech->description);
        return (u_char *) tech->description;
    case OPBXCHANTYPEDEVSTATE:
        long_ret = tech->devicestate  ?  1  :  2;
        return (u_char *) &long_ret;
    case OPBXCHANTYPEINDICATIONS:
        long_ret = tech->indicate  ?  1  :  2;
        return (u_char *) &long_ret;
    case OPBXCHANTYPETRANSFER:
        long_ret = tech->transfer  ?  1  :  2;
        return (u_char *) &long_ret;
    case OPBXCHANTYPECHANNELS:
        long_ret = 0;
        for (chan = opbx_channel_walk_locked(NULL);
             chan;
             chan = opbx_channel_walk_locked(chan))
        {
            opbx_channel_unlock(chan);
            if (chan->tech == tech)
                long_ret++;
        }
        return (u_char *)&long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_Config(struct variable *vp, oid *name, size_t *length,
                               int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct timeval tval;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case OPBXCONFUPTIME:
        gettimeofday(&tval, NULL);
        long_ret = difftime(tval.tv_sec, opbx_startuptime) * 100 + tval.tv_usec / 10000;
        return (u_char *) &long_ret;
    case OPBXCONFRELOADTIME:
        gettimeofday(&tval, NULL);
        if (opbx_lastreloadtime)
            long_ret = difftime(tval.tv_sec, opbx_lastreloadtime) * 100 + tval.tv_usec / 10000;
        else
            long_ret = difftime(tval.tv_sec, opbx_startuptime) * 100 + tval.tv_usec / 10000;
        return (u_char *) &long_ret;
    case OPBXCONFPID:
        long_ret = getpid();
        return (u_char *) &long_ret;
    case OPBXCONFSOCKET:
        *var_len = strlen(opbx_config_OPBX_SOCKET);
        return (u_char *) opbx_config_OPBX_SOCKET;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_indications(struct variable *vp,
                                    oid *name,
                                    size_t *length,
                                    int exact,
                                    size_t *var_len,
                                    WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case OPBXINDCOUNT:
        long_ret = 0;
        while ((tz = opbx_walk_indications(tz)))
            long_ret++;
        return (u_char *) &long_ret;
    case OPBXINDCURRENT:
        tz = opbx_get_indication_zone(NULL);
        if (tz)
        {
            *var_len = strlen(tz->country);
            return (u_char *) tz->country;
        }
        *var_len = 0;
        return NULL;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_indications_table(struct variable *vp, oid *name, size_t *length,
        int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;
    int i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
        return NULL;

    i = name[*length - 1] - 1;
    while ((tz = opbx_walk_indications(tz))  &&  i)
        i--;
    if (tz == NULL)
        return NULL;

    switch (vp->magic)
    {
    case OPBXINDINDEX:
        long_ret = name[*length - 1];
        return (u_char *)&long_ret;
    case OPBXINDCOUNTRY:
        *var_len = strlen(tz->country);
        return (u_char *)tz->country;
    case OPBXINDALIAS:
        if (tz->alias)
        {
            *var_len = strlen(tz->alias);
            return (u_char *)tz->alias;
        }
        return NULL;
    case OPBXINDDESCRIPTION:
        *var_len = strlen(tz->description);
        return (u_char *)tz->description;
    default:
        break;
    }
    return NULL;
}

static int countmodule(const char *mod, const char *desc, int use, const char *like)
{
    return 1;
}

static u_char *opbx_var_Modules(struct variable *vp,
                                oid *name, size_t *length,
                                int exact,
                                size_t *var_len,
                                WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
    case OPBXMODCOUNT:
        long_ret = opbx_update_module_list(countmodule, NULL);
        return (u_char *) &long_ret;
    default:
        break;
    }
    return NULL;
}

static u_char *opbx_var_Version(struct variable *vp, oid *name, size_t *length,
                                int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

#if 0
    FIXME
    Someone please help me out here. I dunno where to find these

    switch (vp->magic)
    {
    case OPBXVERSTRING:
        *var_len = strlen(OPBX_VERSION);
        return (u_char *)OPBX_VERSION;
    case OPBXVERTAG:
        long_ret = CALLWEAVER_VERSION_NUM;
        return (u_char *)&long_ret;
    default:
        break;
    }
#endif
    return NULL;
}

static int term_callweaver_mib(int majorID, int minorID, void *serverarg, void *clientarg)
{
    unregister_sysORTable(callweaver_oid, OID_LENGTH(callweaver_oid));
    return 0;
}

static void init_callweaver_mib(void)
{
    static struct variable4 callweaver_vars[] =
    {
        {OPBXVERSTRING,          ASN_OCTET_STR, RONLY, opbx_var_Version,             2, {OPBXVERSION, OPBXVERSTRING}},
        {OPBXVERTAG,             ASN_UNSIGNED,  RONLY, opbx_var_Version,             2, {OPBXVERSION, OPBXVERTAG}},
        {OPBXCONFUPTIME,         ASN_TIMETICKS, RONLY, opbx_var_Config,              2, {OPBXCONFIGURATION, OPBXCONFUPTIME}},
        {OPBXCONFRELOADTIME,     ASN_TIMETICKS, RONLY, opbx_var_Config,              2, {OPBXCONFIGURATION, OPBXCONFRELOADTIME}},
        {OPBXCONFPID,            ASN_INTEGER,   RONLY, opbx_var_Config,              2, {OPBXCONFIGURATION, OPBXCONFPID}},
        {OPBXCONFSOCKET,         ASN_OCTET_STR, RONLY, opbx_var_Config,              2, {OPBXCONFIGURATION, OPBXCONFSOCKET}},
        {OPBXMODCOUNT,           ASN_INTEGER,   RONLY, opbx_var_Modules ,            2, {OPBXMODULES, OPBXMODCOUNT}},
        {OPBXINDCOUNT,           ASN_INTEGER,   RONLY, opbx_var_indications,         2, {OPBXINDICATIONS, OPBXINDCOUNT}},
        {OPBXINDCURRENT,         ASN_OCTET_STR, RONLY, opbx_var_indications,         2, {OPBXINDICATIONS, OPBXINDCURRENT}},
        {OPBXINDINDEX,           ASN_INTEGER,   RONLY, opbx_var_indications_table,   4, {OPBXINDICATIONS, OPBXINDTABLE, 1, OPBXINDINDEX}},
        {OPBXINDCOUNTRY,         ASN_OCTET_STR, RONLY, opbx_var_indications_table,   4, {OPBXINDICATIONS, OPBXINDTABLE, 1, OPBXINDCOUNTRY}},
        {OPBXINDALIAS,           ASN_OCTET_STR, RONLY, opbx_var_indications_table,   4, {OPBXINDICATIONS, OPBXINDTABLE, 1, OPBXINDALIAS}},
        {OPBXINDDESCRIPTION,     ASN_OCTET_STR, RONLY, opbx_var_indications_table,   4, {OPBXINDICATIONS, OPBXINDTABLE, 1, OPBXINDDESCRIPTION}},
        {OPBXCHANCOUNT,          ASN_INTEGER,   RONLY, opbx_var_channels,            2, {OPBXCHANNELS, OPBXCHANCOUNT}},
        {OPBXCHANINDEX,          ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANINDEX}},
        {OPBXCHANNAME,           ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANNAME}},
        {OPBXCHANLANGUAGE,       ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANLANGUAGE}},
        {OPBXCHANTYPE,           ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANTYPE}},
        {OPBXCHANMUSICCLASS,     ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANMUSICCLASS}},
        {OPBXCHANBRIDGE,         ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANBRIDGE}},
        {OPBXCHANMASQ,           ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANMASQ}},
        {OPBXCHANMASQR,          ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANMASQR}},
        {OPBXCHANWHENHANGUP,     ASN_TIMETICKS, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANWHENHANGUP}},
        {OPBXCHANAPP,            ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANAPP}},
        {OPBXCHANDATA,           ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANDATA}},
        {OPBXCHANCONTEXT,        ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCONTEXT}},
        {OPBXCHANPROCCONTEXT,    ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANPROCCONTEXT}},
        {OPBXCHANPROCEXTEN,      ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANPROCEXTEN}},
        {OPBXCHANPROCPRI,        ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANPROCPRI}},
        {OPBXCHANEXTEN,          ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANEXTEN}},
        {OPBXCHANPRI,            ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANPRI}},
        {OPBXCHANACCOUNTCODE,    ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANACCOUNTCODE}},
        {OPBXCHANFORWARDTO,      ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANFORWARDTO}},
        {OPBXCHANUNIQUEID,       ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANUNIQUEID}},
        {OPBXCHANCALLGROUP,      ASN_UNSIGNED,  RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCALLGROUP}},
        {OPBXCHANPICKUPGROUP,    ASN_UNSIGNED,  RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANPICKUPGROUP}},
        {OPBXCHANSTATE,          ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANSTATE}},
        {OPBXCHANMUTED,          ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANMUTED}},
        {OPBXCHANRINGS,          ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANRINGS}},
        {OPBXCHANCIDDNID,        ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDDNID}},
        {OPBXCHANCIDNUM,         ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDNUM}},
        {OPBXCHANCIDNAME,        ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDNAME}},
        {OPBXCHANCIDANI,         ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDANI}},
        {OPBXCHANCIDRDNIS,       ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDRDNIS}},
        {OPBXCHANCIDPRES,        ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDPRES}},
        {OPBXCHANCIDANI2,        ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDANI2}},
        {OPBXCHANCIDTON,         ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDTON}},
        {OPBXCHANCIDTNS,         ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANCIDTNS}},
        {OPBXCHANAMAFLAGS,       ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANAMAFLAGS}},
        {OPBXCHANADSI,           ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANADSI}},
        {OPBXCHANTONEZONE,       ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANTONEZONE}},
        {OPBXCHANHANGUPCAUSE,    ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANHANGUPCAUSE}},
        {OPBXCHANVARIABLES,      ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANVARIABLES}},
        {OPBXCHANFLAGS,          ASN_OCTET_STR, RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANFLAGS}},
        {OPBXCHANTRANSFERCAP,    ASN_INTEGER,   RONLY, opbx_var_channels_table,      4, {OPBXCHANNELS, OPBXCHANTABLE, 1, OPBXCHANTRANSFERCAP}},
        {OPBXCHANTYPECOUNT,      ASN_INTEGER,   RONLY, opbx_var_channel_types,       2, {OPBXCHANNELS, OPBXCHANTYPECOUNT}},
        {OPBXCHANTYPEINDEX,      ASN_INTEGER,   RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPEINDEX}},
        {OPBXCHANTYPENAME,       ASN_OCTET_STR, RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPENAME}},
        {OPBXCHANTYPEDESC,       ASN_OCTET_STR, RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPEDESC}},
        {OPBXCHANTYPEDEVSTATE,   ASN_INTEGER,   RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPEDEVSTATE}},
        {OPBXCHANTYPEINDICATIONS,ASN_INTEGER,   RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPEINDICATIONS}},
        {OPBXCHANTYPETRANSFER,   ASN_INTEGER,   RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPETRANSFER}},
        {OPBXCHANTYPECHANNELS,   ASN_GAUGE,     RONLY, opbx_var_channel_types_table, 4, {OPBXCHANNELS, OPBXCHANTYPETABLE, 1, OPBXCHANTYPECHANNELS}},
    };

    register_sysORTable(callweaver_oid, OID_LENGTH(callweaver_oid),
                        "CALLWEAVER-MIB implementation for CallWeaver.");

    REGISTER_MIB("res_snmp", callweaver_vars, variable4, callweaver_oid);

    snmp_register_callback(SNMP_CALLBACK_LIBRARY,
                           SNMP_CALLBACK_SHUTDOWN,
                           term_callweaver_mib, NULL);
}
