#pragma once

#include "Definitions.h"
#include "BoardState.h"
#include "pyrrhic/tbprobe.h"

namespace syzygy {
	using namespace defs;
	using namespace bstate;
	using namespace std;

	struct Move {
		uint8_t from;
		uint8_t to;
		uint8_t promo;

		__forceinline bool operator==(const BoardState& board) const {
			return board.from == from && board.to == to && board.promo == promo;
		}
	};

	inline Move moves[MOVES_MAX];
	inline int size = 0;

	Inline bool init(string& path) {
		tb_init(path.c_str());
		return TB_LARGEST > 0;
	}

	template <bool side>
	Inline int probeWdl(const BoardState& board) {
		if (board.casPerms || board.halfClock || Bitcount(board.occB()) > TB_LARGEST) return TB_RESULT_FAILED;

		const U64 occW = Swap(side == white ? board.occM : board.occE);
		const U64 occB = Swap(side == white ? board.occE : board.occM);

		return tb_probe_wdl(occW, occB, Swap(board.pcs[k]), Swap(board.pcs[q]),
			Swap(board.pcs[r]), Swap(board.pcs[b]), Swap(board.pcs[n]), Swap(board.pcs[p]), 
			board.eP != noSquare ? board.eP ^ 56 : 0, !side);
	}

	ForceInline uint8_t promo(int code) {
		return code ? k - code : 0;
	}

	template <bool side>
	Inline void probeRoot(const BoardState& board) {
		size = 0;

		if (board.casPerms || Bitcount(board.occB()) > TB_LARGEST) return;

		const U64 occW = Swap(side == white ? board.occM : board.occE);
		const U64 occB = Swap(side == white ? board.occE : board.occM);

		unsigned results[TB_MAX_MOVES + 1];

		const unsigned best = tb_probe_root(occW, occB, Swap(board.pcs[k]), Swap(board.pcs[q]),
			Swap(board.pcs[r]), Swap(board.pcs[b]), Swap(board.pcs[n]), Swap(board.pcs[p]),
			board.halfClock, board.eP != noSquare ? board.eP ^ 56 : 0, !side, results);

		if (best == TB_RESULT_FAILED) return;

		const int wdl = TB_GET_WDL(best);

		for (int i = 0; i < TB_MAX_MOVES && results[i] != TB_RESULT_FAILED; ++i) {
			const unsigned res = results[i];
			if (TB_GET_WDL(res) != wdl) continue;

			moves[size++] = { uint8_t(TB_GET_FROM(res) ^ 56), uint8_t(TB_GET_TO(res) ^ 56),  promo(TB_GET_PROMOTES(res)) };
		}
	}

	Inline bool processMove(const BoardState& board) {
		if (!size) return true;

		for (int i = 0; i < size; ++i) {
			if (board == moves[i]) return true;
		}
		return false;
	}
}