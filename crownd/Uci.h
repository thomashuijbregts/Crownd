#pragma once

#include "Engine.h"
#include "Game.h"
#include "TranspositionTable.h"
#include "Syzygy.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace uci {
    using namespace defs;
    using namespace bstate;
    using namespace std;

    inline const char* ENGINE_NAME   = "crownd";
    inline const char* ENGINE_AUTHOR = "Thomas Huijbregts";

    inline int curHashMb = 0;
    inline constexpr int DEFAULT_HASH_MB = 64;
    inline constexpr int MAX_HASH_MB = 4096;
    inline constexpr int MAX_THREADS = 256;
    inline constexpr int MAX_OVERHEAD = 5000;
    inline constexpr S64 NO_CLOCK = LLONG_MIN;

    inline thread       mainThread;
    inline atomic<bool> searching{ false };
    inline atomic<bool> stopRequested{ false };

    inline Batch batch;

    struct Params {
        int depth = MAX_PLY - 1;
        S64 movetime = 0;
        S64 wTime = NO_CLOCK;
        S64 bTime = NO_CLOCK;
        S64 wInc = 0;
        S64 bInc = 0;
        U64 nodes = 0;
        int mtg = 0;
        bool infinite = false;
    };

    Inline void stop() {
        if (mainThread.joinable()) {
            stopRequested.store(true);
            stopRequested.notify_one();
            engine::searching.store(false, memory_order_relaxed);
            mainThread.join();
        }
    }

    Inline void initTT(int mb) {
        if (mb <= 0) mb = DEFAULT_HASH_MB;
        if (mb == curHashMb) return;
        tt::free();
        tt::init(size_t(mb));
        curHashMb = mb;
    }

    Inline bool makeMove(const string& mv) {
        batch.init();

        if (game::board.side == white) movegen::generate<false, white>(game::board, &batch);
        else                           movegen::generate<false, black>(game::board, &batch);

        for (int i = 0; i < batch.size; ++i) {
            if (utils::getMoveUci(batch.moves[i]) == mv) {
                game::makeMove(batch.moves[i]);
                return true;
            }
        }
        return false;
    }

    Inline void setPosition(const vector<string>& cmd) {
        if (cmd.size() <= 1) return;

        int i = 1;

        if (cmd[i] == "fen") {
            i++;
            string fen;
            for (; i < cmd.size() && cmd[i] != "moves"; ++i) {
                fen += cmd[i];
                fen += ' ';
            }
            game::setFen(fen.c_str(), false);
        }
        else {
            if (cmd[i] == "startpos")  game::setFen(StartPosition, false);
            else if (cmd[i] == "kiwi") game::setFen(KiwiPete, false);
            i++;
        }

        if (cmd.size() <= i || cmd[i] != "moves") return;
        i++;

        for (; i < cmd.size(); ++i) {
            if (!makeMove(cmd[i])) break;
        }
    }

    Inline void broadcast(int depth, int score, U64 nodes, U64 ms, const BoardState& bestMove) {
        U64 nps = ms > 0 ? (nodes * 1000 / ms) : 0;

        bool mate = abs(score) >= MATE_MAX_PLY;

        if (mate) {
            int plies = MATE - abs(score);
            int val = (plies + 1) / 2;
            if (score < 0) score = -val;
            else           score = val;
        }

        printf("info depth %d score %s %d nodes %llu nps %llu time %llu hashfull %d pv %s\n",
            depth, mate ? "mate" : "cp", score, nodes, nps, ms, 0, utils::getMoveUci(bestMove).c_str());
        fflush(stdout);
    }

    Inline Params parseGo(const vector<string>& cmd) {
        Params prm;
        for (int i = 1; i < cmd.size(); ++i) {
            const string& tok = cmd[i];

            if (tok == "infinite") { prm.infinite = true; continue; }

            S64 v = 0;
            if (i + 1 < cmd.size()) {
                try {
                    v = stoll(cmd[i + 1]);
                }
                catch (...) { continue; }
            }

            if      (tok == "depth")     prm.depth = std::clamp((int)v, 1, MAX_PLY - 1);
            else if (tok == "movetime")  prm.movetime = v;
            else if (tok == "wtime")     prm.wTime = v;
            else if (tok == "btime")     prm.bTime = v;
            else if (tok == "winc")      prm.wInc = v;
            else if (tok == "binc")      prm.bInc = v;
            else if (tok == "nodes")     prm.nodes = v;
            else if (tok == "movestogo") prm.mtg = (int)v;
        }

        return prm;
    }

    Inline int movesToGo(const BoardState& b) {
        int pieces = Bitcount(b.occB());

        if (pieces >= 28) return MTG_OPENING;
        if (pieces >= 14) return MTG_MIDDLE + (pieces - 14) * (MTG_OPENING - MTG_MIDDLE) / 14;
        return MTG_ENDING + (pieces - 2) * (MTG_MIDDLE - MTG_ENDING) / 12;
    }

    Inline void computeBudget(const Params& prm, S64& soft, S64& hard) {
        if (prm.infinite) return;

        if (prm.movetime > 0) {
            hard = max(1LL, prm.movetime - MOVE_LAG);
            soft = hard;
            return;
        }

        S64 myTime = (game::board.side == white) ? prm.wTime : prm.bTime;
        const S64 myInc = (game::board.side == white) ? prm.wInc : prm.bInc;

        if (myTime == NO_CLOCK) return;

        myTime = max(1LL, myTime - MOVE_LAG);

        const S64 mtg = prm.mtg > 0 ? min<S64>(prm.mtg, 50) : movesToGo(game::board);

        soft = myTime / mtg + myInc * INCR_RATIO / 1000;
        hard = prm.mtg > 0 ? min(myTime * 9 / 10, soft * (15 + mtg) / 10) :
            myInc > 0 ? myTime * MOVE_MAX_RATIO / 1000 : min(myTime * MOVE_MAX_RATIO / 1000, soft * HARD_MUL / 1000);

        hard = max(1LL, hard);
        soft = clamp(soft, 1LL, hard);
    }

    Inline void runGo(Params prm) {
        U64 moves;
        if (game::board.side == white) moves = movegen::generate<true, white>(game::board);
        else                           moves = movegen::generate<true, black>(game::board);

        BoardState best;

        if (moves > 0) {
            S64 soft = 0, hard = 0;
            computeBudget(prm, soft, hard);

            engine::Worker& w = engine::pool.main();

            w.hardNodes = prm.nodes > 0 ? prm.nodes : 0;
            w.softNodes = w.hardNodes;

            auto res = [](int d, int score, const BoardState& best, double, U64 totalMs) {
                broadcast(d, score, engine::searchNodes(engine::pool.main()), totalMs, best);
            };

            if (game::board.side == white) best = engine::search<white>(w, prm.depth, game::board, soft, hard, res);
            else                           best = engine::search<black>(w, prm.depth, game::board, soft, hard, res);
        }

        if (prm.infinite) stopRequested.wait(false);

        printf("bestmove %s\n", moves > 0 ? utils::getMoveUci(best).c_str() : "0000");
        fflush(stdout);

        searching.store(false);
    }

    Inline void go(const vector<string>& cmd) {
        stop();

        stopRequested.store(false);
        engine::searching.store(true, memory_order_relaxed);
        searching.store(true);

        mainThread = thread(runGo, parseGo(cmd));
    }

    Inline void setOption(const vector<string>& cmd) {
        if (cmd.size() <= 1) return;

        string name, value;
        int i = 1;

        if (cmd[i] != "name") return;
        i++;

        int cnt = 0;
        while (i < cmd.size() && cmd[i] != "value") { 
            if (cnt > 0) name += ' ';
            name += cmd[i++];
            cnt++;
        }

        if (cmd.size() > i + 1) {
            if (name == "SyzygyPath") {
                string path;
                for (int j = i + 1; j < cmd.size(); ++j) {
                    if (j > i + 1) path += ' ';
                    path += cmd[j];
                }
                if (syzygy::init(path)) printf("info string SyzygyPath set to %s\n", path.c_str());
                else                    printf("info string SyzygyPath not found\n");
            }
            else {
                if (cmd[i] == "value") value = cmd[i + 1];

                if (!utils::isValidNumber(value)) return;

                int val = stoi(value);

                if (name == "Hash")               initTT(clamp(val, 1, MAX_HASH_MB));
                else if (name == "Move Overhead") MOVE_LAG = clamp(val, 0, MAX_OVERHEAD);
                else if (name == "Threads")       engine::pool.init(clamp(val, 1, MAX_THREADS));
                else {
                    tune::set(name, val);
                    if (name == "LMR_BASE") engine::initLmr();
                    else if (name.starts_with("THREAT_")) { 
                        THREAT_VALUE[n] = THREAT_N; 
                        THREAT_VALUE[b] = THREAT_B; 
                        THREAT_VALUE[r] = THREAT_R; 
                        THREAT_VALUE[q] = THREAT_Q; 
                    }
                }

                printf("info string %s set to %d\n", name.c_str(), val);
            }
        }
        else {
            if (name == "Clear Hash") {
                tt::clear();
                printf("info string Hash cleared\n");
            }
        }
        fflush(stdout);
    }

    Inline void sendId() {
        printf("id name %s\n", ENGINE_NAME);
        printf("id author %s\n", ENGINE_AUTHOR);
        printf("option name Hash type spin default %d min 1 max %d\n", DEFAULT_HASH_MB, MAX_HASH_MB);
        printf("option name Threads type spin default 1 min 1 max %d\n", MAX_THREADS);
        printf("option name Move Overhead type spin default 10 min 0 max %d\n", MAX_OVERHEAD);
        printf("option name SyzygyPath type string default <empty>\n");
        printf("option name Clear Hash type button\n");

        for (const tune::Params& p : tune::params) {
            printf("option name %s type spin default %d min %d max %d\n", p.name, p.def, p.min, p.max);
        }

        printf("uciok\n");
        fflush(stdout);
    }

    Inline void ready() {
        if (!searching.load()) {
            if (mainThread.joinable()) mainThread.join();
            initTT(curHashMb);
        }
        printf("readyok\n");
        fflush(stdout);
    }

    Inline void newGame() {
        stop();
        game::setFen(StartPosition);
        tt::clear();
    }

    Inline void spsa() {
        for (const tune::Params& p : tune::params) {
            printf("%s, int, %g, %g, %g, %g, 0.002\n", p.name, double(*p.ref), double(p.min), double(p.max), double(p.cEnd));
        }
        fflush(stdout);
    }

    Inline void loop() {
        batch.sort = false;

        sendId();

        vector<string> cmd;
        string input;
        while (getline(cin, input)) {
            utils::clean(input);

            cmd = utils::split(input, ' ');
            if (cmd.empty()) continue;

            const string& tok = cmd[0];

            if      (tok == "uci")         sendId();
            else if (tok == "isready")     ready();
            else if (tok == "ucinewgame")  newGame();
            else if (tok == "setoption") { stop(); setOption(cmd); }
            else if (tok == "position") {  stop(); setPosition(cmd); }
            else if (tok == "go")          go(cmd);
            else if (tok == "spsa")        spsa();
            else if (tok == "stop")        stop();
            else if (tok == "quit")        break;
        }

        stop();
        exit(0);
    }
}