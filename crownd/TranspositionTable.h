#pragma once
#include "Definitions.h"
#include "Utils.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <new>
#include <bit>
#include <climits>

using namespace defs;

namespace tt {
    constexpr int MAX_ENTRIES = 3;

    constexpr U64 KEY_MASK = 0xFFFF;
    constexpr U64 SCORE_MASK = 0xFFFF;
    constexpr U64 EVAL_MASK = 0xFFFF;
    constexpr U64 MOVE_MASK = 0xFFFF;
    constexpr U64 DEPTH_MASK = 0x7F;
    constexpr U64 BOUND_MASK = 0x3;
    constexpr U64 GEN_MASK = 0x3F;
    constexpr U64 PV_MASK = 0x1;

    constexpr U64 EVAL_SHIFT = 16;
    constexpr U64 MOVE_SHIFT = 32;
    constexpr U64 DEPTH_SHIFT = 48;
    constexpr U64 BOUND_SHIFT = 55;
    constexpr U64 GEN_SHIFT = 57;
    constexpr U64 PV_SHIFT = 63;

    struct alignas(32) Bucket {
        uint16_t key[MAX_ENTRIES];
        U64      data[MAX_ENTRIES];
    };

    struct TTData {
        int16_t  score;
        int16_t  eval;
        uint16_t move;
        uint8_t  depth;
        uint8_t  bound;
        bool     pv;
    };

    enum Bound : uint8_t {
        BNO = 0,
        BUP = 1,
        BLO = 2,
        BEX = 3
    };

    inline Bucket* TABLE = nullptr;
    inline U64     MASK = 0;
    inline U64     BYTES = 0;
    inline uint8_t gen = 0;

    inline bool largePages = false;

    ForceInline U64 index(Zobrist z) noexcept {
        return z & MASK;
    }

    ForceInline Bucket& bucket(Zobrist z) noexcept {
        return TABLE[index(z)];
    }

    ForceInline bool probe(Bucket& b, Zobrist z, TTData& out) noexcept {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            const U64 data = b.data[i];
            if (data && b.key[i] == ((z ^ data) >> 48 & KEY_MASK)) {
                out.score = int16_t(data & SCORE_MASK);
                out.eval  = int16_t((data >> EVAL_SHIFT) & EVAL_MASK);
                out.move  = (data >> MOVE_SHIFT) & MOVE_MASK;
                out.depth = (data >> DEPTH_SHIFT) & DEPTH_MASK;
                out.bound = (data >> BOUND_SHIFT) & BOUND_MASK;
                out.pv    = (data >> PV_SHIFT) & PV_MASK;
                return true;
            }
        }
        return false;
    }

    ForceInline void write(int depth, Bucket& b, Zobrist z, int score, int eval, uint8_t bound, uint16_t move, uint8_t gen, bool pv) noexcept {
        int v = 0;
        int lowest = INT_MAX;

        for (int i = 0; i < MAX_ENTRIES; ++i) {
            const U64 data = b.data[i];

            if (data == 0) {
                v = i;
                break;
            }

            if (b.key[i] == ((z ^ data) >> 48 & KEY_MASK)) {
                int d = (data >> DEPTH_SHIFT) & DEPTH_MASK;
                int g = (data >> GEN_SHIFT) & GEN_MASK;
                bool stale = ((gen - g) & GEN_MASK) != 0;
                if (bound != BEX && !stale && depth < d - TT_REPLACE_DEPTH) return;
                v = i;
                break;
            }

            int d = (data >> DEPTH_SHIFT) & DEPTH_MASK;
            int g = (data >> GEN_SHIFT) & GEN_MASK;
            int age = int((gen - g) & GEN_MASK);

            int val = d - age * TT_AGE_WEIGHT;

            if (val < lowest) { lowest = val; v = i; }
        }

        const U64 data =
            U64(uint16_t(score))
            | U64(uint16_t(eval)) << EVAL_SHIFT
            | U64(move) << MOVE_SHIFT
            | U64(depth & DEPTH_MASK) << DEPTH_SHIFT
            | U64(bound & BOUND_MASK) << BOUND_SHIFT
            | U64(gen & GEN_MASK) << GEN_SHIFT
            | U64(pv & PV_MASK) << PV_SHIFT;

        b.key[v]  = ((z ^ data) >> 48 & KEY_MASK);
        b.data[v] = data;
    }

    ForceInline void prefetch(Zobrist z) noexcept {
        _mm_prefetch(reinterpret_cast<const char*>(&TABLE[index(z)]), _MM_HINT_T0);
    }

    inline void clear() {
        std::memset(TABLE, 0, BYTES);
        gen = 0;
    }

    void init(size_t mb = 0);
    void free();
}