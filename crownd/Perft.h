#pragma once

#include "MoveGenerator.h"

using namespace movegen;
using namespace tt;

namespace perft {
    inline Batch batches[MAX_PLY];

    template <bool side>
    ForceInline U64 iterateBatch(int depth, const BoardState& board) noexcept {
        if (depth == 1) return movegen::generate<true, side>(board);

        U64 nodes = 0ULL;
        Batch& batch = batches[depth];
        batch.size = 0;
        batch.sort = false;

        movegen::generate<false, side>(board, &batch);

        for (int i = 0; i < batch.size; ++i) {
            nodes += iterateBatch<!side>(depth - 1, batch.moves[i]);
        }

        return nodes;
    }

    template <bool side>
    ForceInline U64 start(int depth, const BoardState& board, Batch* batch = nullptr) noexcept {
        if (depth <= 0) return movegen::generate<false, side>(board, batch);
  
        return iterateBatch<side>(depth, board);
    }
}