/*
 * ICD - Intelligent Call Distributor 
 *
 * Copyright (C) 2003, 2004, 2005
 *
 * Written by Anthony Minessale II <anthmct at yahoo dot com>
 * Written by Bruce Atherton <bruce at callenish dot com>
 * Additions, Changes and Support by Tim R. Clark <tclark at shaw dot ca>
 * Changed to adopt to jabber interaction and adjusted for OpenPBX.org by
 * Halo Kwadrat Sp. z o.o., Piotr Figurny and Michal Bielicki
 * 
 * This application is a part of:
 * 
 * OpenPBX -- An open source telephony toolkit.
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Mark Spencer <markster@digium.com>
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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif  

#include "openpbx/icd/icd_module_api.h"
#include "openpbx/icd/icd_command.h"
#include "openpbx/icd/icd_conference.h"
/* Private implemenations */
static int module_id = 0;
static char *module_name = "event_cli";
/* This is the module mask (icd.conf module_mask=) for what module events to show in the default icd cli.*/
static int module_mask[ICD_MAX_MODULES];

/* This is the event mask (icd.conf event_mask=)for what events to show in the default icd cli.*/
static int event_mask[ICD_MAX_EVENTS];     

static int icd_module__event_cli(void *listener, icd_event * event, void *extra);


int icd_module_load(icd_config_registry * registry)
{

    assert(registry != NULL);

    module_id = icd_event_factory__add_module(module_name);
    if (module_id == 0)
        opbx_log(LOG_WARNING, "Unable to register Module Name[%s]", module_name);
    else {
        icd_event_factory__add_listener(event_factory, queues, icd_module__event_cli, NULL);
        opbx_verbose(VERBOSE_PREFIX_3 "Registered ICD Module[%s]!\n",module_name);
    }
    return 0;
}

int icd_module_unload(void)
{
    /*TODO didnt get this far */
    opbx_verbose(VERBOSE_PREFIX_3 "Unloaded ICD Module[%s]!\n", module_name);
    return 0;

}

/* This can act as a listener on all events in the system, or on selected events. */
static int icd_module__event_cli(void *listener, icd_event * factory_event, void *extra)
{
    char *smsg;
    icd_caller *caller = NULL;
    struct opbx_channel *chan = NULL;
    int confnr = 0;
    icd_conference * conf;
    icd_event *event = icd_event__get_extra(factory_event);
    int module_id = icd_event__get_module_id(event);
    int event_id = icd_event__get_event_id(event);
/*
    assert(factory_event != NULL);
      opbx_verbose(VERBOSE_PREFIX_2 "YoYoAPP_ICD:Mod[%d] Event[%d]  \n",
              icd_event__get_module_id(event),
               icd_event__get_event_id(event)
              );
      */
    /*
      opbx_verbose(VERBOSE_PREFIX_2 "APP_ICD:Mod[%d][%d] Event[%d][%d]  \n",
      icd_event__get_module_id(event),
      module_mask[icd_event__get_module_id(event)],
      icd_event__get_event_id(event),
      event_mask[icd_event__get_event_id(event)]
      );
    */
//    if (module_mask[module_id] && event_mask[event_id]) {
        /* filter based on icd.conf events */
        smsg = icd_event__get_message(event);
        switch (event_id) {
        case ICD_EVENT_STATECHANGE:
            caller = (icd_caller *) icd_event__get_source(event);
            chan = icd_caller__get_channel(caller);
	        conf = icd_caller__get_conference(caller);		
            if(conf != NULL)
	          confnr = conf->ztc.confno;
              opbx_verbose(VERBOSE_PREFIX_1 "[%s] ICD_id[%d] ICD_name[%s] ICD_CallerID[%s] CallerIDName[%s] Channel[%s] Uniqueid[%s] conf[%d] state - %s \n",
                        icd_module_strings[icd_event__get_module_id(event)], icd_caller__get_id(caller),
                        icd_caller__get_name(caller), icd_caller__get_caller_id(caller),
                        chan ? chan->cid.cid_num ? chan->cid.cid_num : "unknown" :"nochan",
                        chan ? chan->cid.cid_name ? chan->cid.cid_name : "unknown" : "nochan",
                        chan ? chan->name : "nochan", chan ? chan->uniqueid : "nochan",confnr, smsg);
	    break;
	    
        case ICD_EVENT_READY:
            break;
            /*                
                              case ICD_EVENT_DISTRIBUTE:

                              break;
                              case ICD_EVENT_BRIDGED:

                              break;
            */
	case ICD_EVENT_LINK:
	case ICD_EVENT_UNLINK:
/*	case ICD_EVENT_PUSH:
	case ICD_EVENT_POP: 
        case ICD_EVENT_PUSHBACK: */
	case ICD_EVENT_DISTRIBUTE:
/*	case ICD_EVENT_DISTRIBUTED: */
           if (smsg) 
                opbx_verbose(VERBOSE_PREFIX_1 "[%s][%s] %s \n", icd_module_strings[icd_event__get_module_id(event)],
                            icd_event_strings[icd_event__get_event_id(event)], smsg);	     
	     break;
	     
        default:
            if (smsg)
                /*opbx_verbose(VERBOSE_PREFIX_1 "APP_ICD: %s \n",string); */
                opbx_verbose(VERBOSE_PREFIX_1 "[%s][%s] %s \n", icd_module_strings[icd_event__get_module_id(event)],
                            icd_event_strings[icd_event__get_event_id(event)], smsg);
        }
//    }

    /* No veto today, thank you. */
    return 0;

}

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */


