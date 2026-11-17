#pragma once

#include "Definitions.h"
#include "Zobrist.h"

namespace cuckoo {
    using namespace defs;
    using namespace zobrist;

    inline Zobrist keys[8192];
    inline uint16_t moves[8192];

    inline constexpr int MASK = 8192 - 1;

    ForceInline int hash1(Zobrist z) { return int(z & MASK); }
    ForceInline int hash2(Zobrist z) { return int((z >> 16) & MASK); }

    inline void init() {
        std::memset(keys, 0, sizeof(keys));
        std::memset(moves, 0, sizeof(moves));

        for (int side = 0; side < 2; ++side) {
            for (int pc = n; pc <= k; ++pc) {
                for (int s1 = 0; s1 < 64; ++s1) {
                    for (int s2 = s1 + 1; s2 < 64; ++s2) {
                        Zobrist key;
                        U64 atks;

                        switch (pc) {
                            case n: key = zobrist::KNIGHTS[side][s1] ^ zobrist::KNIGHTS[side][s2] ^ zobrist::SIDE; atks = getKnightAttacks(s1);       break;
                            case b: key = zobrist::BISHOPS[side][s1] ^ zobrist::BISHOPS[side][s2] ^ zobrist::SIDE; atks = getBishopAttacks(s1, 0ULL); break;
                            case r: key = zobrist::ROOKS[side][s1]   ^ zobrist::ROOKS[side][s2]   ^ zobrist::SIDE; atks = getRookAttacks(s1, 0ULL);   break;
                            case q: key = zobrist::QUEENS[side][s1]  ^ zobrist::QUEENS[side][s2]  ^ zobrist::SIDE; atks = getQueenAttacks(s1, 0ULL);  break;
                            case k: key = zobrist::KINGS[side][s1]   ^ zobrist::KINGS[side][s2]   ^ zobrist::SIDE; atks = getKingAttacks(s1);         break;
                        }

                        if (!(atks & (1ULL << s2))) continue;

                        uint16_t move = uint16_t(s1 | s2 << 6);

                        int i = hash1(key);
                        while (true) {
                            std::swap(keys[i], key);
                            std::swap(moves[i], move);
                            if (move == 0) break;
                            i = (i == hash1(key)) ? hash2(key) : hash1(key);
                        }
                    }
                }
            }
        }
    }
}