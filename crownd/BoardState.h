#pragma once

#include <cstdint>
#include "Definitions.h"
#include "Zobrist.h"

namespace bstate {
    using namespace defs;
    using namespace zobrist;

    struct alignas(64) BoardState {
        U64 pcs[6];

        U64 occM;
        U64 occE;

        U64 checks;

        Zobrist zobrist;
        uint32_t pawnKey;
        uint32_t nonPawnKey[2];
        uint32_t minorKey;
        uint32_t majorKey;

        int8_t from;
        int8_t to;
        int8_t kMS;
        int8_t kES;
        int8_t casPerms;
        int8_t eP;
        int8_t vctm;
        int8_t atkr;
        int8_t promo;
        int8_t capSq;
        uint8_t halfClock;
        uint8_t mvType;
        int8_t rFrom;
        int8_t rTo;
        bool side;
        bool quiet;

        BoardState() = default;

        BoardState(
            int8_t from, int8_t to,
            U64 pM, U64 nM, U64 bM, U64 rM, U64 qM, U64 kM,
            U64 pE, U64 nE, U64 bE, U64 rE, U64 qE, U64 kE,
            int8_t kMS, int8_t kES,
            U64 occM, U64 occE,
            U64 checks, int8_t casPerms, int8_t eP, int8_t vctm, int8_t atkr, int8_t promoted,
            int8_t capSq, bool side, bool quiet, uint8_t halfClock, uint8_t mvType, Zobrist zobrist) noexcept :
            pcs{ pM | pE, nM | nE, bM | bE, rM | rE, qM | qE, kM | kE },
            occM(occM), occE(occE),
            checks(checks), zobrist(zobrist),
            from(from), to(to),
            kMS(kMS), kES(kES),
            casPerms(casPerms), eP(eP), vctm(vctm), atkr(atkr), promo(promoted),
            capSq(capSq), halfClock(halfClock), mvType(mvType),
            side(side), quiet(quiet) { }

        __forceinline U64 occB() const noexcept { return occM | occE; }

        __forceinline U64 pM() const noexcept { return pcs[p] & occM; }
        __forceinline U64 nM() const noexcept { return pcs[n] & occM; }
        __forceinline U64 bM() const noexcept { return pcs[b] & occM; }
        __forceinline U64 rM() const noexcept { return pcs[r] & occM; }
        __forceinline U64 qM() const noexcept { return pcs[q] & occM; }
        __forceinline U64 kM() const noexcept { return pcs[k] & occM; }

        __forceinline U64 pE() const noexcept { return pcs[p] & occE; }
        __forceinline U64 nE() const noexcept { return pcs[n] & occE; }
        __forceinline U64 bE() const noexcept { return pcs[b] & occE; }
        __forceinline U64 rE() const noexcept { return pcs[r] & occE; }
        __forceinline U64 qE() const noexcept { return pcs[q] & occE; }
        __forceinline U64 kE() const noexcept { return pcs[k] & occE; }

        __forceinline uint16_t toInt() noexcept {
            const uint8_t pr = promo ? (promo - 1) : 0;
            return uint16_t(from | (to << 6) | (pr << 12) | (mvType << 14));
        }
    };

    template <Pieces piece, bool side, bool capture>
    ForceInline BoardState make(int from, int to, const BoardState& board, U64 discovers) noexcept {
        const U64 t = (1ULL << to);
        const U64 move = (1ULL << from) | t;

        BoardState next = board;

        const U64 occM = board.occM ^ move;
        U64 occE = board.occE;
        U64 occB;
        if constexpr (capture)  occB = board.occB() ^ (1ULL << from);
        else                    occB = board.occB() ^ move;

        uint8_t halfClock = board.halfClock;
        if constexpr (piece == p || capture) halfClock = 0;
        else halfClock++;

        int casPerms = board.casPerms;
        if constexpr (r == piece) casPerms &= NO_CASTLE_ROOK[from];
        if constexpr (k == piece) casPerms &= NO_CASTLE[side];

        int8_t v = noPiece;
        int8_t capSq = noSquare;
        Zobrist zob, pawnKey = board.pawnKey, minorKey = board.minorKey, majorKey = board.majorKey;
        Zobrist nonPawnKey[2] = { board.nonPawnKey[0], board.nonPawnKey[1] };

        if constexpr (capture) {
            occE ^= t;
            capSq = (int8_t)to;

            if (board.pcs[p] & t) {
                next.pcs[p] ^= t;
                zob = zobrist::basic<piece, side, p>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = p;
            }
            else if (board.pcs[n] & t) {
                next.pcs[n] ^= t;
                zob = zobrist::basic<piece, side, n>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = n;
            }
            else if (board.pcs[b] & t) {
                next.pcs[b] ^= t;
                zob = zobrist::basic<piece, side, b>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = b;
            }
            else if (board.pcs[r] & t) {
                casPerms &= NO_CASTLE_ROOK[to];

                next.pcs[r] ^= t;
                zob = zobrist::basic<piece, side, r>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = r;
            }
            else {
                next.pcs[q] ^= t;
                zob = zobrist::basic<piece, side, q>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = q;
            }
        }
        else {
            zob = zobrist::basic<piece, side, k>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
        }

        next.pcs[piece] ^= move;

        U64 checks = 0ULL;
        if constexpr (p == piece) checks |= PAWN_CAPTURES[!side][board.kES] & next.pcs[p] & occM;
        if constexpr (n == piece) checks |= KNIGHT_ATTACKS[board.kES] & next.pcs[n] & occM;
        if constexpr (b == piece) {
            if ((BISHOP_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }
        if constexpr (r == piece) {
            if ((ROOK_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }
        if constexpr (q == piece) {
            if ((QUEEN_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }

        if constexpr (q != piece) {
            if (discovers) [[unlikely]] {
                U64 disc = discovers & DISCOVER_RAYS[board.kES][from];
                if (disc && !(DISCOVER_RAYS[board.kES][to] & disc)) [[unlikely]] checks |= disc;
            }
        }

        next.occM = occE;
        next.occE = occM;
        next.checks = checks;
        next.zobrist = zob;
        next.pawnKey = uint32_t(pawnKey);
        next.nonPawnKey[0] = uint32_t(nonPawnKey[0]);
        next.nonPawnKey[1] = uint32_t(nonPawnKey[1]);
        next.minorKey = uint32_t(minorKey);
        next.majorKey = uint32_t(majorKey);
        next.from = int8_t(from);
        next.to = int8_t(to);
        next.kMS = board.kES;
        next.kES = k == piece ? int8_t(to) : board.kMS;
        next.casPerms = (int8_t)casPerms;
        next.eP = noSquare;
        next.vctm = v;
        next.atkr = int8_t(piece);
        next.promo = 0;
        next.capSq = capSq;
        next.halfClock = halfClock;
        next.mvType = MoveType::NORMAL;
        next.rFrom = 0;
        next.rTo = 0;
        next.side = !side;
        next.quiet = !capture;

        return next;
    }

    template <Pieces piece, bool side, bool capture>
    ForceInline BoardState makePromotion(int from, int to, const BoardState& board, U64 discovers) noexcept {
        const U64 f = (1ULL << from);
        const U64 t = (1ULL << to);

        BoardState next = board;

        const U64 occM = board.occM ^ (f | t);
        U64 occE = board.occE;
        U64 occB;
        if constexpr (capture)  occB = board.occB() ^ f;
        else                    occB = board.occB() ^ (f | t);

        int casPerms = board.casPerms;
        int8_t v = noPiece;
        int8_t capSq = noSquare;
        Zobrist zob, pawnKey = board.pawnKey, minorKey = board.minorKey, majorKey = board.majorKey;
        Zobrist nonPawnKey[2] = { board.nonPawnKey[0], board.nonPawnKey[1] };

        if constexpr (capture) {
            occE ^= t;
            capSq = (int8_t)to;

            if (board.pcs[n] & t) {
                next.pcs[n] ^= t;
                zob = zobrist::promotion<piece, side, n>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = n;
            }
            else if (board.pcs[b] & t) {
                next.pcs[b] ^= t;
                zob = zobrist::promotion<piece, side, b>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = b;
            }
            else if (board.pcs[r] & t) {
                casPerms &= NO_CASTLE_ROOK[to];

                next.pcs[r] ^= t;
                zob = zobrist::promotion<piece, side, r>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = r;
            }
            else {
                next.pcs[q] ^= t;
                zob = zobrist::promotion<piece, side, q>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
                v = q;
            }
        }
        else {
            zob = zobrist::promotion<piece, side, k>(from, to, casPerms, board.casPerms, board.eP, board.zobrist, pawnKey, nonPawnKey, minorKey, majorKey);
        }

        next.pcs[p] ^= f;
        next.pcs[piece] ^= t;

        U64 checks = 0ULL;
        if constexpr (n == piece) checks |= KNIGHT_ATTACKS[board.kES] & next.pcs[n] & occM;
        if constexpr (b == piece) {
            if ((BISHOP_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }
        if constexpr (r == piece) {
            if ((ROOK_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }
        if constexpr (q == piece) {
            if ((QUEEN_XRAYS[board.kES] & t) && !(PIN_MASKS[board.kES][to] & occB)) [[unlikely]] checks |= t;
        }

        if (discovers) [[unlikely]] checks |= discovers & DISCOVER_RAYS[board.kES][from];

        next.occM = occE;
        next.occE = occM;
        next.checks = checks;
        next.zobrist = zob;
        next.pawnKey = uint32_t(pawnKey);
        next.nonPawnKey[0] = uint32_t(nonPawnKey[0]);
        next.nonPawnKey[1] = uint32_t(nonPawnKey[1]);
        next.minorKey = uint32_t(minorKey);
        next.majorKey = uint32_t(majorKey);
        next.from = int8_t(from);
        next.to = int8_t(to);
        next.kMS = board.kES;
        next.kES = board.kMS;
        next.casPerms = int8_t(casPerms);
        next.eP = noSquare;
        next.vctm = v;
        next.atkr = p;
        next.promo = int8_t(piece);
        next.capSq = capSq;
        next.halfClock = 0;
        next.mvType = MoveType::PROMO;
        next.rFrom = 0;
        next.rTo = 0;
        next.side = !side;
        next.quiet = false;

        return next;
    }

    template <bool side>
    ForceInline BoardState makeDoublePush(int from, int to, const BoardState& board, U64 discovers) noexcept {
        int8_t eP = from + PAWN_PUSH[side];
        if (!(PAWN_CAPTURES[side][eP] & board.pE())) eP = noSquare;

        Zobrist pawnKey = board.pawnKey;
        const Zobrist zob = zobrist::doublePush<side>(from, to, eP, board.eP, board.zobrist, pawnKey);

        const U64 t = (1ULL << to);
        const U64 move = (1ULL << from) | t;

        BoardState next = board;
        next.pcs[p] ^= move;

        const U64 occM = board.occM ^ move;
        const U64 pM = next.pcs[p] & occM;

        U64 checks;
        if (discovers) [[unlikely]] {
            U64 disc = discovers & DISCOVER_RAYS[board.kES][from] & ~FILE_BIT[from];
            if (disc) [[unlikely]] checks = disc;
            else                    checks = (PAWN_CAPTURES[!side][board.kES] & pM);
        }
        else                        checks = (PAWN_CAPTURES[!side][board.kES] & pM);

        next.occM = board.occE;
        next.occE = occM;
        next.checks = checks;
        next.zobrist = zob;
        next.pawnKey = uint32_t(pawnKey);
        next.from = int8_t(from);
        next.to = int8_t(to);
        next.kMS = board.kES;
        next.kES = board.kMS;
        next.eP = eP;
        next.vctm = noPiece;
        next.atkr = p;
        next.promo = 0;
        next.capSq = noSquare;
        next.halfClock = 0;
        next.mvType = MoveType::NORMAL;
        next.rFrom = 0;
        next.rTo = 0;
        next.side = !side;
        next.quiet = true;

        return next;
    }

    template <bool side>
    ForceInline BoardState makeEnPassant(int from, int to, const BoardState& board) noexcept {
        const int ePS = (to + PAWN_PUSH[!side]);

        Zobrist pawnKey = board.pawnKey;
        const Zobrist zob = zobrist::enPassant<side>(from, to, ePS, board.eP, board.zobrist, pawnKey);

        const U64 move = (1ULL << from) | (1ULL << to);
        const U64 ePB = (1ULL << ePS);

        BoardState next = board;
        next.pcs[p] ^= move ^ ePB;

        const U64 occM = board.occM ^ move;
        const U64 occE = board.occE ^ ePB;
        const U64 occB = occM | occE;

        U64 checks = (PAWN_CAPTURES[!side][board.kES] & next.pcs[p] & occM);

        const U64 bqM = (board.pcs[b] | board.pcs[q]) & occM;
        const U64 rqM = (board.pcs[r] | board.pcs[q]) & occM;
        if (!checks && (BISHOP_XRAYS[board.kES] & (BISHOP_XRAYS[from] | BISHOP_XRAYS[ePS]) & bqM)) [[unlikely]]
            checks |= getBishopAttacks(board.kES, occB) & bqM;
        if (ROOK_XRAYS[board.kES] & ROOK_XRAYS[from] & rqM) [[unlikely]]
            checks |= getRookAttacks(board.kES, occB) & rqM;

        next.occM = occE;
        next.occE = occM;
        next.checks = checks;
        next.zobrist = zob;
        next.pawnKey = uint32_t(pawnKey);
        next.from = int8_t(from);
        next.to = int8_t(to);
        next.kMS = board.kES;
        next.kES = board.kMS;
        next.eP = noSquare;
        next.vctm = p;
        next.atkr = p;
        next.promo = 0;
        next.capSq = int8_t(ePS);
        next.halfClock = 0;
        next.mvType = MoveType::EP;
        next.rFrom = 0;
        next.rTo = 0;
        next.side = !side;
        next.quiet = false;

        return next;
    }

    template <int castlingSide>
    ForceInline BoardState makeCastling(const BoardState& board) noexcept {
        constexpr bool side = CASTLING_SIDE[castlingSide];
        constexpr int from = CASTLING_KING_SOURCE_SQUARE[castlingSide];
        constexpr int to = CASTLING_KING_TARGET_SQUARE[castlingSide];

        const int casPerms = board.casPerms & NO_CASTLE[side];
        Zobrist minorKey = board.minorKey, majorKey = board.majorKey;
        Zobrist nonPawnKey[2] = { board.nonPawnKey[0], board.nonPawnKey[1] };
        const Zobrist zob = zobrist::castle<castlingSide>(casPerms, board.casPerms, board.eP, board.zobrist, nonPawnKey, minorKey, majorKey);

        BoardState next = board;
        next.pcs[k] ^= kingSwitch<castlingSide>();
        next.pcs[r] ^= rookSwitch<castlingSide>();

        const U64 occM = board.occM ^ bothSwitch<castlingSide>();
        const U64 occB = board.occB() ^ bothSwitch<castlingSide>();

        const U64 checks = getRookAttacks(board.kES, occB) & next.pcs[r] & occM;

        next.occM = board.occE;
        next.occE = occM;
        next.checks = checks;
        next.zobrist = zob;
        next.nonPawnKey[0] = uint32_t(nonPawnKey[0]);
        next.nonPawnKey[1] = uint32_t(nonPawnKey[1]);
        next.minorKey = uint32_t(minorKey);
        next.majorKey = uint32_t(majorKey);
        next.from = int8_t(from);
        next.to = int8_t(to);
        next.kMS = board.kES;
        next.kES = int8_t(to);
        next.casPerms = int8_t(casPerms);
        next.eP = noSquare;
        next.vctm = noPiece;
        next.atkr = k;
        next.promo = 0;
        next.capSq = noSquare;
        next.halfClock = board.halfClock + 1;
        next.mvType = MoveType::CASTLE;
        next.rFrom = CASTLING_ROOK_SOURCE_SQUARE[castlingSide];
        next.rTo = CASTLING_ROOK_TARGET_SQUARE[castlingSide];
        next.side = !side;
        next.quiet = true;

        return next;
    }

    template <bool side>
    ForceInline BoardState makeNull(const BoardState& board) noexcept {
        BoardState next = board;

        next.occM = board.occE;
        next.occE = board.occM;
        next.checks = 0;
        next.zobrist = zobrist::null(board.eP, board.zobrist);
        next.from = noSquare;
        next.to = noSquare;
        next.kMS = board.kES;
        next.kES = board.kMS;
        next.eP = noSquare;
        next.vctm = noPiece;
        next.atkr = noPiece;
        next.promo = 0;
        next.capSq = noSquare;
        next.mvType = MoveType::NULLMV;
        next.rFrom = 0;
        next.rTo = 0;
        next.side = !side;
        next.quiet = false;

        return next;
    }

    ForceInline bool hasEnoughMaterial(const BoardState& board) noexcept {
        return board.occM & ~(board.pcs[p] | board.pcs[k]);
    }
}