#define _REENTRANT

extern pthread_t thrdid[2];

extern void *icd_jabber_initialize();

extern void put_fifo(char *val);
                                                                        
extern char *get_fifo();

extern void start_fifo();

extern sem_t fifo_semaphore;

// extern LmConnection *conn;

extern LmConnection *jabber_connect(char *server); 

