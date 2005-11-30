#include <semaphore.h>
#include <loudmouth/loudmouth.h>
#include <pthread.h>

extern pthread_t icd_jabber_threads[2];

extern void *icd_jabber_initialize();

//extern void icd_jabber_put_fifo(const char *val);
                                                                        
//extern char *icd_jabber_get_fifo();

//extern void icd_jabber_fifo_start();
void icd_jabber_send_message( char *format, ...);
extern sem_t icd_jabber_fifo_semaphore;

// extern LmConnection *conn;

void icd_jabber_clear ();
//char *jabber_server = "taansoftworks.com";
extern char jabber_server[100];
extern char jabber_login[100];
extern char jabber_password[100];
extern char jabber_send_address[100];



