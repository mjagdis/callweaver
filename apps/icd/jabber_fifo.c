//#define _REENTRANT
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <loudmouth/loudmouth.h>
#include <pthread.h>
#include "openpbx/icd/icd_common.h"
#include <malloc.h>
#include <semaphore.h>

#define MSG_SIZE 2048

void *jabber_messages();

// potrzebne do parsowania polecen
// --start--
typedef *func_ptr (int, char *[]);

struct pars
{
  int argc;
  char *argv[];
};

struct pars *p = NULL;
struct pars *p_tmp = NULL;

struct lista
{
  char *nazwa_funkcji;
  func_ptr *funkcja;
  struct pars *p;
  struct lista *next;
};

struct lista *head = NULL;
struct lista *tail = NULL;
// --stop--

// dwie funkcje testowe
// --start--
int
test (int argc, char *argv[])
{
  int i;
    opbx_log(LOG_WARNING, "parameters count: %i, function name: %s\n", argc, argv[0]);
  for (i = 1; i < argc; i++)
      opbx_log(LOG_WARNING, "parametr %i = '%s'\n", i, argv[i]);
  return 0;
}

int
test2 (int argc, char *argv[])
{
  int i;
    opbx_log(LOG_WARNING, "wlasnie wywolales funkcje test2\n");
  for (i = 1; i < argc; i++)
    opbx_log(LOG_WARNING, "prametr %i = '%s'\n", i, argv[i]);
  return 1;
}
// --stop--

// rejestracja polecenia
// --start--
void
reg_func (char *v, void *f)
{
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
  if (v != NULL)
   {
     tail->nazwa_funkcji = strdup (v);
   }
  else
     tail->nazwa_funkcji = strdup("");

  tail->funkcja = f;
  tail->next = NULL;

}
// --stop--

// uruchomienie polecenia
// --start--
void
run_func (char *func)
{
  p = calloc (1024, sizeof (struct pars));
  struct lista *tmp3;
  char *s;
  char *tmp = calloc (1, strlen (func));
  char *str1;
  char *str2;
  char *str_tmp;
  int v, x, y = 0;

  sprintf (tmp, "%s", func);
  s = strtok (func, " ");
  str1 = strtok (tmp, " ");

  p->argc = 0;
  struct lista *tmp2 = head;
  if (tmp2 == NULL)
    return;
  while (tmp2->next != NULL)
    {
      if (strcmp (tmp2->nazwa_funkcji, s) == 0)
        {
          p->argv[p->argc] = strdup (str1);
          p->argc++;
          while (1)
            {
              str1 = strtok (NULL, " ");
              if (str1 == NULL)
                break;
              p->argv[p->argc] = strdup (str1);
              p->argc++;
            }
          break;
        }
      tmp2 = tmp2->next;
    }
  tmp3 = head;
  if (tmp3 == NULL)
    return;

  while (tmp3->next != NULL)
    {
	opbx_log(LOG_WARNING, "funkcja: %s - szukana %s\n", tmp3->nazwa_funkcji, s);
      if (strcmp (tmp3->nazwa_funkcji, s) == 0)
        {
          tmp3->funkcja (p->argc, p->argv);
          return;
        }
      tmp3 = tmp3->next;
    }
  opbx_log(LOG_WARNING, "unknown funcion name\n");
}
// --stop--

int fifo_read, fifo_write;

GMainLoop    *main_loop;
LmConnection *conn;
GError       *error = NULL;
LmMessage    *m;

pthread_t thrdid[2];

sem_t fifo_semaphore;
sem_t fifo_command_semaphore;

void put_fifo(char *str)
{
    write(fifo_write, str, MSG_SIZE);
    sem_post(&fifo_semaphore);
}
    
char *get_fifo()
{
    char *c;
    c = (char*)calloc(1, MSG_SIZE);

    read(fifo_read, c, MSG_SIZE);

    return c;
}
				
void start_fifo()
{
    char fn[]="temp.fifo";
	
    mkfifo(fn, S_IRWXU);

    fifo_read = open(fn, O_RDONLY|O_NONBLOCK);
    fifo_write = open(fn, O_WRONLY);
}

static LmHandlerResult
handle_messages (LmMessageHandler *handler,
                 LmConnection     *connection,
                 LmMessage        *m,
                 gpointer          user_data)
{
	char *s;
	LmMessageNode *node;
	char *body;

	s = (char*)calloc(1, 2048);
//	sprintf(s, "\nIncoming message from: %s - message: '%s'\n",lm_message_node_get_attribute (m->node, "from"), lm_message_node_to_string (m->node));
	node = lm_message_node_get_child(m->node, "body");
	body = node->value;
      sprintf(s, "\nIncoming message from: %s - message: '%s'\n",lm_message_node_get_attribute (m->node, "from"), body);
        opbx_log(LOG_WARNING, s);
        run_func(body);

return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmConnection *jabber_connect(char *server)
{

  LmConnection *conn;
  GError       *error = NULL;
  LmMessage    *m;

  LmMessageHandler *handler;

  conn = lm_connection_new (server);

  handler = lm_message_handler_new (handle_messages, NULL, NULL);
  lm_connection_register_message_handler (conn, handler,
                                                LM_MESSAGE_TYPE_MESSAGE,
                                                LM_HANDLER_PRIORITY_NORMAL);

  lm_message_handler_unref (handler);

  lm_connection_open_and_block (conn, &error);

  lm_connection_authenticate_and_block (conn, "intern", "dupablada","Events", &error);
  m = lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_PRESENCE, LM_MESSAGE_SUB_TYPE_AVAILABLE);
  lm_connection_send (conn, m, &error);
  lm_message_unref (m);

  return conn;
}

void *jabber_messages()
{
// tu musimy odpalic watek odpowiedzialny za parsowanie funkcji
  reg_func ("test", test);
  reg_func ("test2", test2);
  reg_func (NULL, NULL);

        main_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (main_loop);
}

void check_jabber()
{

  char *s;
  int conn_tries = 0; 

  conn = jabber_connect("aster.ultimo.masq");

//  pthread_create(&thrdid[1], NULL, jabber_messages, NULL);

  for(;;)
  { 
        sem_wait(&fifo_semaphore);
        s = get_fifo();
        if (s != NULL) {
           m = lm_message_new ("events@aster.ultimo.masq", LM_MESSAGE_TYPE_MESSAGE);
           lm_message_node_add_child (m->node, "body", s);
           while(((!lm_connection_send (conn, m, &error)) && (conn_tries != 5)))
	   {
		conn = jabber_connect("aster.ultimo.masq");
		conn_tries++;
	   }
           lm_message_unref (m);
        }
  }
}

