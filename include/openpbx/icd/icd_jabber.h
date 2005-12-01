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



