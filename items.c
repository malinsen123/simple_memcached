#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "simple_memcached.h"


/* Forward Declarations */
static void item_link_q(item *it);
static void item_unlink_q(item *it);

#define LARGEST_ID POWER_LARGEST




static item *heads[LARGEST_ID];

static item *tails[LARGEST_ID];

static unsigned int sizes[LARGEST_ID];

/**
 * Generates the variable-sized part of the header for an object.
 *
 * key     - The key
 * nkey    - The length of the key
 * flags   - key flags
 * nbytes  - Number of bytes to hold value and addition CRLF terminator
 * suffix  - Buffer for the "VALUE" line suffix (flags, size).
 * nsuffix - The length of the suffix is stored here.
 *
 * Returns the total size of the header.
 */
static size_t item_make_header(const uint8_t nkey, const int flags, const int nbytes,
                     char *suffix, uint8_t *nsuffix) {
    /* suffix is defined at 40 chars elsewhere.. */
    *nsuffix = (uint8_t) snprintf(suffix, 40, " %d %d\r\n", flags, nbytes - 2);
    return sizeof(item) + nkey + *nsuffix + nbytes;
}

/*@null@*/
item *do_item_alloc(char *key, const size_t nkey, const int flags,
                    const rel_time_t exptime, const int nbytes) {
    uint8_t nsuffix;
    item *it = NULL;
    char suffix[40];
    size_t ntotal = item_make_header(nkey + 1, flags, nbytes, suffix, &nsuffix);

    unsigned int id = slabs_clsid(ntotal);
    if (id == 0)
        return 0;

    /* do a quick check if we have any expired items in the tail.. */
    int tries = 5; 
    int tried_alloc = 0;
    item *search;

    search = tails[id]; 
    for (; tries > 0 && search != NULL; tries--, search=search->prev) {
        uint32_t hv = hash(ITEM_key(search), search->nkey);

        /* Expired or flushed */
        if (search->exptime != 0 && search->exptime < current_time) {
            it = search;
            slabs_adjust_mem_requested(it->slabs_clsid, ITEM_ntotal(it), ntotal);
            do_item_unlink(it, hv); 
            /* Initialize the item block: */
            it->slabs_clsid = 0;
        } else if ((it = slabs_alloc(ntotal, id)) == NULL) {
            
            tried_alloc = 1;
            it = search;
            slabs_adjust_mem_requested(it->slabs_clsid, ITEM_ntotal(it), ntotal);
            do_item_unlink(it, hv);
            /* Initialize the item block: */
            it->slabs_clsid = 0;
        }

        refcount_decr(&search->refcount); 
        break;
    }

    if (!tried_alloc && (tries == 0 || search == NULL))
        it = slabs_alloc(ntotal, id);

    if (it == NULL) {
        fprintf(stderr, "Out of memory.\n" );
        return NULL;
    }

    assert(it->slabs_clsid == 0);
    assert(it != heads[id]);

    /* Item initialization can happen outside of the lock; the item's already
     * been removed from the slab LRU.
     */
    
    it->refcount = 1;     /* the caller will have a reference */
    it->next = it->prev = it->h_next = 0;
    it->slabs_clsid = id;

    it->it_flags = 0;
    it->nkey = nkey;
    it->nbytes = nbytes; 
    memcpy(ITEM_key(it), key, nkey);
    it->exptime = exptime;
    memcpy(ITEM_suffix(it), suffix, (size_t)nsuffix);
    it->nsuffix = nsuffix;

    fprintf(stderr, "Item allocation success.\n");
    return it;
}

void item_free(item *it) {
    size_t ntotal = ITEM_ntotal(it);
    unsigned int clsid;
    assert((it->it_flags & ITEM_LINKED) == 0);
    assert(it != heads[it->slabs_clsid]);
    assert(it != tails[it->slabs_clsid]);
    assert(it->refcount == 0);

    /* so slab size changer can tell later if item is already free or not */
    clsid = it->slabs_clsid;
    it->slabs_clsid = 0;
    slabs_free(it, ntotal, clsid);
}

/**
 * Returns true if an item will fit in the cache (its size does not exceed
 * the maximum for a cache entry.)
 */
bool item_size_ok(const size_t nkey, const int flags, const int nbytes) {
    char prefix[40];
    uint8_t nsuffix;

    size_t ntotal = item_make_header(nkey + 1, flags, nbytes,
                                     prefix, &nsuffix);
    return slabs_clsid(ntotal) != 0;
}


static void item_link_q(item *it) { /* item is the new head */
    item **head, **tail;
    assert(it->slabs_clsid < LARGEST_ID);
    assert((it->it_flags & ITEM_SLABBED) == 0);

    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];
    assert(it != *head);
    assert((*head && *tail) || (*head == 0 && *tail == 0));

	it->prev = 0;
    it->next = *head;
    if (it->next) it->next->prev = it;
    *head = it;

    if (*tail == 0) *tail = it;

    sizes[it->slabs_clsid]++;
    return;
}


static void item_unlink_q(item *it) {
    item **head, **tail;
    assert(it->slabs_clsid < LARGEST_ID);
    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];


    if (*head == it) {
        assert(it->prev == 0);
        *head = it->next;
    }

    if (*tail == it) {
        assert(it->next == 0);
        *tail = it->prev;
    }
    assert(it->next != it);
    assert(it->prev != it);


    if (it->next) it->next->prev = it->prev;

    sizes[it->slabs_clsid]--;
    return;
}

int do_item_link(item *it, const uint32_t hv) {
    assert((it->it_flags & (ITEM_LINKED|ITEM_SLABBED)) == 0);
    it->it_flags |= ITEM_LINKED;
    it->time = current_time;

    stats.curr_bytes += ITEM_ntotal(it);
    stats.curr_items += 1;
    stats.total_items += 1;


    hash_insert(it, hv);

    item_link_q(it);
    refcount_incr(&it->refcount);
    return 1;
}


void do_item_unlink(item *it, const uint32_t hv) {
    if ((it->it_flags & ITEM_LINKED) != 0) {
        it->it_flags &= ~ITEM_LINKED;

        hash_delete(ITEM_key(it), it->nkey, hv);

        item_unlink_q(it);
        do_item_remove(it);
    }
}



void do_item_remove(item *it) {
    assert((it->it_flags & ITEM_SLABBED) == 0);
    assert(it->refcount > 0);
    if (refcount_decr(&it->refcount) == 0) {
        item_free(it);
    }
}


void do_item_update(item *it) {

    if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
        assert((it->it_flags & ITEM_SLABBED) == 0);
        if ((it->it_flags & ITEM_LINKED) != 0) {
            item_unlink_q(it);
            it->time = current_time;
            item_link_q(it);
        }
    }
}


int do_item_replace(item *it, item *new_it, const uint32_t hv) {
    assert((it->it_flags & ITEM_SLABBED) == 0);
    do_item_unlink(it, hv);
    return do_item_link(new_it, hv);
}

/** wrapper around assoc_find which does the lazy expiration logic */

item *do_item_get(const char *key, const size_t nkey, const uint32_t hv) {
    //mutex_lock(&cache_lock);
    item *it = assoc_find(key, nkey, hv);
    if (it != NULL) {
        refcount_incr(&it->refcount);
    }

    int was_found = 0;


    int ii;
    if (it == NULL) {
        fprintf(stderr, "> NOT FOUND ");
    } else {
        fprintf(stderr, "> FOUND KEY ");
        was_found++;
    }
    for (ii = 0; ii < nkey; ++ii) {
        fprintf(stderr, "%c", key[ii]);
    }
    

    if (it != NULL) {

        if (it->exptime != 0 && it->exptime <= current_time) { 
            do_item_unlink(it, hv);
            do_item_remove(it);
            it = NULL;
            if (was_found) {
                fprintf(stderr, " -nuked by expire");
            }
        } 
        else {
            it->it_flags |= ITEM_FETCHED;
        }
    }

    return it;
}


item *do_item_touch(const char *key, size_t nkey, uint32_t exptime,
                    const uint32_t hv) {
    item *it = do_item_get(key, nkey, hv);
    if (it != NULL) {
        it->exptime = exptime;
    }
    return it;
}



