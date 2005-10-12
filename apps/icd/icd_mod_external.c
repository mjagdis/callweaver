/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <icd_module_api.h>

/* Private implemenations */
static int module_id = 0;
static char *module_name = "External_Module_Example";

static icd_status init_icd_distributor_external(icd_distributor * that, char *name, icd_config * data);
static int icd_module__factory_event_listener(void *listener, icd_event * factory_event, void *extra);
static int icd_module__event_listener(void *listener, icd_event * event, void *extra);

int icd_module_load(icd_config_registry * registry)
{

    assert(registry != NULL);
    icd_config_registry__register_ptr(registry, "dist", "external", init_icd_distributor_external);
    icd_config_registry__register_ptr(registry, "dist.link", "external", icd_distributor__link_callers_via_pop);

    icd_config_registry__register_ptr(registry, "state.call.end.function", "external",
        icd_caller__standard_state_call_end);

    module_id = icd_event_factory__add_module(module_name);
    if (module_id == 0)
        opbx_log(LOG_WARNING, "Unable to register Module Name[%s]", module_name);

    ast_verbose(VERBOSE_PREFIX_3 "Registered ICD Module[External]!\n");

    return 0;
}

int icd_module_unload(void)
{
    /*TODO didnt get this far */
    ast_verbose(VERBOSE_PREFIX_3 "Unloaded ICD Module[External]!\n");
    return 0;

}

static icd_status init_icd_distributor_external(icd_distributor * that, char *name, icd_config * data)
{

    assert(that != NULL);
    assert(data != NULL);
    strncpy(that->name, name, sizeof(that->name));
    icd_distributor__set_config_params(that, data);
    icd_distributor__create_lists(that, data);

    icd_list__set_node_insert_func((icd_list *) that->agents, icd_list__insert_ordered,
        icd_caller__cmp_last_state_change_order);
    /* example of adding listener to an object */
    icd_distributor__add_listener((icd_distributor *) that, queues, icd_module__event_listener, NULL);
    icd_distributor__create_thread(that);

    /* Register a global event handler that listens on icd. */
    icd_event_factory__add_listener(event_factory, queues, icd_module__factory_event_listener, NULL);

    ast_verbose(VERBOSE_PREFIX_3 "Registered ICD Distributor[%s] Initialized !\n", name);

    return ICD_SUCCESS;
}
static int icd_module__factory_event_listener(void *listener, icd_event * factory_event, void *extra)
{
    assert(factory_event != NULL);

    //char *smsg;
    icd_queue *queue = NULL;
    icd_distributor *distributor = NULL;
    icd_caller *caller = NULL;
    struct ast_channel *chan = NULL;
    icd_event *event = icd_event__get_extra(factory_event);
    int module_id = icd_event__get_module_id(event);
    int event_id = icd_event__get_event_id(event);
    int call_pos, call_cnt;

    switch (event_id) {
    case ICD_EVENT_ADD:
        /* ok need to get the src see if itsa q add */
        switch (module_id) {
        case ICD_QUEUE:        /*new queue created & added */
            queue = (icd_queue *) icd_event__get_source(event);

            break;
        case ICD_CALLER:
            caller = (icd_caller *) icd_event__get_source(event);
            /* ICD_EVENT_ADD is called when add to queue and dist 
             * hmm no way to tell which ? add new 
             */
            queue = (icd_queue *) icd_event__get_extra(event);
            manager_event(EVENT_FLAG_USER, "icd_addtoqueue",
                "Channel: %s\r\nCallerID: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\n",
                (chan ? chan->name : "unknown"),
                (chan ? chan->cid.cid_num ? chan->cid.cid_num : "unknown" : "unknown"),
                (queue ? icd_queue__get_name(queue) : "unknown"), call_pos, call_cnt);

            break;
        case ICD_AGENT:
        case ICD_CUSTOMER:

            break;
        case ICD_DISTRIBUTOR_LIST:
            distributor = (icd_distributor *) icd_event__get_source(event);
            /*
               manager_event(EVENT_FLAG_USER, "icd_addtodistributor", 
               "Channel: %s\r\nCallerID: %s\r\nDist: %s\r\nPosition: %d\r\nCount: %d\r\n",
               chan->name, (chan->cid.cid_num ? chan->cid.cid_num : "unknown"),
               icd_queue__get_name(queue), call_pos, call_cnt );
             */

            break;

        }
        break;
    case ICD_EVENT_CHANNEL_UP:
        caller = (icd_caller *) icd_event__get_source(event);
        chan = (ast_channel *) icd_caller__get_channel(caller);
        manager_event(EVENT_FLAG_USER, "icd_channelup",
            "Id: %d\r\n" "Channel: %s\r\n" "Uniqueid: %s\r\n" "Callerid: %s\r\n", icd_caller__get_id(caller),
            chan->name, chan->uniqueid, (chan->cid.cid_num ? chan->cid.cid_num : "unknown"));

        ast_verbose(VERBOSE_PREFIX_2 "FAT_AUTODIALER ICD_EVENT_CHANNEL_UP:ID[%d] [%s] msg[%s] \n",
            icd_caller__get_id(caller), icd_caller__get_name(caller), icd_event__get_message(event)
            );
        break;
    case ICD_EVENT_STATECHANGE:
        /* this is called any time you have a state change in icd */

        break;
    default:
        break;                  /* No Op */
    }

    return 0;
}

static int icd_module__event_listener(void *listener, icd_event * event, void *extra)
{
    icd_queue *queue = NULL;
    icd_caller *caller = NULL;
    struct ast_channel *chan = NULL;
    int call_cnt = 0;
    int call_pos = 0;

    assert(event != NULL);

/*
ast_verbose(VERBOSE_PREFIX_2 "AUTODIALER: %s \n",icd_event__get_message(event));
 return 0;
*/
    switch (icd_event__get_event_id(event)) {
    case ICD_EVENT_ADD:
        /* ok need to get the src see if itsa q add */
        switch (icd_event__get_module_id(event)) {
        case ICD_QUEUE:
            caller = (icd_caller *) icd_event__get_source(event);
            queue = (icd_queue *) extra;
            ast_verbose(VERBOSE_PREFIX_2 "AUTODIALER QUEUE ADD:ID[%d] \n", icd_caller__get_id(caller));
            if (queue != NULL) {
                chan = icd_caller__get_channel(caller);

                if (icd_caller__has_role(caller, ICD_AGENT_ROLE))
                    call_cnt = icd_queue__get_agent_count(queue);
                else
                    call_cnt = icd_queue__get_customer_count(queue);
                if (icd_caller__has_role(caller, ICD_AGENT_ROLE))
                    call_pos = icd_queue__get_agent_position(queue, (icd_agent *) caller);
                else
                    call_pos = icd_queue__get_customer_position(queue, (icd_customer *) caller);
            }
            manager_event(EVENT_FLAG_USER, "icd_addtoqueue",
                "Channel: %s\r\nCallerID: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\n",
                (chan ? chan->name : "unknown"),
                (chan ? chan->cid.cid_num ? chan->cid.cid_num : "unknown" : "unknown"),
                (queue ? icd_queue__get_name(queue) : "unknown"), call_pos, call_cnt);
            break;
        case ICD_DISTRIBUTOR_LIST:
            ast_verbose(VERBOSE_PREFIX_2 "AUTODIALER DIST LIST ADD:ID[%s] \n", icd_event__get_message(event));
            /*
               queue = (icd_queue *)icd_event__get_source(event);
               manager_event(EVENT_FLAG_USER, "icd_addtodistributor", 
               "Channel: %s\r\nCallerID: %s\r\nDist: %s\r\nPosition: %d\r\nCount: %d\r\n",
               chan->name, (chan->cid.cid_num ? chan->cid.cid_num : "unknown"),
               icd_queue__get_name(queue), call_pos, call_cnt );
             */

            break;
        case ICD_CALLER:
        case ICD_AGENT:
        case ICD_CUSTOMER:
            caller = (icd_caller *) icd_event__get_source(event);
            break;

        }
        break;
    case ICD_EVENT_CHANNEL_UP:
        caller = (icd_caller *) icd_event__get_source(event);
        chan = (ast_channel *) icd_caller__get_channel(caller);
        manager_event(EVENT_FLAG_USER, "icd_channelup",
            "Id: %d\r\n" "Channel: %s\r\n" "Uniqueid: %s\r\n" "Callerid: %s\r\n", icd_caller__get_id(caller),
            chan->name, chan->uniqueid, (chan->cid.cid_num ? chan->cid.cid_num : "unknown"));

        ast_verbose(VERBOSE_PREFIX_2 "AUTODIALER ICD_EVENT_CHANNEL_UP:ID[%d] [%s] msg[%s] \n",
            icd_caller__get_id(caller), icd_caller__get_name(caller), icd_event__get_message(event)
            );
        break;
    case ICD_EVENT_STATECHANGE:
        /* this is called any time you have a state change in icd */

        break;
    default:
        break;                  /* No Op */
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

