#include "../Include/CacheSet.h"
#include <stdexcept>
#include <limits>

CacheSet::CacheSet(uint32_t assoc) : associativity_(assoc) {
    lines_.resize(assoc);
}

// ─────────────────────────────────────────────────────────────────────────────
// access
// ─────────────────────────────────────────────────────────────────────────────
AccessResult CacheSet::access(uint64_t tag, bool is_write,
                               const std::string& eviction_policy,
                               bool allocate) {
    ++access_clock_;
    AccessResult result;

    // ── 1. HIT PROBE ─────────────────────────────────────────────────────────
    for (uint32_t i = 0; i < associativity_; ++i) {
        if (lines_[i].valid && lines_[i].tag == tag) {
            result.hit = true;
            if (is_write) {
                lines_[i].dirty = true;   // mark modified (WB policy)
            }
            if (eviction_policy == "LRU") {
                lines_[i].lru_counter = access_clock_;
            }
            return result;
        }
    }

    // ── 2. MISS — no-write-allocate shortcut ─────────────────────────────────
    // For WTWNA: on a write-miss we do NOT install the block; just return miss.
    if (is_write && !allocate) {
        return result;   // result.hit == false, no allocation
    }

    // ── 3. FIND EMPTY SLOT ───────────────────────────────────────────────────
    for (uint32_t i = 0; i < associativity_; ++i) {
        if (!lines_[i].valid) {
            lines_[i].valid        = true;
            lines_[i].tag          = tag;
            lines_[i].dirty        = is_write;   // dirty immediately on write-allocate
            lines_[i].lru_counter  = access_clock_;
            lines_[i].fifo_counter = access_clock_;
            return result;                        // miss, clean fill
        }
    }

    // ── 4. EVICTION ──────────────────────────────────────────────────────────
    uint32_t victim = find_victim(eviction_policy);

    if (lines_[victim].dirty) {
        result.writeback_needed = true;
        result.evicted_tag      = lines_[victim].tag;
    }

    lines_[victim].tag          = tag;
    lines_[victim].dirty        = is_write;
    lines_[victim].valid        = true;
    lines_[victim].lru_counter  = access_clock_;
    lines_[victim].fifo_counter = access_clock_;   // reset FIFO birth-time for new occupant

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// invalidate
// ─────────────────────────────────────────────────────────────────────────────
bool CacheSet::invalidate(uint64_t tag, bool& was_dirty) {
    for (auto& line : lines_) {
        if (line.valid && line.tag == tag) {
            was_dirty  = line.dirty;
            line.valid = false;
            line.dirty = false;
            return true;
        }
    }
    was_dirty = false;
    return false;
}

bool CacheSet::contains(uint64_t tag) const {
    for (const auto& line : lines_) {
        if (line.valid && line.tag == tag) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// find_victim  (pure query — does NOT modify state)
// ─────────────────────────────────────────────────────────────────────────────
uint32_t CacheSet::find_victim(const std::string& policy) const {
    uint32_t victim = 0;
    uint32_t min_val = std::numeric_limits<uint32_t>::max();

    for (uint32_t i = 0; i < associativity_; ++i) {
        uint32_t key = (policy == "FIFO") ? lines_[i].fifo_counter
                                          : lines_[i].lru_counter;
        if (key < min_val) {
            min_val = key;
            victim  = i;
        }
    }
    return victim;
}
