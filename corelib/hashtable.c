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

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "callweaver/hashtable.h"

/*
 * When there are this many entries per bucket, on average, rebuild
 * the hash table to make it larger.
 */
#define REBUILD_MULTIPLIER        3

/* Procedure prototypes for static procedures in this file: */
static unsigned int hash_string(const char *string);
static void rebuild_table(hash_table_t *table_ptr);

static hash_entry_t *hash_find_one_word_entry(hash_table_t *table_ptr, const char *key);
static hash_entry_t *hash_create_one_word_entry(hash_table_t *table_ptr, const char *key, int *new_ptr);
static hash_entry_t *hash_find_string_entry(hash_table_t *table_ptr, const char *key);
static hash_entry_t *hash_create_string_entry(hash_table_t *table_ptr, const char *key, int *new_ptr);
static hash_entry_t *hash_find_array_entry(hash_table_t *table_ptr, const char *key);
static hash_entry_t *hash_create_array_entry(hash_table_t *table_ptr, const char *key, int *new_ptr);
static hash_entry_t *hash_find_bogus_entry(hash_table_t *table_ptr, const char *key);
static hash_entry_t *hash_create_bogus_entry(hash_table_t *table_ptr, const char *key, int *new_ptr);

/*
 * The following takes a preliminary integer hash value and
 * produces an index into a hash tables bucket list.  The idea is
 * to make it so that preliminary values that are arbitrarily similar
 * will end up in different buckets.  The hash function was taken
 * from a random-number generator.
 */
static __inline__ int RANDOM_INDEX(hash_table_t *table_ptr, intptr_t i)
{
    return ((i*1103515245) >> table_ptr->down_shift) & table_ptr->mask;
}

/*
 * Given storage for a hash table, set up the fields to prepare the hash table for use.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        table_ptr is now ready to be passed to hash_find_entry and
 *        hash_create_entry.
 */
void hash_init_table(hash_table_t *table_ptr, int key_type)
{
    int i;

    table_ptr->buckets = table_ptr->static_buckets;
    for (i = 0;  i < HASH_SMALL_HASH_TABLE;  i++)
        table_ptr->static_buckets[i] = 0;
    table_ptr->num_buckets = HASH_SMALL_HASH_TABLE;
    table_ptr->num_entries = 0;
    table_ptr->rebuild_size = HASH_SMALL_HASH_TABLE*REBUILD_MULTIPLIER;
    table_ptr->down_shift = 28;
    table_ptr->mask = 3;
    table_ptr->key_type = key_type;
    if (key_type == HASH_STRING_KEYS)
    {
        table_ptr->find_proc = hash_find_string_entry;
        table_ptr->create_proc = hash_create_string_entry;
    }
    else if (key_type == HASH_ONE_WORD_KEYS)
    {
        table_ptr->find_proc = hash_find_one_word_entry;
        table_ptr->create_proc = hash_create_one_word_entry;
    }
    else
    {
        table_ptr->find_proc = hash_find_array_entry;
        table_ptr->create_proc = hash_create_array_entry;
    }
}
/*- End of function --------------------------------------------------------*/

/*
 * Remove a single entry from a hash table.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        The entry given by entry_ptr is deleted from its table and
 *        should never again be used by the caller.  It is up to the
 *        caller to free the client_data field of the entry, if that
 *        is relevant.
 */
void hash_delete_entry(hash_entry_t *entry_ptr)
{
    hash_entry_t *prev_ptr;

    if (*entry_ptr->bucket_ptr == entry_ptr)
    {
        *entry_ptr->bucket_ptr = entry_ptr->next_ptr;
    }
    else
    {
        for (prev_ptr = *entry_ptr->bucket_ptr;  ;  prev_ptr = prev_ptr->next_ptr)
        {
            if (prev_ptr == NULL)
                fprintf(stderr, "malformed bucket chain in hash_delete_entry");
            if (prev_ptr->next_ptr == entry_ptr)
            {
                prev_ptr->next_ptr = entry_ptr->next_ptr;
                break;
            }
        }
    }
    entry_ptr->table_ptr->num_entries--;
    free(entry_ptr);
}
/*- End of function --------------------------------------------------------*/

/*
 * Free up everything associated with a hash table except for
 * the record for the table itself.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        The hash table is no longer useable.
 */
void hash_delete_table(hash_table_t *table_ptr)
{
    hash_entry_t *hptr;
    hash_entry_t *next_ptr;
    int i;

    /* Free up all the entries in the table. */
    for (i = 0;  i < table_ptr->num_buckets;  i++)
    {
        for (hptr = table_ptr->buckets[i];  hptr;  hptr = next_ptr)
        {
            next_ptr = hptr->next_ptr;
            free(hptr);
        }
    }

    /* Free up the bucket array, if it was dynamically allocated. */
    if (table_ptr->buckets != table_ptr->static_buckets)
        free(table_ptr->buckets);

    /*
     * Arrange for panics if the table is used again without
     * re-initialization.
     */
    table_ptr->find_proc = hash_find_bogus_entry;
    table_ptr->create_proc = hash_create_bogus_entry;
}
/*- End of function --------------------------------------------------------*/

/*
 * Locate the first entry in a hash table and set up a record
 * that can be used to step through all the remaining entries
 * of the table.
 *
 * Results:
 *        The return value is a pointer to the first entry in table_ptr,
 *        or NULL if table_ptr has no entries in it.  The memory at
 *        *searchptr is initialized so that subsequent calls to
 *        hash_next_entry will return all the entries in the table,
 *        one at a time.
 *
 * Side effects:
 *        None.
 */
hash_entry_t *hash_first_entry(hash_table_t *table_ptr,
                               hash_search_t *search_ptr)
{
    search_ptr->table_ptr = table_ptr;
    search_ptr->next_index = 0;
    search_ptr->next_entry_ptr = NULL;
    return hash_next_entry(search_ptr);
}
/*- End of function --------------------------------------------------------*/

/*
 * Once a hash table enumeration has been initiated by calling
 * hash_first_entry, this procedure may be called to return
 * successive elements of the table.
 *
 * Results:
 *        The return value is the next entry in the hash table being
 *        enumerated, or NULL if the end of the table is reached.
 *
 * Side effects:
 *        None.
 */
hash_entry_t *hash_next_entry(hash_search_t *search_ptr)
{
    hash_entry_t *hptr;

    while (search_ptr->next_entry_ptr == NULL)
    {
        if (search_ptr->next_index >= search_ptr->table_ptr->num_buckets)
            return NULL;
        search_ptr->next_entry_ptr =
            search_ptr->table_ptr->buckets[search_ptr->next_index++];
    }
    hptr = search_ptr->next_entry_ptr;
    search_ptr->next_entry_ptr = hptr->next_ptr;
    return hptr;
}
/*- End of function --------------------------------------------------------*/

/*
 * Return statistics describing the layout of the hash table
 * in its hash buckets.
 *
 * Results:
 *        The return value is a malloc-ed string containing information
 *        about table_ptr.  It is the caller's responsibility to free
 *        this string.
 *
 * Side effects:
 *        None.
 */
char *hash_stats(hash_table_t *table_ptr)
{
#define NUM_COUNTERS 10
    int count[NUM_COUNTERS];
    int overflow;
    int i;
    int j;
    double average;
    double tmp;
    hash_entry_t *hptr;
    char *result;
    char *p;

    /* Compute a histogram of bucket usage. */
    for (i = 0; i < NUM_COUNTERS; i++)
        count[i] = 0;
    overflow = 0;
    average = 0.0;
    for (i = 0;  i < table_ptr->num_buckets;  i++)
    {
        j = 0;
        for (hptr = table_ptr->buckets[i];  hptr;  hptr = hptr->next_ptr)
            j++;
        if (j < NUM_COUNTERS)
            count[j]++;
        else
            overflow++;
        tmp = j;
        average += (tmp+1.0)*(tmp/table_ptr->num_entries)/2.0;
    }

    /* Print out the histogram and a few other pieces of information. */
    if ((result = (char *) malloc(NUM_COUNTERS*60 + 300)))
    {
        sprintf(result,
                "%d entries in table, %d buckets\n",
                table_ptr->num_entries,
                table_ptr->num_buckets);
        p = result + strlen(result);
        for (i = 0;  i < NUM_COUNTERS;  i++)
        {
            sprintf(p,
                    "number of buckets with %d entries: %d\n",
                    i,
                    count[i]);
            p += strlen(p);
        }
        sprintf(p,
                "number of buckets with %d or more entries: %d\n",
                NUM_COUNTERS,
                overflow);
        p += strlen(p);
        sprintf(p, "average search distance for entry: %.1f", average);
    }
    return result;
}
/*- End of function --------------------------------------------------------*/

/*
 * Compute a one-word summary of a text string, which can be
 * used to generate a hash index.
 *
 * Results:
 *        The return value is a one-word summary of the information in
 *        string.
 *
 * Side effects:
 *        None.
 */
static unsigned int hash_string(const char *string)
{
    unsigned int result;
    char c;

    /*
     * I tried a zillion different hash functions and asked many other
     * people for advice.  Many people had their own favorite functions,
     * all different, but no-one had much idea why they were good ones.
     * I chose the one below (multiply by 9 and add new character)
     * because of the following reasons:
     *
     * 1. Multiplying by 10 is perfect for keys that are decimal strings,
     *    and multiplying by 9 is just about as good.
     * 2. Times-9 is (shift-left-3) plus (old).  This means that each
     *    character's bits hang around in the low-order bits of the
     *    hash value for ever, plus they spread fairly rapidly up to
     *    the high-order bits to fill out the hash value.  This seems
     *    works well both for decimal and non-decimal strings.
     */
    for (result = 0;  ;  result += (result << 3) + c)
    {
        c = *string++;
        if (c == '\0')
            break;
    }
    return result;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with string keys, and a string key, find
 * the entry with a matching key.
 *
 * Results:
 *        The return value is a token for the matching entry in the
 *        hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *        None.
 */
static hash_entry_t *hash_find_string_entry(hash_table_t *table_ptr, const char *key)
{
    hash_entry_t *hptr;
    const char *p1;
    const char *p2;
    int index;

    index = hash_string(key) & table_ptr->mask;

    /* Search all the entries in the appropriate bucket. */
    for (hptr = table_ptr->buckets[index];
         hptr != NULL;
         hptr = hptr->next_ptr)
    {
        for (p1 = key, p2 = hptr->key.string;  ;  p1++, p2++)
        {
            if (*p1 != *p2)
                break;
            if (*p1 == '\0')
                return hptr;
        }
    }
    return NULL;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with string keys, and a string key, find
 * the entry with a matching key.  If there is no matching entry,
 * then create a new entry that does match.
 *
 * Results:
 *        The return value is a pointer to the matching entry.  If this
 *        is a newly-created entry, then *new_ptr will be set to a non-zero
 *        value;  otherwise *new_ptr will be set to 0.  If this is a new
 *        entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *        A new entry may be added to the hash table.
 */
static hash_entry_t *hash_create_string_entry(hash_table_t *table_ptr,
                                              const char *key,
                                              int *new_ptr)
{
    hash_entry_t *hptr;
    const char *p1;
    const char *p2;
    int index;

    index = hash_string(key) & table_ptr->mask;

    /* Search all the entries in this bucket. */
    for (hptr = table_ptr->buckets[index];
         hptr != NULL;
         hptr = hptr->next_ptr)
    {
        for (p1 = key, p2 = hptr->key.string;  ;  p1++, p2++)
        {
            if (*p1 != *p2)
                break;
            if (*p1 == '\0')
            {
                if (new_ptr)
                    *new_ptr = 0;
                return hptr;
            }
        }
    }

    /* Entry not found.  Add a new one to the bucket. */
    if (new_ptr)
        *new_ptr = 1;
    if ((hptr = (hash_entry_t *) malloc(sizeof(hash_entry_t) + strlen((char *) key) - (sizeof(hptr->key) - 1))) == NULL)
        return  NULL;
    hptr->table_ptr = table_ptr;
    hptr->bucket_ptr = &(table_ptr->buckets[index]);
    hptr->next_ptr = *hptr->bucket_ptr;
    hptr->client_data = NULL;
    strcpy(hptr->key.string, (char *) key);
    *hptr->bucket_ptr = hptr;
    table_ptr->num_entries++;

    /*
     * If the table has exceeded a decent size, rebuild it with many
     * more buckets.
     */
    if (table_ptr->num_entries >= table_ptr->rebuild_size)
        rebuild_table(table_ptr);
    return hptr;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with one-word keys, and a one-word key, find
 * the entry with a matching key.
 *
 * Results:
 *        The return value is a token for the matching entry in the
 *        hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *        None.
 */
static hash_entry_t *hash_find_one_word_entry(hash_table_t *table_ptr, const char *key)
{
    hash_entry_t *hptr;
    int index;
    
    index = RANDOM_INDEX(table_ptr, (intptr_t) key);

    /* Search all the entries in the appropriate bucket. */
    for (hptr = table_ptr->buckets[index];
         hptr;
         hptr = hptr->next_ptr)
    {
        if (hptr->key.one_word_value == key)
            return hptr;
    }
    return NULL;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with one-word keys, and a one-word key, find
 * the entry with a matching key.  If there is no matching entry,
 * then create a new entry that does match.
 *
 * Results:
 *        The return value is a pointer to the matching entry.  If this
 *        is a newly-created entry, then *new_ptr will be set to a non-zero
 *        value;  otherwise *new_ptr will be set to 0.  If this is a new
 *        entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *        A new entry may be added to the hash table.
 */
static hash_entry_t *hash_create_one_word_entry(hash_table_t *table_ptr,
                                                const char *key,
                                                int *new_ptr)
{
    hash_entry_t *hptr;
    int index;

    index = RANDOM_INDEX(table_ptr, (intptr_t) key);
    /* Search all the entries in this bucket. */
    for (hptr = table_ptr->buckets[index];  hptr;  hptr = hptr->next_ptr)
    {
        if (hptr->key.one_word_value == key)
        {
            if (new_ptr)
                *new_ptr = 0;
            return hptr;
        }
    }

    /* Entry not found.  Add a new one to the bucket. */
    if (new_ptr)
        *new_ptr = 1;
    if ((hptr = (hash_entry_t *) malloc(sizeof(hash_entry_t))) == NULL)
        return  NULL;
    hptr->table_ptr = table_ptr;
    hptr->bucket_ptr = &(table_ptr->buckets[index]);
    hptr->next_ptr = *hptr->bucket_ptr;
    hptr->client_data = NULL;
    hptr->key.one_word_value = (char *) key;        /* CONST XXXX */
    *hptr->bucket_ptr = hptr;
    table_ptr->num_entries++;

    /*
     * If the table has exceeded a decent size, rebuild it with many
     * more buckets.
     */
    if (table_ptr->num_entries >= table_ptr->rebuild_size)
        rebuild_table(table_ptr);
    return hptr;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with array-of-int keys, and a key, find
 * the entry with a matching key.
 *
 * Results:
 *        The return value is a token for the matching entry in the
 *        hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *        None.
 */
static hash_entry_t *hash_find_array_entry(hash_table_t *table_ptr, const char *key)
{
    hash_entry_t *hptr;
    int *array_ptr;
    int *iPtr1;
    int *iPtr2;
    int index;
    int count;

    array_ptr = (int *) key;
    for (index = 0, count = table_ptr->key_type, iPtr1 = array_ptr;
         count > 0;
         count--, iPtr1++)
    {
        index += *iPtr1;
    }
    index = RANDOM_INDEX(table_ptr, index);

    /* Search all the entries in the appropriate bucket. */
    for (hptr = table_ptr->buckets[index];
         hptr;
         hptr = hptr->next_ptr)
    {
        for (iPtr1 = array_ptr, iPtr2 = hptr->key.words, count = table_ptr->key_type;
             ;
             count--, iPtr1++, iPtr2++)
        {
            if (count == 0)
                return hptr;
            if (*iPtr1 != *iPtr2)
                break;
        }
    }
    return NULL;
}
/*- End of function --------------------------------------------------------*/

/*
 * Given a hash table with array-of-int keys, and a key, find
 * the entry with a matching key. If there is no matching entry,
 * then create a new entry that does match.
 *
 * Results:
 *        The return value is a pointer to the matching entry.  If this
 *        is a newly-created entry, then *new_ptr will be set to a non-zero
 *        value;  otherwise *new_ptr will be set to 0.  If this is a new
 *        entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *        A new entry may be added to the hash table.
 */
static hash_entry_t *hash_create_array_entry(hash_table_t *table_ptr,
                                              const char *key,
                                              int *new_ptr)
{
    hash_entry_t *hptr;
    int *array_ptr;
    int *iPtr1;
    int *iPtr2;
    int index;
    int count;

    array_ptr = (int *) key;
    for (index = 0, count = table_ptr->key_type, iPtr1 = array_ptr;
         count > 0;
         count--, iPtr1++)
    {
        index += *iPtr1;
    }
    index = RANDOM_INDEX(table_ptr, index);

    /* Search all the entries in the appropriate bucket. */
    for (hptr = table_ptr->buckets[index];  hptr;  hptr = hptr->next_ptr)
    {
        for (iPtr1 = array_ptr, iPtr2 = hptr->key.words, count = table_ptr->key_type;
             ;
             count--, iPtr1++, iPtr2++)
        {
            if (count == 0)
            {
                if (new_ptr)
                    *new_ptr = 0;
                return hptr;
            }
            if (*iPtr1 != *iPtr2)
                break;
        }
    }

    /* Entry not found.  Add a new one to the bucket. */
    if (new_ptr)
        *new_ptr = 1;
    if ((hptr = (hash_entry_t *) malloc(sizeof(hash_entry_t) + (table_ptr->key_type*sizeof(int)) - 4)) == NULL)
        return  NULL;
    hptr->table_ptr = table_ptr;
    hptr->bucket_ptr = &(table_ptr->buckets[index]);
    hptr->next_ptr = *hptr->bucket_ptr;
    hptr->client_data = NULL;
    for (iPtr1 = array_ptr, iPtr2 = hptr->key.words, count = table_ptr->key_type;
         count > 0;
         count--, iPtr1++, iPtr2++)
    {
        *iPtr2 = *iPtr1;
    }
    *hptr->bucket_ptr = hptr;
    table_ptr->num_entries++;

    /*
     * If the table has exceeded a decent size, rebuild it with many
     * more buckets.
     */
    if (table_ptr->num_entries >= table_ptr->rebuild_size)
        rebuild_table(table_ptr);
    return hptr;
}
/*- End of function --------------------------------------------------------*/

/*
 * This procedure is invoked when an hash_find_entry is called
 * on a table that has been deleted.
 *
 * Results:
 *        If panic returns (which it shouldn't) this procedure returns
 *        NULL.
 *
 * Side effects:
 *        Generates a panic.
 */
static hash_entry_t *hash_find_bogus_entry(hash_table_t *table_ptr, const char *key)
{
    fprintf(stderr, "called hash_find_entry on deleted table");
    return NULL;
}
/*- End of function --------------------------------------------------------*/

/*
 * This procedure is invoked when an hash_create_entry is called
 * on a table that has been deleted.
 *
 * Results:
 *        If panic returns (which it shouldn't) this procedure returns
 *        NULL.
 *
 * Side effects:
 *        Generates a panic.
 */
static hash_entry_t *hash_create_bogus_entry(hash_table_t *table_ptr,
                                            const char *key,
                                            int *new_ptr)
{
    fprintf(stderr, "called hash_create_entry on deleted table");
    return NULL;
}
/*- End of function --------------------------------------------------------*/

/*
 * This procedure is invoked when the ratio of entries to hash
 * buckets becomes too large.  It creates a new table with a
 * larger bucket array and moves all the entries into the
 * new table.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Memory gets reallocated and entries get re-hashed to new
 *        buckets.
 */
static void rebuild_table(hash_table_t *table_ptr)
{
    int old_size;
    int count;
    int index;
    int *iptr;
    hash_entry_t **old_buckets;
    hash_entry_t **old_chain_ptr;
    hash_entry_t **new_chain_ptr;
    hash_entry_t *hptr;
    
    old_size = table_ptr->num_buckets;
    old_buckets = table_ptr->buckets;

    /*
     * Allocate and initialize the new bucket array, and set up
     * hashing constants for new array size.
     */
    table_ptr->num_buckets *= 4;
    /* TODO: report the memory failure some way. Don't just bomb out */
    if ((table_ptr->buckets = (hash_entry_t **) malloc(table_ptr->num_buckets*sizeof(hash_entry_t *))) == NULL)
        return;
    for (count = table_ptr->num_buckets, new_chain_ptr = table_ptr->buckets;
         count > 0;
         count--, new_chain_ptr++)
    {
        *new_chain_ptr = NULL;
    }
    table_ptr->rebuild_size *= 4;
    table_ptr->down_shift -= 2;
    table_ptr->mask = (table_ptr->mask << 2) + 3;

    /* Rehash all the existing entries into the new bucket array. */
    for (old_chain_ptr = old_buckets;  old_size > 0;  old_size--, old_chain_ptr++)
    {
        for (hptr = *old_chain_ptr;  hptr;  hptr = *old_chain_ptr)
        {
            *old_chain_ptr = hptr->next_ptr;
            if (table_ptr->key_type == HASH_STRING_KEYS)
            {
                index = hash_string(hptr->key.string) & table_ptr->mask;
            }
            else if (table_ptr->key_type == HASH_ONE_WORD_KEYS)
            {
                index = RANDOM_INDEX(table_ptr, (intptr_t) hptr->key.one_word_value);
            }
            else
            {
                for (index = 0, count = table_ptr->key_type, iptr = hptr->key.words;
                     count > 0;
                     count--, iptr++)
                {
                    index += *iptr;
                }
                index = RANDOM_INDEX(table_ptr, index);
            }
            hptr->bucket_ptr = &(table_ptr->buckets[index]);
            hptr->next_ptr = *hptr->bucket_ptr;
            *hptr->bucket_ptr = hptr;
        }
    }

    /* Free up the old bucket array, if it was dynamically allocated. */
    if (old_buckets != table_ptr->static_buckets)
        free(old_buckets);
}
/*- End of function --------------------------------------------------------*/

intptr_t stuff[1000000];

#if defined(__TEST_HASHTABLE_C__)
int main(int argc, char *argv[])
{
    hash_table_t fred;
    hash_entry_t *x;
    int new;
    int r;
    int i;

    hash_init_table(&fred, HASH_ONE_WORD_KEYS);

    for (i = 0;  i < 1000000;  i++)
    {
        do
        {
            r = rand();
            stuff[i] = r;
            x = hash_create_entry(&fred, (const void *) stuff[i], &new);
            if (!new)
            {
                if (r != stuff[(int) hash_get_value(x)])
                    printf("Clash at 0x%x 0x%x\n", r, stuff[(int) hash_get_value(x)]);
            }
        }
        while (new == 0);
        hash_set_value(x, (void *) (intptr_t) i);
    }

    printf("Filled\n");
    for (i = 0;  i < 1000000;  i++)
    {
        x = hash_find_entry(&fred, (const void *) stuff[i]);
        if ((int) hash_get_value(x) != i)
            printf("At %d - 0x%lx %p\n", i, stuff[i], x->client_data);
    }
    printf("Checked\n");
    return  0;
}
#endif
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
