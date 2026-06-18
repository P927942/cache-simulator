#ifndef CACHE_H
#define CACHE_H

#include "CacheSet.h"
#include <vector>
#include <string>
#include <cstdint>

// Supported write policies
//   WBWA  : Write-Back  + Write-Allocate      (typical L1/L2)
//   WTWNA : Write-Through + No-Write-Allocate  (typical simple L1 or LLC→memory)
enum class WritePolicy { WBWA, WTWNA };

// Supported eviction policies
enum class EvictionPolicy { LRU, FIFO };

struct CacheConfig {
    std::string   name;
    uint32_t      num_sets;
    uint32_t      block_size;       // bytes, must be power-of-2
    uint32_t      associativity;
    EvictionPolicy eviction  = EvictionPolicy::LRU;
    WritePolicy    write_pol = WritePolicy::WBWA;
};

class Cache {
public:
    explicit Cache(const CacheConfig& cfg);

    // Attach a lower-level cache (L1 → L2 → …).
    // The Cache object does NOT own the pointer.
    void set_next_level(Cache* next) { next_level_ = next; }

    // Called by the CPU (or upper cache layer) for every memory operation.
    // Returns true on a hit at *this* level.
    bool access(char type, uint64_t address);

    // Reconstruct the block-aligned address from tag + index
    uint64_t reconstruct_address(uint64_t tag, uint32_t index) const;

    void print_stats() const;

    // --- used by inclusion protocol ---
    // Evict (invalidate) the block that maps to 'address' from this cache.
    void invalidate_block(uint64_t address);
    bool probe(uint64_t address) const;   // check presence without side-effects

    const std::string& name() const { return cfg_.name; }

private:
    CacheConfig            cfg_;
    std::vector<CacheSet>  sets_;

    uint32_t num_offset_bits_ = 0;
    uint32_t num_index_bits_  = 0;

    Cache* next_level_ = nullptr;

    // Statistics
    uint64_t reads_        = 0;
    uint64_t writes_       = 0;
    uint64_t read_hits_    = 0;
    uint64_t write_hits_   = 0;
    uint64_t read_misses_  = 0;
    uint64_t write_misses_ = 0;
    uint64_t writebacks_   = 0;

    void     compute_bit_widths();
    void     parse_address(uint64_t addr, uint64_t& tag, uint32_t& index) const;

    // Handle a miss: fetch from lower level (or memory) and install block.
    void     handle_miss(char type, uint64_t address, uint64_t tag, uint32_t index);
};

#endif // CACHE_H
