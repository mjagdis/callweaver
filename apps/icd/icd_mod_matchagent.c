/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *   korczynski@gmail.com                                                                      *
 ***************************************************************************/

#include <icd_module_api.h>

extern icd_fieldset *agents;
icd_status link_callers_via_pop_customer_match_agent(icd_distributor * dist, void *extra);
icd_status distribute_customer_match_agent(icd_distributor * dist, 
                                           icd_caller *agent_caller,
                                           icd_caller *customer_caller,
                                           icd_member *customer);

static icd_status init_icd_distributor_match_agent(icd_distributor * that, char *name, icd_config * data)
{

    assert(that != NULL);
    assert(data != NULL);
    strncpy(that->name, name, sizeof(that->name));
    icd_distributor__set_config_params(that, data);
    icd_distributor__create_lists(that, data);
    icd_list__set_node_insert_func((icd_list *) that->agents, icd_list__insert_fifo, NULL);
    icd_distributor__set_link_callers_fn(that, link_callers_via_pop_customer_match_agent, NULL);
    icd_distributor__create_thread(that);

    ast_verbose(VERBOSE_PREFIX_3 "Registered ICD Distributor[%s] Initialized !\n", name);
    return ICD_SUCCESS;
}

int icd_module_load(icd_config_registry * registry)
{

    assert(registry != NULL);
    icd_config_registry__register_ptr(registry, "dist", "matchagent", init_icd_distributor_match_agent);

    icd_config_registry__register_ptr(registry, "dist.link", "popcustmatchagent",
      link_callers_via_pop_customer_match_agent  );

    ast_verbose(VERBOSE_PREFIX_3 "Registered ICD Module[MatchAgent]!\n");

    return 0;
}

int icd_module_unload(void)
{
    /*TODO didnt get this far */
    ast_verbose(VERBOSE_PREFIX_3 "Unloaded ICD Module[MatchAgent]!\n");
    return 0;

}

/* Match the customers "identifier" to the agents "identifier".
 * the "identifier" is set to what ever value is required typical use case
 * agent identifier=agent_id and the customer identifier = agent_id
 * exten => 9001,1,icd_customer(identifier=123|name=roundrobin-customer|queue=roundrobin_q|moh=ringing)
 * exten => 8001,1,icd_agent(identifier=123|agent=101|noauth=1|queue=test_q
 * */
icd_status link_callers_via_pop_customer_match_agent(icd_distributor * dist, void *extra)
{
    icd_member *customer = NULL;
    icd_member *agent = NULL;
    icd_caller *customer_caller = NULL;
    icd_caller *agent_caller = NULL;
    char *tmp_str;
    icd_list_iterator *iter;
    icd_status result;

    assert(dist != NULL);
    assert(dist->customers != NULL);
    assert(dist->agents != NULL);
    if (!icd_member_list__has_members(dist->customers)) {
        return ICD_ENOTFOUND;
    }

    /* Go through all the customers looking for a match */
    /* For each customer on the list, try to find the match agent. 
     * Return after all customer has been pass looked over once */
    result = icd_distributor__lock(dist);     
    iter = icd_distributor__get_customer_iterator(dist);
    while (icd_list_iterator__has_more(iter)) {
        customer = (icd_member *) icd_list_iterator__next(iter);
	if (customer != NULL) {
            customer_caller = icd_member__get_caller(customer);
	}    
        if (customer == NULL || customer_caller == NULL) {
            opbx_log(LOG_ERROR, "MatchAgent Distributor %s could not retrieve customer from list\n",
                    icd_distributor__get_name(dist));
                continue;
        }
	
        tmp_str = icd_caller__get_param(customer_caller, "identifier");
	
        if (tmp_str == NULL) {
            opbx_log(LOG_WARNING, "MatchAgent Distributor [%s] reports that customer [%s] has no identifier\n",
                icd_distributor__get_name(dist), icd_caller__get_name(customer_caller));
                continue;
            }
        agent_caller = (icd_caller *) icd_fieldset__get_value(agents, tmp_str);   
        if (agent_caller == NULL) {
            opbx_log(LOG_WARNING, "MatchAgent Distributor [%s] reports that agent [%s] is not in ICD\n",
                icd_distributor__get_name(dist), tmp_str);
                continue;
            }
        if(icd_caller__get_state(agent_caller) != ICD_CALLER_STATE_READY){
	   continue;
	}       
        agent = icd_caller__get_member_for_distributor(agent_caller, dist);
	if ((agent==NULL) || (icd_distributor__agent_position(dist, (icd_agent *) agent_caller) < 0)) {
/*            opbx_log(LOG_WARNING, "MatchAgent Distributor [%s] reports that agent [%s] is not in distributor\n",
                icd_distributor__get_name(dist), tmp_str);
*/                
		continue;
            }
        result = icd_member__distribute(agent);
        if (result != ICD_SUCCESS) {
            opbx_log(LOG_WARNING, "MatchAgent Distributor [%s] reports that cannot distribute agent [%s]\n",
                icd_distributor__get_name(dist), tmp_str);
                continue;
        }
//        result = icd_distributor__unlock(dist);     
	icd_distributor__remove_caller(dist, agent_caller);
        result=distribute_customer_match_agent(dist,agent_caller, customer_caller, customer); 
//        result = icd_distributor__lock(dist);     
     } /*  while (icd_list_iterator__has_more(iter)) */

    result = icd_distributor__unlock(dist);     
    destroy_icd_list_iterator(&iter);
    return ICD_SUCCESS;
}

icd_status distribute_customer_match_agent(icd_distributor * dist,
                                          icd_caller *agent_caller,
                                           icd_caller *customer_caller,
                                            icd_member *customer) {
    icd_status result;
    int cust_id = icd_caller__get_id(customer_caller);
    int agent_id = agent_id = icd_caller__get_id(agent_caller);

    icd_distributor__remove_caller(dist, customer_caller);
    result = icd_member__distribute(customer);
    if (result != ICD_SUCCESS) {
        icd_caller__set_state(agent_caller, ICD_CALLER_STATE_READY);
        return result;
    }

    result = icd_caller__join_callers(customer_caller, agent_caller);

    /* Figure out who the bridger is, and who the bridgee is */
    result = icd_distributor__select_bridger(agent_caller, customer_caller);

    ast_verbose(VERBOSE_PREFIX_3 "MatchAgent Distributor [%s] Link CustomerID[%d] to AgentID[%d]\n",
        icd_distributor__get_name(dist), cust_id, agent_id);
    if (icd_caller__has_role(customer_caller, ICD_BRIDGER_ROLE)) {
        result = icd_caller__bridge(customer_caller);
    } else if (icd_caller__has_role(agent_caller, ICD_BRIDGER_ROLE)) {
        result = icd_caller__bridge(agent_caller);
    } else {
        opbx_log(LOG_ERROR, "MatchAgent Distributor %s found no bridger responsible to bridge call\n",
            icd_distributor__get_name(dist));
        return ICD_EGENERAL;
    }

    return ICD_SUCCESS;
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


