/** Maximum length of a key. */
#define KEY_MAX_LENGTH 250

/* Initial power multiplier for the hash table */
#define HASHPOWER_DEFAULT 16
/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items.
 */
#define ITEM_UPDATE_INTERVAL 60

/* Slab sizing definitions. */
#define POWER_SMALLEST 1
#define POWER_LARGEST  200
#define CHUNK_ALIGN_BYTES 8
#define MAX_NUMBER_OF_SLAB_CLASSES (POWER_LARGEST + 1)
#define FACTOR_DEFAULT 1.25

#define MAX_BYTES_DEFAULT 1024* 1024


#define ITEM_key(item) (((char*)&((item)->data)) \
         + (((item)->it_flags) ? sizeof(uint64_t) : 0))

#define ITEM_suffix(item) ((char*) &((item)->data) + (item)->nkey + 1 \
         + (((item)->it_flags) ? sizeof(uint64_t) : 0))

#define ITEM_data(item) ((char*) &((item)->data) + (item)->nkey + 1 \
         + (item)->nsuffix \
         + (((item)->it_flags) ? sizeof(uint64_t) : 0))

#define ITEM_ntotal(item) (sizeof(struct _stritem) + (item)->nkey + 1 \
         + (item)->nsuffix + (item)->nbytes \
         + (((item)->it_flags) ? sizeof(uint64_t) : 0))

/** Time relative to server start. Smaller than time_t on 64-bit systems. */
typedef unsigned int rel_time_t;



#define ITEM_LINKED 1

/* temp */
#define ITEM_SLABBED 4


#define ITEM_FETCHED 8

/**
 * Structure for storing items within memcached.
 */
typedef struct _stritem {
    struct _stritem *next;
    struct _stritem *prev;
    struct _stritem *h_next;    /* hash chain next */

    rel_time_t      time;       /* least recent access */

    rel_time_t      exptime;    /* expire time */

    int             nbytes;     /* size of data */

    unsigned short  refcount;

    uint8_t         nsuffix;    /* length of flags-and-length string */
    uint8_t         it_flags;   /* ITEM_* above */

    uint8_t         slabs_clsid;/* which slab class we're in */

    uint8_t         nkey;       /* key length, w/terminating null and padding */

    char         data[];


} item;

typedef struct stat_{
    
    uint64_t hash_power_value;
    bool hash_is_expanding;

    uint8_t slab_factor;
    

    uint64_t current_bytes;
    uint64_t total_items;
    uint64_t current_items;

    uint64_t put_cmds;
    uint64_t put_hits;
    uint64_t put_misses;

    uint64_t get_cmds;
    uint64_t get_hits;
    uint64_t get_misses;

    uint64_t del_cmds;
    uint64_t del_hits;
    uint64_t del_misses;

} stat;




#include "slab.h"
#include "hash_functions.h"
#include "items.h"


uint32_t hash(const char *key, const int nkey);

item *item_alloc(char *key, size_t nkey, int flags, rel_time_t exptime, int nbytes, int values, stat* stats );
item *item_get(const char *key, const size_t nkey, stat* stats);
item *item_touch(const char *key, const size_t nkey, uint32_t exptime);
int   item_link(item *it);
void  item_remove(item *it);
int   item_replace(item *it, item *new_it, const uint32_t hv);
void  item_unlink(item *it);
void  item_update(item *it);

unsigned short refcount_incr(unsigned short *refcount);
unsigned short refcount_decr(unsigned short *refcount);



