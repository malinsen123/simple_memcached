#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "simple_memcached.h"
/* powers-of-N allocation structures */

typedef struct {
 
    unsigned int size;      /* sizes of items  */
    unsigned int perslab;   /* how many items per slab  */
    void *slots;           /* list of item ptrs */
    unsigned int sl_curr;   /* total free items in list */
    unsigned int slabs;     /* how many slabs were allocated for this class */
    void **slab_list;       /* array of slab pointers */
    unsigned int list_size; /* size of prev array  */
    unsigned int killing;  /* index+1 of dying slab, or zero if none */
    size_t requested; /* The number of requested bytes */
} slabclass_t;

static slabclass_t slabclass[MAX_NUMBER_OF_SLAB_CLASSES]; 
static size_t mem_limit = 0;
static size_t mem_malloced = 0;
static size_t mem_avail = 0;
static int power_largest;

static void *mem_base = NULL; 
static void *mem_current = NULL;

/*
 * Forward Declarations
 */
static int do_slabs_newslab(const unsigned int id);
static void *memory_allocate(size_t size);
static void do_slabs_free(void *ptr, const size_t size, unsigned int id);

/* Preallocate as many slab pages as possible (called from slabs_init)
   on start-up, so users don't get confused out-of-memory errors when
   they do have free (in-slab) space, but no space to make new slabs.
   if maxslabs is 18 (POWER_LARGEST - POWER_SMALLEST + 1), then all
   slab types can be made.  if max memory is less than 18 MB, only the
   smaller ones will be made.  */
static void slabs_preallocate (const unsigned int maxslabs);

/*
 * Figures out which slab class (chunk size) is required to store an item of
 * a given size.
 *
 * Given object size, return id to use when allocating/freeing memory for object
 * 0 means error: can't store such a large object
 */

unsigned int slabs_clsid(const size_t size) {
    int res = POWER_SMALLEST;

    if (size == 0)
        return 0;
    while (size > slabclass[res].size)
        if (res++ == power_largest)     /* won't fit in the biggest slab */
            return 0;
    return res;
}

/**
 * Determines the chunk sizes and initializes the slab class descriptors
 * accordingly.
 */
void slabs_init(const size_t limit, 
                  const double factor,
                  const bool prealloc)  {
    int i = POWER_SMALLEST - 1;
    unsigned int size = sizeof(item) + 48;

    mem_limit = limit;


    if (prealloc) {
        /* Allocate everything in a big chunk with malloc */
        mem_base = malloc(mem_limit);
        if (mem_base != NULL) {
            mem_current = mem_base;
            mem_avail = mem_limit;
        } else {
            fprintf(stderr, "Warning: Failed to allocate requested memory in"
                    " one large chunk.\nWill allocate in smaller chunks\n");
        }
    }

    memset(slabclass, 0, sizeof(slabclass));

    while (++i < POWER_LARGEST && size <= 1048576 / factor) {
        /* Make sure items are always n-byte aligned */
        if (size % CHUNK_ALIGN_BYTES)
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES); 
        slabclass[i].size = size;
        slabclass[i].perslab = 1048576 / slabclass[i].size; 
        size *= factor; 
        fprintf(stderr, "slab class %3d: chunk size %9u perslab %7u\n", i, slabclass[i].size, slabclass[i].perslab);

    }

    power_largest = i;
    slabclass[power_largest].size = 1048576;
    slabclass[power_largest].perslab = 1;

    fprintf(stderr, "slab class %3d: chunk size %9u perslab %7u\n",
                i, slabclass[i].size, slabclass[i].perslab);



    if (prealloc) {
        slabs_preallocate(power_largest);
    }
}

static void slabs_preallocate (const unsigned int maxslabs) {
    int i;
    unsigned int prealloc = 0;

    /* pre-allocate a 1MB slab in every size class so people don't get
       confused by non-intuitive "SERVER_ERROR out of memory"
       messages.  this is the most common question on the mailing
       list.  if you really don't want this, you can rebuild without
       these three lines.  */
    for (i = POWER_SMALLEST; i <= POWER_LARGEST; i++) {
        if (++prealloc > maxslabs)
            return;
        if (do_slabs_newslab(i) == 0) {
            fprintf(stderr, "Error while preallocating slab memory!\n"
                "If using -L or other prealloc options, max memory must be "
                "at least %d megabytes.\n", power_largest);
            exit(1);
        }
    }

}

static int grow_slab_list (const unsigned int id) {
    slabclass_t *p = &slabclass[id];
    if (p->slabs == p->list_size) { 
        size_t new_size =  (p->list_size != 0) ? p->list_size * 2 : 16; 
        void *new_list = realloc(p->slab_list, new_size * sizeof(void *));
        if (new_list == 0) return 0;
        p->list_size = new_size;
        p->slab_list = new_list;
    }
    return 1;
}


static void split_slab_page_into_freelist(char *ptr, const unsigned int id) {
    slabclass_t *p = &slabclass[id];
    int x;
    for (x = 0; x < p->perslab; x++) {
        do_slabs_free(ptr, 0, id);
        ptr += p->size;
    }
}


static int do_slabs_newslab(const unsigned int id) {
    slabclass_t *p = &slabclass[id];

    int len =  p->size * p->perslab;
    char *ptr;

    if ((mem_limit && mem_malloced + len > mem_limit && p->slabs > 0) ||
        (grow_slab_list(id) == 0) ||
        ((ptr = memory_allocate((size_t)len)) == 0)) {

        return 0;
    }

    memset(ptr, 0, (size_t)len);
    split_slab_page_into_freelist(ptr, id); 

    p->slab_list[p->slabs++] = ptr; 
    mem_malloced += len; 

    return 1;
}

/*@null@*/


static void *do_slabs_alloc(const size_t size, unsigned int id) {
    slabclass_t *p;
    void *ret = NULL;
    item *it = NULL;

    if (id < POWER_SMALLEST || id > POWER_LARGEST) {
        return NULL;
    }

    p = &slabclass[id];
    assert(p->sl_curr == 0 || ((item *)p->slots)->slabs_clsid == 0);


    /* fail unless we have space at the end of a recently allocated page,
       we have something on our freelist, or we could allocate a new page */
    if (! (p->sl_curr != 0 || do_slabs_newslab(id) != 0)) {
        /* We don't have more memory available */
        ret = NULL;
    } else if (p->sl_curr != 0) {
        /* return off our freelist */

        it = (item *)p->slots;
        p->slots = it->next;
        if (it->next) it->next->prev = 0;
        p->sl_curr--;
        ret = (void *)it;
    }

    if (ret) {
        p->requested += size; 
    } 

    return ret;
}


static void do_slabs_free(void *ptr, const size_t size, unsigned int id) {
    slabclass_t *p;
    item *it;

    assert(((item *)ptr)->slabs_clsid == 0);
    assert(id >= POWER_SMALLEST && id <= POWER_LARGEST);
    if (id < POWER_SMALLEST || id > POWER_LARGEST)
        return;

    p = &slabclass[id];

    it = (item *)ptr;
    it->it_flags |= ITEM_SLABBED;
    it->prev = 0;
    

    it->next = p->slots; 
    if (it->next) it->next->prev = it;
    p->slots = it;

    p->sl_curr++;
    p->requested -= size; 
    return;
}



static void *memory_allocate(size_t size) {
    void *ret;

    if (mem_base == NULL) {
        /* We are not using a preallocated large memory chunk */
        ret = malloc(size);
    } else {

        ret = mem_current;

        if (size > mem_avail) {
            return NULL;
        }

        /* mem_current pointer _must_ be aligned!!! */
        if (size % CHUNK_ALIGN_BYTES) {
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES); 
        }

        mem_current = ((char*)mem_current) + size;
        if (size < mem_avail) {
            mem_avail -= size;
        } else {
            mem_avail = 0;
        }
    }

    return ret;
}


void *slabs_alloc(size_t size, unsigned int id) {
    void *ret;
    ret = do_slabs_alloc(size, id);
    return ret;
}

void slabs_free(void *ptr, size_t size, unsigned int id) {
    do_slabs_free(ptr, size, id);
}

void slabs_adjust_mem_requested(unsigned int id, size_t old, size_t ntotal)
{

    slabclass_t *p;
    if (id < POWER_SMALLEST || id > power_largest) {
        fprintf(stderr, "Internal error! Invalid slab class\n");
        abort();
    }

    p = &slabclass[id];
    p->requested = p->requested - old + ntotal;
}


