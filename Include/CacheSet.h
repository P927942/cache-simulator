#ifndef CACHESET_H
#define CACHESET_H

#include "CacheLine.h"
#include <vector>
#include <string>
#include <cstdint>

// Result of a single cache-set access
struct AccessResult {
    bool     hit               = false;
    bool     writeback_needed  = false;  // A dirty block was evicted
    uint64_t evicted_tag       = 0;      // Tag of the evicted line (valid only when writeback_needed)
};

class CacheSet {
public:
    explicit CacheSet(uint32_t assoc);

    // Core access method.
    // is_write        : true for store operations
    // eviction_policy : "LRU" | "FIFO"
    // allocate        : false ⇒ no-write-allocate (do NOT install on write-miss)
    AccessResult access(uint64_t tag, bool is_write,
                        const std::string& eviction_policy,
                        bool allocate);

    // Forcibly invalidate a tag (used by inclusive-cache protocol).
    // Returns true if the line was present (and dirty ⇒ writeback needed upstream).
    bool invalidate(uint64_t tag, bool& was_dirty);

    bool contains(uint64_t tag) const;

private:
    std::vector<CacheLine> lines_;
    uint32_t               associativity_;
    uint32_t               access_clock_ = 0;  // Monotonic counter for LRU/FIFO

    uint32_t find_victim(const std::string& policy) const;
};

#endif // CACHESET_H
