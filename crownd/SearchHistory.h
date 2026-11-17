#pragma once

#include "Definitions.h"
#include "BoardState.h"
#include <cstdint>
#include <cstring>

namespace hist {
    using namespace defs;

    inline constexpr int CONT_KEYS = 6 * 64 + 1;

    struct SearchHistory {
        //int16_t contCorr[2][7][65];
        int16_t contCorr[2][2][CONT_KEYS][6][64];
        int16_t pawnCorr[2][CORR_SIZE];
        int16_t nonPawnCorr[2][2][CORR_SIZE];
        int16_t minorCorr[2][CORR_SIZE];
        int16_t majorCorr[2][CORR_SIZE];

        int16_t history[2][2][2][64][64];
        int16_t noisyHistory[2][6][64][7];
        int16_t contKeys[MAX_PLY + 4];
        int16_t contHistory[3][CONT_KEYS][6][64];
        int16_t pawnHistory[PH_SIZE][2][6][64];

        uint16_t killers[MAX_PLY][2];
        uint16_t cntrMove[2][7][65];

        inline void clear() noexcept { std::memset(this, 0, sizeof(*this)); }
        inline void clearKillers() noexcept { std::memset(killers, 0, sizeof(killers)); }
    };

    ForceInline int calcBonus(int depth) {
        return std::min(HIST_BONUS_MUL * depth - HIST_BONUS_SUB, HIST_BONUS_MAX);

    }
    ForceInline int calcMalus(int depth) {
        return std::min(HIST_MALUS_MUL * depth - HIST_MALUS_SUB, HIST_MALUS_MAX);
    }

    ForceInline void updateHistory(int16_t& val, int bonus) {
        val += bonus - val * std::abs(bonus) / HIST_MAX;
    }

    ForceInline void updateCorrectionHistory(int16_t& val, int bonus) {
        val += bonus - val * std::abs(bonus) / CORR_MAX;
    }

    ForceInline void updateContKey(const BoardState& m, SearchHistory& h, int ply) {
        h.contKeys[ply + 4] = int16_t(m.atkr * 64 + m.to + 1);
    }

    ForceInline void clearContKey(SearchHistory& h, int ply) {
        h.contKeys[ply + 4] = 0;
    }

    ForceInline int contValue(const SearchHistory& h, int type, int key, int pc, int to) {
        return h.contHistory[type][key][pc][to];
    }

    ForceInline int contOrder(const int16_t(*c1)[64], const int16_t(*c2)[64], const int16_t(*c4)[64], int pc, int to) {
        return c1[pc][to] * CONT_ORD_1 + c2[pc][to] * CONT_ORD_2 + c4[pc][to] * CONT_ORD_4;
    }

    ForceInline void contUpdate(SearchHistory& h, int ply, int pc, int to, int bonus) {
        if (h.contKeys[ply + 3] > 0) updateHistory(h.contHistory[0][h.contKeys[ply + 3]][pc][to], bonus * CONT_UPD_1 / 1024);
        if (h.contKeys[ply + 2] > 0) updateHistory(h.contHistory[1][h.contKeys[ply + 2]][pc][to], bonus * CONT_UPD_2 / 1024);
        if (h.contKeys[ply]     > 0) updateHistory(h.contHistory[2][h.contKeys[ply]][pc][to], bonus * CONT_UPD_4 / 1024);
    }
}