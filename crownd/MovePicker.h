#pragma once

#include "MoveGenerator.h"

namespace movepick {
    using namespace defs;
    using namespace bstate;
    using namespace batch;

    enum class State : uint8_t { TT_MOVE, GEN_INIT, NOISY_INIT, GOOD_NOISY, QUIET_INIT, GOOD_QUIET, BAD_NOISY, BAD_QUIET, DONE };

    struct MovePicker {
        Batch* batch;
        SearchHistory* hist;
        const BoardState* board;
        bool ttMoveValid;
        uint16_t ttMove;
        uint16_t seMove;
        int depth;
        int ply;

        BoardState ttBoard;

        BoardState* move;
        int idx;

        U64 eAtks;
        U64 discs;
        U64 bPins;
        U64 rPins;
        State state;

        bool skipQ = false;
        bool noisyOnly = false;

        bool probCut;
        int threshold;

        template <bool side>
        inline void init(Batch& batch, SearchHistory& hist, const BoardState& board, uint16_t ttMove, uint16_t seMove, int depth, int ply, int th = 0, bool pc = false) {
            this->batch = &batch;
            this->hist = &hist;
            this->board = &board;
            this->ttMove = ttMove;
            this->seMove = seMove;
            this->depth = depth;
            this->ply = ply;
            this->idx = -1;
            this->probCut = pc;
            this->threshold = th;
            this->discs = 0ULL;
            this->ttBoard.capSq = noSquare;
            this->eAtks = movegen::enemyAttacks<side>(board, this->batch);
            this->state = State::TT_MOVE;
        }

        template <bool side>
        inline bool hasNext() {
            switch (state) {
            case State::TT_MOVE:
                state = State::GEN_INIT;
                ttMoveValid = false;
                if (ttMove && ttMove != seMove && (!noisyOnly || isNoisy(ttMove))) {
                    if ((ttMoveValid = movegen::generateFromInt<side>(*board, ttMove, ttBoard, eAtks, discs)))
                        return update(&ttBoard);
                }
                [[fallthrough]];

            case State::GEN_INIT:
                state = State::NOISY_INIT;

                batch->init();
                batch->hist = hist;
                if (!noisyOnly) batch->prefetch<side>(board->pawnKey, ply);
                if (ttMoveValid) batch->ttMove = ttMove;
                batch->seMove = seMove;

                if (!discs) discs = movegen::findDiscoverers(*board);
                bPins = movegen::findBishopPins(*board);
                rPins = movegen::findRookPins(*board);
                [[fallthrough]];

            case State::NOISY_INIT:
                movegen::generate<false, side, NOISY>(*board, batch, eAtks, discs, bPins, rPins);
                state = State::GOOD_NOISY;
                [[fallthrough]];

            case State::GOOD_NOISY:
                while (BoardState* m = batch->pick()) {
                    if (noisyOnly) {
                        if (probCut && !see::see(*m, threshold)) continue;
                        return update(m);
                    }
                    if (see::see(*m, -batch->score() / GOOD_NOISY_SEE_DIV)) return update(m);
                    batch->demote();
                }
                if (noisyOnly || skipQ) { state = State::BAD_NOISY; goto badNoisy; }
                state = State::QUIET_INIT;
                [[fallthrough]];

            case State::QUIET_INIT:
                batch->prepareQuiet<side>(*board, ply);
                movegen::generate<false, side, QUIET>(*board, batch, eAtks, discs, bPins, rPins);
                state = State::GOOD_QUIET;
                [[fallthrough]];

            case State::GOOD_QUIET:
                if (!skipQ) if (BoardState* m = batch->pick()) return update(m);
                state = State::BAD_NOISY;
                [[fallthrough]];

            case State::BAD_NOISY:
            badNoisy:
                if (BoardState* m = batch->pickBadNoisy()) return update(m);
                if (noisyOnly) { state = State::DONE; return false; }
                state = State::BAD_QUIET;
                [[fallthrough]];

            case State::BAD_QUIET:
                if (!skipQ) if (BoardState* m = batch->pickBadQuiet()) return update(m);
                state = State::DONE;
                [[fallthrough]];

            case State::DONE:
                return false;
            }
            return false;
        }

        __forceinline BoardState& next() const { return *move; }
        __forceinline int index() const { return idx; }
        __forceinline int count() const { return idx + 1; }
        __forceinline void skipQuiets() { skipQ = true; }

    private:
        __forceinline bool update(BoardState* m) { move = m; ++idx; return true; }

        __forceinline bool isNoisy(uint16_t m) const {
            const int to = (m >> 6) & 63;
            const uint8_t type = (m >> 14) & 3;
            return (board->occE & (1ULL << to)) || type == MoveType::EP || type == MoveType::PROMO;
        }
    };
}