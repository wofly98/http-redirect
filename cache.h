#ifndef CACHE_H
#define CACHE_H

#include <stddef.h> // For size_t
#include <time.h>   // For time_t

// Structure for a cache entry
typedef struct {
    char *key;
    void *value;
    size_t value_size;
    time_t expires_at;
} CacheEntry;

// Structure for the cache module
typedef struct {
    CacheEntry *entries; // Array of cache entries
    size_t capacity;     // Maximum number of entries
    int default_ttl;     // Default time-to-live in seconds
    size_t count;        // Current number of entries
} Cache;

// Global cache instance (for simplicity in this example)
extern Cache global_cache;

// Initialize the cache
// capacity: maximum number of entries
// default_ttl: default time-to-live for entries in seconds
int cache_init(size_t capacity, int default_ttl);

// Add an entry to the cache
// key: the cache key (string)
// value: pointer to the data to cache
// value_size: size of the data
// Returns 0 on success, -1 on failure
int cache_add(const char *key, void *value, size_t value_size);

// Get an entry from the cache
// key: the cache key (string)
// value_size: pointer to a size_t to store the size of the retrieved value
// Returns a pointer to the cached value, or NULL if not found or expired
void *cache_get(const char *key, size_t *value_size);

// Destroy and clean up the cache
void cache_destroy();

#endif // CACHE_H