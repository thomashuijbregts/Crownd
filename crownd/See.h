#pragma once

#include "Definitions.h"
#include "BoardState.h"
#include <algorithm>

using namespace defs;
using namespace bstate;

namespace see {
    constexpr int SEE_VALUE[7] = { 100, 320, 330, 500, 900, 10000, 0 };
    constexpr int SEE_PROMO[5] = { 0, 220, 230, 400, 800 };

    constexpr bool bishop[6] = { true, false, true, false, true, false };
    constexpr bool rook[6] = { false, false, false, true, true, false };

    ForceInline U64 getAttackers(const BoardState& board, int sq, U64 occ, U64 bq, U64 rq) {
        U64 atkers = 0;
        atkers |= PAWN_CAPTURES[!board.side][sq] & board.pM();
        atkers |= PAWN_CAPTURES[board.side][sq] & board.pE();
        atkers |= getKnightAttacks(sq) & board.pcs[n];
        atkers |= getKingAttacks(sq) & board.pcs[k];
        if (bq) atkers |= getBishopAttacks(sq, occ) & bq;
        if (rq) atkers |= getRookAttacks(sq, occ) & rq;
        return atkers;
    }

    Inline bool see(const BoardState& board, int threshold) {
        if (board.atkr == k) return SEE_VALUE[board.vctm] >= threshold;

        int value = SEE_VALUE[board.vctm] + SEE_PROMO[board.promo] - threshold;
        if (value < 0) return false;
        value = (board.mvType == PROMO ? SEE_VALUE[board.promo] : SEE_VALUE[board.atkr]) - value;
        if (value <= 0) return true;

        const U64 bq = board.pcs[b] | board.pcs[q];
        const U64 rq = board.pcs[r] | board.pcs[q];

        U64 us = board.occM;
        U64 them = board.occE;

        U64 fromBit;
        const int to = board.to;
        U64 occ = board.occB();
        U64 atkers = getAttackers(board, to, occ, bq, rq);
        bool res = true;

        while (true) {
            U64 atks = atkers & us;
            if (!atks) break;

            int pc = p;
            for (; pc <= k; ++pc) { atks = atkers & board.pcs[pc] & us; if (atks) break; }
            res = !res;
            if (pc == k) return (atkers & them) ? !res : res;

            if ((value = SEE_VALUE[pc] - value) < res) break;

            fromBit = _blsi_u64(atks);

            occ ^= fromBit;
            atkers ^= fromBit;
            
            if (bishop[pc] && (bq & occ)) atkers |= getBishopAttacks(to, occ) & bq & occ;
            if (rook[pc] && (rq & occ))   atkers |= getRookAttacks(to, occ) & rq & occ;

            std::swap(us, them);
        }

        return res;
    }
}