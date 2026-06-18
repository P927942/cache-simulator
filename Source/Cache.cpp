#include "../Include/Cache.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
Cache::Cache(const CacheConfig& cfg) : cfg_(cfg) {
    if (cfg_.num_sets == 0 || (cfg_.num_sets & (cfg_.num_sets - 1)) != 0)
        throw std::invalid_argument("num_sets must be a power of 2");
    if (cfg_.block_size == 0 || (cfg_.block_size & (cfg_.block_size - 1)) != 0)
        throw std::invalid_argument("block_size must be a power of 2");

    compute_bit_widths();
    sets_.assign(cfg_.num_sets, CacheSet(cfg_.associativity));
}

void Cache::compute_bit_widths() {
    num_offset_bits_ = static_cast<uint32_t>(std::log2(cfg_.block_size));
    num_index_bits_  = static_cast<uint32_t>(std::log2(cfg_.num_sets));
}

void Cache::parse_address(uint64_t addr, uint64_t& tag, uint32_t& index) const {
    uint64_t index_mask = (1ULL << num_index_bits_) - 1;
    index = static_cast<uint32_t>((addr >> num_offset_bits_) & index_mask);
    tag   = addr >> (num_offset_bits_ + num_index_bits_);
}

uint64_t Cache::reconstruct_address(uint64_t tag, uint32_t index) const {
    return (tag << (num_offset_bits_ + num_index_bits_)) |
           (static_cast<uint64_t>(index) << num_offset_bits_);
}

// ─────────────────────────────────────────────────────────────────────────────
// access  — the main entry point called by CPU / upper-level cache
// ─────────────────────────────────────────────────────────────────────────────
bool Cache::access(char type, uint64_t address) {
    const bool is_write = (type == 'W');
    is_write ? ++writes_ : ++reads_;

    uint64_t tag   = 0;
    uint32_t index = 0;
    parse_address(address, tag, index);

    const std::string evpol = (cfg_.eviction == EvictionPolicy::LRU) ? "LRU" : "FIFO";
    const bool        alloc = (cfg_.write_pol == WritePolicy::WBWA);   // allocate on write miss?

    AccessResult ar = sets_[index].access(tag, is_write, evpol, alloc);

    if (ar.hit) {
        is_write ? ++write_hits_ : ++read_hits_;

        // Write-Through: every write (hit or miss) must propagate to lower level
        if (!is_write) return true;
        if (cfg_.write_pol == WritePolicy::WTWNA && next_level_ != nullptr) {
            next_level_->access('W', address);
        }
        return true;
    }

    // ── MISS ─────────────────────────────────────────────────────────────────
    is_write ? ++write_misses_ : ++read_misses_;

    // No-Write-Allocate: write miss bypasses this cache; just forward to lower level
    if (is_write && cfg_.write_pol == WritePolicy::WTWNA) {
        if (next_level_ != nullptr) next_level_->access('W', address);
        return false;
    }

    // For everything else (read miss, or WBWA write miss) we must fill the block
    handle_miss(type, address, tag, index);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// handle_miss
//   • writeback evicted dirty block to lower level (WBWA)
//   • fetch the missed block from lower level
//   • install it into this set — exactly ONE allocation, no double-access bug
// ─────────────────────────────────────────────────────────────────────────────
void Cache::handle_miss(char type, uint64_t address, uint64_t tag, uint32_t index) {
    const bool  is_write = (type == 'W');
    const std::string evpol = (cfg_.eviction == EvictionPolicy::LRU) ? "LRU" : "FIFO";

    // ── Step 1: fetch block from lower level (or pretend main memory supplies it)
    if (next_level_ != nullptr) {
        next_level_->access('R', address);   // bring block into L2 (if not already there)
    }

    // ── Step 2: install block into this set (allocate=true for WBWA)
    //    We call access() again on the set with allocate=true to perform the install.
    //    Because step 1 already fetched the data, this call is guaranteed to either
    //    find an empty slot or trigger exactly one eviction — never a spurious hit.
    AccessResult install = sets_[index].access(tag, is_write, evpol, /*allocate=*/true);

    // ── Step 3: if a dirty block was evicted during install, write it back
    if (install.writeback_needed) {
        ++writebacks_;
        if (next_level_ != nullptr) {
            uint64_t wb_addr = reconstruct_address(install.evicted_tag, index);
            next_level_->access('W', wb_addr);

            // ── Inclusive policy: eviction from L1 must invalidate in L2 ──────
            // (If this is L1 and L2 is inclusive, the evicted tag must leave L2 too.)
            // We only do this for WBWA; WT caches are always consistent.
            if (cfg_.write_pol == WritePolicy::WBWA) {
                next_level_->invalidate_block(wb_addr);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// invalidate_block  — remove a block from this cache (inclusive protocol)
// ─────────────────────────────────────────────────────────────────────────────
void Cache::invalidate_block(uint64_t address) {
    uint64_t tag   = 0;
    uint32_t index = 0;
    parse_address(address, tag, index);

    bool was_dirty = false;
    sets_[index].invalidate(tag, was_dirty);
    // If dirty, someone upstream already wrote back — no further action needed here.
}

bool Cache::probe(uint64_t address) const {
    uint64_t tag   = 0;
    uint32_t index = 0;
    parse_address(address, tag, index);
    return sets_[index].contains(tag);
}

// ─────────────────────────────────────────────────────────────────────────────
// print_stats
// ─────────────────────────────────────────────────────────────────────────────
void Cache::print_stats() const {
    const uint64_t total   = reads_ + writes_;
    const uint64_t hits    = read_hits_ + write_hits_;
    const uint64_t misses  = read_misses_ + write_misses_;
    const double   hit_rt  = total ? 100.0 * hits  / total : 0.0;
    const double   miss_rt = total ? 100.0 * misses / total : 0.0;

    const uint32_t cache_kb =
        (cfg_.num_sets * cfg_.associativity * cfg_.block_size) / 1024;

    std::cout << "┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│  Layer : " << std::left << std::setw(42) << cfg_.name << "│\n";
    std::cout << "│  Size  : " << std::setw(5) << cache_kb << " KB  |  "
              << cfg_.associativity << "-way  |  "
              << cfg_.block_size << "B blocks  |  "
              << ((cfg_.eviction == EvictionPolicy::LRU) ? "LRU " : "FIFO")
              << "               │\n";
    std::cout << "├─────────────────────────────────────────────────────┤\n";
    std::cout << "│  Total Accesses : " << std::setw(10) << total          << "                         │\n";
    std::cout << "│  Read  Hits     : " << std::setw(10) << read_hits_
              << "  Read  Misses : " << std::setw(7) << read_misses_  << " │\n";
    std::cout << "│  Write Hits     : " << std::setw(10) << write_hits_
              << "  Write Misses : " << std::setw(7) << write_misses_ << " │\n";
    std::cout << "│  Writebacks     : " << std::setw(10) << writebacks_    << "                         │\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "│  Hit  Rate      : " << std::setw(6)  << hit_rt  << "%"
              << "   Miss Rate   : " << std::setw(6) << miss_rt << "%       │\n";
    std::cout << "└─────────────────────────────────────────────────────┘\n\n";
}
