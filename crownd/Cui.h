#ifndef CUI_H
#define CUI_H

#include "Game.h"
#include "PerftSuite.h"
#include "Uci.h"
#include "Perft.h"
#include "Datagen.h"
#include "Syzygy.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <chrono>

namespace batch {
	struct Batch;
}

struct Command {
	string command;
	string options;
};

struct PerftTest {
	const char* fen;
	int depth;
	int searchDepth;
	U64 result;
};

namespace cui {
	using namespace std::chrono;
	using namespace game;
	using namespace movegen;
	using namespace tt;
	using namespace perft;

	using std::cout;
	using std::cin;
	using std::endl;
	using std::string;
	using std::vector;
	using std::invalid_argument;

	const string EXIT = "exit";
	const string HELP = "help";
	const string SET_FEN = "setfen";
	const string GET_FEN = "fen";
	const string PERFT = "perft";
	const string PRINT_BOARD = "print";
	const string MOVES = "moves";
	const string RESET = "reset";
	const string PIECES = "pieces";
	const string UNDO = "undo";
	const string PLAY = "play";
	const string EVAL = "eval";
	const string TEST = "test";
	const string PERFT_SUITE = "perftsuite";
	const string BENCH = "bench";
	const string TT = "tt";
	const string UCI = "uci";
	const string GEN = "gen";

	const string ARG_T = "-t";

	const Command COMMANDS[]{
		{ EXIT, {} },
		{ HELP, {} },
		{ SET_FEN, {} },
		{ GET_FEN, {} },
		{ PERFT, {}  },
		{ PRINT_BOARD, {} },
		{ MOVES, {} },
		{ RESET, {} },
		{ PIECES, {} },
		{ UNDO, {} },
		{ PLAY,{} },
		{ EVAL,{} },
		{ TEST,{} },
		{ PERFT_SUITE,{} },
		{ BENCH,{} },
		{ TT,{} },
		{ UCI,{} },
		{ GEN,{} }
	};

	const PerftTest TESTS[]{
		{ StartPosition, 7, 18, 3195901860 },
		{ KiwiPete, 6, 18, 8031647685 },
		{ EndGame, 6, 18, 849167880 },
		{ Pos3, 8, 18, 3009794393 },
		{ Pos4, 6, 18, 706045033 },
		{ Pos5, 5, 18, 89941194 },
		{ Pos6, 6, 18, 6923051137 }
	};

	inline batch::Batch batch;

	Inline U64 generateMoves(int depth, const BoardState& board, batch::Batch* batch = nullptr) {
		if (board.side == white)	return perft::start<white>(depth, board, batch);
		else						return perft::start<black>(depth, board, batch);
	}

	Inline U64 generateMoves(int depth, batch::Batch* batch = nullptr) {
		return generateMoves(depth, game::board, batch);
	}

	Inline void initMoves() {
		batch.reset();
		generateMoves(0, &batch);
	}

	Inline void play(vector<string>& cmd) {
		int depth = 10;
		long long hardMs = 0;
		for (size_t i = 1; i < cmd.size(); i++) {
			string& tok = cmd[i];
			if (tok == cui::ARG_T) {
				if (i + 1 < cmd.size() && utils::isValidNumber(cmd[i + 1])) {
					hardMs = stoi(cmd[++i]);
				}
				else {
					cout << "Incorrect time value" << endl;
					return;
				}
			}
			else if (utils::isValidNumber(tok) && stoi(tok) > 0) {
				depth = stoi(tok);
			}
			else {
				cout << "Invalid argument" << endl;
				return;
			}
		}

		if (hardMs > 0) depth = MAX_PLY;

		U64 nodes = 0;
		double time = 0;

		high_resolution_clock::time_point start, end;

		start = high_resolution_clock::now();
		BoardState bestMove;
		engine::Worker& w = engine::pool.main();
		if (game::board.side == white) engine::start<white>(w, depth, game::board, &bestMove, nodes, time, hardMs);
		else						   engine::start<black>(w, depth, game::board, &bestMove, nodes, time, hardMs);
		end = high_resolution_clock::now();

		long long total = duration_cast<microseconds>(end - start).count();
		const U64 nps = time > 0.0 ? U64(nodes / time) : 0;

		cout << "Time:\t" << total / 1000 << " ms" << endl;
		cout << "Nps:\t" << nps << endl;

		game::makeMove(bestMove);

		initMoves();
	}

	Inline void eval(vector<string>& cmd) {
		string fen = game::getFen(game::board);

		if (cmd.size() > 1) {
			string req;
			for (int i = 1; i < cmd.size(); ++i) {
				req += cmd[i];
				if (i < cmd.size() - 1) {
					req += " ";
				}
			}
			try {
				game::setFen(req.c_str());
			}
			catch (invalid_argument& e) {
				cout << "Error while setting fen: " << e.what() << endl;
				game::setFen(fen.c_str());
				return;
			}
		}

		nnue::Nnue& st = engine::pool.main().nnue;

		int eval;
		if (game::board.side == white) {
			nnue::initRoot<white>(st, game::board);
			eval = nnue::evaluate<white>(st, 0, game::board, game::board);
		}
		else {
			nnue::initRoot<black>(st, game::board);
			eval = nnue::evaluate<black>(st, 0, game::board, game::board);
		}

		cout << "Position: " << game::getFen(game::board) << endl;
		cout << "NNUE eval: " << eval << endl;

		game::setFen(fen.c_str());
	}

	Inline void undo() {
		if (game::moveCount > 0) {
			game::unmakeMove();
			initMoves();
		}
		else {
			cout << "No move to undo" << endl;
		}
	}

	Inline void showMoves() {
		BoardState* mv = batch.moves;
		for (int i = 0; i < batch.size; ++i) {
			cout << utils::getMoveSimple(mv[i]) << endl;
		}
		cout << "Moves:\t" << batch.size << endl;
	}

	Inline void printBoard() {
		game::printBoard(game::board);
	}

	Inline void reset() {
		game::setFen(StartPosition);
		initMoves();
	}

	Inline void setBoard(vector<string>& cmd, int size) {
		string fen;
		for (int i = 1; i < size; ++i) {
			fen += cmd[i];
			if (i < size - 1) {
				fen += " ";
			}
		}
		try {
			game::setFen(fen.c_str());
			initMoves();
		}
		catch (invalid_argument& e) {
			cout << "Error while setting fen: " << e.what() << endl;
		}
	}

	Inline void getFen() {
		cout << game::getFen(game::board) << endl;
	}

	Inline void resetTT(vector<string>& cmd) {
		int ttSize = 0;
		if (cmd.size() >= 2) {
			if (utils::isValidNumber(cmd[1])) {
				ttSize = stoi(cmd[1]);
			}
			else {
				cout << "Incorrect size value" << endl;
				return;
			}
		}

		tt::free();

		if (ttSize > 0) tt::init(ttSize);
		else			tt::init();
	}

	Inline void perftFast(int depth) {
		string fen = game::getFen(game::board);

		high_resolution_clock::time_point start, end;

		start = high_resolution_clock::now();
		U64 nodes = generateMoves(depth);
		end = high_resolution_clock::now();

		long long total = duration_cast<microseconds>(end - start).count();

		cout << "Depth:\t\t" << depth << endl;
		cout << "Nodes:\t\t" << nodes << endl;
		cout << "Time:\t\t" << total / 1000 << " ms" << endl;
		if (total > 0) {
			cout << "Average:\t" << (nodes * 1.0 / total) << " Mn/s" << endl;
		}

		game::setFen(fen.c_str());
	}

	Inline void perft(vector<string>& cmd) {
		int depth = -1;

		for (size_t i = 1; i < cmd.size(); i++) {
			string& tok = cmd[i];
			if (utils::isValidNumber(tok) && stoi(tok) > 0) {
				depth = stoi(tok);
			}
			else {
				cout << "Incorrect perft option" << endl;
				return;
			}
		}

		if (depth <= 0) {
			cout << "incorrect depth value" << endl;
			return;
		}

		perftFast(depth);
	}

	Inline void test() {
		string fen = game::getFen(game::board);

		high_resolution_clock::time_point start, end;
		U64 nodes;
		int success = 0;
		int tests = 0;

		for (PerftTest t : cui::TESTS) {
			cout << "Position:\t\t" << t.fen << endl;
			cout << "Depth:\t\t\t" << t.depth << endl;
			game::setFen(t.fen);
			start = high_resolution_clock::now();
			nodes = generateMoves(t.depth);
			end = high_resolution_clock::now();

			long long total = duration_cast<microseconds>(end - start).count();

			cout << "Nodes:\t\t\t" << nodes << endl;
			cout << "Expected nodes:\t\t" << t.result << endl;
			cout << "Time:\t\t\t" << total / 1000 << " ms" << endl;
			if (nodes == t.result) {
				cout << "Test Succeeded!" << endl;
				success++;
			}
			else {
				cout << "Test Failed!" << endl;
			}
			cout << "--------------------------" << endl;

			tests++;
		}

		cout << "Final results:\t\t" << success << "/" << tests << endl;

		game::setFen(fen.c_str());
	}

	Inline void perftsuite() {
		string fenS = game::getFen(game::board);

		int success = 0;
		int tests = 0;
		for (const char* pos : pftsuite::POSITIONS) {
			auto v = utils::split(pos, ';');
			std::string fen = v[0];
			cout << fen << endl;
			int to = v.size();
			for (int i = 1; i < to; i++) {
				game::setFen(fen.c_str());

				auto perftvals = utils::split(v[i], ' ');
				U64 expected = static_cast<U64>(std::strtol(perftvals[1].c_str(), NULL, 10));
				U64 result = generateMoves(i);
				std::string status = expected == result ? "OK" : "ERROR";
				if (expected == result) {
					cout << "   " << i << ": " << result << " " << status << endl;
					success++;
				}
				else  cout << "xxx -> " << i << ": " << result << " vs " << expected << " " << status << endl;

				tests++;
			}
		}
		cout << "Final results:\t\t" << success << "/" << tests << endl;

		game::setFen(fenS.c_str());
	}

	Inline void executeBenchmark(int depth, int amount, bool print) {
		string fen = game::getFen(game::board);

		high_resolution_clock::time_point start, end;
		long long total, best = 0;

		for (int i = 0; i < amount; i++) {
			start = high_resolution_clock::now();
			auto volatile result = generateMoves(depth);
			end = high_resolution_clock::now();

			total = duration_cast<microseconds>(end - start).count();
			if (print) cout << "Time:\t\t" << total / 1000 << "\t" << result << endl;
			if (i == 0) {
				best = total;
			}
			else if (total < best) {
				best = total;
			}
		}

		cout << "Best time:\t\t" << best / 1000 << endl;

		game::setFen(fen.c_str());
	}

	Inline void bench(vector<string>& cmd) {
		int threads = 1;

		for (size_t i = 1; i < cmd.size(); i++) {
			string& tok = cmd[i];
			if (tok == cui::ARG_T) {
				if (i + 1 < cmd.size() && utils::isValidNumber(cmd[i + 1]) && stoi(cmd[i + 1]) > 0) {
					threads = stoi(cmd[++i]);
				}
				else {
					cout << "Incorrect thread value" << endl;
					return;
				}
			}
			else {
				cout << "Invalid argument" << endl;
				return;
			}
		}

		string fen = game::getFen(game::board);

		U64 nodes = 0;
		double time = 0;

		high_resolution_clock::time_point start, end;

		engine::pool.init(threads);

		start = high_resolution_clock::now();
		for (PerftTest t : cui::TESTS) {
			cout << "Position:\t\t" << t.fen << endl;
			cout << "Depth:\t\t\t" << t.searchDepth << endl;

			game::setFen(t.fen);

			tt::clear();

			BoardState bestMove;
			engine::Worker& w = engine::pool.main();
			if (game::board.side == white) engine::start<white>(w, t.searchDepth, game::board, &bestMove, nodes, time);
			else						   engine::start<black>(w, t.searchDepth, game::board, &bestMove, nodes, time);
		}
		end = high_resolution_clock::now();
		long long total = duration_cast<microseconds>(end - start).count();
		const U64 nps = time > 0.0 ? U64(nodes / time) : 0;

		cout << "Time:\t" << total / 1000 << " ms" << endl;
		cout << "Nps:\t" << nps << endl;

		game::setFen(fen.c_str());
	}

	Inline void iteratePieces(U64 p, U64 n, U64 b, U64 r, U64 q) {
		cout << "Type: PAWN\t\tCount: " << Bitcount(p) << endl;
		cout << "Type: KNIGHT\t\tCount: " << Bitcount(n) << endl;
		cout << "Type: BISHOP\t\tCount: " << Bitcount(b) << endl;
		cout << "Type: ROOK\t\tCount: " << Bitcount(r) << endl;
		cout << "Type: QUEEN\t\tCount: " << Bitcount(q) << endl;
	}

	Inline void pcs() {
		cout << "WHITE:" << endl;
		if (game::board.side == white) iteratePieces(game::board.pM(), game::board.nM(), game::board.bM(), game::board.rM(), game::board.qM());
		else						   iteratePieces(game::board.pE(), game::board.nE(), game::board.bE(), game::board.rE(), game::board.qE());

		cout << "BLACK:" << endl;
		if (game::board.side == black) iteratePieces(game::board.pM(), game::board.nM(), game::board.bM(), game::board.rM(), game::board.qM());
		else						   iteratePieces(game::board.pE(), game::board.nE(), game::board.bE(), game::board.rE(), game::board.qE());
	}

	Inline void generateData() {
		dgen::start();
		exit(0);
	}

	Inline void help() {
		cout << "\n******Moves******\n" << endl;
		cout << "Make a move by entering the source and destination square" << endl;
		cout << "Example: a2a3\tAdd Q,R,B,N for promotions" << endl;
		cout << "\n******Commands******\n" << endl;
		cout << "play\t\tPlays the best move for a given depth and time" << endl;
		cout << "\t-t\tTime budget allocated to the search (in ms)" << endl;
		cout << "eval\t\tPrints the NNUE score for a given fen (default is current fen)" << endl;
		cout << "undo\t\tCancels the last move made" << endl;
		cout << "moves\t\tPrints the legal moves of the current position" << endl;
		cout << "reset\t\tResets the game to its initial position" << endl;
		cout << "print\t\tPrints the board representation of the current position" << endl;
		cout << "fen\t\tPrints the FEN record of the current position" << endl;
		cout << "setfen\t\tSets the position given by a FEN record" << endl;
		cout << "perft\t\tGenerates all moves down to a given depth" << endl;
		cout << "tt\t\tToggles the transposition table on/off (tt [size])" << endl;
		cout << "bench\t\tSearches popular positions and provides an average" << endl;
		cout << "\t-t\tAmount of threads used (default is 1)" << endl;
		cout << "perftsuite\tTests full list of positions to validate perft results" << endl;
		cout << "pieces\t\tPrints the pieces' count for each side" << endl;
		cout << "uci\t\tEnter Uci mode" << endl;
		cout << "exit\t\tExit the application" << endl;
	}

	Inline bool executeMove(string& move) {
		BoardState* mv = batch.moves;

		for (int i = 0; i < batch.size; ++i) {
			if (move == utils::getMoveSimple(mv[i])) {
				game::makeMove(mv[i]);
				return true;
			}
		}
		return false;
	}

	Inline bool isCommand(vector<string>& cmd) {
		int size = cmd.size();
		if (size == 0) {
			return false;
		}

		if (cmd[0].length() == 0) {
			return false;
		}

		string first = cmd[0];
		for (Command CMD : cui::COMMANDS) {
			if (CMD.command == first) {
				return true;
			}
		}

		if (utils::validMove(first)) {
			if (executeMove(first)) {
				initMoves();
			}
			else {
				cout << "Move does not exist for this position" << endl;
			}
			return true;
		}

		cout << "Invalid command... type 'help' for command list" << endl;
		return false;
	}

	Inline void execute(vector<string>& cmd) {

		if (!isCommand(cmd)) {
			return;
		}

		string first = cmd[0];

		if (first == cui::PLAY) {
			play(cmd);
			return;
		}

		if (first == cui::EVAL) {
			eval(cmd);
			return;
		}

		if (first == cui::UCI) {
			uci::loop();
			return;
		}

		if (first == cui::UNDO) {
			undo();
			return;
		}

		if (first == cui::MOVES) {
			showMoves();
			return;
		}

		if (first == cui::RESET) {
			reset();
			return;
		}

		if (first == cui::PRINT_BOARD) {
			printBoard();
			return;
		}

		if (first == cui::GET_FEN) {
			getFen();
			return;
		}

		if (first == cui::SET_FEN) {
			setBoard(cmd, cmd.size());
			return;
		}

		if (first == cui::PERFT) {
			perft(cmd);
			return;
		}

		if (first == cui::TT) {
			resetTT(cmd);
			return;
		}

		if (first == cui::TEST) {
			test();
			return;
		}

		if (first == cui::PERFT_SUITE) {
			perftsuite();
			return;
		}

		if (first == cui::BENCH) {
			bench(cmd);
			return;
		}

		if (first == cui::PIECES) {
			pcs();
			return;
		}

		if (first == cui::GEN) {
			generateData();
			return;
		}

		if (first == cui::HELP) {
			help();
			return;
		}

		if (first == cui::EXIT) {
			exit(0);
		}
	}

	Inline void start() {
		string input;
		vector<string> cmd;
		batch.sort = false;
		cout << "+---+---+---+---+---+---+---+---+---+---+" << endl;
		cout << "|      crownd by Thomas Huijbregts      |" << endl;
		cout << "+---+---+---+---+---+---+---+---+---+---+" << endl;

		help();

		tt::init(64);
		initMoves();

		while (getline(cin, input)) {
			utils::clean(input);

			cmd = utils::split(input, ' ');
			execute(cmd);
		};
	}
}

#endif