#include "../Include/Cache.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Usage:
//   cache_sim <trace_file>
//             [l1_sets l1_block l1_ways l1_evict l1_wpol]
//             [l2_sets l2_block l2_ways l2_evict l2_wpol]
//
// Defaults (when no extra args supplied):
//   L1 :  64 sets, 64 B blocks,  4-way, LRU, WBWA   →   16 KB
//   L2 : 512 sets, 64 B blocks,  8-way, LRU, WBWA   →  256 KB
//
// Examples:
//   ./cache_sim trace.trace
//   ./cache_sim trace.trace 128 64 4 LRU WBWA 1024 64 16 LRU WBWA
// ─────────────────────────────────────────────────────────────────────────────

static EvictionPolicy parse_eviction(const std::string& s) {
    if (s == "FIFO") return EvictionPolicy::FIFO;
    return EvictionPolicy::LRU;
}

static WritePolicy parse_write(const std::string& s) {
    if (s == "WTWNA") return WritePolicy::WTWNA;
    return WritePolicy::WBWA;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <trace_file> [l1_sets l1_blk l1_ways l1_ev l1_wp"
                  << " l2_sets l2_blk l2_ways l2_ev l2_wp]\n";
        return 1;
    }

    // ── Parse optional configuration args ────────────────────────────────────
    CacheConfig l1_cfg, l2_cfg;

    l1_cfg.name         = "L1-Data";
    l1_cfg.num_sets     = 64;
    l1_cfg.block_size   = 64;
    l1_cfg.associativity= 4;
    l1_cfg.eviction     = EvictionPolicy::LRU;
    l1_cfg.write_pol    = WritePolicy::WBWA;

    l2_cfg.name         = "L2-Unified";
    l2_cfg.num_sets     = 512;
    l2_cfg.block_size   = 64;
    l2_cfg.associativity= 8;
    l2_cfg.eviction     = EvictionPolicy::LRU;
    l2_cfg.write_pol    = WritePolicy::WBWA;

    if (argc == 12) {
        l1_cfg.num_sets      = static_cast<uint32_t>(std::stoul(argv[2]));
        l1_cfg.block_size    = static_cast<uint32_t>(std::stoul(argv[3]));
        l1_cfg.associativity = static_cast<uint32_t>(std::stoul(argv[4]));
        l1_cfg.eviction      = parse_eviction(argv[5]);
        l1_cfg.write_pol     = parse_write(argv[6]);

        l2_cfg.num_sets      = static_cast<uint32_t>(std::stoul(argv[7]));
        l2_cfg.block_size    = static_cast<uint32_t>(std::stoul(argv[8]));
        l2_cfg.associativity = static_cast<uint32_t>(std::stoul(argv[9]));
        l2_cfg.eviction      = parse_eviction(argv[10]);
        l2_cfg.write_pol     = parse_write(argv[11]);
    }

    // ── Build hierarchy ───────────────────────────────────────────────────────
    Cache l1(l1_cfg);
    Cache l2(l2_cfg);
    l1.set_next_level(&l2);

    // ── Open trace file ───────────────────────────────────────────────────────
    std::ifstream infile(argv[1]);
    if (!infile.is_open()) {
        std::cerr << "Error: cannot open trace file '" << argv[1] << "'\n";
        return 1;
    }

    std::cout << "Processing: " << argv[1] << "\n\n";

    std::string line;
    uint64_t    line_num = 0;

    while (std::getline(infile, line)) {
        ++line_num;
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        char        type    = 0;
        std::string addr_str;

        if (!(ss >> type >> addr_str)) continue;   // size field is optional, ignore

        if (type != 'R' && type != 'W' && type != 'I') continue;

        uint64_t address = 0;
        try {
            address = std::stoull(addr_str, nullptr, 16);
        } catch (...) {
            std::cerr << "Warning: bad address on line " << line_num
                      << " — skipping\n";
            continue;
        }

        // Treat instruction fetches as reads (unified-cache model)
        if (type == 'I') type = 'R';

        l1.access(type, address);
    }

    infile.close();

    // ── Print results ─────────────────────────────────────────────────────────
    std::cout << "\n════════════ SIMULATION RESULTS ════════════\n\n";
    l1.print_stats();
    l2.print_stats();

    return 0;
}
