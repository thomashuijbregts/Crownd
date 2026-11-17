#ifndef MOVEGENERATOR_H
#define MOVEGENERATOR_H

#include "Batch.h"
#include <vector>

using namespace bstate;
using namespace batch;

namespace movegen {

    /****************************************************
    * Naming conventions
    *
    * pM -> kM	|	pawn to king, current side
    * pE -> kE	|	pawn to king, opposite side
    * occM|E|B	|	occupancies (current, opposite, both)
    *
    *****************************************************/

    template <GenType gen>
    ForceInline U64 filter(U64 attacks, const BoardState& board) noexcept {
        if constexpr (gen == NOISY) return attacks & board.occE;
        if constexpr (gen == QUIET) return attacks & ~board.occE;
        return attacks;
    }

    template <bool side>
    ForceInline U64 enemyAttacks(const BoardState& board, Batch* batch = nullptr) noexcept {
        U64 atks = getKingAttacks(board.kES);
        U64 pAtks = pawnsAtkLeft<!side>(board.pE()) | pawnsAtkRight<!side>(board.pE());

        //remove king to avoid collisions, as it should not be considered when checking for threats
        U64 occB = board.occB() ^ board.kM();

        U64 bitboard;

        U64 nAtks = 0ULL;
        bitboard = board.nE();
        Bitloop(bitboard) {
            nAtks |= getKnightAttacks(SquareOf(bitboard));
        }

        U64 bAtks = 0ULL;
        bitboard = board.bE();
        Bitloop(bitboard) {
            bAtks |= getBishopAttacks(SquareOf(bitboard), occB);
        }

        U64 rAtks = 0ULL;
        bitboard = board.rE();
        Bitloop(bitboard) {
            rAtks |= getRookAttacks(SquareOf(bitboard), occB);
        }

        bitboard = board.qE();
        Bitloop(bitboard) {
            atks |= getQueenAttacks(SquareOf(bitboard), occB);
        }

        atks |= pAtks | nAtks | bAtks | rAtks;

        if (batch) {
            batch->threats[n] = pAtks;
            batch->threats[b] = pAtks;
            U64 min = pAtks | nAtks | bAtks;
            batch->threats[r] = min;
            batch->threats[q] = min | rAtks;

            batch->threats[noPiece] = atks;
        }

        return atks;
    }

    template <int castlingSide>
    ForceInline bool castle(const BoardState& board, U64 attacks) noexcept {
        return !(!(board.casPerms & CASTLING[castlingSide]) | (CASTLING_OCCUPIED_SQUARES[castlingSide] & board.occB()) | (attacks & CASTLING_PASSING_SQUARES[castlingSide]));
    }

    template <bool side>
    ForceInline bool passantPinned(const BoardState& board, int from) noexcept {
        if (!(EN_PASSANT_RANK[side] & board.kM())) return false;

        U64 occB = board.occB();
        int enemyPawn = board.eP + PAWN_PUSH[!side];
        PopBit(occB, enemyPawn);
        PopBit(occB, from);

        return getRookAttacks(board.kMS, occB) & (board.rE() | board.qE());
    }

    ForceInline U64 findPins(U64 sE, const BoardState& board) {
        U64 pins = 0ULL;

        Bitloop(sE)
        {
            int sS = SquareOf(sE);

            U64 pinMask = PIN_MASKS[board.kMS][sS];
            U64 pin = pinMask & board.occB();

            if (!BitReset(pin)) [[unlikely]] {
                pins |= pinMask | SQUARE_BITS[sS];
            }
        }

        return pins;
    }

    ForceInline U64 findBishopPins(const BoardState& board) noexcept {
        return findPins((board.bE() | board.qE()) & BISHOP_XRAYS[board.kMS] & ~board.checks, board);
    }

    ForceInline U64 findRookPins(const BoardState& board) noexcept {
        return findPins((board.rE() | board.qE()) & ROOK_XRAYS[board.kMS] & ~board.checks, board);
    }

    ForceInline U64 findDiscoverers(const BoardState& board) noexcept {
        U64 disc = 0ULL;

        U64 sM = ((board.bM() | board.qM()) & BISHOP_XRAYS[board.kES]) | ((board.rM() | board.qM()) & ROOK_XRAYS[board.kES]);

        Bitloop(sM) {
            int sS = SquareOf(sM);

            U64 pinMask = PIN_MASKS[board.kES][sS];
            U64 pin = pinMask & board.occB();

            if (!BitReset(pin)) [[unlikely]] {
                disc |= SQUARE_BITS[sS];
            }
        }

        return disc;
    }

    template <bool side, Pieces piece, bool capture>
    ForceInline void enumMoves(U64 moves, int from, const BoardState& board, U64 discovers, Batch* batch) noexcept {
        Bitloop(moves) {
            int to = SquareOf(moves);

            batch->add<side, capture>(board, from, to, [&] { return bstate::make<piece, side, capture>(from, to, board, discovers); });
        }
    }

    template <bool side, Pieces piece, GenType type>
    ForceInline void makeMoves(U64 attacks, int from, const BoardState& board, U64 discovers, Batch* batch) noexcept {
        if constexpr (type != NOISY)
            enumMoves<side, piece, false>(attacks & ~board.occE, from, board, discovers, batch);
        if constexpr (type != QUIET)
            enumMoves<side, piece, true>(attacks & board.occE, from, board, discovers, batch);
    }

    template <bool side, bool capture, Pieces piece>
    ForceInline void makeMove(int from, int to, const BoardState& board, U64 discovers, Batch* batch) noexcept {
        batch->add<side, capture>(board, from, to, [&] { return bstate::make<piece, side, capture>(from, to, board, discovers); });
    }

    template <bool side>
    ForceInline void makeEnPassant(int from, const BoardState& board, Batch* batch) noexcept {
        batch->add<side, true, 0, MoveType::EP>(board, from, board.eP, [&] { return bstate::makeEnPassant<side>(from, board.eP, board); });
    }

    template <bool side>
    ForceInline void makeDoublePush(int from, int to, const BoardState& board, U64 discovers, Batch* batch) noexcept {
        batch->add<side, false>(board, from, to, [&] { return bstate::makeDoublePush<side>(from, to, board, discovers); });
    }

    template <bool side, bool capture>
    ForceInline void makePromotionMoves(int from, int to, const BoardState& board, U64 discovers, Batch* batch) noexcept {
        batch->add<side, capture, n, MoveType::PROMO>(board, from, to, [&] { return bstate::makePromotion<n, side, capture>(from, to, board, discovers); });
        batch->add<side, capture, b, MoveType::PROMO>(board, from, to, [&] { return bstate::makePromotion<b, side, capture>(from, to, board, discovers); });
        batch->add<side, capture, r, MoveType::PROMO>(board, from, to, [&] { return bstate::makePromotion<r, side, capture>(from, to, board, discovers); });
        batch->add<side, capture, q, MoveType::PROMO>(board, from, to, [&] { return bstate::makePromotion<q, side, capture>(from, to, board, discovers); });
    }

    template <bool side, int castlingSide>
    ForceInline void makeCastling(const BoardState& board, Batch* batch) noexcept {
        constexpr int from = CASTLING_KING_SOURCE_SQUARE[castlingSide];
        constexpr int to = CASTLING_KING_TARGET_SQUARE[castlingSide];

        batch->add<side, false, 0, MoveType::CASTLE>(board, from, to, [&] { return bstate::makeCastling<castlingSide>(board); });
    }

    template <bool count, bool side, GenType gen = ALL>
    Inline U64 generate(const BoardState& board, Batch* batch = nullptr, U64 eAtks = 0ULL, U64 discs = 0ULL, U64 bPins = 0ULL, U64 rPins = 0ULL) noexcept {
        constexpr bool genNoisy = gen != QUIET;
        constexpr bool genQuiet = gen != NOISY;

        int from, to;
        U64 bitboard, attacks;

        U64 nodes = 0ULL;
        if constexpr (gen == ALL) eAtks = enemyAttacks<side>(board);

        if constexpr (!count && gen == ALL) discs = findDiscoverers(board);

        if (board.checks) [[unlikely]] {
            /*

                KING MOVES

            */
            U64 pseudoAttacks = getKingAttacks(board.kMS) & ~board.occM;
            attacks = filter<gen>(pseudoAttacks & ~eAtks, board);

            if constexpr (count) nodes += Bitcount(attacks);
            else makeMoves<side, k, gen>(attacks, board.kMS, board, discs, batch);

            if (!BitReset(board.checks)) [[likely]] {
                int checkSquare = SquareOf(board.checks);

                if constexpr (gen == ALL) {
                    bPins = findBishopPins(board);
                    rPins = findRookPins(board);
                }
                const U64 allPins = bPins | rPins;
                if (board.checks & (board.pE() | board.nE())) {
                    if constexpr (!genNoisy) return nodes;
                    /*
                        PAWN MOVES
                    */
                    to = checkSquare;

                    const U64 pawns = board.pM() & ~allPins;
                    U64 enPassant = pawns & PASSANT_CAPTURES[board.eP];
                    U64 caps = pawns & PAWN_CAPTURES[!side][checkSquare];
                    U64 promos = caps & PROMO_RANKS[side];
                    caps ^= promos;

                    if constexpr (count) nodes += Bitcount(enPassant | caps) + (Bitcount(promos) << 2);
                    else {
                        Bitloop(enPassant)
                        {
                            makeEnPassant<side>(SquareOf(enPassant), board, batch);
                        }

                        Bitloop(promos) {
                            makePromotionMoves<side, true>(SquareOf(promos), to, board, discs, batch);
                        }

                        Bitloop(caps) {
                            makeMove<side, true, p>(SquareOf(caps), to, board, discs, batch);
                        }
                    }

                    /*
                        KNIGHT MOVES
                    */
                    bitboard = board.nM() & ~allPins;
                    attacks = getKnightAttacks(checkSquare) & bitboard;
                    if constexpr (count) nodes += Bitcount(attacks);
                    else {
                        Bitloop(attacks) {
                            makeMove<side, true, n>(SquareOf(attacks), to, board, discs, batch);
                        }
                    }

                    /*
                        BISHOP MOVES
                    */
                    bitboard = (board.bM() | board.qM()) & ~allPins & BISHOP_XRAYS[checkSquare];
                    if (bitboard) [[unlikely]] {
                        attacks = getBishopAttacks(checkSquare, board.occB()) & bitboard;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else {
                            Bitloop(attacks) {
                                from = SquareOf(attacks);

                                ((1ULL << from) & board.qM())
                                    ? makeMove<side, true, q>(from, to, board, discs, batch)
                                    : makeMove<side, true, b>(from, to, board, discs, batch);
                            }
                        }
                    }

                    /*
                        ROOK MOVES
                    */
                    bitboard = (board.rM() | board.qM()) & ~allPins & ROOK_XRAYS[checkSquare];
                    if (bitboard) [[unlikely]] {
                        attacks = getRookAttacks(checkSquare, board.occB()) & bitboard;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else {
                            Bitloop(attacks) {
                                from = SquareOf(attacks);

                                ((1ULL << from) & board.qM())
                                    ? makeMove<side, true, q>(from, to, board, discs, batch)
                                    : makeMove<side, true, r>(from, to, board, discs, batch);
                            }
                        }
                    }
                }
                else {
                    const U64 block = PIN_MASKS[board.kMS][checkSquare];
                    const U64 validSquares = filter<gen>(board.checks | block, board);

                    /*
                        PAWN MOVES
                    */
                    const U64 pawns = board.pM() & ~allPins;

                    U64 pawnsLeft = pawnsAtkLeft<side>(pawns) & board.occE & validSquares;
                    U64 pawnsRight = pawnsAtkRight<side>(pawns) & board.occE & validSquares;
                    U64 pawnsFwd = pawnsAtkForward<side>(pawns) & ~board.occB();
                    U64 pawnsDbl = 0ULL;
                    if constexpr (genQuiet) pawnsDbl = pawnsAtkForward<side>(pawnsFwd & FIRST_PUSH_RANK[side]) & ~board.occB() & block;
                    pawnsFwd &= block;
                    if constexpr (!genNoisy) pawnsFwd &= ~LAST_RANK[side];
                    if constexpr (!genQuiet) pawnsFwd &= LAST_RANK[side];

                    if ((pawnsLeft | pawnsRight | pawnsFwd) & LAST_RANK[side]) [[unlikely]] {
                        U64 promosLeft = pawnsLeft & LAST_RANK[side];
                        U64 promosRight = pawnsRight & LAST_RANK[side];
                        U64 promosFwd = pawnsFwd & LAST_RANK[side];


                        pawnsLeft ^= promosLeft;
                        pawnsRight ^= promosRight;
                        pawnsFwd ^= promosFwd;

                        if constexpr (count) nodes += (Bitcount(promosLeft) + Bitcount(promosRight | promosFwd)) << 2;
                        else {
                            Bitloop(promosLeft) {
                                to = SquareOf(promosLeft);
                                from = to + PAWN_RIGHT[!side];
                                makePromotionMoves<side, true>(from, to, board, discs, batch);
                            }

                            Bitloop(promosRight) {
                                to = SquareOf(promosRight);
                                from = to + PAWN_LEFT[!side];
                                makePromotionMoves<side, true>(from, to, board, discs, batch);
                            }

                            Bitloop(promosFwd) {
                                to = SquareOf(promosFwd);
                                from = to + PAWN_PUSH[!side];
                                makePromotionMoves<side, false>(from, to, board, discs, batch);
                            }
                        }
                    }

                    if constexpr (count) {
                        nodes += Bitcount(pawnsLeft);
                        nodes += Bitcount(pawnsRight | pawnsFwd | pawnsDbl);
                    }
                    else {
                        Bitloop(pawnsLeft) {
                            to = SquareOf(pawnsLeft);
                            from = to + PAWN_RIGHT[!side];
                            makeMove<side, true, p>(from, to, board, discs, batch);
                        }

                        Bitloop(pawnsRight) {
                            to = SquareOf(pawnsRight);
                            from = to + PAWN_LEFT[!side];
                            makeMove<side, true, p>(from, to, board, discs, batch);
                        }

                        if constexpr (genQuiet) {
                            Bitloop(pawnsFwd) {
                                to = SquareOf(pawnsFwd);
                                from = to + PAWN_PUSH[!side];
                                makeMove<side, false, p>(from, to, board, discs, batch);
                            }

                            Bitloop(pawnsDbl) {
                                to = SquareOf(pawnsDbl);
                                from = to + PAWN_DOUBLE_PUSH[!side];
                                makeDoublePush<side>(from, to, board, discs, batch);
                            }
                        }
                    }
                    /*
                        KNIGHT MOVES
                    */
                    bitboard = board.nM() & ~allPins;
                    Bitloop(bitboard)
                    {
                        from = SquareOf(bitboard);

                        attacks = getKnightAttacks(from) & validSquares;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else makeMoves<side, n, gen>(attacks, from, board, discs, batch);
                    }

                    /*
                        BISHOP MOVES
                    */
                    bitboard = board.bM() & ~allPins;
                    Bitloop(bitboard)
                    {
                        from = SquareOf(bitboard);

                        attacks = getBishopAttacks(from, board.occB()) & validSquares;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else makeMoves<side, b, gen>(attacks, from, board, discs, batch);
                    }

                    /*
                        ROOK MOVES
                    */
                    bitboard = board.rM() & ~allPins;
                    Bitloop(bitboard)
                    {
                        from = SquareOf(bitboard);

                        attacks = getRookAttacks(from, board.occB()) & validSquares;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else makeMoves<side, r, gen>(attacks, from, board, discs, batch);
                    }

                    /*
                        QUEEN MOVES
                    */
                    bitboard = board.qM() & ~allPins;
                    Bitloop(bitboard)
                    {
                        from = SquareOf(bitboard);

                        attacks = getQueenAttacks(from, board.occB()) & validSquares;
                        if constexpr (count) nodes += Bitcount(attacks);
                        else makeMoves<side, q, gen>(attacks, from, board, 0ULL, batch);
                    }
                }
            }

            return nodes;
        }

        if constexpr (gen == ALL) {
            bPins = findBishopPins(board);
            rPins = findRookPins(board);
        }
        const U64 allPins = bPins | rPins;

        /*
            KING MOVES
        */
        U64 pseudoAttacks = getKingAttacks(board.kMS) & ~board.occM;
        attacks = filter<gen>(pseudoAttacks & ~eAtks, board);

        if constexpr (count) nodes += Bitcount(attacks);
        else makeMoves<side, k, gen>(attacks, board.kMS, board, discs, batch);

        /*
            PAWN MOVES
        */
        const U64 pawnsAtk = board.pM() & ~rPins;
        const U64 pawnsPush = board.pM() & ~bPins;

        const U64 pawnsLeftAll = pawnsAtkLeft<side>(pawnsAtk & ~bPins) | (pawnsAtkLeft<side>(pawnsAtk & bPins) & bPins);
        U64 pawnsLeft = pawnsLeftAll & board.occE;
        const U64 pawnsRightAll = pawnsAtkRight<side>(pawnsAtk & ~bPins) | (pawnsAtkRight<side>(pawnsAtk & bPins) & bPins);
        U64 pawnsRight = pawnsRightAll & board.occE;
        const U64 pawnsFwdAll = pawnsAtkForward<side>(pawnsPush & ~rPins) | (pawnsAtkForward<side>(pawnsPush & rPins) & rPins);
        U64 pawnsFwd = pawnsFwdAll & ~board.occB();
        U64 pawnsDbl = 0ULL;
        if constexpr (genQuiet) {
            const U64 pawnsDblAll = pawnsAtkForward<side>(pawnsFwd & FIRST_PUSH_RANK[side]);
            pawnsDbl = pawnsDblAll & ~board.occB();
        }
        if constexpr (!genNoisy) {
            pawnsLeft = pawnsRight = 0ULL;
            pawnsFwd &= ~LAST_RANK[side];
        }

        if (genNoisy && board.eP != noSquare) [[unlikely]] {
            const U64 ePBit = (1ULL << board.eP);
            U64 ePP = pawnsAtkRight<!side>(pawnsLeftAll & ePBit) | pawnsAtkLeft<!side>(pawnsRightAll & ePBit);

            if (!passantPinned<side>(board, SquareOf(ePP))) [[likely]] {
                if constexpr (count) nodes += Bitcount(ePP);
                else {
                    Bitloop(ePP) {
                        makeEnPassant<side>(SquareOf(ePP), board, batch);
                    }
                }
            }
        }

        if ((pawnsLeft | pawnsRight | pawnsFwd) & LAST_RANK[side]) [[unlikely]] {
            U64 promosLeft = pawnsLeft & LAST_RANK[side];
            U64 promosRight = pawnsRight & LAST_RANK[side];
            U64 promosFwd = pawnsFwd & LAST_RANK[side];

            pawnsLeft ^= promosLeft;
            pawnsRight ^= promosRight;
            pawnsFwd ^= promosFwd;

            if constexpr (count) nodes += (Bitcount(promosLeft) + Bitcount(promosRight | promosFwd)) << 2;
            else {
                Bitloop(promosLeft) {
                    to = SquareOf(promosLeft);
                    from = to + PAWN_RIGHT[!side];
                    makePromotionMoves<side, true>(from, to, board, discs, batch);
                }
                Bitloop(promosRight) {
                    to = SquareOf(promosRight);
                    from = to + PAWN_LEFT[!side];
                    makePromotionMoves<side, true>(from, to, board, discs, batch);
                }
                Bitloop(promosFwd) {
                    to = SquareOf(promosFwd);
                    from = to + PAWN_PUSH[!side];
                    makePromotionMoves<side, false>(from, to, board, discs, batch);
                }
            }
        }

        if constexpr (count) {
            nodes += Bitcount(pawnsLeft);
            nodes += Bitcount(pawnsRight | pawnsFwd | pawnsDbl);
        }
        else {
            Bitloop(pawnsLeft) {
                to = SquareOf(pawnsLeft);
                from = to + PAWN_RIGHT[!side];
                makeMove<side, true, p>(from, to, board, discs, batch);
            }

            Bitloop(pawnsRight) {
                to = SquareOf(pawnsRight);
                from = to + PAWN_LEFT[!side];
                makeMove<side, true, p>(from, to, board, discs, batch);
            }

            if constexpr (genQuiet) {
                Bitloop(pawnsFwd) {
                    to = SquareOf(pawnsFwd);
                    from = to + PAWN_PUSH[!side];
                    makeMove<side, false, p>(from, to, board, discs, batch);
                }

                Bitloop(pawnsDbl) {
                    to = SquareOf(pawnsDbl);
                    from = to + PAWN_DOUBLE_PUSH[!side];
                    makeDoublePush<side>(from, to, board, discs, batch);
                }
            }
        }

        /*
            KNIGHT MOVES
        */
        bitboard = board.nM() & ~allPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(getKnightAttacks(from) & ~board.occM, board);

            if constexpr (count) nodes += Bitcount(attacks);
            else makeMoves<side, n, gen>(attacks, from, board, discs, batch);
        }

        /*
            BISHOP MOVES
        */
        bitboard = board.bM() & ~allPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(getBishopAttacks(from, board.occB()) & ~board.occM, board);

            if constexpr (count) nodes += Bitcount(attacks);
            else makeMoves<side, b, gen>(attacks, from, board, discs, batch);
        }

        bitboard = (board.bM() | board.qM()) & bPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(bPins & PIN_RAYS[board.kMS][from], board);

            if constexpr (count) nodes += Bitcount(attacks);
            else {
                const bool isQueen = ((1ULL << from) & board.qM()) != 0ULL;

                if (isQueen)    makeMoves<side, q, gen>(attacks, from, board, 0ULL, batch);
                else            makeMoves<side, b, gen>(attacks, from, board, discs, batch);
            }
        }

        /*
            ROOK MOVES
        */
        bitboard = board.rM() & ~allPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(getRookAttacks(from, board.occB()) & ~board.occM, board);

            if constexpr (count) nodes += Bitcount(attacks);
            else makeMoves<side, r, gen>(attacks, from, board, discs, batch);
        }

        bitboard = (board.rM() | board.qM()) & rPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(rPins & PIN_RAYS[board.kMS][from], board);

            if constexpr (count) nodes += Bitcount(attacks);
            else {
                const bool isQueen = ((1ULL << from) & board.qM()) != 0ULL;

                if (isQueen)    makeMoves<side, q, gen>(attacks, from, board, 0ULL, batch);
                else            makeMoves<side, r, gen>(attacks, from, board, discs, batch);
            }
        }

        /*
            QUEEN MOVES
        */
        bitboard = board.qM() & ~allPins;
        Bitloop(bitboard)
        {
            from = SquareOf(bitboard);

            attacks = filter<gen>(getQueenAttacks(from, board.occB()) & ~board.occM, board);

            if constexpr (count) nodes += Bitcount(attacks);
            else makeMoves<side, q, gen>(attacks, from, board, 0ULL, batch);
        }

        /*
            CASTLING
        */
        if constexpr (genQuiet) {
            constexpr int kSide = CASTLING_SIDE_K[side];
            constexpr int qSide = CASTLING_SIDE_Q[side];

            if (castle<kSide>(board, eAtks)) {
                if constexpr (count) nodes++;
                else makeCastling<side, kSide>(board, batch);
            }
            if (castle<qSide>(board, eAtks)) {
                if constexpr (count) nodes++;
                else makeCastling<side, qSide>(board, batch);
            }
        }

        return nodes;
    }

    template <bool side>
    ForceInline bool exposesKing(const BoardState& board, U64 frB, U64 toB) {
        if (frB & BISHOP_XRAYS[board.kMS]) {
            return getBishopAttacks(board.kMS, ((board.occB() ^ frB) | toB)) & (board.bE() | board.qE()) & ~toB;
        }
        else if (frB & ROOK_XRAYS[board.kMS]) {
            return getRookAttacks(board.kMS, ((board.occB() ^ frB) | toB)) & (board.rE() | board.qE()) & ~toB;
        }
        return false;
    }

    template <bool side>
    Inline bool generateFromInt(const BoardState& board, uint16_t move, BoardState& out, U64 eAtks, U64& discs) {
        int from = move & 63;
        U64 frB = 1ULL << from;
        if (!(board.occM & frB)) return false;

        int to = (move >> 6) & 63;
        U64 toB = (1ULL << to);
        if (board.occM & toB) return false;

        if (!(board.kM() & frB) && exposesKing<side>(board, frB, toB)) return false;

        uint8_t mvType = (move >> 14) & 3;

        if (board.checks) {
            if (Bitcount(board.checks) == 2) {
                if (!(board.kM() & frB))           return false;
                if (!(getKingAttacks(from) & toB)) return false;
                if (toB & eAtks)                   return false;

                discs = findDiscoverers(board);
                if (toB & board.occE)   return out = bstate::make<k, side, true>(from, to, board, discs), true;
                else                    return out = bstate::make<k, side, false>(from, to, board, discs), true;
            }
            if (!(board.kM() & frB)) {
                if (board.checks & (board.pE() | board.nE())) {
                    if (mvType == MoveType::EP) {
                        if (board.checks & board.nE()) return false;
                    }
                    else if (!(toB & board.checks)) return false;
                }
                else {
                    U64 valid = PIN_MASKS[board.kMS][SquareOf(board.checks)] | board.checks;
                    if (!(toB & valid)) return false;
                }
            }
        }

        if (mvType == MoveType::NORMAL) {
            discs = findDiscoverers(board);

            if (frB & board.pM()) {
                if (toB & LAST_RANK[side]) return false;
                if (PAWN_CAPTURES[side][from] & toB) {
                    if (!(toB & board.occE)) return false;
                }
                else {
                    if (toB & board.occB()) return false;
                    if (to != from + PAWN_PUSH[side]) {
                        if (!(frB & FIRST_RANKS[side])) return false;
                        if (to != from + PAWN_DOUBLE_PUSH[side]) return false;
                        if ((1ULL << (from + PAWN_PUSH[side])) & board.occB()) return false;
                    }
                }

                if (toB & board.occE) {
                    return out = bstate::make<p, side, true>(from, to, board, discs), true;
                }
                else {
                    if (to == from + PAWN_DOUBLE_PUSH[side]) {
                        return out = bstate::makeDoublePush<side>(from, to, board, discs), true;
                    }

                    return out = bstate::make<p, side, false>(from, to, board, discs), true;
                }
            }
            else if (frB & board.nM()) {
                if (!(getKnightAttacks(from) & toB)) return false;

                if (toB & board.occE)   return out = bstate::make<n, side, true>(from, to, board, discs), true;
                else                    return out = bstate::make<n, side, false>(from, to, board, discs), true;
            }
            else if (frB & board.bM()) {
                if (!(getBishopAttacks(from, board.occB()) & toB)) return false;

                if (toB & board.occE)   return out = bstate::make<b, side, true>(from, to, board, discs), true;
                else                    return out = bstate::make<b, side, false>(from, to, board, discs), true;
            }
            else if (frB & board.rM()) {
                if (!(getRookAttacks(from, board.occB()) & toB)) return false;

                if (toB & board.occE)   return out = bstate::make<r, side, true>(from, to, board, discs), true;
                else                    return out = bstate::make<r, side, false>(from, to, board, discs), true;
            }
            else if (frB & board.qM()) {
                if (!(getQueenAttacks(from, board.occB()) & toB)) return false;

                if (toB & board.occE)   return out = bstate::make<q, side, true>(from, to, board, discs), true;
                else                    return out = bstate::make<q, side, false>(from, to, board, discs), true;
            }

            if (!(getKingAttacks(from) & toB)) return false;
            if (toB & eAtks)                   return false;

            if (toB & board.occE)   return out = bstate::make<k, side, true>(from, to, board, discs), true;
            else                    return out = bstate::make<k, side, false>(from, to, board, discs), true;
        }
        else if (mvType == MoveType::PROMO) {
            if (!(frB & board.pM() & PROMO_RANKS[side])) return false;
            if (PAWN_CAPTURES[side][from] & toB) {
                if (!(toB & board.occE)) return false;
            }
            else {
                if (toB & board.occB())           return false;
                if (to != from + PAWN_PUSH[side]) return false;
            }

            discs = findDiscoverers(board);
            uint8_t pc = ((move >> 12) & 3) + 1;

            if (pc == n) {
                if (toB & board.occE)   return out = bstate::makePromotion<n, side, true>(from, to, board, discs), true;
                else                    return out = bstate::makePromotion<n, side, false>(from, to, board, discs), true;
            }
            else if (pc == b) {
                if (toB & board.occE)   return out = bstate::makePromotion<b, side, true>(from, to, board, discs), true;
                else                    return out = bstate::makePromotion<b, side, false>(from, to, board, discs), true;
            }
            else if (pc == r) {
                if (toB & board.occE)   return out = bstate::makePromotion<r, side, true>(from, to, board, discs), true;
                else                    return out = bstate::makePromotion<r, side, false>(from, to, board, discs), true;
            }

            if (toB & board.occE)       return out = bstate::makePromotion<q, side, true>(from, to, board, discs), true;
            else                        return out = bstate::makePromotion<q, side, false>(from, to, board, discs), true;
        }
        else if (mvType == MoveType::EP) {
            if (to != board.eP)                     return false;
            if (!(board.pM() & frB))                return false;
            if (!(PAWN_CAPTURES[side][from] & toB)) return false;
            if (board.occB() & toB)                 return false;
            if (passantPinned<side>(board, from))   return false;

            return out = bstate::makeEnPassant<side>(from, board.eP, board), true;
        }
        else {
            if (!(board.kM() & frB) || board.checks) return false;

            constexpr int kSide = CASTLING_SIDE_K[side];
            constexpr int qSide = CASTLING_SIDE_Q[side];
            if (to == CASTLING_KING_TARGET_SQUARE[kSide]) {
                if (castle<kSide>(board, eAtks)) return out = bstate::makeCastling<kSide>(board), true;
            }
            else if (to == CASTLING_KING_TARGET_SQUARE[qSide]) {
                if (castle<qSide>(board, eAtks)) return out = bstate::makeCastling<qSide>(board), true;
            }
        }

        return false;
    }
}

#endif