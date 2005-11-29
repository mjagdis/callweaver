#define _REENTRANT

#include <app_icd.h>
#include <icd_common.h>
#include <icd_globals.h>
#include <icd_distributor.h>
#include <voidhash.h>
#include <icd_command.h>
#include <icd_fieldset.h>
#include <icd_queue.h>
#include <icd_customer.h>
#include <icd_conference.h>
#include <icd_agent.h>
#include <icd_bridge.h>
#include <icd_caller.h>
#include <icd_config.h>
#include <icd_member.h>
#include <icd_event.h>
#include <openpbx/app.h>

#include <assert.h>
#include <icd_types.h>
#include <icd_common.h>
#include <icd_caller.h>
#include <icd_distributor.h>
#include <icd_caller_list.h>
#include <icd_globals.h>
#include <icd_member_list.h>
#include <icd_list.h>
#include <icd_member.h>
#include <icd_queue.h>
#include <icd_customer.h>
#include <icd_agent.h>
#include <icd_bridge.h>
#include <icd_plugable_fn.h>
#include <icd_plugable_fn_list.h>
#include <icd_listeners.h>


#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <loudmouth/loudmouth.h>
#include <pthread.h>
#include <icd_common.h>
#include <malloc.h>
#include <semaphore.h>

#include <openpbx/app.h>

#include <icd_caller_private.h>
#include <icd_jabber.h>

#define MSG_SIZE 2048
#define JABBER_FUNCT_ARGS_MAX 5
#define JABBER_FUNCT_LEN_MAX 100

extern icd_fieldset *agents;
char jabber_server[100];
char jabber_login[100];
char jabber_password[100];
char jabber_send_address[100];
int JabberOK = 0;
 
extern struct opbx_channel *agent_channel0;
void *jabber_messages ();

sem_t icd_jabber_fifo_semaphore;
sem_t icd_jabber_fifo_command_semaphore;

typedef void *func_ptr (int, char *[]);

struct pars
{
  int argc;
  func_ptr *funct;
  char *args;
  char *argv[JABBER_FUNCT_ARGS_MAX];
};

struct lista
{
  char funct_name[JABBER_FUNCT_LEN_MAX];
  func_ptr *funkcja;
  struct lista *next;
};

struct lista *head = NULL;
struct lista *tail = NULL;

/*
extern struct icd_queue {
    char *name;
    icd_distributor *distributor;
    icd_member_list *customers;
    icd_member_list *agents;
    icd_queue_holdannounce holdannounce;
    icd_queue_chimeinfo chimeinfo;
    char monitor_args[256];
    int priority;               // priority of this queue in relation to other queues 
//    int wait_timeout;           // How many seconds before timing out of queue 
//    void_hash_table *params;
//    icd_listeners *listeners;
//    icd_queue_state state;
//    int flag;                   //accept calls, tagged iter mem q, match em from config untag mark for delete 
//      icd_status(*dump_fn) (icd_queue *, int verbosity, int fd, void *extra);
    void *dump_fn_extra;
    icd_memory *memory;
    opbx_mutex_t lock;
    int allocated;
};
*/
extern icd_agent *app_icd__dtmf_login (struct opbx_channel *chan, char *login,
				       char *pass, int tries);

int icd_jabber_ack_req (int argc, char *argv[])
{
  char * agentname;
  icd_agent *agent = NULL;

  opbx_log (LOG_WARNING, "parameters count: %i, function name: %s\n", argc,
	   argv[0]);
  if  (argc != 2) {
     opbx_log (LOG_WARNING, "Bad number of parameters [%i], function name: %s\n", argc,
	   argv[0]);
     return 1;
  }
  agentname = argv[1];   	   
  agent = (icd_agent *) icd_fieldset__get_value(agents, agentname);
  if (!agent) {
        opbx_log(LOG_WARNING,
                    "Function Ack failed. Agent '%s' could not be found.\n", agentname);        
	return 1;
  }		    
  if(icd_caller__get_state(icd_caller *) agent) == ICD_CALLER_STATE_READY ||
     icd_caller__get_state(icd_caller *) agent) == ICD_CALLER_STATE_DISTRIBUTING ||
     icd_caller__get_state(icd_caller *) agent) == ICD_CALLER_STATE_GET_CHANNELS_AND_BRIDGE) {
     	icd_caller__add_flag((icd_caller *)agent, ICD_ACK_EXTERN_FLAG);
     	opbx_log(LOG_NOTICE, "Jabber Function Ack for agent '%s' .\n", agentname);
     } else {
     	opbx_log(LOG_WARNING, "Function Ack failed, Agent [%s] is not in appropriate state [%s]\n", agentname, icd_caller__get_state_string((icd_caller *) agent));
     	return 1;
     }
  return 0;
}

int icd_jabber_hang_up (int argc, char *argv[])
{
    icd_agent *agent = NULL;
    char *agentname;
    opbx_log(LOG_WARNING,"Function Hang up [%d]\n", argc);

    if (argc != 2) {
       opbx_log(LOG_WARNING,"Function Hang up failed- bad number of parameters [%d]\n", argc);
       return 1;
    }
    agentname = argv[1];   
    agent = (icd_agent *) icd_fieldset__get_value(agents, agentname);
    if (!agent) {
        opbx_log(LOG_WARNING,
                    "Function Hang up failed. Agent '%s' could not be found.\n", agentname);        
	return 1;
     }		    
     if(icd_caller__get_state((icd_caller *) agent) != ICD_CALLER_STATE_BRIDGED &&
        icd_caller__get_state((icd_caller *) agent) != ICD_CALLER_STATE_CONFERENCED){
        opbx_log(LOG_WARNING,
                    "Function Hang up failed. Agent '%s' in state [%s].\n", agentname,
		    icd_caller__get_state_string((icd_caller *) agent));        
	return 1;
     }  
     opbx_log(LOG_NOTICE, "Function Hang up for agent '%s' executed.\n", agentname);
     icd_caller__set_state_on_associations((icd_caller *) agent, ICD_CALLER_STATE_CALL_END);     
  
     return 0;
}


int
icd_jabber_login_req (int argc, char *argv[])
{
/* The code is copied frop app_icd_agent_exec and slightly modified. In the future there should be one function */
    struct opbx_channel *chan;
    icd_agent *agent = NULL;
    char *agentname;
    int res = 0;
    int  oldrformat = 0, oldwformat = 0;
    char *passwd=NULL;
    char * channelstring;

    opbx_log(LOG_WARNING,"funkcja login [%d]\n", argc);

    if ((argc != 3) && (argc !=4)){
         icd_jabber_send_message("LOGIN FAILURE! - wrong parameters number.");
         return 1;
    }	 
    channelstring = argv[1];
    agentname = argv[2];
    if (argc==4) passwd = argv[3];
      	 
    
//    LOCAL_USER_ADD(u);

    // check state and do nothing, logout or enyth else
    agent = (icd_agent *) icd_fieldset__get_value(agents, agentname);
    
    if (!agent) {
//chech passwd    
        opbx_log(LOG_WARNING,
                    "AGENT LOGGIN FAILURE!  Agent '%s' could not be found.\n"
                    "Please correct the 'agent' argument in the extensions.conf file\n", agentname);        
        icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] could not be found.", agentname);
        return 1;
       }       
    if (icd_caller__get_state((icd_caller *) agent) != ICD_CALLER_STATE_SUSPEND &&
      icd_caller__get_state((icd_caller *) agent) != ICD_CALLER_STATE_INITIALIZED) {
        opbx_log(LOG_WARNING, "Login - Agent '%s' already logged in nothing to do\n", agentname);        
        icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] already logged in.", agentname);
        return 1;
     }
        
    chan =icd_bridge_get_openpbx_channel(channelstring, NULL, NULL, NULL);
    if(!chan) {
        opbx_log(LOG_WARNING,"Not avaliable channel [%s] \n", channelstring);
        icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] - Not avaliable channel [%s].", agentname, channelstring);
	return 1;
    }
    /* Make sure channel is properly set up */
    
   if (chan->_state != OPBX_STATE_UP) {
        res = opbx_answer(chan);
    }
    oldrformat = chan->readformat;
    oldwformat = chan->writeformat;
    
    if(!(res=opbx_set_read_format(chan,  OPBX_FORMAT_SLINEAR))) {
        res = opbx_set_write_format(chan,  OPBX_FORMAT_SLINEAR);
    }
    
    if(res) {
        opbx_log(LOG_WARNING,"Unable to prepare channel %s\n",chan->name);
        icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] - Unable to prepare channel [%s].", agentname, channelstring);
        if(oldrformat)
            opbx_set_read_format(chan, oldrformat);
        if(oldwformat)
            opbx_set_write_format(chan, oldwformat);
//        LOCAL_USER_REMOVE(u);
        opbx_hangup(chan);
        return 1;
    }

    /* We need to find the appropriate agent:
     *   1. find match for "agent" parameter from extensions.conf in agents registry
     *   2. if "dynamic" is true, generate an agent as for customers if "agent" doesn't exist already
     *           (in this case, "queue" in extensions.conf will be a good idea unless agent adds self to queues)login Zap/g2/103 1002 1002
     *   3. if "identify" is true and channel is up, get DTMF and search for agent (authentication comes later)
     *   4. otherwise error
     * TBD - Do we need to protect against two users trying to use the same agent structure? YES!
     */
     icd_caller__set_channel((icd_caller *) agent, chan);
     icd_caller__set_channel_string((icd_caller *) agent, channelstring);
     icd_caller__set_param_string((icd_caller *) agent, "channel", channelstring);
     res = icd_bridge_dial_openpbx_channel((icd_caller *) agent, channelstring, 20000);
     if (res != OPBX_CONTROL_ANSWER){
         opbx_log(LOG_WARNING, "Login of agent [%s] failed - unable to get answer from channel [%s] .\n", agentname, channelstring);
        icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] - unable to get answer from channel [%s].", agentname, channelstring);
	 
/* More detailed check why there is no answer probably needed in the future. */	 
         opbx_hangup(chan);
	 return 1;
     }	 
     agent = app_icd__dtmf_login(chan, agentname, passwd, 3);
     if (!agent){
            opbx_log(LOG_WARNING, "Agent [%s] wrong password.\n",agentname);
            icd_jabber_send_message("LOGIN FAILURE!  Agent [%s] - wrong password.", agentname);
            opbx_hangup(chan);
            return 1;
      }    
       	  
 //       if(res!= OPBX_CONTROL_ANSWER){
//        opbx_log(LOG_WARNING,
//                    "AGENT FAILURE!  Agent '%s' timeout\n", agentname);        
//          return 1;
//	}
//       res = icd_bridge__play_sound_file(chan, "agent-loginok");
       
    opbx_log(LOG_NOTICE, "Agent [%s] found in registry and marked in use.\n",
                    icd_caller__get_name((icd_caller *) agent));
    icd_jabber_send_message("LOGIN OK!  Agent [%s] - successfully logged in.", agentname);

		  
       
        /* At this point, we have an agent. We hope he is already in queues but not in distributors. */
    if (icd_caller__get_state((icd_caller *) agent) == ICD_CALLER_STATE_SUSPEND ||
      icd_caller__get_state((icd_caller *) agent) == ICD_CALLER_STATE_INITIALIZED)
    {
    ((icd_caller *) agent)->thread_state = ICD_THREAD_STATE_UNINITIALIZED;
    icd_caller__set_state((icd_caller *) agent, ICD_CALLER_STATE_READY);
    
    if (icd_caller__get_onhook((icd_caller *) agent)) {
        /* On hook - Tell caller to start thread */
        opbx_log(LOG_NOTICE, "Agent login: Agent onhook %s starting independent caller thread\n", agentname);
//        icd_bridge__safe_hangup((icd_caller *) agent);
//        opbx_hangup(chan);
//	icd_caller__set_channel((icd_caller *) agent, NULL);
        opbx_stopstream(chan);
        opbx_generator_deactivate(chan);
        opbx_clear_flag(chan ,  OPBX_FLAG_BLOCKING);
        opbx_softhangup(chan ,  OPBX_SOFTHANGUP_EXPLICIT);
        opbx_hangup(chan);
	icd_caller__set_channel((icd_caller *) agent, NULL);

        icd_caller__add_role((icd_caller *) agent, ICD_LOOPER_ROLE);
        icd_caller__loop((icd_caller *) agent, 1);
    } else {
        /* Off hook - Use the PBX thread */
        opbx_log(LOG_NOTICE, "Agent login: Agent offhook %s starting independent caller thread\n", agentname);
//        icd_caller__assign_channel((icd_caller *) agent, chan);
        icd_caller__add_role((icd_caller *) agent, ICD_LOOPER_ROLE);

        /* This becomes the thread to manage agent state and incoming stream */
        icd_caller__loop((icd_caller *) agent, 0);
        /* Once we hit here, the call is finished */
        icd_caller__stop_waiting((icd_caller *) agent);
        opbx_softhangup(chan ,  OPBX_SOFTHANGUP_EXPLICIT);
        opbx_hangup(chan );
        icd_caller__set_channel((icd_caller *) agent, NULL);
    }
    opbx_log(LOG_NOTICE, "Agent login: Jabber thread for Agent %s ending\n", agentname);
   } else {
//Agent has thread already   
         opbx_log(LOG_NOTICE, "Agent login: Agent [%s] in state [%s]\n", agentname,
	 icd_caller__get_state_string((icd_caller *) agent));
               if(icd_caller__get_state((icd_caller *) agent) != ICD_CALLER_STATE_READY)
                icd_caller__set_state((icd_caller *) agent, ICD_CALLER_STATE_CALL_END);		
     }
//    LOCAL_USER_REMOVE(u);
    return -1;
}

//extern static icd_status icd_caller__remove_from_all_queues(icd_caller * that);
//icd_status icd_caller__remove_all_associations(icd_caller * that);
int
icd_jabber_logout_req (int argc, char *argv[])
{
    icd_agent *agent = NULL;
    char *agentname;
    char *passwd_to_check;
    char *passwd; 

    /* Identify agent just like app_icd__agent_exec, only this time we skip
       dynamically creating an agent. */
    if (argc != 3) {
         icd_jabber_send_message("LOGOUT FAILURE!  - wrong parameters number.");
         return 1;
    }	 
    agentname = argv[1];
    passwd_to_check = argv[2];
    agent = (icd_agent *) icd_fieldset__get_value(agents, agentname);   
    if (agent == NULL) {
        opbx_log(LOG_WARNING,
                    "LOGOUT FAILURE!  Agent '%s' could not be found.\n", agentname);
        icd_jabber_send_message("LOGOUT FAILURE!  Agent [%s] - could not be found.", agentname);
		    
        return 1;
    }
    passwd = icd_caller__get_param((icd_caller *) agent, "passwd");
    if (passwd) 
          if(strcmp(passwd, passwd_to_check)){
          opbx_log(LOG_WARNING,
                    "LOGOUT FAILURE! Wrong password for Agent '%s'.\n", agentname);
          icd_jabber_send_message("LOGOUT FAILURE!  Agent [%s] - wrong password [%s].", agentname, passwd_to_check);
        return 1;
    }     
    
    opbx_log(LOG_NOTICE, "Agent [%s] (found in registry) will be logged out.\n", agentname);
    /* TBD - Implement state change to ICD_CALLER_STATE_WAIT. We can't just pause the thread
     * because the caller's members would still be in the distributors. We need to go into a
     * caller state that is actually different, a paused/waiting/down state.
     */
    
     icd_jabber_send_message("LOGOUT OK!  Agent [%s].", agentname);
     
     if (icd_caller__set_state((icd_caller *) agent, ICD_CALLER_STATE_SUSPEND)  != ICD_SUCCESS){
             opbx_log(LOG_WARNING,
                    "LOGOUT FAILURE!  Agent '%s' vetoed or ivalid state change, state [%s].\n", agentname,icd_caller__get_state_string((icd_caller *) agent));
	       return 1;	    
	} 
	else {	    
               opbx_log(LOG_WARNING, "LOGOUT OK!  Agent '%s' logged out.\n", agentname);
	       return 0; 
	}
    return 1;
}
// --stop--

static struct opbx_channel *
my_opbx_get_channel_by_name_locked (char *channame)
{
  struct opbx_channel *chan;
  chan = opbx_channel_walk_locked (NULL);
  while (chan)
    {
      if (!strncasecmp (chan->name, channame, strlen (channame)))
	return chan;
      opbx_mutex_unlock (&chan->lock);
      chan = opbx_channel_walk_locked (chan);
    }
  return NULL;
}

int icd_jabber_hangup_channel (int argc, char *argv[])
{
   char *chan_name;
   struct opbx_channel *chan;

   if (argc != 2) {
       opbx_log(LOG_WARNING,"Function Hang up channel failed - bad number of parameters [%d]\n", argc);
       return 1;
    }
   chan_name = argv[1];
   chan = my_opbx_get_channel_by_name_locked(chan_name);
   if (chan == NULL) {
       opbx_log(LOG_WARNING,"Function Hang up channel failed - channel not found [%s]\n", chan_name);
       return 1;
   }
   opbx_mutex_unlock (&chan->lock);
   opbx_softhangup(chan ,  OPBX_SOFTHANGUP_EXPLICIT);
   return 0;
}


/*
params:
argv[0] = record
argv[1] = start or stop
argv[2] = customer token 
argv[3] = if start - directory & file name. %D -day, %M - minute, %S - second. To this failename wil be added
	  token and .WAV. Example: 
argv[3] = /tmp/%D/%m/  fliename is: /tmp/29/59/openpbx123123423423454.WAV	                 
*/

int
icd_jabber_record(int argc, char *argv[])
{
  icd_caller * customer;
  char rec_directory_buf[200];
  char rec_format_buf[50]="";
  char *rec_format;
  char buf[300];
  opbx_channel * chan;
  char * customer_source;
  int fd;
  struct tm *ptr;
  time_t tm;
  int record_start = -1;

  if (argc != 3  && argc != 4 ) {
       opbx_log (LOG_WARNING, "Function record bad no of parameters [%d]\n", argc);
       icd_jabber_send_message("RECORD FAILURE! - wrong parameters number.");
       return 1;
   }    
   
   if (!strcasecmp(argv[1], "start"))
        record_start = 1;
   if (!strcasecmp(argv[1],"stop"))
        record_start = 0;
   if (record_start == -1) {
       opbx_log (LOG_WARNING, "Function record first parameter [%s] start/stop allowed\n", argv[1]);
       icd_jabber_send_message("RECORD FAILURE! - first parameter [%s] start/stop allowed.", argv[1]);
       return 1;
   }    
	
   if (record_start) {
        strcpy(buf,"MuxMon start ");
   }
   else {
        strcpy(buf,"MuxMon stop ");
   }	 
   customer_source = argv[2]; 
   customer = (icd_caller *) icd_fieldset__get_value(customers, customer_source);
   if (customer == NULL) {
            opbx_log(LOG_WARNING, "Record FAILURE! Customer [%s] not found\n", customer_source);
            icd_jabber_send_message("RECORD FAILURE! - customer [%s] not found.", customer_source);
	    return 1;
   }
   chan = icd_caller__get_channel(customer);
   if (chan == NULL) {
            opbx_log(LOG_WARNING, "Record FAILURE! Channel for customer [%s] not found\n", customer_source);
            icd_jabber_send_message("RECORD FAILURE! - channel for customer  [%s] not found.", customer_source);
	    return 1;
   }
   if (chan->name == NULL) {
            opbx_log(LOG_WARNING, "Record FAILURE! Channel name for customer [%s] not found\n", customer_source);
            icd_jabber_send_message("RECORD FAILURE! - channel name for customer  [%s] not found.", customer_source);
	    return 1;
   }
   strncpy(buf + strlen(buf), chan->name, sizeof(buf) - strlen(buf));
   if (!record_start){
   	opbx_log(LOG_NOTICE, "Stop of recording for customer [%s] \n", customer_source);
        icd_jabber_send_message("RECORD STOP OK! - customer [%s].", customer_source);
        fd = fileno(stderr);
        opbx_cli_command(fd, buf);
	return 0;
   }
   strcpy(buf + strlen(buf), " ");
   strncpy(rec_directory_buf, argv[3],sizeof(rec_directory_buf)-1);
   if(rec_format=strchr(rec_directory_buf,'.')){
      *rec_format='\0';
      *rec_format++;
      rec_format_buf[0]='.';
      strncpy(rec_format_buf+1, rec_format, sizeof(rec_format_buf)-2);
   }   
   tm = time(NULL);
   ptr = localtime(&tm);
   strftime(buf + strlen(buf), sizeof(buf) - strlen(buf), rec_directory_buf, ptr);
   strncpy(buf + strlen(buf),  customer_source, sizeof(buf) - strlen(buf)-1);
   strncpy(buf + strlen(buf),  rec_format_buf, sizeof(buf) - strlen(buf)-1);
 
//   muxmon <start|stop> <chan_name> <args>opbx_cli_command(fd, command);fd can be like fileno(stderr)
   fd = fileno(stderr);
   opbx_cli_command(fd, buf);
   opbx_log(LOG_NOTICE, "Start of recording for customer [%s] \n", customer_source);
   icd_jabber_send_message("RECORD START OK! - customer [%s].", customer_source);

   return 0;
}
/*
params:
argv[0] = tr
argv[1] = customer (or agent?) id?
argv[2] = queue name to which customer will be transfered
argv[3] = agent id to which customer will be transfered, queue should be connected to matchagent distributor 
*/

int
icd_jabber_transfer (int argc, char *argv[])
{
  icd_caller *customer, *agent;
  char *customer_source;
  char *queue_destination;
  char *agent_id_destination;
  char *ident;
  icd_queue * queue;
  char key[30];  

  if (argc != 3 && argc != 4 ) {
       opbx_log (LOG_WARNING, "bad parameters\n");
       icd_jabber_send_message("TRANSFER FAILURE! - wrong parameters number.");
       return 1;
   }    
   customer_source = argv[1];
   queue_destination = argv[2];
   agent_id_destination = NULL;
   if (argc == 4){
      agent_id_destination = argv[3];
   }   
   customer = (icd_caller *) icd_fieldset__get_value(customers, customer_source);
   if (customer == NULL) {
            opbx_log(LOG_WARNING, "Transfer FAILURE! Customer [%s] not found\n", customer_source);
            icd_jabber_send_message("TRANSFER FAILURE! - customer [%s] not found", customer_source);
	    return 1;
   }
   queue = (icd_queue *) icd_fieldset__get_value(queues, queue_destination);
   if (queue == NULL) {
            opbx_log(LOG_WARNING, "Transfer FAILURE! Customer transfered to undefined Queue [%s]\n", queue_destination);
            icd_jabber_send_message("TRANSFER FAILURE! - customer [%s] transfer to undefined Queue [%s].",
	    customer_source, queue_destination);
	    return 1;
   }
   if (agent_id_destination != NULL){
/* prepare for matchagent distributor "idetifier" fields should be the same */    
       agent = (icd_caller *) icd_fieldset__get_value(agents, agent_id_destination);
       if (agent ==NULL) {
            opbx_log(LOG_WARNING, "Transfer FAILURE! Agent [%s] not found\n", agent_id_destination);
            icd_jabber_send_message("TRANSFER FAILURE! - customer [%s] transfer to undefined agent [%s].",
	    customer_source, agent_id_destination);
	    return 1;
       }
       ident = icd_caller__get_param(agent, "identifier");
       if (ident == NULL) {
           snprintf(key, 30, icd_caller__get_param(agent, "agent_id"));
           icd_caller__set_param_string(agent, "identifier", key);
           ident = icd_caller__get_param(agent, "identifier");
       }    
       icd_caller__set_param_string(customer, "identifier", ident);
   }    
   opbx_log (LOG_NOTICE, "Transfer customer [%s] to queue [%s]",
		 icd_caller__get_name (customer), queue_destination);
   icd_jabber_send_message("TRANSFER OK! - customer [%s] to queue [%s]",
			 icd_caller__get_name (customer), queue_destination);
   icd_caller__pause_caller_response(customer);
   if (icd_caller__get_state(customer) != ICD_CALLER_STATE_READY) {
      if (icd_caller__get_state(customer) == ICD_CALLER_STATE_GET_CHANNELS_AND_BRIDGE) {
          icd_caller__set_state_on_associations(customer, ICD_CALLER_STATE_CHANNEL_FAILED);	 
      }
      else{	  
        icd_caller__set_state_on_associations(customer, ICD_CALLER_STATE_CALL_END);	 
      }	
    }
    icd_caller__remove_all_associations(customer);
    icd_caller__remove_from_all_queues(customer);
    icd_caller__set_active_member(customer, NULL);
    icd_caller__add_to_queue(customer, queue);
    icd_caller__start_caller_response(customer);
    if(icd_caller__get_state(customer) != ICD_CALLER_STATE_READY)
         icd_caller__set_state(customer, ICD_CALLER_STATE_READY);
    icd_caller__return_to_distributors(customer);		 
//    icd_caller__set_state(customer, ICD_CALLER_STATE_BRIDGE_FAILED);	 
    
//    iter = icd_list__get_iterator((icd_list *) (customer->memberships));
//    while (icd_list_iterator__has_more(iter)) {
//        member = (icd_member *) icd_list_iterator__next(iter);
//        icd_queue__customer_join(icd_member__get_queue(member), member);
//    }
 //   destroy_icd_list_iterator(&iter);
//    icd_caller__add_to_queue(customer, queue);
    return 0;
}

struct fast_originate_helper
{
  char tech[256];
  char data[256];
  int timeout;
  char app[256];
  char appdata[256];
  char callerid[256];
  char variable[256];
  char account[256];
  char context[256];
  char exten[256];
  char cid_num[256];
  char cid_name[256];
  int priority;
};

const char *control_frame_state(int control_frame)
{
   switch (control_frame){
	case 0: return "TIMEOUT";
      	case  OPBX_CONTROL_HANGUP: return "HANGUP";
/*! Local ring */
	case  OPBX_CONTROL_RING	: return "RING";
/*! Remote end is ringing */
	case   OPBX_CONTROL_RINGING : return "RINGING";
/*! Remote end has answered */
	case  OPBX_CONTROL_ANSWER	: return "ANSWER";
/*! Remote end is busy */
	case  OPBX_CONTROL_BUSY	: return "BUSY";
/*! Make it go off hook */
	case  OPBX_CONTROL_TAKEOFFHOOK: return "TAKEOFFHOOK";
/*! Line is off hook */
	case  OPBX_CONTROL_OFFHOOK: return "OFFHOOK";
/*! Congestion (circuits busy) */
	case  OPBX_CONTROL_CONGESTION: return "CONGESTION";
/*! Flash hook */
	case  OPBX_CONTROL_FLASH	: return "FLASH";
/*! Wink */
	case  OPBX_CONTROL_WINK: return "WINK";
/*! Set a low-level option */
	case  OPBX_CONTROL_OPTION	: return "OPTION";
/*! Key Radio */
	case	 OPBX_CONTROL_RADIO_KEY	: return "RADIO_KEY";
/*! Un-Key Radio */
	case	 OPBX_CONTROL_RADIO_UNKEY	: return "RADIO_UNKEY";
/*! Indicate PROGRESS */
	case  OPBX_CONTROL_PROGRESS : return "PROGRESS";
/*! Indicate CALL PROCEEDING */
	case  OPBX_CONTROL_PROCEEDING: return "PROCEEDING";
/*! Indicate call is placed on hold */
	case  OPBX_CONTROL_HOLD	: return "HOLD";
/*! Indicate call is left from hold */
	case  OPBX_CONTROL_UNHOLD	: return "UNHOLD";
	default: return "UNKNOWN";
  }
  return "";
}

static void *
originate (void *arg)
{
  struct fast_originate_helper *in = arg;
  int reason = 0;
  struct opbx_channel *chan = NULL;
  int res;

  res = opbx_pbx_outgoing_exten (in->tech,  OPBX_FORMAT_SLINEAR, in->data, in->timeout,
			  in->context, in->exten, in->priority, 1, 
			  !opbx_strlen_zero (in->cid_num) ? in->cid_num : NULL,
			  !opbx_strlen_zero (in->callerid) ? in->callerid
			   : NULL, in->variable, in->account, &chan);
			  
  if(res){
     icd_jabber_send_message("Originate channel tech [%s] [%s] to extension [%s] in context [%s] FAILED reason[%d] [%s]", in->tech, in->data, in->exten, in->context, reason, control_frame_state(reason));
   }
   else{
     icd_jabber_send_message("Originate channel tech [%s] [%s] to extension [%s] in context [%s] OK, state [%s]", in->tech, in->data, in->exten, in->context, control_frame_state(reason));
   }
			  
  /* Locked by opbx_pbx_outgoing_exten or opbx_pbx_outgoing_app */
  if (chan)
    {
      opbx_mutex_unlock (&chan->lock);
    }
  free (in);
  return NULL;
}


/*
params:
argv[0] = or
argv[1] = channelname
argv[2] = extension@context if 2 agruments or context if 3 arguments 
argv[3] = extension if 3 arguments
*/


int
icd_jabber_originate (int argc, char *argv[])
{
  char *chan_name, *context, *exten, *tech, *data, *callerid, *account;
  int timeout = 60000; /*miliseconds */
  struct fast_originate_helper *in;
  pthread_t thread;
  pthread_attr_t attr;
  int result;

  if ((argc!= 3 ) && (argc !=4)){    
      opbx_log (LOG_WARNING, "bad parameters\n");
      icd_jabber_send_message("ORIGINATE FAILURE! - wrong parameters number");
  }    
  chan_name = opbx_strdupa (argv[1]);
  if (argc == 4){
    context = opbx_strdupa(argv[2]);
    exten = opbx_strdupa (argv[3]);
  }
  else{
    exten = opbx_strdupa (argv[2]);
    if (context = strchr (exten, '@')){
       *context = 0;
  	context++;
    }
    else{
    	context = opbx_strdupa("to_queue");
    }
  }
  tech = opbx_strdupa (chan_name);
  if (data = strchr (tech, '/'))
  {
      *data = '\0';
      data++;
   }
   else {
      opbx_log (LOG_WARNING, "ORIGINATE FAILURE! - Wrong dial srting [%s].\n", tech);
      icd_jabber_send_message("ORIGINATE FAILURE! - Wrong dial string [%s].", tech);
      return -1;
   }
   in = malloc (sizeof (struct fast_originate_helper));
   if (!in)
   {
      opbx_log (LOG_WARNING, "No Memory!\n");
      icd_jabber_send_message("ORIGINATE FAILURE! - no memory.");
      return -1;
    }
    memset (in, 0, sizeof (struct fast_originate_helper));

    callerid = NULL;
    account = NULL;

    strncpy (in->tech, tech, sizeof (in->tech));
    strncpy (in->data, data, sizeof (in->data));
    in->timeout = timeout;
    strncpy (in->context, context, sizeof (in->context));
    strncpy (in->exten, exten, sizeof (in->exten));
    in->priority = 1;
    opbx_log (LOG_WARNING, "Originating Call %s/%s %s %s %d\n", in->tech,
		   in->data, in->context, in->exten, in->priority);
    result = pthread_attr_init (&attr);
    pthread_attr_setschedpolicy (&attr, SCHED_RR);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    opbx_pthread_create (&thread, &attr, originate, in);
    pthread_attr_destroy (&attr);


  return 0;
}

// rejestracja polecenia
// --start--
void
icd_jabber_reg_func (char *v, void *f)
{
  if(!v || !f) return;
  
  if (tail != NULL)
    {
      tail->next = calloc (1, sizeof (struct lista));
      tail = tail->next;
    }
  else
    {
      tail = calloc (1, sizeof (struct lista));
      head = tail;
    }
   strncpy(tail->funct_name, v, JABBER_FUNCT_LEN_MAX);
   tail->funkcja = f;
   tail->next = NULL;

}

// --stop--

void *jabber_thread_funct(void * parsf)
{

  struct pars *p =  parsf;

  p->funct(p->argc, p->argv);

  free(p->args);
  free(p);
  return NULL;
}

// uruchomienie polecenia
// --start--
// This is not reeentrant - only one (jabber receiver therad) uses this
void *
icd_jabber_virtual_func (char *func)
{
  struct pars *p = malloc(sizeof(struct pars));
  struct lista *nodep;
  char *token;
  char *tmp = calloc (1, strlen (func)+1);
  pthread_t thread;

  p->args = tmp; 
  strcpy (tmp, func);
  token = strtok (tmp, " ");
  if (token)
      while (*token == ' ') token++;

  nodep = head;
  while (nodep && token)
    {
      if (strcmp (nodep->funct_name, token) == 0)
	{
	  p->argc = 0;
	  while ((token != NULL) && (p->argc < JABBER_FUNCT_ARGS_MAX) )
	    {
	      p->argv[p->argc] = token;
	      p->argc++;
	      if (p->argc >= JABBER_FUNCT_ARGS_MAX)
		break;
	      token = strtok (NULL, " ");
              if (token)
	         while (*token == ' ') token++;
	    }
          
        opbx_log (LOG_WARNING, "Call of function : [%s]\n", func);
        icd_jabber_send_message("Call of function [%s]", func);
// New thread to call the function	  
	  p->funct = nodep->funkcja ;
	  pthread_create( &thread, NULL, jabber_thread_funct, (void*) p);
	  return NULL;
	}
	nodep = nodep->next;
    }
    opbx_log (LOG_WARNING, "Not found jabber function [%s]\n", func);
    icd_jabber_send_message("Not found jabber function [%s]", func);
    return NULL;
}
// --stop--

int icd_jabber_fifo_read, icd_jabber_fifo_write;

GMainLoop *icd_jabber_main_loop;
LmConnection *icd_jabber_connection;
GError *icd_jabber_general_error = NULL;
LmMessage *icd_jabber_message;

pthread_t icd_jabber_threads[2];


void
icd_jabber_put_fifo (const char *str)
{
  if(!JabberOK) return;
  
  write (icd_jabber_fifo_write, str, MSG_SIZE);
  sem_post (&icd_jabber_fifo_semaphore);
}

char *
icd_jabber_get_fifo ()
{
  char *c;
  c = (char *) calloc (1, MSG_SIZE + 1);
//  struct timeval now;

  read (icd_jabber_fifo_read, c, MSG_SIZE);
//  gettimeofday(&now, NULL);
//  opbx_log (LOG_WARNING, "Get Jabber message [%ld:%6ld] [%s]\n", now.tv_sec, now.tv_usec, c);
//  sprintf(c + strlen(c), " [%ld:%6ld]",now.tv_sec, now.tv_usec);

  return c;
}

void
icd_jabber_fifo_start ()
{
  char fn[] = "/tmp/temp.fifo";

  mkfifo (fn, S_IRWXU);

  icd_jabber_fifo_read = open (fn, O_RDONLY | O_NONBLOCK);
  icd_jabber_fifo_write = open (fn, O_WRONLY);
}

static LmHandlerResult
icd_jabber_internal_handle_messages (LmMessageHandler * handler,
				     LmConnection * connection,
				     LmMessage * m, gpointer user_data)
{
  LmMessageNode *node;
  char *body;
 
  if (! JabberOK)
         return LM_HANDLER_RESULT_REMOVE_MESSAGE;
   
  node = lm_message_node_get_child (m->node, "body");
  body = node->value;
  opbx_log (LOG_WARNING, "ICD_Jabber: Incoming message from: %s - message: '%s'\n",
	   lm_message_node_get_attribute (m->node, "from"), body);
  icd_jabber_virtual_func (body);
 
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

int
icd_jabber_reconnect ()
{

  GError *error = NULL;
  LmMessage *m;

  if (lm_connection_open_and_block (icd_jabber_connection, &error)){
    if (lm_connection_authenticate_and_block (icd_jabber_connection, jabber_login, jabber_password,
					"AsterEvents", &error)){
      m =  lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_PRESENCE,
				  LM_MESSAGE_SUB_TYPE_AVAILABLE);
      if(lm_connection_send (icd_jabber_connection, m, &error)){
         lm_message_unref (m);
	 return 0;
      }	 
      lm_message_unref (m);
    }
  }   
  opbx_log(LOG_WARNING, "Jabber connection error [%s].\n", error->message);
  return 1;
}

void icd_jabber_connect (const char *server)
{
  LmMessageHandler *handler;

  icd_jabber_connection = lm_connection_new (server);
  lm_connection_set_port (icd_jabber_connection, 5222);

  handler =
    lm_message_handler_new (icd_jabber_internal_handle_messages, NULL, NULL);
  lm_connection_register_message_handler (icd_jabber_connection, handler,
					  LM_MESSAGE_TYPE_MESSAGE,
					  LM_HANDLER_PRIORITY_NORMAL);

  lm_message_handler_unref (handler);
  return;
}

void *
icd_jabber_messages ()
{
  icd_jabber_reg_func ("tr", icd_jabber_transfer);
  icd_jabber_reg_func ("ack", icd_jabber_ack_req);
  icd_jabber_reg_func ("login", icd_jabber_login_req);
  icd_jabber_reg_func ("or", icd_jabber_originate);
  icd_jabber_reg_func ("logout",icd_jabber_logout_req);
  icd_jabber_reg_func ("hangup",icd_jabber_hang_up);
  icd_jabber_reg_func ("record",icd_jabber_record);
  icd_jabber_reg_func ("hangup_chan",icd_jabber_hangup_channel);
  icd_jabber_main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (icd_jabber_main_loop);
  return NULL;
}

void opbx_channel_listen_events(struct opbx_channel *chan, const char *address)
{
    char buf[20];
    char *pos;
    
    if(address){
       if(!strcmp(address, "HANGUP")){
           icd_jabber_send_message("CHANNEL[%s] STATE[%s]", chan->name, address);
       }   
       else {
           strncpy(buf, chan->name, 19);
	   if (pos = strchr(buf, '/')) { 
	       *pos = '\0';
	   }    
           icd_jabber_send_message("CHANNEL CREATE NAME[%s] DIALSTRING[%s/%s]", chan->name, buf, address);
       }   
    }   
    else {
       icd_jabber_send_message("CHANNEL[%s] STATE[%s]", chan->name, opbx_state2str(chan->_state));      
    }
}; 

void *
icd_jabber_initialize ()
{
  char *icd_jabber_message_body;
  int conn_tries = 0;

  if (!jabber_server[0] || !jabber_login[0] || !jabber_password[0] || !jabber_send_address[0]){
     opbx_log(LOG_WARNING, "Jabber initialization error - not enough jabber info in icd.conf file.\n");
     JabberOK = 0; 
     return NULL;
  }
  icd_jabber_connect (jabber_server);
  if(icd_jabber_reconnect()){
     opbx_log(LOG_WARNING, "Jabber initialization error unable to connect to server.\n");
     JabberOK = 0; 
     return NULL;
   }      
      
  JabberOK = 1;
  icd_jabber_fifo_start ();
  
  opbx_pthread_create (&icd_jabber_threads[1], NULL, icd_jabber_messages,
		      NULL);
//  opbx_channel_register_listen_events(opbx_channel_listen_events);
  for (;;)
    {
      sem_wait (&icd_jabber_fifo_semaphore);
      icd_jabber_message_body = icd_jabber_get_fifo ();
      if (icd_jabber_message_body != NULL)
	{
	  icd_jabber_message =
	    lm_message_new (jabber_send_address, LM_MESSAGE_TYPE_MESSAGE);
	    lm_message_node_add_child (icd_jabber_message->node, "body",
				     icd_jabber_message_body);
//    for(conn_tries=0;conn_tries <20; conn_tries++){
//    	 lm_connection_send(icd_jabber_connection, icd_jabber_message,&icd_jabber_general_error);
//    } 	 
	    conn_tries = 0;			     
	    while (((!lm_connection_send
		   (icd_jabber_connection, icd_jabber_message,
		    &icd_jabber_general_error)) && (conn_tries <= 5)))
	    {
	      icd_jabber_reconnect();
	      conn_tries++;
	    }    
	    lm_message_unref (icd_jabber_message);
	    if (conn_tries > 5) {
                 opbx_log(LOG_WARNING, "Jabber message not sent '%s'.\n", icd_jabber_message_body);
	    }	 
	    free(icd_jabber_message_body);
	}
    }
}

void 
icd_jabber_clear ()
{
   JabberOK =0;
//   opbx_channel_register_listen_events(NULL);
   g_main_loop_quit (icd_jabber_main_loop);
   lm_connection_close (icd_jabber_connection, NULL);
   lm_connection_unref(icd_jabber_connection);
  
} 
void icd_jabber_send_message( char *format, ...)
{
   va_list args;
   char message[1024];
   
   va_start(args, format);
   vsnprintf(message, 1024, format, args);
   va_end(args);	
   icd_jabber_put_fifo(message);
}   


