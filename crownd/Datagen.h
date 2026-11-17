#pragma once

#include "Definitions.h"
#include "Game.h"
#include "Engine.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <random>
#include <algorithm>
#include <filesystem>
#if defined(_WIN32)
#include <process.h>
inline int pId() { return _getpid(); }
inline FILE* openFile(const char* name, const char* mode) {
	FILE* f = nullptr;
	return fopen_s(&f, name, mode) == 0 ? f : nullptr;
}
#else
#include <unistd.h>
inline int pId() { return getpid(); }
inline FILE* openFile(const char* name, const char* mode) { return fopen(name, mode); }
#endif

namespace dgen {
	using namespace defs;
	using namespace bstate;

	inline constexpr int TT_MB = 16;

	inline constexpr int MAX_SAMPLES = 7'200'000;
	inline constexpr int FLUSH_LIMIT = 2000;

	inline constexpr int MAX_GAME_PLIES = 12800;

	inline constexpr U64 SOFT_NODES = 5000;
	inline constexpr U64 HARD_NODES = 2000000;
	inline constexpr int MAX_DEPTH = MAX_PLY - 1;

	inline constexpr int VERIFY_DEPTH = 9;

	inline constexpr int RANDOM_PLIES = 8;
	inline constexpr int OPENING_MIN = 50;
	inline constexpr int OPENING_LIMIT = 400;

	inline constexpr int SCORE_LIMIT = 20000;

	inline constexpr int ADJ_WIN_SCORE = 2000;
	inline constexpr int ADJ_WIN_PLIES = 4;
	inline constexpr int ADJ_DRAW_SCORE = 10;
	inline constexpr int ADJ_DRAW_PLIES = 10;
	inline constexpr int ADJ_DRAW_MIN_PLY = 70;

	inline constexpr int CONTRADICTION_SCORE = 700;
	inline constexpr int CONTRADICTION_MAX = 5;

	inline constexpr int PIECE_WEIGHT[6] = { 35, 15, 15, 5, 15, 15 };

	//white point of view
	struct Sample {
		BoardState board;
		int	score;		
		int	fullMove;
		float wdl;
	};

	inline Sample samples[FLUSH_LIMIT + MAX_GAME_PLIES];
	inline int	  sampleCount = 0;

	struct OrderingSample {
		BoardState	board;
		int	fullMove;
		uint16_t move;
	};

	inline OrderingSample orderingSamples[FLUSH_LIMIT + MAX_GAME_PLIES];
	inline int orderingCount = 0;
	inline U64 orderingTotal = 0;

	inline U64 games = 0;
	inline U64 discarded = 0;

	inline U64 tested = 0;
	inline U64 kept = 0;

	inline U64 openingsTried = 0;
	inline U64 openingsRejected = 0;

	inline int winPlies = 0;
	inline int drawPlies = 0;

	inline std::mt19937 rng{ std::random_device{}() };

	inline batch::Batch batch;

	template <bool count, bool side>
	Inline U64 generateMoves(const BoardState& board) {
		batch.init();

		return movegen::generate<count, side>(board, &batch);
	}

	Inline bool lowMaterial(const BoardState& board) {
		if (board.pcs[p] | board.pcs[r] | board.pcs[q]) return false;

		U64 knights = board.pcs[n];
		U64 bishops = board.pcs[b];

		int minors = (int)Bitcount(knights | bishops);

		if (minors <= 1) return true;

		if (minors == 2 && !knights) return !(bishops & SQUARE_COLOR) || !(bishops & ~SQUARE_COLOR);

		return false;
	}

	Inline bool isDraw() {
		if (game::board.halfClock >= 100) return true;
		if (lowMaterial(game::board)) return true;

		int end = std::min((int)game::board.halfClock, game::moveCount);

		int count = 0;
		for (int i = 4; i <= end; i += 2) {
			if (game::movesPlayed[game::moveCount - i].zobrist != game::board.zobrist) continue;
			if (++count == 2) return true;
		}

		return false;
	}

	template <bool side>
	Inline uint16_t relativeMove(const BoardState& pos, const BoardState& best) {
		int flip = (side == white) ? 0 : 56;
		int mir = MIRROR[pos.kMS & 7];

		return uint16_t((best.from ^ flip ^ mir) | (best.to ^ flip ^ mir) << 6);
	}

	Inline int getPiece(const BoardState& brd, int sq) {
		U64 bit = 1ULL << sq;
		if (brd.pcs[p] & bit) return p;
		if (brd.pcs[n] & bit) return n;
		if (brd.pcs[b] & bit) return b;
		if (brd.pcs[r] & bit) return r;
		if (brd.pcs[q] & bit) return q;
		return k;
	}

	template <bool side>
	Inline bool randomMove() {
		generateMoves<false, side>(game::board);

		if (!batch.size) return false;

		uint8_t idx[6][MOVES_MAX];
		int     cnt[6]{};

		for (int i = 0; i < batch.size; ++i) {
			int pc = getPiece(game::board, batch.moves[i].from);
			idx[pc][cnt[pc]++] = i;
		}

		int total = 0;
		for (int pc = p; pc <= k; ++pc) if (cnt[pc]) total += PIECE_WEIGHT[pc];

		int r = (int)(rng() % total);
		int pc = p;
		for (; pc <= k; ++pc) {
			if (!cnt[pc])			  continue;
			if (r < PIECE_WEIGHT[pc]) break;

			r -= PIECE_WEIGHT[pc];
		}

		game::makeMove(batch.moves[idx[pc][rng() % cnt[pc]]]);

		return true;
	}

	Inline bool verifyOpening() {
		int score = 0;
		auto record = [&score](int, int s, const BoardState&, double, long long) { score = s; };

		engine::Worker& w = engine::pool.main();

		w.softNodes = 0;
		w.hardNodes = 0;

		if (!game::board.side) engine::search<white>(w, VERIFY_DEPTH, game::board, 0, 0, record);
		else				   engine::search<black>(w, VERIFY_DEPTH, game::board, 0, 0, record);

		w.hardNodes = HARD_NODES;

		openingsTried++;
		if (std::abs(score) < OPENING_MIN || std::abs(score) > OPENING_LIMIT) { openingsRejected++; return false; }

		return true;
	}

	template <bool side>
	Inline float play() {
		const U64 legalMoves = generateMoves<true, side>(game::board);

		if (!legalMoves) return game::board.checks ? (!side ? 0.0f : 1.0f) : 0.5f;
		if (isDraw()) return 0.5f;

		int score = 0;
		auto res = [&score](int, int s, const BoardState&, double, long long) {
			score = s;
		};

		engine::Worker& w = engine::pool.main();

		w.softNodes = SOFT_NODES;

		const BoardState best = engine::search<side>(w, MAX_DEPTH, game::board, 0, 0, res);

		OrderingSample& o = orderingSamples[orderingCount++];
		o.board = game::board;
		o.fullMove = game::moveCount / 2 + 1;
		o.move = relativeMove<side>(game::board, best);

		if (std::abs(score) > SCORE_LIMIT) return ((score > 0) == !side) ? 1.0f : 0.0f;

		tested++;

		if (!game::board.checks && best.quiet) {
			kept++;

			Sample& s = samples[sampleCount++];
			s.board = game::board;
			s.score = !side ? score : -score;
			s.fullMove = game::moveCount / 2 + 1;
			s.wdl = 0.5f;
		}

		if (std::abs(score) >= ADJ_WIN_SCORE) {
			if (++winPlies >= ADJ_WIN_PLIES) return ((score > 0) == !side) ? 1.0f : 0.0f;
		}
		else winPlies = 0;

		if (game::moveCount >= ADJ_DRAW_MIN_PLY && std::abs(score) <= ADJ_DRAW_SCORE) {
			if (++drawPlies >= ADJ_DRAW_PLIES) return 0.5f;
		}
		else drawPlies = 0;

		game::makeMove(best);

		return -1.0f;
	}

	Inline void startGame() {
		while (true) {
			game::setFen(StartPosition);
			tt::clear();

			int plies = RANDOM_PLIES + (int)(rng() & 1);

			bool ok = true;
			for (int i = 0; i < plies && ok; ++i) {
				ok = !game::board.side ? randomMove<white>() : randomMove<black>();
			}

			if (!ok) continue;
			if (!game::board.side ? generateMoves<true, white>(game::board) == 0 : generateMoves<true, black>(game::board) == 0) continue;
			if (verifyOpening()) break;
		}

		winPlies = 0;
		drawPlies = 0;

		int start = sampleCount;
		float result;

		while (true) {
			const float r = !game::board.side ? play<white>() : play<black>();

			if (r >= 0.0f) { result = r; break; }
		}

		games++;

		int bad = 0;
		for (int i = start; i < sampleCount; ++i) {
			if (samples[i].score >= CONTRADICTION_SCORE && result <= 0.5f) bad++;
			if (samples[i].score <= -CONTRADICTION_SCORE && result >= 0.5f) bad++;
		}

		if (bad > CONTRADICTION_MAX) { discarded++; sampleCount = start; return; }

		for (int i = start; i < sampleCount; ++i) samples[i].wdl = result;
	}

	Inline void flush() {
		char name[64];
		snprintf(name, sizeof(name), "datagen/datagen_%d.txt", pId());

		FILE* f = openFile(name, "a");
		if (!f) { printf("cannot open %s\n", name); return; }

		for (int i = 0; i < sampleCount; ++i) {
			const Sample& s = samples[i];
			fprintf(f, "%s | %d | %.1f\n", game::getFen(s.board, s.fullMove).c_str(), s.score, s.wdl);
		}

		fclose(f);

		sampleCount = 0;
	}

	Inline void flushOrdering() {
		char name[64];
		snprintf(name, sizeof(name), "nnue_ordering/datagen_%d.txt", pId());

		FILE* f = openFile(name, "a");
		if (!f) { printf("cannot open %s\n", name); return; }

		for (int i = 0; i < orderingCount; ++i) {
			const OrderingSample& o = orderingSamples[i];
			fprintf(f, "%s - %u\n", game::getFen(o.board, o.fullMove).c_str(), o.move);
		}

		fclose(f);

		orderingTotal += orderingCount;
		orderingCount = 0;
	}

	Inline void start() {
		using clock = std::chrono::steady_clock;

		std::filesystem::create_directories("datagen");
		std::filesystem::create_directories("nnue_ordering");

		tt::clear();
		batch.sort = false;
		engine::Worker& w = engine::pool.main();

		w.softNodes = SOFT_NODES;
		w.hardNodes = HARD_NODES;

		games = discarded = tested = kept = 0;
		openingsTried = openingsRejected = 0;
		orderingTotal = 0;

		int cnt = 0;
		int gameId = 0;

		const auto t0 = clock::now();

		while (cnt < MAX_SAMPLES) {
			gameId++;

			startGame();

			printf("game %d  -  positions: %d  -  total: %d  -  ordering: %llu\n", gameId, sampleCount, cnt + sampleCount, orderingTotal + orderingCount);

			if (sampleCount >= FLUSH_LIMIT) {
				cnt += sampleCount;
				flush();
			}

			if (orderingCount >= FLUSH_LIMIT) flushOrdering();
		}

		if (orderingCount) flushOrdering();

		const double totalSec = std::chrono::duration<double>(clock::now() - t0).count();
		const double keptPct = tested ? 100.0 * (double)kept / (double)tested : 0.0;

		printf("games: %llu  -  discarded: %llu (%.1f%%)\n", games, discarded, games ? 100.0 * (double)discarded / (double)games : 0.0);
		printf("openings: %llu tried  -  rejected: %.1f%%\n", openingsTried, openingsTried ? 100.0 * (double)openingsRejected / (double)openingsTried : 0.0);
		printf("Total time: %.1fs  -  Total samples: %d  -  kept: %.1f%% of %llu  -  ordering samples: %llu\n", totalSec, cnt, keptPct, tested, orderingTotal);

		w.softNodes = 0;
		w.hardNodes = 0;
	}
}