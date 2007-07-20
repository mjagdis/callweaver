/*
 * An implementation of hash tables based on a section of TCL which is:
 *
 * Copyright (c) 1991-1993 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

/*! \file */

#ifndef _HASH_HASH_H_
#define _HASH_HASH_H_

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN extern
#endif

#ifdef __cplusplus
/*
 * Forward declaration of hash_table_s.  Needed by some C++ compilers
 * to prevent errors when the forward reference to hash_table_s is
 * encountered in the hash_entry_s structure.
 */

struct hash_table_s;
#endif

/*
 * Structure definition for an entry in a hash table. Clients should not
 * directly access any of these fields directly. They should use the macros
 * defined below.
 */

typedef struct hash_entry_s
{
    /*! Pointer to next entry in this hash bucket, or NULL for end of the chain. */
    struct hash_entry_s *next_ptr;
    /*! Pointer to table containing entry. */  
    struct hash_table_s *table_ptr;
    /*! Pointer to bucket that points to first entry in this entry's chain. Used
        for deleting the entry. */
    struct hash_entry_s **bucket_ptr;
    /*! Application stores something here with hash_set_value. */
    void *client_data;
    union
    {                                   /* Key has one of these forms: */
        void *one_word_value;           /* One-word value for key. */
        int words[1];                   /* Multiple integer words for key.
                                         * The actual size will be as large
                                         * as necessary for this table's
                                         * keys. */
        char string[sizeof(int)];     /* String for key. The actual size
                                         * will be as large as needed to hold
                                         * the key. */
    } key;                              /* MUST BE LAST FIELD IN RECORD!! */
} hash_entry_t;

/*
 * Structure definition for a hash table.  This must be publicly declared,
 * so clients can allocate space for these structures, and access members
 * through macros. Clients should never directly access any fields in this
 * structure.
 */

#define HASH_SMALL_HASH_TABLE 4
typedef struct hash_table_s
{
    hash_entry_t **buckets;             /* Pointer to bucket array.  Each
                                         * element points to first entry in
                                         * bucket's hash chain, or NULL. */
    hash_entry_t *static_buckets[HASH_SMALL_HASH_TABLE];
                                        /* Bucket array used for small tables
                                         * (to avoid mallocs and frees). */
    int num_buckets;                    /* Total number of buckets allocated
                                         * at **bucketPtr. */
    int num_entries;                    /* Total number of entries present
                                         * in table. */
    int rebuild_size;                   /* Enlarge table when numEntries gets
                                         * to be this large. */
    int down_shift;                     /* Shift count used in hashing
                                         * function.  Designed to use high-
                                         * order bits of randomized keys. */
    int mask;                           /* Mask value used in hashing
                                         * function. */
    int key_type;                       /* Type of keys used in this table. 
                                         * It's either HASH_STRING_KEYS,
                                         * HASH_ONE_WORD_KEYS, or an integer
                                         * giving the number of ints that
                                         * is the size of the key.
                                         */
    hash_entry_t *(*find_proc)(struct hash_table_s *table_ptr, const char *key);
    hash_entry_t *(*create_proc)(struct hash_table_s *table_ptr, const char *key, int *new_ptr);
} hash_table_t;

/*
 * Structure definition for information used to keep track of searches
 * through hash tables:
 */

typedef struct hash_search_s
{
    hash_table_t *table_ptr;            /* Table being searched. */
    int next_index;                     /* Index of next bucket to be
                                         * enumerated after present one. */
    hash_entry_t *next_entry_ptr;       /* Next entry to be enumerated in the
                                         * the current bucket. */
} hash_search_t;

/* Acceptable key types for hash tables: */

#define HASH_STRING_KEYS                0
#define HASH_ONE_WORD_KEYS              1
/* All higher values are the length of an integer array that is the key */

/* Macros for clients to use to access fields of hash entries: */

static __inline__ void *hash_get_value(hash_entry_t *h)
{
    return h->client_data;
}

static __inline__ void hash_set_value(hash_entry_t *h, void *value)
{
    h->client_data = value;
}

static __inline__ void *hash_get_key(hash_table_t *table_ptr, hash_entry_t *h)
{
    return (table_ptr->key_type == HASH_ONE_WORD_KEYS)  ?  h->key.one_word_value  :  h->key.string;
}

/*
 * Macros to use for clients to use to invoke find and create procedures
 * for hash tables:
 */

static __inline__ hash_entry_t *hash_find_entry(hash_table_t *table_ptr, const char *key)
{
    return (*table_ptr->find_proc)(table_ptr, key);
}

static __inline__ hash_entry_t *hash_create_entry(hash_table_t *table_ptr, const char *key, int *new_ptr)
{
    return (*table_ptr->create_proc)(table_ptr, key, new_ptr);
}

EXTERN void hash_delete_entry(hash_entry_t *entryPtr);
EXTERN void hash_delete_table(hash_table_t *table_ptr);
EXTERN hash_entry_t *hash_first_entry(hash_table_t *table_ptr, hash_search_t *search_ptr);
EXTERN char *hash_stats(hash_table_t *table_ptr);
EXTERN void hash_init_table(hash_table_t *table_ptr, int key_type);
EXTERN hash_entry_t *hash_next_entry(hash_search_t *search_ptr);

EXTERN void panic(char *format, ...);

#endif
