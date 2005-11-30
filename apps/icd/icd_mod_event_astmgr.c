/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <openpbx/icd/icd_module_api.h>

/* Private implemenations */
static int module_id = 0;
static char *module_name = "event_astmgr";

/* This is the module mask (icd.conf module_mask_astmgr=) 
for what module events to show in the default icd cli.
*/
static int module_mask[ICD_MAX_MODULES];

/* This is the event mask (icd.conf event_mask_astmgr=)for what events to show in the default icd cli.*/
static int event_mask[ICD_MAX_EVENTS];     

static int icd_module__event_astmgr(void *listener, icd_event * factory_event, void *extra);


int icd_module_load(icd_config_registry * registry)
{

    assert(registry != NULL);

    module_id = icd_event_factory__add_module(module_name);
    if (module_id == 0)
        opbx_log(LOG_WARNING, "Unable to register Module Name[%s]", module_name);
    else {
        icd_event_factory__add_listener(event_factory, queues, icd_module__event_astmgr, NULL);
        opbx_verbose(VERBOSE_PREFIX_3 "Registered ICD Module[%s]!\n",module_name);
    }
    return 0;
}

int icd_module_unload(void)
{
    opbx_verbose(VERBOSE_PREFIX_3 "Unloaded ICD Module[%s]!\n", module_name);
    return 0;

}

/* This can act as a listener on all events in the system, or on selected events. */
static int icd_module__event_astmgr(void *listener, icd_event * factory_event, void *extra)
{
    char *smsg;
    icd_caller *caller = NULL;
    struct opbx_channel *chan = NULL;

    icd_event *event = icd_event__get_extra(factory_event);
    int module_id = icd_event__get_module_id(event);
    int event_id = icd_event__get_event_id(event);

    assert(factory_event != NULL);
    /* 
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
    if (module_mask[module_id] && event_mask[event_id]) {
        /* filter based on icd.conf events */
        smsg = icd_event__get_message(event);
        switch (event_id) {
        case ICD_EVENT_STATECHANGE:
            caller = (icd_caller *) icd_event__get_source(event);
            chan = icd_caller__get_channel(caller);
            manager_event(EVENT_FLAG_USER, "icd_state_change",
                "Module: %s\r\nID: %s\r\nCallerID: %s\r\nMessage: %s\r\n",
                icd_module_strings[icd_event__get_module_id(event)], icd_caller__get_id(caller),
                /*icd_caller__get_name(caller),
                "Module: %s\r\nID: %s\r\nCallerID: %s\r\nCallerName: %s\r\nMessage: %s\r\n",
                (chan ? chan->name : "unknown"),
                */
                (chan ? chan->cid.cid_num ? chan->cid.cid_num : "unknown" : "unknown"),
                smsg);

            break;
        case ICD_EVENT_READY:
            break;
        case ICD_EVENT_DISTRIBUTE:
            break;
        case ICD_EVENT_BRIDGED:
            break;
        case ICD_EVENT_BRIDGE_END:
            break;
        default:
            if (smsg)
                manager_event(EVENT_FLAG_USER, "icd_event",
                "Module: %s\r\nEvent: %s\r\nMessage: %s\r\n",
                icd_module_strings[icd_event__get_module_id(event)],
                icd_event_strings[icd_event__get_event_id(event)], 
                smsg);

        }
    }

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


