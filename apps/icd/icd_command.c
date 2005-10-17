/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <app_icd.h>
#include <icd_command.h>
#include <icd_common.h>
#include <icd_fieldset.h>
/* For dump function only */
#include <icd_queue.h>
#include <icd_distributor.h>
#include <icd_list.h>
#include <icd_caller.h>
#include <icd_member.h>
#include <icd_member_list.h>

static int verbosity = 1;

/*
static char show_icd_help[] =
"Usage: icd command <command>\n"
"       run a particular icd command.\n";
*/

static void_hash_table *COMMAND_HASH;
static icd_status icd_command_show_queue(int fd, int argc, char **argv);
static icd_status icd_command_show_agent(int fd, int argc, char **argv);
static icd_status icd_command_dump_queue(int fd, int argc, char **argv);
static icd_status icd_command_dump_distributor(int fd, int argc, char **argv);
static icd_status icd_command_dump_agent(int fd, int argc, char **argv);
static icd_status icd_command_dump_customer(int fd, int argc, char **argv);
static icd_status icd_command_load_queues(int fd, int argc, char **argv);
static icd_status icd_command_load_agents(int fd, int argc, char **argv);
static icd_status icd_command_load_conferences(int fd, int argc, char **argv);
static icd_status icd_command_load_app_icd(int fd, int argc, char **argv);

typedef struct icd_command_node icd_command_node;

struct icd_command_node {
    int (*func) (int, int, char **);
    char name[ICD_STRING_LEN];
    char short_help[ICD_STRING_LEN];
    char syntax_help[ICD_STRING_LEN];
    char long_help[ICD_STRING_LEN];
    icd_memory *memory;
};

void create_command_hash(void)
{
    COMMAND_HASH = (void_hash_table *) vh_init("Command Hash");
    /* some default given commands 
     * "name" , "func pointer" , "short description" , "syntax usage text" , "long specific help message."
     */
    icd_command_register("help", icd_command_help, "help with the icd command system", "[topic]",
        "Enter a specific command for detailed help\nor no arguement for a full listing of avaliable commands");
    /* blank values will allow this one to not show up in the listing. */
    icd_command_register("_bad_command", icd_command_bad, "", "", "");

    icd_command_register("verbose", icd_command_verbose, "set verbosity of icd command output", "Level [1..9]",
        "Set a value of 1 to 9");

    icd_command_register("debug", icd_command_debug, "set debug icd debug output", "icd debug [on|off]",
        "control of debug dumps reading config files and other process messages during icd call routing");

    icd_command_register("show", icd_command_show, "show agent or queue infomation",
        "<object type> [specific object]", "Available object types:\n\nqueue, agent");

    icd_command_register("dump", icd_command_dump, "dump internal information", "<object type> [specific object]",
        "Available object types:\n\nqueue,distributor,caller, agent, customer");

    icd_command_register("load", icd_command_load, "reload icd queues and agents from config files ",
        "<agents|queues>", "Load new configuration data from the icd config files");

}

static icd_command_node *create_command_node(int (*func) (int, int, char **), char *name, char *short_help,
    char *syntax_help, char *long_help)
{
    icd_command_node *new;

    ICD_MALLOC(new, sizeof(icd_command_node));
    new->func = func;
    strncpy(new->name, name, sizeof(new->name));
    strncpy(new->short_help, short_help, sizeof(new->short_help));
    strncpy(new->syntax_help, syntax_help, sizeof(new->syntax_help));
    strncpy(new->long_help, long_help, sizeof(new->long_help));

    return new;
}

static void destroy_command_node(icd_command_node ** node)
{
    if (*node == NULL)
        return;
    ICD_FREE((*node));
}

static int cli_line(int fd, char *c, int y)
{
    int x = 0;

    for (x = 0; x < y; x++)
        opbx_cli(fd, "%s", c);
    opbx_cli(fd, "\n");
    /* BCA - What should this return? */
    return ICD_SUCCESS;
}

int icd_command_register(char *name, int (*func) (int, int, char **), char *short_help, char *syntax_help,
    char *long_help)
{
    icd_command_node *insert = NULL;

    if (!COMMAND_HASH)
        create_command_hash();

    insert = (icd_command_node *) create_command_node(func, name, short_help, syntax_help, long_help);
    if (vh_write(COMMAND_HASH, name, insert) != -1) {
        return ICD_SUCCESS;
    }
    return ICD_EGENERAL;
}

void *icd_command_pointer(char *name)
{
    icd_command_node *fetch = NULL;

    fetch = (icd_command_node *) vh_read(COMMAND_HASH, name);
    if (fetch)
        return fetch->func;
    else
        return NULL;
}

void destroy_command_hash(void)
{
    icd_command_node *fetch;
    vh_keylist *keys;

    for (keys = vh_keys((COMMAND_HASH)); keys; keys = keys->next) {
        fetch = (icd_command_node *) vh_read(COMMAND_HASH, keys->name);
        vh_delete(COMMAND_HASH, keys->name);
        destroy_command_node(&fetch);
        fetch = NULL;
    }
    vh_destroy(&COMMAND_HASH);
}

int icd_command_cli(int fd, int argc, char **argv)
{
    int (*func) (int, int, char **);
    char **newargv;
    int newargc;
    int x = 0, y = 0;
    int mem = 0;

    func = NULL;

    if (argc > 1) {
        func = icd_command_pointer(argv[1]);
        if (func == NULL)
            func = icd_command_pointer("_bad_command");
    } else
        func = icd_command_pointer("help");

    if (func != NULL) {
        for (x = 1; x < argc; x++) {
            mem += (strlen(argv[x]) + 1);
        }
        newargv = calloc(mem, 1);

        for (x = 1; x < argc; x++) {
            newargv[y] = malloc(strlen(argv[x]) + 1);
            strncpy(newargv[y], argv[x], strlen(argv[x]) + 1);
            y++;
        }

        newargc = argc - 1;
        func(fd, newargc, newargv);
        y = 0;
        for (x = 2; x < argc; x++) {
            free(newargv[y++]);
        }
        free(newargv);
    } else
        opbx_cli(fd, "Mega Error %d\n", argc);

    return ICD_SUCCESS;
}

static int icd_command_short_help(int fd, icd_command_node * node)
{
    opbx_cli(fd, "'%s'", node->short_help);

    return ICD_SUCCESS;
}

static int icd_command_syntax_help(int fd, icd_command_node * node)
{
    opbx_cli(fd, "Usage: %s %s", node->name, node->syntax_help);

    return ICD_SUCCESS;
}

static int icd_command_long_help(int fd, icd_command_node * node)
{
    opbx_cli(fd, "%s", node->long_help);

    return ICD_SUCCESS;
}

/* all our commands */
int icd_command_list(int fd, int argc, char **argv)
{
    icd_command_node *fetch;
    vh_keylist *keys;

    if (argc < 2) {
        opbx_cli(fd, "\n\n");
        opbx_cli(fd, "Available Commands\n");
        cli_line(fd, "=", 80);
        opbx_cli(fd, "\n");

        for (keys = vh_keys((COMMAND_HASH)); keys; keys = keys->next) {

            fetch = (icd_command_node *) vh_read(COMMAND_HASH, keys->name);
            if (fetch && strcmp(fetch->short_help, "")) {
                opbx_cli(fd, "%s: ", fetch->name);
                icd_command_short_help(fd, fetch);
                opbx_cli(fd, "\n");
            }
        }

        opbx_cli(fd, "\n");
        cli_line(fd, "=", 80);
        opbx_cli(fd, "\n");

        return ICD_SUCCESS;
    }
    // else 

    fetch = (icd_command_node *) vh_read(COMMAND_HASH, argv[1]);
    if (fetch) {

        opbx_cli(fd, "\n\n");
        opbx_cli(fd, "Help with '%s'\n", fetch->name);
        cli_line(fd, "=", 80);
        opbx_cli(fd, "\n");

        opbx_cli(fd, "%s: ", fetch->name);
        icd_command_short_help(fd, fetch);
        opbx_cli(fd, "\n");
        icd_command_syntax_help(fd, fetch);
        opbx_cli(fd, "\n");
        opbx_cli(fd, "\n");
        icd_command_long_help(fd, fetch);
        opbx_cli(fd, "\n");
        opbx_cli(fd, "\n");
        cli_line(fd, "=", 80);
        opbx_cli(fd, "\n");

    }

    return ICD_SUCCESS;
}

int icd_command_help(int fd, int argc, char **argv)
{
    icd_command_list(fd, argc, argv);
    opbx_cli(fd, "\nUsage 'icd <command> <arg1> .. <argn>\n");

    /* BCA - What should this return? */
    return ICD_SUCCESS;
}

int icd_command_bad(int fd, int argc, char **argv)
{
    int x;

    for (x = 0; x < argc; x++)
        opbx_cli(fd, "%d=%s\n", x, argv[x]);

    opbx_cli(fd, "\n\nInvalid Command\n");
    icd_command_help(fd, argc, argv);

    /* BCA - What should this return? */
    return ICD_SUCCESS;
}

int icd_command_verbose(int fd, int argc, char **argv)
{

    if (argc == 2) {
        if (!strcmp(argv[1], "ast")) {
            icd_verbose = option_verbose;
            return ICD_SUCCESS;
        }
        icd_verbose = atoi(argv[1]);
        if (icd_verbose > 0 && icd_verbose < 10)
            opbx_cli(fd, "ICD Verbosity[%d] set \n", icd_verbose);
        else
            opbx_cli(fd, "ICD Verbosity[%d] range is 1-9 not [%s] \n", icd_verbose, argv[1]);
    } else
        opbx_cli(fd, "ICD Verbosity[%d] range is 1-9 not [%s] \n", icd_verbose, argv[1]);

    return ICD_SUCCESS;
}

int icd_command_debug(int fd, int argc, char **argv)
{

    if (argc == 2) {
        if (!strcmp(argv[1], "on"))
            icd_debug = 1;
        else if (!strcmp(argv[1], "off"))
            icd_debug = 0;
        else
            opbx_cli(fd, "ICD debug[%d] must be either [on] or [off] not[%s]\n", icd_debug, argv[1]);
    } else
        opbx_cli(fd, "ICD debug[%d] must be either [on] or [off] not [%s]\n", icd_debug, argv[1]);

    return ICD_SUCCESS;
}

int icd_command_show(int fd, int argc, char **argv)
{
    static char *help[2] = { "help", "show" };

    if (argc >= 2) {
        if (!strcmp(argv[1], "q") || !strcmp(argv[1], "queue") || !strcmp(argv[1], "queues")) {
            icd_command_show_queue(fd, argc, argv);
        }

        if (!strcmp(argv[1], "a") || !strcmp(argv[1], "agent") || !strcmp(argv[1], "agents")) {
            icd_command_show_agent(fd, argc, argv);
        }
    } else
        icd_command_help(fd, 2, help);

    return ICD_SUCCESS;
}

icd_status icd_command_show_queue(int fd, int argc, char **argv)
{
//QUEUE UNATTENDED CALLS        ASSIGNED/THIS QUEUE/OTHER QUEUE
#define FMT_QUEUE_HEADING "%7s %-12s %-7s %-10s %12s %-14s\n"

    icd_fieldset_iterator *iter;
    char *curr_key;
    icd_queue *queue;

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, FMT_QUEUE_HEADING, "QUEUE", "UNATTENDED", "CALLS", "ASSIGNED", "THIS QUEUE", "OTHER QUEUES");

    iter = icd_fieldset__get_key_iterator(queues);
    while (icd_fieldset_iterator__has_more(iter)) {
        curr_key = icd_fieldset_iterator__next(iter);
        if (argc == 2 || (!strcmp(curr_key, argv[2]))) {
            queue = (icd_queue *) icd_fieldset__get_value(queues, curr_key);
            icd_queue__show(queue, verbosity, fd);
            if (argc != 2)
                break;
        }
    }
    destroy_icd_fieldset_iterator(&iter);

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

/* Create a cli ui display of the agent */
icd_status icd_command_show_agent(int fd, int argc, char **argv)
{
#define FMT_AGENT_HEADING "%10s %-3s %-15s %-20s %20s %-10s  %-5s\n"
#define FMT_AGENT_DATA1   "%10s %-3d %-15s %-20s %20s %-10s  %-5s\n"
#define FMT_AGENT_DATA2   "%s:%d:%s:%s:%s:%s:%s:\n"

    char *curr_key;
    struct opbx_channel *chan = NULL;
    icd_agent *agent = NULL;
    icd_caller *caller = NULL;
    icd_caller *associate = NULL;
    icd_fieldset_iterator *iter;
    icd_list_iterator *list_iter;
    char buf[256];

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, FMT_AGENT_HEADING, "GROUP", "ID", "NAME", "CHANNEL", "TALKING", "QUEUE", "LISTEN CODE");

    iter = icd_fieldset__get_key_iterator(agents);
    while (icd_fieldset_iterator__has_more(iter)) {
        curr_key = icd_fieldset_iterator__next(iter);
        agent = icd_fieldset__get_value(agents, curr_key);
        caller = (icd_caller *) agent;
        buf[0] = '\0';

        /* lets find all the channels they are talking to */
        if (icd_caller__get_state(caller) == ICD_CALLER_STATE_BRIDGED || icd_caller__get_state(caller) == ICD_CALLER_STATE_CONFERENCED) {
            list_iter = icd_list__get_iterator((icd_list *) (icd_caller__get_associations(caller)));
            while (icd_list_iterator__has_more(list_iter)) {
                associate = (icd_caller *) icd_list_iterator__next(list_iter);

                if (icd_caller__get_state(associate) == ICD_CALLER_STATE_BRIDGED || icd_caller__get_state(caller) == ICD_CALLER_STATE_CONFERENCED) {
                    chan = icd_caller__get_channel(associate);
                  
                    if ((sizeof(buf) - strlen(buf) - strlen(chan->name) - 1) > 0)
                        strcat(buf, chan->name);
/*
                if ((sizeof(buf) - strlen(buf) -strlen(chan->callerid ? chan->callerid : "unknown") -1) > 0 )
                    strcat(buf,chan->callerid ? chan->callerid : "unknown");                
*/
                }
            }
            destroy_icd_list_iterator(&list_iter);

        }

        opbx_cli(fd, "%10s  %-3d %-15s %-20s %-20s", (char *) icd_caller__get_param(caller, "group"),
            icd_caller__get_id(caller), icd_caller__get_name(caller),
            icd_caller__get_channel(caller) ? icd_caller__get_channel(caller)->name : "(None)", buf);

        opbx_cli(fd, "\n");
    }

    destroy_icd_fieldset_iterator(&iter);

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;

}

int icd_command_dump(int fd, int argc, char **argv)
{
    static char *help[2] = { "help", "dump" };

    if (argc >= 2) {
        if (!strcmp(argv[1], "q") || !strcmp(argv[1], "queue") || !strcmp(argv[1], "queues")) {
            icd_command_dump_queue(fd, argc, argv);
        }
        if (!strcmp(argv[1], "d") || !strcmp(argv[1], "dist") || !strcmp(argv[1], "distributors")) {
            icd_command_dump_distributor(fd, argc, argv);
        }
        if (!strcmp(argv[1], "caller") || !strcmp(argv[1], "callers")) {
            icd_command_dump_customer(fd, argc, argv);
            icd_command_dump_agent(fd, argc, argv);
        }
        if (!strcmp(argv[1], "a") || !strcmp(argv[1], "agent") || !strcmp(argv[1], "agents")) {
            icd_command_dump_agent(fd, argc, argv);
        }
        if (!strcmp(argv[1], "c") || !strcmp(argv[1], "customer") || !strcmp(argv[1], "customers")) {
            icd_command_dump_customer(fd, argc, argv);
        }
    } else
        icd_command_help(fd, 2, help);

    /* BCA - What should this return? */
    return ICD_SUCCESS;
}

static icd_status icd_command_dump_queue(int fd, int argc, char **argv)
{
    icd_fieldset_iterator *iter;
    char *curr_key;
    icd_queue *queue;
    icd_distributor *dist;

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "Queue Dump \n");

    iter = icd_fieldset__get_key_iterator(queues);
    while (icd_fieldset_iterator__has_more(iter)) {
        curr_key = icd_fieldset_iterator__next(iter);
        if (argc == 2 || (!strcmp(curr_key, argv[2]))) {
            opbx_cli(fd, "\nFound %s\n", curr_key);
            queue = (icd_queue *) icd_fieldset__get_value(queues, curr_key);
            icd_queue__dump(queue, verbosity, fd);
            dist = (icd_distributor *) icd_queue__get_distributor(queue);
            /*
               if (dist)
               icd_distributor__dump(dist, verbosity, fd);
             */
            if (argc != 2)
                break;
        }
    }
    destroy_icd_fieldset_iterator(&iter);

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

static icd_status icd_command_dump_distributor(int fd, int argc, char **argv)
{
/*
    icd_distributor *dist;

     opbx_cli(fd,"\n");
     cli_line(fd,"=",80);
     opbx_cli(fd,"\n");

 
     opbx_cli(fd,"\n");
     cli_line(fd,"=",80);
     opbx_cli(fd,"\n");
*/
    return ICD_SUCCESS;
}

static icd_status icd_command_dump_customer(int fd, int argc, char **argv)
{
    icd_fieldset_iterator *fs_iter;
    char *curr_key;
    icd_queue *queue;
    icd_member_list *customers;

    icd_list_iterator *iter;
    icd_member *member;
    icd_caller *caller;

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "Customer Dump \n");

    fs_iter = icd_fieldset__get_key_iterator(queues);
    if (fs_iter == NULL) {
        return 0;
    }
    while (icd_fieldset_iterator__has_more(fs_iter)) {
        curr_key = icd_fieldset_iterator__next(fs_iter);
        opbx_cli(fd, "\nCustomers in Queue %s\n", curr_key);
        queue = icd_fieldset__get_value(queues, curr_key);
        customers = (icd_member_list *) icd_queue__get_customers(queue);

        if (verbosity > 1) {
            iter = icd_list__get_iterator((icd_list *) (customers));
            if (iter != NULL) {
                while (icd_list_iterator__has_more(iter)) {
                    member = (icd_member *) icd_list_iterator__next(iter);
                    caller = (icd_caller *) icd_member__get_caller(member);
                    if (caller) {
                        //caller->dump_fn_extra
                        icd_caller__dump(caller, verbosity, fd);
                    }
                    destroy_icd_list_iterator(&iter);
                }
            }
        } else {
            icd_member_list__dump(customers, verbosity, fd);
        }

    }
    destroy_icd_fieldset_iterator(&fs_iter);

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

static icd_status icd_command_dump_agent(int fd, int argc, char **argv)
{
    icd_fieldset_iterator *iter;
    char *curr_key;
    icd_queue *queue;
    icd_member_list *agents;

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "Agent Dump \n");

    iter = icd_fieldset__get_key_iterator(queues);
    while (icd_fieldset_iterator__has_more(iter)) {
        curr_key = icd_fieldset_iterator__next(iter);
        opbx_cli(fd, "\nAgents in Queue %s\n", curr_key);
        queue = icd_fieldset__get_value(queues, curr_key);
        agents = (icd_member_list *) icd_queue__get_agents(queue);
        icd_member_list__dump(agents, verbosity, fd);
    }
    destroy_icd_fieldset_iterator(&iter);

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

/*
static icd_status icd_command_dump__agent(int fd, int argc, char **argv) {

    icd_queue *queue;
    icd_caller *caller;

     opbx_cli(fd,"\n");
     cli_line(fd,"=",80);
     opbx_cli(fd,"\n");
     opbx_cli(fd,"Agent Dump \n",);

     iter = icd_fieldset__get_key_iterator(agents);
     while (icd_fieldset_iterator__has_more(iter)) {
       curr_key = icd_fieldset_iterator__next(iter);
       opbx_cli(fd,"\nFound %s\n",curr_key);
       agent = icd_fieldset__get_value(agents, curr_key) ;
     }
     destroy_icd_fieldset_iterator(&iter);

     opbx_cli(fd,"\n");
     cli_line(fd,"=",80);
     opbx_cli(fd,"\n");

    return ICD_SUCCESS;
}
*/
int icd_command_load(int fd, int argc, char **argv)
{
    static char *help[2] = { "help", "load" };

    if (argc >= 2) {
        if (!strcmp(argv[1], "i") || !strcmp(argv[1], "icd")) {
            icd_command_load_app_icd(fd, argc, argv);
        }
        if (!strcmp(argv[1], "q") || !strcmp(argv[1], "queue") || !strcmp(argv[1], "queues")) {
            icd_command_load_queues(fd, argc, argv);
        }
        if (!strcmp(argv[1], "a") || !strcmp(argv[1], "agent") || !strcmp(argv[1], "agents")) {
            icd_command_load_agents(fd, argc, argv);
        }
        if (!strcmp(argv[1], "c") || !strcmp(argv[1], "conference") || !strcmp(argv[1], "conferences")) {
            icd_command_load_conferences(fd, argc, argv);
        }
    } else
        icd_command_help(fd, 2, help);

    return ICD_SUCCESS;
}

icd_status icd_command_load_app_icd(int fd, int argc, char **argv)
{
    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "APP_ICD Reload \n");

    reload_app_icd(APP_ICD);    /*implemenation in app_icd.c */

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

icd_status icd_command_load_conferences(int fd, int argc, char **argv)
{
    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "Conferences Reload \n");

    reload_app_icd(ICD_CONFERENCE);     /*implemenation in app_icd.c */

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

icd_status icd_command_load_agents(int fd, int argc, char **argv)
{
    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "Agents Reload \n");

    reload_app_icd(ICD_AGENT);  /*implemenation in app_icd.c */

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

    return ICD_SUCCESS;
}

icd_status icd_command_load_queues(int fd, int argc, char **argv)
{

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");
    opbx_cli(fd, "Queue Reload \n");

    reload_app_icd(ICD_QUEUE);  /*implemenation in app_icd.c */

    opbx_cli(fd, "\n");
    cli_line(fd, "=", 80);
    opbx_cli(fd, "\n");

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

