#pragma once

#include "TranspositionTable.h"
#include "See.h"
#include "SearchHistory.h"

namespace batch {
    using namespace bstate;
    using namespace tt;
    using namespace hist;

    struct Batch {
        static constexpr uint32_t BIT_FLIP = 0x80000000u;
        BoardState moves[MOVES_MAX];

        uint64_t keys[MOVES_MAX];
        uint64_t badNoisy[MOVES_MAX];
        uint64_t badQuiet[MOVES_MAX];

        int size = 0;
        int nGood = 0, curGood = 0;
        int nBadNoisy = 0, curBadNoisy = 0;
        int nBadQuiet = 0, curBadQuiet = 0;

        BoardState* triedQ[MOVES_MAX];
        BoardState* triedN[MOVES_MAX];
        int nQuiet = 0;
        int nNoisy = 0;

        bool sort = true;
        U64 threats[7]{};

        uint16_t ttMove = 0;
        uint16_t seMove = 0;
        uint16_t k1, k2, cm;

        SearchHistory* hist;
        int16_t(*ph)[64];
        int16_t(*c1)[64];
        int16_t(*c2)[64];
        int16_t(*c3)[64];

        template <bool side, bool cap, uint8_t promo = 0, uint8_t mvType = NORMAL, class Build>
        __forceinline void add(const BoardState& prev, int from, int to, Build&& build) noexcept {
            if (sort) {
                const uint16_t pm = uint16_t(from | (to << 6) | ((promo ? (promo - 1) : 0) << 12) | (mvType << 14));

                if (pm == ttMove || pm == seMove) return;

                BoardState& b = moves[size] = build();

                tt::prefetch(b.zobrist);

                int32_t score = 0;

                if constexpr (promo || cap) {
                    if constexpr (promo) score += see::SEE_PROMO[b.promo] * 16;
                    if constexpr (cap) {
                        const bool recap = (prev.capSq != noSquare && prev.to == b.to);
                        score += MVV_LVA[b.vctm][b.atkr] + (hist->noisyHistory[side][b.atkr][b.to][b.vctm] / (2 - recap));
                    }
                }
                else {
                    score += hist->history[side][(threats[6] >> b.from) & 1][(threats[6] >> b.to) & 1][b.from][b.to];
                    score += (ph[b.atkr][b.to] * ORD_PAWN + contOrder(c1, c2, c3, b.atkr, b.to)) / 1024;

                    const int fromThreat = int((threats[b.atkr] >> b.from) & 1);
                    const int toThreat = int((threats[b.atkr] >> b.to) & 1);
                    score += (fromThreat - toThreat) * (THREAT_VALUE[b.atkr] * THREAT_MUL / 3);

                    if      (pm == k1) score += KILLER_1;
                    else if (pm == k2) score += KILLER_2;
                    else if (pm == cm) score += COUNTER_MOVE_BONUS;

                    if (b.checks && see::see(b, CHECK_SEE_THRESHOLD)) score += CHECK_BONUS;
                }

                const uint64_t key = (uint64_t(uint32_t(score) ^ BIT_FLIP) << 32) | size;

                if constexpr (cap || promo) keys[nGood++] = key;
                else if (score >= 0)        keys[nGood++] = key;
                else                        badQuiet[nBadQuiet++] = key;
            }
            else moves[size] = build();

            ++size;
        }

        inline void reset() noexcept {
            size = 0;
        }

        inline void init() noexcept {
            size = 0;
            nGood = curGood = 0;
            nBadNoisy = curBadNoisy = 0;
            nBadQuiet = curBadQuiet = 0;
            ttMove = 0;
            seMove = 0;
        }

        __forceinline BoardState* pick()         noexcept { return select(keys, nGood, curGood); }
        __forceinline BoardState* pickBadNoisy() noexcept { return select(badNoisy, nBadNoisy, curBadNoisy); }
        __forceinline BoardState* pickBadQuiet() noexcept { return select(badQuiet, nBadQuiet, curBadQuiet); }

        __forceinline int score() const noexcept { return int(keys[curGood - 1] >> 32 ^ BIT_FLIP); }

        __forceinline void demote() noexcept { badNoisy[nBadNoisy++] = keys[curGood - 1]; }

        template <bool side>
        __forceinline void prefetch(Zobrist pKey, int ply) {
            ph = hist->pawnHistory[pKey & PH_MASK][side];
            c1 = hist->contHistory[0][hist->contKeys[ply + 3]];
            c2 = hist->contHistory[1][hist->contKeys[ply + 2]];
            c3 = hist->contHistory[2][hist->contKeys[ply]];
            //6 * 64 entries in both tables, times 2 bytes per entry
            for (int i = 0; i < 6 * 64 * 2; i += 64) {
                _mm_prefetch(reinterpret_cast<const char*>(ph) + i, _MM_HINT_T0);
                _mm_prefetch(reinterpret_cast<const char*>(c1) + i, _MM_HINT_T0);
                _mm_prefetch(reinterpret_cast<const char*>(c2) + i, _MM_HINT_T0);
                _mm_prefetch(reinterpret_cast<const char*>(c3) + i, _MM_HINT_T0);
            }
        }

        template <bool side>
        __forceinline void prepareQuiet(const BoardState& board, int ply) {
            k1 = hist->killers[ply][0];
            k2 = hist->killers[ply][1];
            cm = hist->cntrMove[side][board.atkr][board.to];
        }

    private:
        inline BoardState* select(uint64_t* k, int n, int& cur) noexcept {
            if (cur >= n) return nullptr;

            int best = cur;
            uint64_t bestKey = k[cur];
            for (int j = cur + 1; j < n; ++j) {
                if (k[j] > bestKey) { best = j; bestKey = k[j]; }
            }
            k[best] = k[cur];
            k[cur++] = bestKey;

            return &moves[bestKey & 0xFFFF];
        }
    };
}