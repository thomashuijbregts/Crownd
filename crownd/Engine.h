#pragma once

#include "MoveGenerator.h"
#include "MovePicker.h"
#include "Game.h"
#include "Utils.h"
#include "Nnue.h"
#include "See.h"
#include "Cuckoo.h"
#include "Syzygy.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <cmath>

namespace engine {

    using namespace defs;
    using namespace bstate;
    using namespace movegen;
    using namespace batch;
    using namespace nnue;
    using namespace movepick;
    using namespace std;

    using clock = chrono::steady_clock;

    struct StopSearch {};
    inline atomic<bool> searching;

    struct TTHit {
        uint16_t move = 0;
        int eval = NO_EVAL;
        int score = NO_EVAL;
        int depth = 0;
        bool pv = false;
    };

    struct RootInfo {
        BoardState best;
        uint16_t bestInt;
        int bestScore;
        int depth;
        U64 moveNodes[4096];
    };

    struct Worker {
        thread th;

        U64 softNodes;
        U64 hardNodes;
        clock::time_point timeLimit;
        bool canAbort;
        bool helper;
        RootInfo root;
        atomic<U64> nodes;

        SearchHistory hist;
        Nnue nnue;
        Batch batches[MAX_PLY];
        TTHit seHit[MAX_PLY];
        int evals[MAX_PLY];
        int nullDist[MAX_PLY];
        Zobrist repHistory[101 + MAX_PLY];
        int repCount;
        int nullPlies;
    };

    template <bool side, bool mainThread, class SearchResult>
    BoardState iterate(Worker& w, int maxDepth, const BoardState& board, U64 softMs, U64 hardMs, const SearchResult& results);

    struct ThreadPool {
        vector<unique_ptr<Worker>> workers;
        mutex mtx;
        condition_variable cv;

        BoardState board;
        int maxDepth;

        U64 gen = 0;
        int running;
        bool stopped;

        inline Worker& main() {
            return *workers[0];
        }

        inline void stop() {
            {
                unique_lock<mutex> lock(mtx);
                stopped = true;
            }

            cv.notify_all();
            for (auto& wk : workers) if (wk->th.joinable()) wk->th.join();
        }

        inline void loop(Worker* w, U64 g) {
            while (true) {
                unique_lock<mutex> lock(mtx);
                cv.wait(lock, [&] { return stopped || gen != g; });

                if (stopped) return;

                g = gen;
                lock.unlock();

                if (board.side == white) iterate<white, false>(*w, maxDepth, board, 0, 0, nullptr);
                else                     iterate<black, false>(*w, maxDepth, board, 0, 0, nullptr);

                lock.lock();
                if (--running == 0) cv.notify_all();
            }
        }

        inline void pause() {
            searching.store(false, memory_order_relaxed);

            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [&] { return running == 0; });
        }

        inline void start(const BoardState& board, int maxDepth) {
            for (int i = 1; i < workers.size(); ++i) workers[i]->nodes.store(0, memory_order_relaxed);

            {
                unique_lock<mutex> lock(mtx);
                this->board = board;
                this->maxDepth = maxDepth;
                running = int(workers.size()) - 1;
                ++gen;
            }

            cv.notify_all();
        }

        inline void clear() {
            for (auto& wk : workers) wk->hist.clear();
        }

        inline void init(int n) {
            stop();
            stopped = false;

            workers.resize(1);
            for (int i = 1; i < n; ++i) {
                workers.push_back(make_unique<Worker>());
                workers.back()->helper = true;
            }

            for (int i = 1; i < n; ++i) workers[i]->th = thread(&ThreadPool::loop, this, workers[i].get(), gen);
        }

        ~ThreadPool() { stop(); }
    };

    inline ThreadPool pool;

    Inline U64 searchNodes(const Worker& w) {
        U64 total = w.nodes.load(memory_order_relaxed);
        if (!w.helper) {
            for (int i = 1; i < pool.workers.size(); ++i) {
                total += pool.workers[i]->nodes.load(memory_order_relaxed);
            }
        }
        return total;
    }

    inline int reductions[MOVES_MAX];

    Inline void initLmr() {
        reductions[0] = 0;
        for (int i = 1; i < MOVES_MAX; ++i) {
            reductions[i] = int(LMR_BASE / 10.0 * log(i));
        }
    }

    Inline void init() {
        initLmr();
        pool.workers.push_back(make_unique<Worker>());
    }

    Inline bool shouldAbort(Worker& w) {
        if (!w.canAbort)                                  return false;
        if (!searching.load(memory_order_relaxed))        return true;
        if (w.hardNodes && searchNodes(w) >= w.hardNodes) return true;
        if (clock::now() >= w.timeLimit)                  return true;
        return false;
    }

    ForceInline int valueToTT(int v, int ply) {
        if (v >= TB_MATE_MAX_PLY)  return v + ply;
        if (v <= -TB_MATE_MAX_PLY) return v - ply;
        return v;
    }

    ForceInline int valueFromTT(int v, int ply) {
        if (v >= TB_MATE_MAX_PLY)  return v - ply;
        if (v <= -TB_MATE_MAX_PLY) return v + ply;
        return v;
    }

    ForceInline void clear() {
        pool.clear();
    }

    ForceInline bool isRepetition(Worker& w, const BoardState& board, int ply) {
        const int current = w.repCount + ply;
        const int end = min(min(int(board.halfClock), w.nullDist[ply]), current);
        if (end < 4) return false;

        int count = 0;
        for (int i = 4; i <= end; i += 2) {
            const int idx = current - i;

            if (w.repHistory[idx] != board.zobrist) continue;

            if (idx > w.repCount) return true;
            if (++count == 2)     return true;
        }
        return false;
    }

    template <bool side>
    ForceInline bool isFutureRepetition(Worker& w, const BoardState& board, int ply) {
        const int current = w.repCount + ply;
        const int end = min(min(int(board.halfClock), w.nullDist[ply]), current);
        if (end < 3) return false;

        Zobrist other = board.zobrist ^ w.repHistory[current - 1] ^ zobrist::SIDE;

        for (int i = 3; i <= end; i += 2) {
            other ^= w.repHistory[current - i + 1] ^ w.repHistory[current - i] ^ zobrist::SIDE;
            if (other != 0) continue;

            const int idx = current - i;
            const Zobrist delta = board.zobrist ^ w.repHistory[idx];

            int j = cuckoo::hash1(delta);
            if (cuckoo::keys[j] != delta) {
                j = cuckoo::hash2(delta);
                if (cuckoo::keys[j] != delta) continue;
            }

            const uint16_t mv = cuckoo::moves[j];
            const int s1 = mv & 63;
            const int s2 = (mv >> 6) & 63;

            const int to = ((1ULL << s1) & board.occM) ? s2 : s1;

            if ((PIN_MASKS[s1][s2] | (1ULL << to)) & board.occB()) continue;

            if (idx > w.repCount) return true;
        }
        return false;
    }

    template <bool side>
    ForceInline int correctEval(Worker& w, const BoardState& b, int eval, int ply) {
        eval = eval * (CORR_HALF_CLOCK - b.halfClock) / CORR_HALF_CLOCK;

        int corr = w.hist.pawnCorr[side][b.pawnKey & CORR_MASK] * CORR_W_PAWN;
        corr += w.hist.nonPawnCorr[side][white][b.nonPawnKey[white] & CORR_MASK] * CORR_W_NONPAWN;
        corr += w.hist.nonPawnCorr[side][black][b.nonPawnKey[black] & CORR_MASK] * CORR_W_NONPAWN;
        corr += w.hist.minorCorr[side][b.minorKey & CORR_MASK] * CORR_W_MINOR;
        corr += w.hist.majorCorr[side][b.majorKey & CORR_MASK] * CORR_W_MAJOR;
        //corr += w.hist.contCorr[side][b.atkr][b.to] * CORR_W_CONT;
        if (b.atkr != noPiece) {
            corr += w.hist.contCorr[side][0][w.hist.contKeys[ply + 2]][b.atkr][b.to] * CORR_W_CONT2;
            corr += w.hist.contCorr[side][1][w.hist.contKeys[ply]][b.atkr][b.to] * CORR_W_CONT4;
        }
        corr /= CORR_DIV;

        return clamp(eval + corr, -TB_MATE_MAX_PLY + 1, TB_MATE_MAX_PLY - 1);
    }

    ForceInline int timeFactor(int stability, double frac, int drop) {
        int stab = STABILITY_BASE - STABILITY_MULT * stability;
        int node = clamp(int(NODE_BASE - NODE_MULT * frac), NODE_MIN, NODE_BASE);
        int eval = clamp(1000 + drop * 1000 / EVAL_DIV, 1000, EVAL_MAX);

        return min(stab * node / 1000 * eval / 1000, FACTOR_MAX);
    }

    template <bool side>
    Inline int quiescence(Worker& w, const BoardState& parent, const BoardState& board, int ply, int alpha, int beta, int razEval = NO_EVAL) {
        w.nodes.store(w.nodes.load(memory_order_relaxed) + 1, memory_order_relaxed);
        if ((w.nodes & 2047) == 0 && shouldAbort(w)) throw StopSearch{};

        if (ply >= MAX_PLY - 1) return nnue::evaluate<side>(w.nnue, ply, parent, board);

        const bool pvNode = beta - alpha > 1;

        uint16_t ttMove = 0;
        int ttScore = NO_EVAL;
        uint8_t ttBound = 0;
        bool ttPv = pvNode;
        int eval = razEval;

        const Zobrist z = board.zobrist;
        Bucket* ttBucket = &tt::bucket(z);
        TTData e;
        if (tt::probe(*ttBucket, z, e)) {
            ttScore = valueFromTT(e.score, ply);
            ttBound = e.bound;

            if (!pvNode && (ttBound == BEX || (ttBound == BLO && ttScore >= beta) || (ttBound == BUP && ttScore <= alpha))) return ttScore;

            ttMove = e.move;
            ttPv = e.pv || pvNode;
            eval = e.eval;
        }

        int rawEval = -INF;
        int best = -INF;
        int standPat = -INF;

        if (!board.checks) {
            if (eval != NO_EVAL) rawEval = eval;
            else {
                rawEval = nnue::evaluate<side>(w.nnue, ply, parent, board);

                tt::write(0, *ttBucket, z, 0, rawEval, BNO, 0, tt::gen, ttPv);
            }

            standPat = correctEval<side>(w, board, rawEval, ply);

            if (abs(ttScore) < TB_MATE_MAX_PLY && (ttBound == BEX || (ttBound == BLO && ttScore > standPat) || (ttBound == BUP && ttScore < standPat))) {
                standPat = ttScore;
            }

            best = standPat;

            if (best >= beta) return best;

            if (best > alpha) alpha = best;

            //delta pruning
            if (best + see::SEE_VALUE[q] + DELTA_MARGIN < alpha) return alpha;
        }

        MovePicker mp;
        mp.init<side>(w.batches[ply], w.hist, board, ttMove, 0, 0, ply);
        mp.noisyOnly = !board.checks;

        const int futBase = best + FP_MARGIN_Q;
        int score;
        BoardState* bestMoveTT = nullptr;

        while (mp.hasNext<side>()) {
            BoardState& m = mp.next();
            const int i = mp.index();

            if (best > -TB_MATE_MAX_PLY) {
                if (!m.checks && m.quiet) break;

                if (!m.promo && (!m.checks || m.quiet) && i > 2) continue;

                if (!board.checks) {
                    //futility pruning
                    if (!m.checks && futBase + (see::SEE_VALUE[m.vctm] + see::SEE_PROMO[m.promo]) <= alpha) continue;

                    //SEE pruning
                    if (!see::see(m, SEE_MARGIN_NOISY_Q)) continue;
                }
                else if (!see::see(m, SEE_MARGIN_Q)) continue;
            }

            nnue::compute<side>(w.nnue, ply, parent, board);

            hist::updateContKey(m, w.hist, ply);

            score = -quiescence<!side>(w, board, m, ply + 1, -beta, -alpha);

            if (score > best) {
                best = score;
                bestMoveTT = &m;
                if (score > alpha) {
                    alpha = score;
                    if (score >= beta) break;
                }
            }
        }

        if (mp.count() == 0) return board.checks ? -MATE + ply : standPat;

        const uint8_t bound = (best >= beta) ? BLO : BUP;
        const uint16_t mv = bestMoveTT ? bestMoveTT->toInt() : ttMove;
        tt::write(0, *ttBucket, board.zobrist, valueToTT(best, ply), board.checks ? NO_EVAL : rawEval, bound, mv, tt::gen, ttPv);

        return best;
    }

    template <bool side, bool first = false, bool null = false, bool nullVerif = false>
    Inline int alphaBeta(Worker& w, int depth, const BoardState& parent, const BoardState& board, int ply, int alpha, int beta, bool cutNode, uint16_t seMove = 0) {
        if (ply >= MAX_PLY - 1) return nnue::evaluate<side>(w.nnue, ply, parent, board);

        if constexpr (!first) {
            w.nullDist[ply] = null ? 0 : w.nullDist[ply - 1] + 1;

            if (isRepetition(w, board, ply)) return 0;

            if (board.halfClock >= 100) {
                if (!board.checks) return 0;

                return movegen::generate<true, side>(board) ? 0 : (-MATE + ply);
            }

            w.repHistory[w.repCount + ply] = board.zobrist;
        }

        if (depth == 0) return quiescence<side>(w, parent, board, ply, alpha, beta);

        const bool pvNode = beta - alpha > 1;
        const int  alphaOrig = alpha;

        if constexpr (!first) {
            if (!pvNode && !seMove && alpha < 0 && isFutureRepetition<side>(w, board, ply)) {
                alpha = 0;
                if (alpha >= beta) return alpha;
            }

            //mate distance pruning
            alpha = max(alpha, -MATE + ply);
            beta = min(beta, MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }

        w.nodes.store(w.nodes.load(memory_order_relaxed) + 1, memory_order_relaxed);
        if ((w.nodes & 2047) == 0 && shouldAbort(w)) throw StopSearch{};

        auto [ttMove, ttEval, ttScore, ttDepth, ttPv] = seMove ? w.seHit[ply] : TTHit{};

        Bucket* ttBucket = nullptr;
        int evalCorr = NO_EVAL;
        int eval = NO_EVAL;
        uint8_t ttBound = 0;
        bool ttHit = false;
        int best = -INF;

        ttPv = ttPv || pvNode;

        if (!seMove) {
            const Zobrist z = board.zobrist;
            ttBucket = &tt::bucket(z);
            TTData e;
            if (tt::probe(*ttBucket, z, e)) {
                ttEval = e.eval;

                if (e.bound != BNO) {
                    ttHit = true;
                    ttMove = e.move;
                    ttEval = e.eval;
                    ttScore = valueFromTT(e.score, ply);
                    ttDepth = e.depth;
                    ttBound = e.bound;
                    ttPv = e.pv || pvNode;

                    if constexpr (!first) {
                        if (!pvNode && e.depth > depth - (ttScore <= beta)) {
                            if (ttBound == BEX || (ttBound == BLO && ttScore >= beta) || (ttBound == BUP && ttScore <= alpha)) return ttScore;
                        }
                    }
                }
            }

            //internal iterative reductions
            if (!ttHit && (pvNode || cutNode) && depth >= IIR_MIN_DEPTH && !board.checks) depth--;

            if constexpr (!first) {
                const int wdl = syzygy::probeWdl<side>(board);
                if (wdl != TB_RESULT_FAILED) {
                    int score; uint8_t bound;

                    if (wdl == TB_WIN) {
                        score = TB_MATE - ply;
                        bound = BLO;
                    }
                    else if (wdl == TB_LOSS) {
                        score = -TB_MATE + ply;
                        bound = BUP;
                    }
                    else {
                        score = 0;
                        bound = BEX;
                    }

                    if (bound == BEX || (bound == BLO && score >= beta) || (bound == BUP && score <= alpha)) {
                        tt::write(depth, *ttBucket, board.zobrist, valueToTT(score, ply), NO_EVAL, bound, 0, tt::gen, ttPv);
                        return score;
                    }

                    if (pvNode && bound == BLO) {
                        best = score;
                        if (score > alpha) alpha = score;
                    }
                }
            }
        }

        bool improving = false;
        w.evals[ply] = NO_EVAL;

        if (!board.checks) {
            if (ttEval == NO_EVAL) {
                ttEval = nnue::evaluate<side>(w.nnue, ply, parent, board);

                tt::write(0, *ttBucket, board.zobrist, 0, ttEval, BNO, 0, tt::gen, ttPv);
            }

            evalCorr = correctEval<side>(w, board, ttEval, ply);
            eval = evalCorr;

            if (!seMove && (ttBound == BEX || (ttBound == BLO && ttScore > eval) || (ttBound == BUP && ttScore < eval))) {
                eval = ttScore;
            }

            if (ply >= 2 && w.evals[ply - 2] != NO_EVAL) improving = evalCorr > w.evals[ply - 2];
            else if (ply >= 4)                           improving = evalCorr > w.evals[ply - 4];

            w.evals[ply] = evalCorr;

            if (!pvNode) {
                //Stability reductions
                if (!cutNode && !seMove && ply >= STAB_PLIES) {
                    int lo = evalCorr, hi = evalCorr, mul = -1;
                    for (int i = ply - 1; i >= ply - STAB_PLIES; --i) {
                        if (w.evals[i] == NO_EVAL) continue;

                        const int val = w.evals[i] * mul;
                        lo = min(lo, val);
                        hi = max(hi, val);
                        mul = -mul;
                    }
                    if ((hi - lo) * depth <= STAB_MARGIN) depth--;
                    //if ((hi - lo) * depth <= STAB_MARGIN && --depth == 0) return quiescence<side>(w, parent, board, ply, alpha, beta, ttEval);
                }

                //Razoring
                if (!cutNode && eval + RAZOR_MUL * depth < alpha) {
                    int score = quiescence<side>(w, parent, board, ply, alpha, beta, ttEval);
                    if (score <= alpha) return score;
                }

                //Reverse futility pruning
                if (depth <= RFP_MAX_DEPTH && beta < TB_MATE_MAX_PLY && eval - RFP_MARGIN * (depth - improving) >= beta) return eval;

                //Null move pruning
                if constexpr (!null) {
                    if (!seMove && cutNode && bstate::hasEnoughMaterial(board) && eval >= beta + NMP_BETA_MARGIN_BASE - NMP_BETA_MARGIN_MUL * depth - NMP_BETA_MARGIN_IMP * improving && ply >= w.nullPlies) {
                        const int nullDepth = depth - (NMP_BASE + depth * NMP_DEPTH_MUL + min((eval - beta) * 1024 / NMP_EVAL_DIV, NMP_EVAL_MAX)) / 1024;

                        const BoardState nullBoard = bstate::makeNull<side>(board);

                        nnue::compute<side>(w.nnue, ply, parent, board);

                        hist::clearContKey(w.hist, ply);

                        int score;
                        if (nullDepth <= 0) score = -quiescence<!side>(w, board, nullBoard, ply + 1, -beta, -beta + 1);
                        else                score = -alphaBeta<!side, false, true>(w, nullDepth, board, nullBoard, ply + 1, -beta, -beta + 1, !cutNode);

                        //if (score >= beta) return score >= TB_MATE_MAX_PLY ? beta : score;
                        //if (score >= beta && score < TB_MATE_MAX_PLY) return score;
                        if (score >= beta) {
                            if (score >= TB_MATE_MAX_PLY) score = beta;

                            if (w.nullPlies || depth < NMP_VERIF_DEPTH) return score;

                            w.nullPlies = ply + nullDepth * 3 / 4;
                            int verif = alphaBeta<side, false, false, true>(w, nullDepth, parent, board, ply, beta - 1, beta, false);
                            w.nullPlies = 0;

                            if (verif >= beta) return score;
                        }
                    }
                }

                //Prob cut
                int probCutBeta = beta + PROBCUT_MARGIN - (improving * PROBCUT_IMPROVING);

                if (depth >= PROBCUT_MIN_DEPTH && abs(beta) < TB_MATE_MAX_PLY && !(ttDepth >= depth - PROBCUT_REDUCTION && ttScore < probCutBeta)) {
                    const int seeThreshold = probCutBeta - eval;

                    MovePicker mp;
                    mp.init<side>(w.batches[ply], w.hist, board, ttMove, seMove, depth, ply, seeThreshold, true);
                    mp.noisyOnly = true;

                    while (mp.hasNext<side>()) {
                        BoardState& m = mp.next();

                        nnue::compute<side>(w.nnue, ply, parent, board);

                        hist::updateContKey(m, w.hist, ply);

                        int score = -quiescence<!side>(w, board, m, ply + 1, -probCutBeta, -probCutBeta + 1);

                        if (score >= probCutBeta) {
                            int pcDepth = max(0, depth - PROBCUT_REDUCTION);

                            score = -alphaBeta<!side>(w, pcDepth, board, m, ply + 1, -probCutBeta, -probCutBeta + 1, !cutNode);

                            if (score >= probCutBeta) {
                                if (!seMove) tt::write(pcDepth + 1, *ttBucket, board.zobrist, valueToTT(score, ply), ttEval, BLO, m.toInt(), tt::gen, ttPv);
                                return score;
                            }
                        }
                    }
                }
            }
        }

        Batch& batch = w.batches[ply];
        MovePicker mp;
        mp.init<side>(batch, w.hist, board, ttMove, seMove, depth, ply);
        batch.nQuiet = batch.nNoisy = 0;

        BoardState* bestMoveTT = nullptr;
        int score;
        const int bonus = hist::calcBonus(depth);
        const int malus = hist::calcMalus(depth);

        const int depthSq = depth * depth;
        const int lmpFull = (LMP_BASE + LMP_MUL * depthSq) / 1024;
        const int lmpBase = improving ? lmpFull : lmpFull * LMP_NOT_IMP / 1024;

        uint16_t& killer1 = w.hist.killers[ply][0];
        uint16_t& killer2 = w.hist.killers[ply][1];
        uint16_t& cntrMove = w.hist.cntrMove[side][board.atkr][board.to];

        const int contKey1 = w.hist.contKeys[ply + 3];
        const int contKey2 = w.hist.contKeys[ply + 2];
        const int contKey4 = w.hist.contKeys[ply];

        while (mp.hasNext<side>()) {
            BoardState& m = mp.next();

            if (first && !syzygy::processMove(m)) continue;

            const int i = mp.index();
            const uint16_t pm = m.toInt();

            int16_t& h = w.hist.history[side][(batch.threats[6] >> m.from) & 1][(batch.threats[6] >> m.to) & 1][m.from][m.to];
            int16_t& ph = w.hist.pawnHistory[board.pawnKey & PH_MASK][side][m.atkr][m.to];
            int16_t& noisyh = w.hist.noisyHistory[side][m.atkr][m.to][m.vctm];

            int qPrune = 0, qLmr = 0;
            if (m.quiet) {
                const int c1 = w.hist.contHistory[0][contKey1][m.atkr][m.to];
                const int c2 = w.hist.contHistory[1][contKey2][m.atkr][m.to];

                qPrune = (h * PRUNE_W_MAIN + ph * PRUNE_W_PAWN + c1 * CONT_PRUNE_1 + c2 * CONT_PRUNE_2) / 1024;
                qLmr = h + (c1 * LMR_W_C1 + c2 * LMR_W_C2) / 1024;
            }

            if (i > 0 && best > -TB_MATE_MAX_PLY) {
                if (i >= lmpBase) mp.skipQuiets();

                if (m.quiet && !m.checks) {
                    if (!pvNode) {
                        //futility pruning
                        if (depth <= FP_MAX_DEPTH && eval + FP_BASE + FP_MUL * depth <= alpha) continue;
        
                        //Late move pruning with history modulation
                        if (depth <= LMP_MAX_DEPTH) {
                            int lmp = lmpBase + clamp(qPrune / HIST_DIV, -LMP_HIST_CLAMP, LMP_HIST_CLAMP);

                            if (i >= lmp) continue;
                        }

                        //history pruning
                        if (qPrune < -(HP_QUIET_BASE + HP_QUIET_MUL * depth)) continue;

                        //quiet SEE pruning
                        if (!see::see(m, SEE_MARGIN_QUIET * depthSq)) continue;
                    }
                }
                else {
                    //noisy history pruning
                    if (!pvNode && noisyh < -(HP_NOISY_BASE + HP_NOISY_MUL * depth)) continue;

                    //noisy SEE pruning
                    if (!see::see(m, SEE_MARGIN_NOISY * depthSq)) continue;
                }
            }

            int extension = 0;

            if constexpr (!first) {
                //singular extensions
                if (!seMove && pm == ttMove && depth >= SE_MIN_DEPTH && ttDepth >= depth - SE_TT_DEPTH_MARGIN && (ttBound & BLO) && abs(ttScore) < TB_MATE_MAX_PLY) {
                    const int sBeta = ttScore - (SE_MARGIN + SE_TT_MARGIN * (ttPv && !pvNode)) * depth / SE_DIV;
                    const int sDepth = (depth - 1) / 2;

                    w.seHit[ply] = { ttMove, ttEval, ttScore, ttDepth, ttPv };

                    nnue::compute<side>(w.nnue, ply, parent, board);

                    const int sScore = alphaBeta<side, false, null>(w, sDepth, parent, board, ply, sBeta - 1, sBeta, cutNode, pm);

                    if (sScore < sBeta) 
                        extension = 1 + (!pvNode && sScore < sBeta - SE_DOUBLE_MARGIN) + (!pvNode && sScore < sBeta - SE_TRIPLE_MARGIN);
                    else if (sBeta >= beta) {
                        return sBeta;
                    }
                    else if (ttScore >= beta) extension = -2 - cutNode;
                    else if (cutNode)         extension = -2;
                }
            }

            const int newDepth = max(0, depth - 1 + extension);

            U64 nodesStart = w.nodes;

            nnue::compute<side>(w.nnue, ply, parent, board);

            hist::updateContKey(m, w.hist, ply);

            //Late move reductions
            if (i > 0) {
                int reduction = 0;
                if (m.quiet) {
                    reduction = reductions[depth] * reductions[i] + LMR_CONST;
                    if (pm == killer1 || pm == killer2 || pm == cntrMove)
                        reduction -= LMR_GOOD_QUIET;
                    if (!improving) reduction += LMR_NOT_IMPROVING;
                    if (cutNode)    reduction += LMR_CUTNODE;
                    reduction -= qLmr * LMR_HIST_MUL / HIST_MAX;
                }
                else {
                    reduction = reductions[depth] * reductions[i] * 1024 / LMR_NOISY_DIV;
                    if (cutNode) reduction += LMR_CUTNODE_NOISY;
                    reduction -= noisyh * LMR_NOISY_HIST_MUL / HIST_MAX;
                }

                if (!pvNode)      reduction += LMR_NOT_PV;
                if (board.checks) reduction -= LMR_PARENT_CHECK;
                if (m.checks)     reduction -= LMR_MOVE_CHECK;
                if (ttPv) {
                    if (ttHit && ttScore <= alpha) reduction += LMR_TT_PV_BAD;
                    else                           reduction -= LMR_TT_PV;
                }
                if (mp.ttBoard.capSq != noSquare) reduction += LMR_TT_CAP;

                reduction /= 1024;

                const int reducedDepth = clamp(newDepth - reduction, 0, newDepth + 1);

                score = -alphaBeta<!side>(w, reducedDepth, board, m, ply + 1, -alpha - 1, -alpha, true);

                if (score > alpha && reducedDepth < newDepth) {
                    score = -alphaBeta<!side>(w, newDepth, board, m, ply + 1, -alpha - 1, -alpha, !cutNode);
                }

                if (score > alpha && score < beta) {
                    score = -alphaBeta<!side>(w, newDepth, board, m, ply + 1, -beta, -alpha, false);
                }
            }
            else {
                score = -alphaBeta<!side>(w, newDepth, board, m, ply + 1, -beta, -alpha, pvNode ? false : !cutNode);
            }

            if constexpr (first) w.root.moveNodes[pm & 4095] += w.nodes - nodesStart;

            if (score > best) {
                best = score;
                bestMoveTT = &m;
                if constexpr (first) {
                    if (score > alpha) { w.root.best = m; w.root.bestInt = pm; w.root.bestScore = score; }
                }
            }

            if (score >= beta) {
                if (!seMove) {
                    if (m.quiet) {
                        if constexpr (!null) cntrMove = pm;
                        if (pm != killer1) { killer2 = killer1; killer1 = pm; }

                        hist::updateHistory(h, bonus);
                        hist::updateHistory(ph, bonus);
                        hist::contUpdate(w.hist, ply, m.atkr, m.to, bonus);

                        for (int j = 0; j < batch.nQuiet; ++j) {
                            const BoardState& q = *batch.triedQ[j];
                            hist::updateHistory(w.hist.history[side][(batch.threats[6] >> q.from) & 1][(batch.threats[6] >> q.to) & 1][q.from][q.to], -malus);
                            hist::updateHistory(w.hist.pawnHistory[board.pawnKey & PH_MASK][side][q.atkr][q.to], -malus);
                            hist::contUpdate(w.hist, ply, q.atkr, q.to, -malus);
                        }
                    }
                    else {
                        hist::updateHistory(noisyh, bonus);
                    }

                    for (int j = 0; j < batch.nNoisy; ++j) {
                        const BoardState& c = *batch.triedN[j];
                        hist::updateHistory(w.hist.noisyHistory[side][c.atkr][c.to][c.vctm], -malus);
                    }
                }
                break;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (!seMove) {
                if (m.quiet) batch.triedQ[batch.nQuiet++] = &m;
                else         batch.triedN[batch.nNoisy++] = &m;
            }
        }

        if (mp.count() == 0) return seMove ? alpha : board.checks ? -MATE + ply : 0;


        if (!seMove) {
            const bool failHigh = best >= beta;
            const bool failLow = best <= alphaOrig;
            const bool noisyBest = !failLow && bestMoveTT && !bestMoveTT->quiet;

            if (!board.checks && !noisyBest && abs(best) < TB_MATE_MAX_PLY && !(failHigh && best <= evalCorr) && !(failLow && best >= evalCorr)) {
                auto bonus = clamp(int(best - evalCorr) * depth / 8, -CORR_MAX / 4, CORR_MAX / 4);

                hist::updateCorrectionHistory(w.hist.pawnCorr[side][board.pawnKey & CORR_MASK], bonus);
                hist::updateCorrectionHistory(w.hist.nonPawnCorr[side][white][board.nonPawnKey[white] & CORR_MASK], bonus);
                hist::updateCorrectionHistory(w.hist.nonPawnCorr[side][black][board.nonPawnKey[black] & CORR_MASK], bonus);
                hist::updateCorrectionHistory(w.hist.minorCorr[side][board.minorKey & CORR_MASK], bonus);
                hist::updateCorrectionHistory(w.hist.majorCorr[side][board.majorKey & CORR_MASK], bonus);

                if constexpr (!null) {
                    //hist::updateCorrectionHistory(w.hist.contCorr[side][board.atkr][board.to], bonus);
                    if (contKey2 > 0) hist::updateCorrectionHistory(w.hist.contCorr[side][0][contKey2][board.atkr][board.to], bonus);
                    if (contKey4 > 0) hist::updateCorrectionHistory(w.hist.contCorr[side][1][contKey4][board.atkr][board.to], bonus);
                }
            }

            const uint8_t bound = failHigh ? BLO : failLow ? BUP : bestMoveTT ? BEX : BLO;
            const uint16_t mv = failLow || !bestMoveTT ? ttMove : bestMoveTT->toInt();
            tt::write(depth, *ttBucket, board.zobrist, valueToTT(best, ply), ttEval, bound, mv, tt::gen, ttPv);
        }

        return best;
    }

    template <bool side, bool mainThread, class SearchResult>
    BoardState iterate(Worker& w, int maxDepth, const BoardState& board, U64 softMs, U64 hardMs, const SearchResult& results) {
        w.hist.clearKillers();
        nnue::initRoot<side>(w.nnue, board);

        w.repCount = 0;
        w.nullPlies = 0;
        const int s = max(0, game::moveCount - (int)board.halfClock);
        for (int i = s; i <= game::moveCount; ++i) {
            w.repHistory[w.repCount++] = game::movesPlayed[i].zobrist;
        }
        w.repCount--;

        if (maxDepth >= MAX_PLY) maxDepth = MAX_PLY - 1;
        if (board.atkr != noPiece) hist::updateContKey(board, w.hist, -1);

        w.canAbort = !mainThread;
        w.timeLimit = (mainThread && hardMs > 0) ? clock::now() + chrono::milliseconds(hardMs) : clock::time_point::max();

        w.nodes.store(0, memory_order_relaxed);
        RootInfo& root = w.root;
        root.bestScore = -INF;
        root.depth = 0;
        BoardState& best = root.best;
        uint16_t& bestPacked = root.bestInt;
        int score = 0, newScore, alpha, beta, delta;
        const auto t0 = clock::now();

        int stab = 0;
        int prevScore = 0;
        uint16_t prevBest = 0;

        for (int d = 1; d <= maxDepth; ++d) {
            const auto iterStart = clock::now();

            w.nullDist[0] = w.repCount;
  
            const U64 startNodes = w.nodes;
            memset(root.moveNodes, 0, sizeof(root.moveNodes));
            bool aborted = false;

            try {
                if (d >= AW_DEPTH) {
                    delta = AW_DELTA + score * score / AW_DIV;
                    alpha = max(score - delta, -INF);
                    beta = min(score + delta, INF);
                    while (true) {
                        newScore = alphaBeta<side, true>(w, d, board, board, 0, alpha, beta, false);

                        if (newScore <= alpha) { 
                            beta = (alpha + beta) / 2;
                            delta *= 2;
                            alpha = newScore - delta;
                        }
                        else if (newScore >= beta) { 
                            delta *= 2; 
                            beta = newScore + delta; 
                        }
                        else break;

                        if (delta > AW_THRESHOLD) { alpha = -INF; beta = INF; }
                    }
                    score = newScore;
                }
                else {
                    score = alphaBeta<side, true>(w, d, board, board, 0, -INF, INF, false);
                }
            }
            catch (const StopSearch&) {
                aborted = true;
            }

            const auto iterEnd = clock::now();
            const double iterSec = chrono::duration<double>(iterEnd - iterStart).count();
            const U64 totalMs = chrono::duration_cast<chrono::milliseconds>(iterEnd - t0).count();

            if (aborted) {
                if constexpr (mainThread) results(root.depth, root.bestScore, best, iterSec, totalMs);
                break;
            }

            w.canAbort = true;
            root.depth = d;

            if constexpr (mainThread) results(d, score, best, iterSec, totalMs);

            if (abs(score) > MATE_MAX_PLY && MATE - abs(score) <= d) break;

            if constexpr (!mainThread) continue;

            if (!searching.load(memory_order_relaxed)) break;
            if (w.softNodes && searchNodes(w) >= w.softNodes) break;

            if (softMs > 0) {
                if (totalMs + iterSec * NEXT_DEPTH_RATIO > hardMs) break;

                stab = (bestPacked == prevBest) ? min(stab + 1, STABILITY_MAX) : 0;
                prevBest = bestPacked;

                const U64 iterNodes = w.nodes - startNodes;
                const double frac = iterNodes ? root.moveNodes[bestPacked & 4095] / double(iterNodes) : 0.5;
                const int drop = (d > 1) ? prevScore - score : 0;
                prevScore = score;

                U64 budget = softMs * timeFactor(stab, frac, drop) / 1000;
                if (hardMs > 0) budget = min(budget, hardMs);

                if (totalMs >= budget) break;
            }
        }

        return best;
    }

    Inline Worker& vote() {
        int votes[64][64][5]{};

        int minScore = INF;
        int bestScore = -INF;
        Worker* bestWorker;
        for (auto& wp : pool.workers) {
            RootInfo& root = wp->root;
            if (root.bestScore == -INF) continue;

            minScore = min(minScore, root.bestScore);
            if (root.bestScore > bestScore) {
                bestScore = root.bestScore;
                bestWorker = wp.get();
            }
        }

        if (bestScore >= MATE_MAX_PLY) return *bestWorker;

        for (auto& wp : pool.workers) {
            RootInfo& root = wp->root;
            if (root.bestScore == -INF) continue;

            votes[root.best.from][root.best.to][root.best.promo] += (root.bestScore - minScore + 10) * root.depth;
        }

        Worker* mostVoted;
        int mostVotedScore = -INF;
        for (auto& wp : pool.workers) {
            RootInfo& root = wp->root;
            if (root.bestScore == -INF) continue;

            int v = votes[root.best.from][root.best.to][root.best.promo];
            if (v > mostVotedScore) {
                mostVotedScore = v;
                mostVoted = wp.get();
            }
        }

        return *mostVoted;
    }

    template <bool side, class SearchResult>
    Inline BoardState search(Worker& w, int maxDepth, const BoardState& board, U64 softMs, U64 hardMs, SearchResult results) {
        tt::gen = (tt::gen + 1) & tt::GEN_MASK;
        const auto t0 = clock::now();

        syzygy::probeRoot<side>(board);

        const bool smp = pool.workers.size() > 1;
        if (smp) pool.start(board, maxDepth);

        const BoardState best = iterate<side, true>(w, maxDepth, board, softMs, hardMs, results);

        if (!smp) return best;

        pool.pause();

        Worker& voted = vote();
        RootInfo& r = voted.root;
        if (&voted != &w) {
            U64 totalMs = chrono::duration_cast<chrono::milliseconds>(clock::now() - t0).count();
            results(r.depth, r.bestScore, r.best, 0.0, totalMs);
        }

        return r.best;
    }

    template <bool side>
    Inline void start(Worker& w, int maxDepth, const BoardState& board, BoardState* bestMove, U64& nodes, double& time, U64 hardMs = 0) {
        printf("depth   score  bestmove        nodes      time          nps     EBF\n");
        printf("-------------------------------------------------------------------\n");

        U64 prevNodes = 0;
        U64 prevTotal = 0;
        w.hardNodes = w.softNodes = 0;
        searching.store(true, memory_order_relaxed);

        auto res = [&](int d, int score, const BoardState& best, double iterSec, U64) {
            const U64 iterNodes = w.nodes - prevTotal;
            if (iterNodes == 0) return;
            prevTotal = w.nodes;

            const U64 nps = iterSec > 0.0 ? iterNodes / iterSec : 0;
            const double ebf = prevNodes ? iterNodes / double(prevNodes) : 0.0;

            printf("%5d %+7d %9s %12llu %8.3fs %12llu %7.2f\n", d, score, utils::getMoveSimple(best).c_str(), iterNodes, iterSec, nps, ebf);

            prevNodes = iterNodes;
        };

        const auto t0 = clock::now();

        BoardState best = search<side>(w, maxDepth, board, 0, hardMs, res);

        time += chrono::duration<double>(clock::now() - t0).count();
        nodes += searchNodes(w);

        *bestMove = best;
    }
}