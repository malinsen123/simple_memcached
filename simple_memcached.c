#include "simple_memcached.h"
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h>


#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1

#define MAX_TOKENS 8

typedef struct token_s {
    char *value;
    size_t length;
} token_t;

uint32_t hash(const char *key, const int nkey)
{
  const char *p;
  int i;
  uint32_t hv = 5381;

  for(i = 0, p = key; i < nkey; p++, i++) {
    hv = (hv << 5) + hv + *p;
  }

  return hv;
}
/*dsds*/

/*
 * Tokenize the command string by replacing whitespace with '\0' and update
 * the token array tokens with pointer to start of each token and length.
 * Returns total number of tokens.  The last valid token is the terminal
 * token (value points to the first unprocessed character of the string and
 * length zero).
 *
 * Usage example:
 *
 *  while(tokenize_command(command, ncommand, tokens, max_tokens) > 0) {
 *      for(int ix = 0; tokens[ix].length != 0; ix++) {
 *          ...
 *      }
 *      ncommand = tokens[ix].value - command;
 *      command  = tokens[ix].value;
 *   }
 */

static size_t tokenize_command(char *command, token_t *tokens, const size_t max_tokens) {
    char *s, *e;
    size_t ntokens = 0;
    size_t len = strlen(command);
    unsigned int i = 0;

    assert(command != NULL && tokens != NULL && max_tokens > 1);

    s = e = command;
    for (i = 0; i < len; i++) {
        if (*e == ' ') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
                *e = '\0';
                if (ntokens == max_tokens - 1) {
                    e++;
                    s = e; /* so we don't add an extra token */
                    break;
                }
            }
            s = e + 1;
        }
        e++;
    }

    if (s != e) {
        tokens[ntokens].value = s;
        tokens[ntokens].length = e - s;
        ntokens++;
    }

    /*
     * If we scanned the whole string, the terminal value pointer is null,
     * otherwise it is the first unprocessed character.
     */
    tokens[ntokens].value =  *e == '\0' ? NULL : e;
    tokens[ntokens].length = 0;
    ntokens++;

    return ntokens;
}


item * Command_process_set(token_t *tokens, stat* stats){

    item* it;
    if(tokens[KEY_TOKEN].length > 200){
        fprintf(stderr, "The length for the key is too long\n");
        return NULL;
    }
    else if(tokens[2].value !='0'){
        fprintf(stderr, "Invalid flag\n" );
        return NULL;
    }
    else if(tokens[3].value !='0' && atoi(tokens[3].value) == 0){
        fprintf(stderr, "Invalid exptime\n" );
        return NULL;
    }
    else if(atoi(tokens[4].value)==0){
        fprintf(stderr, "Invaid nbytes\n" );
        return NULL;
    } 
    else if(atoi(tokens[5].value)==0 && tokens[5].value !='0'){
        fprintf(stderr, "Invalid values\n");
        return NULL;
    }
    else{
        it = item_alloc(tokens[KEY_TOKEN].value, tokens[KEY_TOKEN].length, atoi(tokens[2].value), atoi(tokens[3].value), atoi(tokens[4].value ), atoi(tokens[5]).value, stats);
        return it;
    }
}

item * Command_process_get(token_t *tokens, stat* stats){

    item* it;
    if(tokens[KEY_TOKEN].length > 200){
        fprintf(stderr, "The length for the key is too long\n");
        return NULL;
    }
    else{
        it = item_get(tokens[KEY_TOKEN].value, tokens[KEY_TOKEN].length, stats);
        return it;
    }
}


void Command_process_delete(token_t *tokens, stat* stats){
    if(tokens[KEY_TOKEN].length > 200){
        fprintf(stderr, "The length for the key is too long\n" );
        return;
    }
    else{
        item* it;
        it = item_get(tokens[KEY_TOKEN].value, tokens[KEY_TOKEN].length, stats);
        if(it ==NULL){
            fprintf(stderr, "Invalid key value\n");
            return;
        }
        else{
            item_unlink(it);
            return;
        }
    }   
}

void Command_process_stats(stat* stats){
    stat_print(stats);
    printf("Successful\n");
    return;
}




static void stat_print(stat* stats) {
    printf("hash_power_value: %llu\n", stats->hash_power_value);
    printf("slab_factor: %u\n", stats->slab_factor);

    printf("current_bytes: %llu\n", stats->current_bytes);
    printf("total_items: %llu\n", stats->total_items);
    printf("current_items: %llu\n", stats->current_items);

    printf("put_cmds: %llu\n", stats->put_cmds);
    printf("put_hits: %llu\n", stats->put_hits);
    printf("put_misses: %llu\n", stats->put_misses);

    printf("get_cmds: %llu\n", stats->get_cmds);
    printf("get_hits: %llu\n", stats->get_hits);
    printf("get_misses: %llu\n", stats->get_misses);

    printf("del_cmds: %llu\n", stats->del_cmds);
    printf("del_hits: %llu\n", stats->del_hits);
    printf("del_misses: %llu\n", stats->del_misses);

}

static stat* stats_initial(uint64_t hash_power_value){
    stat* stats = stat();
    stats->hash_power_value= hash_power_value ;
    stats->slab_factor = 1.2 ;

    if(stats != NULL ){
        printf("Stats initialization success.\n", );

    }

}

item *item_alloc(char *key, size_t nkey, int flags, rel_time_t exptime, int nbytes, int values, stat* stats){
    item* it;
    it= do_item_alloc(key, nkey, flags, exptime, nbytes, values);
    stats-> put_cmds++;
    if (it !=NULL){
        stats->current_items ++;
        stats->put_hits ++;
    }
    else{
        stats->put_misses ++;
    }
    return it;


}

item *item_get(const char *key, const size_t nkey, stat* stats){
    item* it;
    uint32_t hv = hash(key, nkey);
    it = do_item_get(key, nkey, hv);
    stats->get_cmds ++;
    if(it !=NULL){
        stats->get_hits ++;
    }
    else{
        stats->get_misses ++;
    }

    return it;
}

item *item_touch(const char *key, const size_t nkey, uint32_t exptime){
    item* it;
    uint32_t hv = hash(key, nkey);
    it = do_item_touch( key, nkey, exptime, hv);
    return it;
}

int item_link(item *it){
    uint32_t hv = hash(key, nkey);
    if(do_item_link(it, hv)){
        printf("Successful link an item\n");
        return 1;
    }
    else{
        return 0;
    }
}

void  item_remove(item *it){
      do_item_remove(it);

}

int item_replace(item *it, item *new_it){
    int ret;
    uint32_t hv = hash(it->key, it->nkey);
    ret= do_item_replace(it, new_it, hv);
    if (ret){
        printf("Successful replace an item\n");
        return ret;
    }
    else{
        return ret;
    }
}

void  item_unlink(item *it){
    uint32_t hv = hash(it-> key, it->nkey);
    do_item_unlink(it);
    if(do_item_get)
}

void  item_update(item *it){
    uint32_t hv = hash(it-> key, it->nkey);
    do_item_update(it,hv);
}


unsigned short refcount_incr(unsigned short *refcount) {
    unsigned short res;
    (*refcount)++;
    res = *refcount;
    return res;
}

unsigned short refcount_decr(unsigned short *refcount) {
    unsigned short res;
    (*refcount)--;
    res = *refcount;
     return res;

}

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t current_time;


int main (int argc, char **argv) {
    bool preallocate = false;
    uint64_t maxbytes = MAX_BYTES_DEFAULT;
    double factor = FACTOR_DEFAULT;
    uint64_t hash_power_value = HASHPOWER_DEFAULT;

    printf("Welcome to simple_memcached\n");
    hash_init(hash_power_value);
    slabs_init(settings.maxbytes, settings.factor, preallocate);

    return 0;
}
