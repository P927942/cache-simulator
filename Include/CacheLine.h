#ifndef CACHELINE_H
#define CACHELINE_H

#include <cstdint>

struct CacheLine {
    bool     valid        = false;
    bool     dirty        = false;
    uint64_t tag          = 0;
    uint32_t lru_counter  = 0;   // Updated on every access  (LRU)
    uint32_t fifo_counter = 0;   // Set only on first load    (FIFO)
};

#endif // CACHELINE_H
