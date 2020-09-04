// Initialize the hashtable
void do_hash_init(const int hashpower_init);

// Find a item in the hashtable
item *hash_find(const char *key, const size_t nkey, const uint32_t hv);

// Add a new item in the hashtable
int hash_insert(item *it, const uint32_t hv, stat* stats);

// Delete an item in the hashtable
void hash_delete(const char *key, const size_t nkey, const uint32_t hv);


extern unsigned int hashpower;