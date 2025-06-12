#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // For debugging, can be removed later
#include <time.h> // Include time.h for time()

// Global cache instance definition
Cache global_cache = { NULL, 0, 0, 0 };

int cache_init(size_t capacity, int default_ttl) {
    if (global_cache.entries != NULL) {
        // Cache already initialized
        fprintf(stderr, "Cache already initialized.\n");
        return -1;
    }

    if (capacity == 0) {
        fprintf(stderr, "Cache capacity cannot be zero.\n");
        return -1;
    }

    global_cache.entries = (CacheEntry *)calloc(capacity, sizeof(CacheEntry));
    if (global_cache.entries == NULL) {
        perror("Failed to allocate memory for cache entries");
        return -1;
    }

    global_cache.capacity = capacity;
    global_cache.default_ttl = default_ttl;
    global_cache.count = 0;

    fprintf(stdout, "Cache initialized with capacity %zu and default TTL %d.\n", capacity, default_ttl);
    return 0;
}

int cache_add(const char *key, void *value, size_t value_size) {
    if (global_cache.entries == NULL) {
        fprintf(stderr, "Cache not initialized.\n");
        return -1;
    }

    if (key == NULL || value == NULL || value_size == 0) {
        fprintf(stderr, "Invalid arguments for cache_add.\n");
        return -1;
    }

    // Find an empty slot or an expired entry to replace
    size_t i;
    int found_slot = -1;
    time_t current_time = time(NULL);

    for (i = 0; i < global_cache.capacity; ++i) {
        if (global_cache.entries[i].key == NULL || global_cache.entries[i].expires_at <= current_time) {
            // Found an empty or expired slot
            found_slot = i;
            break;
        }
    }

    if (found_slot == -1) {
        // Cache is full and no expired entries found
        fprintf(stderr, "Cache is full. Cannot add entry for key '%s'.\n", key);
        return -1;
    }

    // If replacing an existing entry, free old memory
    if (global_cache.entries[found_slot].key != NULL) {
        free(global_cache.entries[found_slot].key);
        free(global_cache.entries[found_slot].value);
        global_cache.count--; // Decrement count as we are replacing
    }

    // Allocate memory for key and value
    global_cache.entries[found_slot].key = strdup(key);
    if (global_cache.entries[found_slot].key == NULL) {
        perror("Failed to allocate memory for cache key");
        // Attempt to clean up if value was already allocated
        if (global_cache.entries[found_slot].value != NULL) {
             free(global_cache.entries[found_slot].value);
             global_cache.entries[found_slot].value = NULL;
        }
        return -1;
    }

    global_cache.entries[found_slot].value = malloc(value_size);
    if (global_cache.entries[found_slot].value == NULL) {
        perror("Failed to allocate memory for cache value");
        free(global_cache.entries[found_slot].key); // Free key if value allocation fails
        global_cache.entries[found_slot].key = NULL;
        return -1;
    }

    // Copy value and set expiration
    memcpy(global_cache.entries[found_slot].value, value, value_size);
    global_cache.entries[found_slot].value_size = value_size;
    global_cache.entries[found_slot].expires_at = current_time + global_cache.default_ttl;

    global_cache.count++;
    fprintf(stdout, "Added key '%s' to cache. Expires at %ld.\n", key, (long)global_cache.entries[found_slot].expires_at);

    return 0;
}

void *cache_get(const char *key, size_t *value_size) {
    if (global_cache.entries == NULL) {
        fprintf(stderr, "Cache not initialized.\n");
        return NULL;
    }

    if (key == NULL || value_size == NULL) {
        fprintf(stderr, "Invalid arguments for cache_get.\n");
        return NULL;
    }

    time_t current_time = time(NULL);

    // Find the entry by key
    size_t i;
    for (i = 0; i < global_cache.capacity; ++i) {
        if (global_cache.entries[i].key != NULL && strcmp(global_cache.entries[i].key, key) == 0) {
            // Found the key, check if expired
            if (global_cache.entries[i].expires_at > current_time) {
                // Not expired, return value
                *value_size = global_cache.entries[i].value_size;
                fprintf(stdout, "Cache hit for key '%s'.\n", key);
                return global_cache.entries[i].value;
            } else {
                // Expired, free memory and invalidate entry
                fprintf(stdout, "Cache entry for key '%s' expired.\n", key);
                free(global_cache.entries[i].key);
                free(global_cache.entries[i].value);
                global_cache.entries[i].key = NULL;
                global_cache.entries[i].value = NULL;
                global_cache.entries[i].value_size = 0;
                global_cache.entries[i].expires_at = 0;
                global_cache.count--;
                return NULL;
            }
        }
    }

    // Key not found
    fprintf(stdout, "Cache miss for key '%s'.\n", key);
    *value_size = 0;
    return NULL;
}

void cache_destroy() {
    if (global_cache.entries != NULL) {
        size_t i;
        for (i = 0; i < global_cache.capacity; ++i) {
            if (global_cache.entries[i].key != NULL) {
                free(global_cache.entries[i].key);
                free(global_cache.entries[i].value);
            }
        }
        free(global_cache.entries);
        global_cache.entries = NULL;
        global_cache.capacity = 0;
        global_cache.default_ttl = 0;
        global_cache.count = 0;
        fprintf(stdout, "Cache destroyed.\n");
    }
}