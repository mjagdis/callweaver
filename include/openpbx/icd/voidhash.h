/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef _VOIDHASH_H
#define _VOIDHASH_H
#include "openpbx/icd/icd_types.h"
#include <sys/types.h>

#define VOID_HASH_TABLE_KEY_SIZE 255

typedef struct void_hash_table void_hash_table;

struct vh_keylist {
    char name[100];
    icd_memory *memory;
    struct vh_keylist *next;
};

struct void_hash_table {
    char name[100];
    struct hash_storage *data[VOID_HASH_TABLE_KEY_SIZE];
    struct void_hash_table *next;
    icd_memory *memory;
    int allocated;
};

struct hash_storage {
    char *var;
    void *val;
    int allocated;
    icd_memory *memory;
    struct hash_storage *next;
};

typedef struct hash_storage hash_storage;
typedef struct vh_keylist vh_keylist;

unsigned long VH_ElfHash(const unsigned char *name);

hash_storage *vh_init_hash_storage(void);

void_hash_table *vh_init(char *);
int vh_write(void_hash_table * hash, char *name, void *value);
int vh_write_store(void_hash_table * hash, hash_storage * store);
int vh_write_cp_string(void_hash_table * hash, char *key, char *string);
void *vh_read(void_hash_table * hash, char *name);
int vh_delete(void_hash_table * hash, char *name);
int vh_split_and_add(void_hash_table * hash, char *pair);
void vh_print(void_hash_table * hash);
char *vh_trim_spaces(char *var);
int vh_carve_data(void_hash_table * hash, char *data, char delim);
int strncpy_if_exists(void_hash_table * hash, char *key, char *cpy_to, size_t size);
int atoi_if_exists(void_hash_table * hash, char *key, int dft);
char *vh_get_strdup(void_hash_table * hash, char *key, char *deflt_str);
int vh_destroy(void_hash_table ** hash);
vh_keylist *vh_keylist_init(void);
void vh_keylist_destroy(vh_keylist ** nuke);
vh_keylist *vh_keys(void_hash_table * hash);
int vh_cp_string(void_hash_table * hash, char *key, char *string);
int vh_carve_data_perm(void_hash_table * hash, char *data, char delim);
void vh_merge(void_hash_table * hash, void_hash_table * new);
void vh_merge_if_undef(void_hash_table * hash, void_hash_table * new);
void vh_destroy_hash_storage(hash_storage ** nuke);
int split_and_add(void_hash_table * hash, char *pair);
void_hash_table *vh_clone(void_hash_table * hash);
#endif

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */

