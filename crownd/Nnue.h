#pragma once

#include "Definitions.h"
#include "BoardState.h"
#include "Simd.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <immintrin.h>

namespace nnue {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
    static const unsigned char NNUE_WEIGHTS[] = {
        #embed "nnue.bin"
    };
#pragma clang diagnostic pop

    using namespace defs;
    using namespace bstate;
    using namespace simd;

    inline constexpr int W16 = sizeof(vec) / 2;
    inline constexpr int W32 = W16 / 2;

    inline constexpr int L1 = 768;
    inline constexpr int PW = L1 / 2;
    inline constexpr int L2 = 16;
    inline constexpr int L2_IN = 2 * L2;
    inline constexpr int L3 = 32;
    inline constexpr int OUT_BUCKETS = 8;

    inline constexpr uint8_t BUCKET_IDS[33] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7 };

    inline constexpr int16_t QA = 255;
    inline constexpr int32_t QB = 64;
    inline constexpr int32_t SCALE = 300;
    inline constexpr int32_t ACT = 127;
    inline constexpr int32_t OUT_SCALE = ACT * QB;
    inline constexpr int32_t ACT_DIV = OUT_SCALE * OUT_SCALE / ACT;

    inline constexpr int KING_BUCKETS = 32;
    inline constexpr int COLORS = 2;
    inline constexpr int PIECE_TYPES = 6;
    inline constexpr int SQUARES = 64;
    inline constexpr int FT_IN = KING_BUCKETS * COLORS * PIECE_TYPES * SQUARES;
    inline constexpr int REL_IN = 50626;
    inline constexpr int FT_TOTAL = FT_IN + REL_IN;

    inline constexpr uint16_t REL_NONE = UINT16_MAX;

#if HAS_AVX512
    inline constexpr int NUM_REGS = 24;
#else
    inline constexpr int NUM_REGS = 12;
#endif
    inline constexpr int TILE = NUM_REGS * W16;

    inline constexpr int MAX_ACTIVE = 128;

    struct Network {
        alignas(64) int8_t  ftW[FT_TOTAL][L1];
        alignas(64) int16_t ftB[L1];

        alignas(64) int8_t  l1W[OUT_BUCKETS][L1 / 4][L2][4];
        alignas(64) int32_t l1B[OUT_BUCKETS][L2];
        alignas(64) int8_t  l2W[OUT_BUCKETS][L2_IN / 4][L3][4];
        alignas(64) int32_t l2B[OUT_BUCKETS][L3];
        alignas(64) int8_t  l3W[OUT_BUCKETS][L3];
        alignas(64) int32_t l3B[OUT_BUCKETS];
    };

    inline Network net;

    struct Accumulator {
        alignas(64) int16_t vals[2][L1];
    };

    struct Feature {
        uint16_t id[2];

        constexpr uint16_t& operator[](int i) { return id[i]; }
        constexpr const uint16_t& operator[](int i) const { return id[i]; }
    };

    struct PcRel {
        uint8_t atker, vctm, from, to;
    };

    inline Feature pcRelFtIds[10][10][64][64];

    struct FeatureTracker {
        const int8_t* adds[2][MAX_ACTIVE];
        const int8_t* subs[2][MAX_ACTIVE];
        int32_t addSize[2] = {}, subSize[2] = {};
        int wMir = 0, bMir = 0;

        inline void add(Feature f) {
            adds[white][addSize[white]++] = net.ftW[f[white]];
            adds[black][addSize[black]++] = net.ftW[f[black]];
            _mm_prefetch((const char*)net.ftW[f[white]], _MM_HINT_T0);
            _mm_prefetch((const char*)net.ftW[f[black]], _MM_HINT_T0);
        }

        inline void sub(Feature f) {
            subs[white][subSize[white]++] = net.ftW[f[white]];
            subs[black][subSize[black]++] = net.ftW[f[black]];
            _mm_prefetch((const char*)net.ftW[f[white]], _MM_HINT_T0);
            _mm_prefetch((const char*)net.ftW[f[black]], _MM_HINT_T0);
        }

        inline void add(PcRel r) {
            const uint32_t w = pcRelFtIds[r.atker][r.vctm][r.from ^ wMir][r.to ^ wMir][white];
            const uint32_t b = pcRelFtIds[r.atker][r.vctm][r.from ^ bMir][r.to ^ bMir][black];
            if (w != REL_NONE) { adds[white][addSize[white]++] = net.ftW[FT_IN + w]; _mm_prefetch((const char*)net.ftW[FT_IN + w], _MM_HINT_T0); }
            if (b != REL_NONE) { adds[black][addSize[black]++] = net.ftW[FT_IN + b]; _mm_prefetch((const char*)net.ftW[FT_IN + b], _MM_HINT_T0); }
        }

        inline void sub(PcRel r) {
            const uint32_t w = pcRelFtIds[r.atker][r.vctm][r.from ^ wMir][r.to ^ wMir][white];
            const uint32_t b = pcRelFtIds[r.atker][r.vctm][r.from ^ bMir][r.to ^ bMir][black];
            if (w != REL_NONE) { subs[white][subSize[white]++] = net.ftW[FT_IN + w]; _mm_prefetch((const char*)net.ftW[FT_IN + w], _MM_HINT_T0); }
            if (b != REL_NONE) { subs[black][subSize[black]++] = net.ftW[FT_IN + b]; _mm_prefetch((const char*)net.ftW[FT_IN + b], _MM_HINT_T0); }
        }

        inline void clear() { addSize[white] = addSize[black] = subSize[white] = subSize[black] = 0; }
    };

    struct HalfKACache {
        alignas(64) int16_t vals[L1];
        U64 pcs[6]{};
        U64 occ[2]{};
        Zobrist zobrist = 0;

        HalfKACache() { std::memcpy(vals, net.ftB, sizeof(vals)); }
    };

    struct MailBox {
        static constexpr uint8_t EMPTY = 0xFF;

        alignas(64) uint8_t board[MAX_PLY][64];
        uint8_t* ptr;

        __forceinline uint8_t get(int sq) {
            return ptr[sq];
        }

        template <bool ep = false>
        inline void update(const BoardState& b, int ply, uint8_t pcTo) {
            std::memcpy(board[ply], board[ply - 1], 64);
            ptr = board[ply];

            ptr[b.from] = EMPTY;
            if constexpr (ep) ptr[b.capSq] = EMPTY;
            ptr[b.to] = pcTo;
            if (b.mvType == MoveType::CASTLE) {
                ptr[b.rTo] = ptr[b.rFrom];
                ptr[b.rFrom] = EMPTY;
            }
        }

        __forceinline void add(uint8_t pc, bool clr, int sq) {
            ptr[sq] = uint8_t(pc * 2 + clr);
        }

        inline void init(const BoardState& b) {
            std::memset(board, EMPTY, sizeof(board));
            ptr = board[0];

            U64 bb = b.pM();
            Bitloop(bb) { add(p, b.side, SquareOf(bb)); }
            bb = b.pE();
            Bitloop(bb) { add(p, !b.side, SquareOf(bb)); }
            bb = b.nM();
            Bitloop(bb) { add(n, b.side, SquareOf(bb)); }
            bb = b.nE();
            Bitloop(bb) { add(n, !b.side, SquareOf(bb)); }
            bb = b.bM();
            Bitloop(bb) { add(defs::b, b.side, SquareOf(bb)); }
            bb = b.bE();
            Bitloop(bb) { add(defs::b, !b.side, SquareOf(bb)); }
            bb = b.rM();
            Bitloop(bb) { add(r, b.side, SquareOf(bb)); }
            bb = b.rE();
            Bitloop(bb) { add(r, !b.side, SquareOf(bb)); }
            bb = b.qM();
            Bitloop(bb) { add(q, b.side, SquareOf(bb)); }
            bb = b.qE();
            Bitloop(bb) { add(q, !b.side, SquareOf(bb)); }
            add(k, b.side, b.kMS);
            add(k, !b.side, b.kES);
        }
    };

    struct Nnue {
        Accumulator accs[MAX_PLY];
        FeatureTracker tracker;
        MailBox mailBox;
        HalfKACache hkCache[2][64];
        Zobrist zob[MAX_PLY];

        inline void init(const BoardState& board) {
            mailBox.init(board);
            std::memset(zob, 0, sizeof(zob));
            zob[0] = board.zobrist;
        }
    };

    inline constexpr bool pawn[12] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    inline constexpr bool knight[12] = { 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    inline constexpr bool bishop[12] = { 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 };
    inline constexpr bool rook[12] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0 };
    inline constexpr bool REL_TARGET[5][5] = {
        { 1, 1, 0, 1, 0 },
        { 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 0 },
        { 1, 1, 1, 1, 0 },
        { 1, 1, 1, 1, 1 },
    };

    template <int reps, int i = 0, class F>
    ForceInline void repeat(F&& f) {
        if constexpr (i < reps) {
            f(i);
            repeat<reps, i + 1>(f);
        }
    }

    template <bool kingCache, bool noSub>
    Inline void updateAcc(int16_t* dst, const int16_t* src, const int8_t* const* adds, int nAdd, const int8_t* const* subs = nullptr, int nSub = 0, const int16_t* cacheFrom = nullptr, const int16_t* cacheTo = nullptr) {
        int pairs = noSub ? 0 : nAdd < nSub ? nAdd : nSub;

        for (int i = 0; i < L1; i += TILE) {
            vec acc[NUM_REGS];

            const int16_t* srcAcc = src + i;
            repeat<NUM_REGS>([&](auto k) { acc[k] = load(srcAcc + k * W16); });

            if constexpr (kingCache) {
                const int16_t* cFrom = cacheFrom + i;
                const int16_t* cTo = cacheTo + i;
                repeat<NUM_REGS>([&](auto k) { acc[k] = sub_epi16(add_epi16(acc[k], load(cTo + k * W16)), load(cFrom + k * W16)); });
            }

            for (int j = pairs; j < nAdd; ++j) {
                const int8_t* wAdd = adds[j] + i;
                repeat<NUM_REGS>([&](auto k) { acc[k] = add_epi16(acc[k], cvtepi8_epi16(load_h(wAdd + k * W16))); });
            }
            if constexpr (!noSub) {
                for (int j = 0; j < pairs; ++j) {
                    const int8_t* wAdd = adds[j] + i;
                    const int8_t* wSub = subs[j] + i;
                    repeat<NUM_REGS>([&](auto k) { acc[k] = sub_epi16(add_epi16(acc[k], cvtepi8_epi16(load_h(wAdd + k * W16))), cvtepi8_epi16(load_h(wSub + k * W16))); });
                }

                for (int j = pairs; j < nSub; ++j) {
                    const int8_t* wSub = subs[j] + i;
                    repeat<NUM_REGS>([&](auto k) { acc[k] = sub_epi16(acc[k], cvtepi8_epi16(load_h(wSub + k * W16))); });
                }
            }

            int16_t* dstAcc = dst + i;
            repeat<NUM_REGS>([&](auto k) { store(dstAcc + k * W16, acc[k]); });
        }
    }

    template <bool persp>
    Inline Feature halfKAIndex(uint8_t pc, int sq, int wKb, int bKb, int wMir, int bMir) {
        Feature f;

        const int piece = pc >> 1;
        const int relW = (pc & 1) ^ int(white);
        const int relB = relW ^ 1;

        f[white] = ((wKb * COLORS + relW) * PIECE_TYPES + piece) * SQUARES + (sq ^ wMir);
        f[black] = ((bKb * COLORS + relB) * PIECE_TYPES + piece) * SQUARES + ((sq ^ 56) ^ bMir);

        return f;
    }

    template <bool persp>
    Inline uint16_t halfKAIndexOne(uint8_t pc, int sq, int kBkt, int mir) {
        const int s = (persp == white) ? (sq ^ mir) : ((sq ^ 56) ^ mir);

        const int piece = pc >> 1;
        const int rel = (pc & 1) ^ persp;

        return uint16_t(((kBkt * COLORS + rel) * PIECE_TYPES + piece) * SQUARES + s);
    }

    template <bool persp>
    Inline uint16_t halfKAIndexOne(int piece, bool color, int sq, int kBkt, int mir) {
        const int s = (persp == white) ? (sq ^ mir) : ((sq ^ 56) ^ mir);

        return uint16_t(((kBkt * COLORS + (color == persp ? 0 : 1)) * PIECE_TYPES + piece) * SQUARES + s);
    }

    Inline PcRel pcRelIndex(uint8_t atker, uint8_t vctm, int fromSq, int toSq) {
        return { atker, vctm, uint8_t(fromSq), uint8_t(toSq) };
    }

    Inline void addRelations(int atker, int atkrCol, int vctmCol, int sq, U64 atks, int& ftId) {
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            for (int pc = 0; pc < 5; pc++) {
                if (pc == int(p) && (toSq < 8 || toSq > 55)) continue;
                const bool twin = pc == atker && (atkrCol != vctmCol || atker != int(p));
                const bool kept = REL_TARGET[atker][pc] && !(twin && sq < toSq);
                pcRelFtIds[atker * 2 + atkrCol][pc * 2 + vctmCol][sq][toSq][white] = kept ? uint16_t(ftId++ - FT_IN) : REL_NONE;
            }
        }
    }

    Inline void initRelations() {
        std::memset(pcRelFtIds, 0xFF, sizeof(pcRelFtIds));

        int ftId = FT_IN;
        U64 atks;
        for (int atkrCol = 0; atkrCol <= 1; atkrCol++) {
            for (int vctmCol = 0; vctmCol <= 1; vctmCol++) {
                for (int i = 0; i < 64; i++) {
                    if (i >= 8 && i <= 55) {
                        atks = PAWN_CAPTURES[atkrCol][i];
                        addRelations(int(p), atkrCol, vctmCol, i, atks, ftId);
                    }
                    atks = KNIGHT_ATTACKS[i];
                    addRelations(int(n), atkrCol, vctmCol, i, atks, ftId);

                    atks = getBishopAttacks(i, 0ULL);
                    addRelations(int(b), atkrCol, vctmCol, i, atks, ftId);

                    atks = getRookAttacks(i, 0ULL);
                    addRelations(int(r), atkrCol, vctmCol, i, atks, ftId);

                    atks = getQueenAttacks(i, 0ULL);
                    addRelations(int(q), atkrCol, vctmCol, i, atks, ftId);
                }
            }
        }

        for (int a = 0; a < 10; ++a)
            for (int v = 0; v < 10; ++v)
                for (int f = 0; f < 64; ++f)
                    for (int t = 0; t < 64; ++t)
                        pcRelFtIds[a][v][f][t][black] = pcRelFtIds[a ^ 1][v ^ 1][f ^ 56][t ^ 56][white];
    }

    template <bool side, bool persp>
    Inline int kingSquare(const BoardState& board) {
        if constexpr (persp == white) return (side == white) ? board.kMS : board.kES;
        else                          return (side == white) ? board.kES : board.kMS;
    }

    template <bool side, bool persp>
    Inline void refreshHalfKA(MailBox& mb, const BoardState& board, const int8_t** ptr, int& cnt) {
        const int king = kingSquare<side, persp>(board);
        const int kMS = (persp == white) ? king : (king ^ 56);
        const int mir = MIRROR[kMS & 7];
        const int kBkt = KING_BUCKET[kMS ^ mir];

        U64 bb = board.occB();
        Bitloop(bb) {
            int sq = SquareOf(bb);
            ptr[cnt++] = net.ftW[halfKAIndexOne<persp>(mb.get(sq), sq, kBkt, mir)];
        }
    }

    template <bool persp>
    Inline void addPcRel(MailBox& mb, int from, int to, int mir, const int8_t** ptr, int& cnt) {
        const uint16_t id = pcRelFtIds[mb.get(from)][mb.get(to)][from ^ mir][to ^ mir][persp];
        if (id != REL_NONE) ptr[cnt++] = net.ftW[FT_IN + id];
    }

    template <bool side, bool persp>
    Inline void refreshPcRel(MailBox& mb, const BoardState& board, const int8_t** ptr, int& cnt) {
        U64 occB = board.occB() ^ (board.pcs[k]);
        const U64 pawnTgt = board.pcs[p] | board.pcs[n] | board.pcs[r];
        const int mir = MIRROR[kingSquare<side, persp>(board) & 7];

        U64 bb = board.pM();
        Bitloop(bb) {
            int from = SquareOf(bb);
            U64 atks = PAWN_CAPTURES[side][from] & pawnTgt;
            Bitloop(atks) { addPcRel<persp>(mb, from, SquareOf(atks), mir, ptr, cnt); }
        }
        bb = board.pE();
        Bitloop(bb) {
            int from = SquareOf(bb);
            U64 atks = PAWN_CAPTURES[!side][from] & pawnTgt;
            Bitloop(atks) { addPcRel<persp>(mb, from, SquareOf(atks), mir, ptr, cnt); }
        }
        bb = board.pcs[n];
        Bitloop(bb) {
            int from = SquareOf(bb);
            U64 atks = KNIGHT_ATTACKS[from] & occB;
            Bitloop(atks) { addPcRel<persp>(mb, from, SquareOf(atks), mir, ptr, cnt); }
        }
        bb = board.pcs[b] | board.pcs[q];
        Bitloop(bb) {
            int from = SquareOf(bb);
            U64 atks = getBishopAttacks(from, board.occB()) & occB;
            Bitloop(atks) { addPcRel<persp>(mb, from, SquareOf(atks), mir, ptr, cnt); }
        }
        bb = board.pcs[r] | board.pcs[q];
        Bitloop(bb) {
            int from = SquareOf(bb);
            U64 atks = getRookAttacks(from, board.occB()) & occB;
            Bitloop(atks) { addPcRel<persp>(mb, from, SquareOf(atks), mir, ptr, cnt); }
        }
    }

    template <bool side, bool persp>
    Inline void refresh(MailBox& mb, Accumulator& acc, const BoardState& board) {
        const int8_t* ptr[MAX_ACTIVE];
        int cnt = 0;
        refreshHalfKA<side, persp>(mb, board, ptr, cnt);
        refreshPcRel<side, persp>(mb, board, ptr, cnt);
        updateAcc<false, true>(acc.vals[persp], net.ftB, ptr, cnt);
    }

    template <bool persp>
    Inline const int16_t* syncCache(Nnue& nnue, const BoardState& board, int kMS, const U64(&occ)[2]) {
        HalfKACache& e = nnue.hkCache[persp][kMS];

        if (e.zobrist == board.zobrist) return e.vals;

        const int king = (persp == white) ? kMS : (kMS ^ 56);
        const int mir = MIRROR[king & 7];
        const int kBkt = KING_BUCKET[king ^ mir];

        const int8_t* add[32];
        const int8_t* sub[32];
        int nAdd = 0, nSub = 0;

        for (int col = 0; col < 2; ++col) {
            for (int pc = p; pc <= k; ++pc) {
                const U64 cur = board.pcs[pc] & occ[col];
                const U64 old = e.pcs[pc] & e.occ[col];
                U64 a = cur & ~old;
                U64 s = old & ~cur;
                Bitloop(a) add[nAdd++] = net.ftW[halfKAIndexOne<persp>(pc, col, SquareOf(a), kBkt, mir)];
                Bitloop(s) sub[nSub++] = net.ftW[halfKAIndexOne<persp>(pc, col, SquareOf(s), kBkt, mir)];
            }
        }

        if (nAdd | nSub) {
            updateAcc<false, false>(e.vals, e.vals, add, nAdd, sub, nSub);
            std::memcpy(e.pcs, board.pcs, sizeof(board.pcs));
            std::memcpy(e.occ, occ, sizeof(occ));
            e.zobrist = board.zobrist;
        }

        return e.vals;
    }

    template <bool persp, bool king>
    Inline void computeDeltas(Accumulator& dst, const Accumulator& src, const FeatureTracker& ft, const int16_t* cacheFrom = nullptr, const int16_t* cacheTo = nullptr) {
        updateAcc<king, false>(dst.vals[persp], src.vals[persp], ft.adds[persp], ft.addSize[persp], ft.subs[persp], ft.subSize[persp], cacheFrom, cacheTo);
    }

    template <bool side>
    Inline void pcRelEnPassant(MailBox& mb, uint8_t pcFrom, int sq, const BoardState& board, FeatureTracker& ft) {
        U64 occNoKing = board.occB() ^ board.pcs[k];

        U64 atks = PAWN_CAPTURES[side][sq] & occNoKing;
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            uint8_t pc = mb.get(toSq);

            ft.sub(pcRelIndex(pcFrom, pc, sq, toSq));
            if (board.pE() & (1ULL << toSq)) ft.sub(pcRelIndex(pc, pcFrom, toSq, sq));
        }

        atks = PAWN_CAPTURES[!side][sq] & board.pM();
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            ft.sub(pcRelIndex(mb.get(toSq), pcFrom, toSq, sq));
        }

        atks = KNIGHT_ATTACKS[sq] & board.pcs[n];
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            ft.sub(pcRelIndex(mb.get(toSq), pcFrom, toSq, sq));
        }

        U64 bRay = getBishopAttacks(sq, board.occB());
        atks = bRay & (board.pcs[b] | board.pcs[q]);
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            uint8_t pc = mb.get(toSq);

            ft.sub(pcRelIndex(pc, pcFrom, toSq, sq));
            U64 newTarget = (bRay & BISHOP_XRAYS[toSq]) & occNoKing;
            if (newTarget) {
                int tgtSq = SquareOf(newTarget);
                ft.add(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
            }
        }

        U64 rRay = getRookAttacks(sq, board.occB());
        atks = rRay & (board.pcs[r] | board.pcs[q]);
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            uint8_t pc = mb.get(toSq);

            ft.sub(pcRelIndex(pc, pcFrom, toSq, sq));
            U64 newTarget = (rRay & ROOK_XRAYS[toSq]) & occNoKing;
            if (newTarget) {
                int tgtSq = SquareOf(newTarget);
                ft.add(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
            }
        }
    }

#if HAS_VBMI
    ForceInline U64 superPieceAttacks(U64 occupied, U64 mask) {
        const U64 o = occupied | 0x8181818181818181;
        const U64 x = o ^ (o - 0x0303030303030303);
        return x & mask;
    }

    template <bool king, bool to, bool cap = false>
    Inline void processSquare(MailBox& mb, vec mbox, int sq, uint8_t pc, const BoardState& board, FeatureTracker& ft, int ply, uint8_t pcCap = 0) {
        U64 mask = PERM_MASKS[sq];

        vec places = mask_permutexvar_epi8(set1_epi8(0xFF), mask, load(PERM_VALUES[sq]), mbox);
        //convert mailbox piece codes to align with attack masks
        places = shuffle_epi8(PIECE_FLAGS, places);

        U64 kings = test_epi8_mask(places, set1_epi8(KING));

        U64 occupied = test_epi8_mask(places, places);
        U64 visible = superPieceAttacks(occupied, mask) & occupied & ~kings;
        U64 atkers = test_epi8_mask(places, ATTACKER_MASKS) & visible;

        if constexpr (!king) {
            U64 rmFrom = visible & ATTACKS[pc];
            Bitloop(rmFrom) {
                int toSq = PERM_VALUES[sq][SquareOf(rmFrom)];
                if constexpr (to) ft.add(pcRelIndex(pc, mb.get(toSq), sq, toSq));
                else              ft.sub(pcRelIndex(pc, mb.get(toSq), sq, toSq));
            }
        }

        if constexpr (cap) {
            U64 rmCap = visible & ATTACKS[pcCap];
            Bitloop(rmCap) {
                int toSq = PERM_VALUES[sq][SquareOf(rmCap)];
                ft.sub(pcRelIndex(pcCap, mb.get(toSq), sq, toSq));
            }
        }

        if constexpr (!king || cap) {
            U64 rmAtkers = atkers;
            Bitloop(rmAtkers) {
                int frSq = PERM_VALUES[sq][SquareOf(rmAtkers)];
                if constexpr (!king) {
                    if constexpr (to) ft.add(pcRelIndex(mb.get(frSq), pc, frSq, sq));
                    else              ft.sub(pcRelIndex(mb.get(frSq), pc, frSq, sq));
                }
                if constexpr (cap)    ft.sub(pcRelIndex(mb.get(frSq), pcCap, frSq, sq));
            }
        }

        if constexpr (!to || !cap) {
            U64 sliders = test_epi8_mask(places, SLIDER_MASKS) & atkers;
            Bitloop(sliders) {
                int pos = SquareOf(sliders);
                int opp = (pos / 8) ^ 4; //get the opposite lane of the slider according to PERM_VALUES
                U64 beyond = (visible >> (opp * 8)) & 0xFE; //0xFE first bit at 0 => ignores the knight
                if (beyond) {
                    int slSq = PERM_VALUES[sq][pos];
                    int tgtSq = PERM_VALUES[sq][opp * 8 + SquareOf(beyond)];
                    if constexpr (to) ft.sub(pcRelIndex(mb.get(slSq), mb.get(tgtSq), slSq, tgtSq));
                    else              ft.add(pcRelIndex(mb.get(slSq), mb.get(tgtSq), slSq, tgtSq));
                }
            }
        }
    }

    template <bool side, bool king, bool cap, bool ep = false>
    Inline void updatePcRel(MailBox& mb, int from, int to, int capSq, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, const BoardState& prev, const BoardState& board, FeatureTracker& ft, int ply) {
        vec mbox = load(mb.ptr);

        processSquare<king, false>(mb, mbox, from, pcFrom, prev, ft, ply);

        mbox = mask_mov_epi8(mbox, 1ULL << from, set1_epi8(MailBox::EMPTY));
        mbox = mask_mov_epi8(mbox, 1ULL << to, set1_epi8(pcTo));

        processSquare<king, true, cap>(mb, mbox, to, pcTo, prev, ft, ply, pcCap);

        mb.update<ep>(board, ply, pcTo);
        //ep after mb update, because we use the new board state
        if constexpr (ep) pcRelEnPassant<!side>(mb, pcCap, capSq, board, ft);
        //if constexpr (ep) processSquare<false, false, false>(mb, mbox, capSq, pcCap, board, ft, ply);
    }

    Inline void updatePcRelCastle(MailBox& mb, uint8_t king, uint8_t rook, int from, int to, const BoardState& prev, const BoardState& board, FeatureTracker& ft, int ply) {
        vec mbox = load(mb.ptr);

        processSquare<false, false>(mb, mbox, from, rook, prev, ft, ply);

        const vec empty = set1_epi8(MailBox::EMPTY);
        mbox = mask_mov_epi8(mbox, (1ULL << board.from) | (1ULL << board.rFrom), empty);
        mbox = mask_mov_epi8(mbox, 1ULL << board.to, set1_epi8(king));
        mbox = mask_mov_epi8(mbox, 1ULL << board.rTo, set1_epi8(rook));

        processSquare<false, true>(mb, mbox, to, rook, prev, ft, ply);

        mb.update(board, ply, king);
    }
#else
    template <bool side, bool king, bool cap>
    Inline void pcRelPawn(MailBox& mb, int from, int to, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, U64 occNoKing, const BoardState& board, FeatureTracker& ft, int ply) {
        const U64 targets = board.pcs[p] | board.pcs[n] | board.pcs[r];

        if constexpr (!king) {
            U64 atks = PAWN_CAPTURES[side][from] & targets & (board.pE() | (0ULL - U64(pawn[pcFrom])));
            Bitloop(atks) {
                int toSq = SquareOf(atks);
                uint8_t pc = mb.get(toSq);

                if (pawn[pcFrom])              ft.sub(pcRelIndex(pcFrom, pc, from, toSq));
                if (board.pE() & (1ULL << toSq)) ft.sub(pcRelIndex(pc, pcFrom, toSq, from));
            }

            atks = PAWN_CAPTURES[!side][from] & board.pM();
            Bitloop(atks) {
                int toSq = SquareOf(atks);
                ft.sub(pcRelIndex(mb.get(toSq), pcFrom, toSq, from));
            }
        }

        U64 atks = PAWN_CAPTURES[side][to] & targets & (board.pE() | (0ULL - U64(pawn[pcTo])));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            uint8_t pc = mb.get(toSq);

            if (pawn[pcTo]) ft.add(pcRelIndex(pcTo, pc, to, toSq));
            if (board.pE() & (1ULL << toSq)) {
                if constexpr (!king) ft.add(pcRelIndex(pc, pcTo, toSq, to));
                if constexpr (cap)   ft.sub(pcRelIndex(pc, pcCap, toSq, to));
            }
        }

        atks = PAWN_CAPTURES[!side][to] & (targets & ~(1ULL << from)) & (board.pM() | (0ULL - U64(cap && pawn[pcCap])));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            uint8_t pc = mb.get(toSq);

            if (cap && pawn[pcCap]) ft.sub(pcRelIndex(pcCap, pc, to, toSq));
            if (board.pM() & (1ULL << toSq)) {
                if constexpr (!king) ft.add(pcRelIndex(pc, pcTo, toSq, to));
                if constexpr (cap)   ft.sub(pcRelIndex(pc, pcCap, toSq, to));
            }
        }
    }

    template <bool king, bool cap>
    Inline void pcRelKnight(MailBox& mb, int from, int to, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, U64 occNoKing, const BoardState& board, FeatureTracker& ft) {
        const U64 knights = board.pcs[n];

        if constexpr (!king) {
            U64 atks = KNIGHT_ATTACKS[from] & occNoKing & (knights | (0ULL - U64(knight[pcFrom])));
            Bitloop(atks) {
                int toSq = SquareOf(atks);
                int8_t pc = mb.get(toSq);

                if (knight[pcFrom]) ft.sub(pcRelIndex(pcFrom, pc, from, toSq));
                if (knight[pc]) ft.sub(pcRelIndex(pc, pcFrom, toSq, from));
            }
        }

        U64 atks = KNIGHT_ATTACKS[to] & (occNoKing & ~(1ULL << from)) & (knights | (0ULL - U64(knight[pcTo] || (cap && knight[pcCap]))));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            int8_t pc = mb.get(toSq);

            if (knight[pcTo]) ft.add(pcRelIndex(pcTo, pc, to, toSq));
            if (cap && knight[pcCap]) ft.sub(pcRelIndex(pcCap, pc, to, toSq));
            if (knight[pc]) {
                if constexpr (!king) ft.add(pcRelIndex(pc, pcTo, toSq, to));
                if constexpr (cap) ft.sub(pcRelIndex(pc, pcCap, toSq, to));
            }
        }
    }

    template <bool king, bool cap>
    Inline void pcRelBishop(MailBox& mb, int from, int to, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, U64 occNoKing, const BoardState& board, FeatureTracker& ft) {
        U64 frBit = (1ULL << from);
        U64 toBit = (1ULL << to);

        U64 bFrom = getBishopAttacks(from, board.occB());
        U64 bTo = getBishopAttacks(to, board.occB() ^ frBit);

        const U64 bishops = board.pcs[b] | board.pcs[q];

        if constexpr (cap) {
            if (bFrom & toBit) {
                if (bishop[pcFrom]) ft.sub(pcRelIndex(pcFrom, pcCap, from, to));
                if constexpr (!king) {
                    if (bishop[pcCap]) ft.sub(pcRelIndex(pcCap, pcFrom, to, from));
                }
            }
        }

        U64 atks = bFrom & (occNoKing & ~toBit) & (bishops | (0ULL - U64(bishop[pcFrom])));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            int8_t pc = mb.get(toSq);
            if (bishop[pcFrom]) ft.sub(pcRelIndex(pcFrom, pc, from, toSq));
            if (bishop[pc]) {
                if constexpr (!king) ft.sub(pcRelIndex(pc, pcFrom, toSq, from));
                U64 ray = (bFrom & BISHOP_XRAYS[toSq]);
                U64 newTgt = ray & occNoKing;
                if (newTgt && !(ray & toBit)) {
                    int tgtSq = SquareOf(newTgt);
                    ft.add(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
                }
            }
        }

        U64 newTgt = bTo & DISCOVER_RAYS[to][from] & occNoKing;
        if (newTgt) {
            int tgtSq = SquareOf(newTgt);
            int8_t pc = mb.get(tgtSq);

            if (bishop[pcTo])        ft.add(pcRelIndex(pcTo, pc, to, tgtSq));
            if (!king && bishop[pc]) ft.add(pcRelIndex(pc, pcTo, tgtSq, to));
        }

        atks = bTo & (occNoKing & ~(frBit | newTgt)) & (bishops | (0ULL - U64(bishop[pcTo] || (cap && bishop[pcCap]))));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            int8_t pc = mb.get(toSq);

            if (bishop[pcTo]) ft.add(pcRelIndex(pcTo, pc, to, toSq));
            if (cap && bishop[pcCap]) ft.sub(pcRelIndex(pcCap, pc, to, toSq));
            if (bishop[pc]) {
                if constexpr (!king) ft.add(pcRelIndex(pc, pcTo, toSq, to));
                if constexpr (cap)   ft.sub(pcRelIndex(pc, pcCap, toSq, to));
                else {
                    U64 ray = (bTo & BISHOP_XRAYS[toSq]);
                    U64 oldTgt = ray & occNoKing;
                    if (oldTgt && !(ray & frBit)) {
                        int tgtSq = SquareOf(oldTgt);
                        ft.sub(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
                    }
                }
            }
        }
    }

    template <bool king, bool cap>
    Inline void pcRelRook(MailBox& mb, int from, int to, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, U64 occNoKing, bool cstl, const BoardState& board, FeatureTracker& ft) {
        U64 frBit = (1ULL << from);
        U64 toBit = (1ULL << to);

        U64 rFrom = getRookAttacks(from, board.occB());

        U64 occTo = board.occB() ^ frBit;
        if (cstl) occTo ^= board.kM();
        U64 rTo = getRookAttacks(to, occTo);

        const U64 rooks = board.pcs[r] | board.pcs[q];

        if constexpr (cap) {
            if (rFrom & toBit) {
                if (rook[pcFrom]) ft.sub(pcRelIndex(pcFrom, pcCap, from, to));
                if constexpr (!king) {
                    if (rook[pcCap]) ft.sub(pcRelIndex(pcCap, pcFrom, to, from));
                }
            }
        }

        U64 atks = rFrom & (occNoKing & ~toBit) & (rooks | (0ULL - U64(rook[pcFrom])));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            int8_t pc = mb.get(toSq);
            if (rook[pcFrom]) ft.sub(pcRelIndex(pcFrom, pc, from, toSq));
            if (rook[pc]) {
                if constexpr (!king) ft.sub(pcRelIndex(pc, pcFrom, toSq, from));
                U64 ray = (rFrom & ROOK_XRAYS[toSq]);
                U64 newTgt = ray & occNoKing;
                if (newTgt && !(ray & toBit)) {
                    int tgtSq = SquareOf(newTgt);
                    ft.add(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
                }
            }
        }

        U64 newTgt = rTo & DISCOVER_RAYS[to][from] & occNoKing;
        if (newTgt) {
            int tgtSq = SquareOf(newTgt);
            int8_t pc = mb.get(tgtSq);

            if (rook[pcTo])        ft.add(pcRelIndex(pcTo, pc, to, tgtSq));
            if (!king && rook[pc]) ft.add(pcRelIndex(pc, pcTo, tgtSq, to));
        }

        atks = rTo & (occNoKing & ~(frBit | newTgt)) & (rooks | (0ULL - U64(rook[pcTo] || (cap && rook[pcCap]))));
        Bitloop(atks) {
            int toSq = SquareOf(atks);
            int8_t pc = mb.get(toSq);

            if (rook[pcTo]) ft.add(pcRelIndex(pcTo, pc, to, toSq));
            if (cap && rook[pcCap]) ft.sub(pcRelIndex(pcCap, pc, to, toSq));
            if (rook[pc]) {
                if constexpr (!king) ft.add(pcRelIndex(pc, pcTo, toSq, to));
                if constexpr (cap)   ft.sub(pcRelIndex(pc, pcCap, toSq, to));
                else {
                    U64 ray = (rTo & ROOK_XRAYS[toSq]);
                    U64 oldTgt = ray & occNoKing;
                    if (oldTgt && !(ray & frBit)) {
                        int tgtSq = SquareOf(oldTgt);
                        ft.sub(pcRelIndex(pc, mb.get(tgtSq), toSq, tgtSq));
                    }
                }
            }
        }
    }

    template <bool side, bool king, bool cap, bool ep = false>
    Inline void updatePcRel(MailBox& mb, int from, int to, int capSq, uint8_t pcFrom, uint8_t pcTo, uint8_t pcCap, const BoardState& prev, const BoardState& board, FeatureTracker& ft, int ply) {
        U64 occNoKing = prev.occB() ^ (prev.pcs[k] | (1ULL << from));

        pcRelPawn<side, king, cap>(mb, from, to, pcFrom, pcTo, pcCap, occNoKing, prev, ft, ply);
        pcRelKnight<king, cap>(mb, from, to, pcFrom, pcTo, pcCap, occNoKing, prev, ft);
        pcRelBishop<king, cap>(mb, from, to, pcFrom, pcTo, pcCap, occNoKing, prev, ft);
        pcRelRook<king, cap>(mb, from, to, pcFrom, pcTo, pcCap, occNoKing, false, prev, ft);

        mb.update<ep>(board, ply, pcTo);
        //ep after mb update, because we use the new board state
        if constexpr (ep) pcRelEnPassant<!side>(mb, pcCap, capSq, board, ft);
    }

    Inline void updatePcRelCastle(MailBox& mb, uint8_t king, uint8_t rook, int from, int to, const BoardState& prev, const BoardState& board, FeatureTracker& ft, int ply) {
        U64 occNoKing = prev.occB() ^ (prev.pcs[k] | (1ULL << from));

        pcRelKnight<false, false>(mb, from, to, rook, rook, 0, occNoKing, prev, ft);
        pcRelBishop<false, false>(mb, from, to, rook, rook, 0, occNoKing, prev, ft);
        pcRelRook<false, false>(mb, from, to, rook, rook, 0, occNoKing, true, prev, ft);

        mb.update(board, ply, king);
    }
#endif

    template <bool side, bool king>
    Inline void updateTracker(MailBox& mb, const BoardState& prev, const BoardState& board, FeatureTracker& ft, int ply) {
        ft.clear();

        mb.ptr = mb.board[ply - 1];

        const int from = board.from, to = board.to, capSq = board.capSq;

        const uint8_t pcFrom = mb.get(from);
        const uint8_t pcTo = pcFrom + (board.promo << 1);
        const uint8_t pcCap = mb.get(capSq);

        const int wK = (side == white ? prev.kMS : prev.kES);
        const int bK = (side == black ? prev.kMS : prev.kES) ^ 56;
        const int wMir = MIRROR[wK & 7];
        const int bMir = MIRROR[bK & 7];
        const int wKb = KING_BUCKET[wK ^ wMir];
        const int bKb = KING_BUCKET[bK ^ bMir];

        ft.wMir = wMir;
        ft.bMir = bMir;

        if (king && board.mvType == MoveType::CASTLE) {
            uint8_t pcR = mb.get(board.rFrom);

            ft.sub(halfKAIndex<side>(pcFrom, from, wKb, bKb, wMir, bMir));
            ft.add(halfKAIndex<side>(pcFrom, to, wKb, bKb, wMir, bMir));
            ft.sub(halfKAIndex<side>(pcR, board.rFrom, wKb, bKb, wMir, bMir));
            ft.add(halfKAIndex<side>(pcR, board.rTo, wKb, bKb, wMir, bMir));

            updatePcRelCastle(mb, pcTo, pcR, board.rFrom, board.rTo, prev, board, ft, ply);
            return;
        }

        ft.sub(halfKAIndex<side>(pcFrom, from, wKb, bKb, wMir, bMir));
        ft.add(halfKAIndex<side>(pcTo, to, wKb, bKb, wMir, bMir));
        if (capSq != noSquare) {
            ft.sub(halfKAIndex<side>(pcCap, capSq, wKb, bKb, wMir, bMir));

            //cap is false for ep to consider "to" square as empty
            if (board.mvType == MoveType::EP) updatePcRel<side, king, false, true>(mb, from, to, capSq, pcFrom, pcTo, pcCap, prev, board, ft, ply);
            else                              updatePcRel<side, king, true>(mb, from, to, capSq, pcFrom, pcTo, pcCap, prev, board, ft, ply);
        }
        else updatePcRel<side, king, false>(mb, from, to, capSq, pcFrom, pcTo, pcCap, prev, board, ft, ply);
    }

    template <bool side>
    Inline void compute(Nnue& nnue, int ply, const BoardState& prev, const BoardState& board) {
        Accumulator* accs = nnue.accs;

        if (board.mvType == MoveType::NULLMV) { 
            accs[ply] = accs[ply - 1]; 
            memcpy(nnue.mailBox.board[ply], nnue.mailBox.board[ply - 1], 64);
            nnue.zob[ply] = nnue.zob[ply - 1];
            return; 
        }

        if (nnue.zob[ply] == board.zobrist) return;

        nnue.zob[ply] = board.zobrist;

        if (board.atkr == k) {
            updateTracker<!side, true>(nnue.mailBox, prev, board, nnue.tracker, ply);

            const U64 occ[2] = { side == white ? board.occM : board.occE, side == white ? board.occE : board.occM };
            const int16_t* cacheTo = syncCache<!side>(nnue, board, board.to, occ);

            computeDeltas<side, false>(accs[ply], accs[ply - 1], nnue.tracker);

            if (MIRROR[board.from & 7] != MIRROR[board.to & 7]) {
                const int8_t* ptrR[MAX_ACTIVE];
                int cntR = 0;
                refreshPcRel<side, !side>(nnue.mailBox, board, ptrR, cntR);
                updateAcc<false, true>(accs[ply].vals[!side], cacheTo, ptrR, cntR);
            }
            else {
                const int16_t* cacheFrom = syncCache<!side>(nnue, board, board.from, occ);
                computeDeltas<!side, true>(accs[ply], accs[ply - 1], nnue.tracker, cacheFrom, cacheTo);
            }
        }
        else {
            updateTracker<!side, false>(nnue.mailBox, prev, board, nnue.tracker, ply);

            computeDeltas<white, false>(accs[ply], accs[ply - 1], nnue.tracker);
            computeDeltas<black, false>(accs[ply], accs[ply - 1], nnue.tracker);
        }
    }

    template <bool side>
    Inline void initRoot(Nnue& nnue, const BoardState& board) {
        nnue.init(board);
        refresh<side, white>(nnue.mailBox, nnue.accs[0], board);
        refresh<side, black>(nnue.mailBox, nnue.accs[0], board);
    }

    ForceInline vec screlu(vec x) {
        const vec c = min_epi32(max_epi32(x, setzero()), set1_epi32(OUT_SCALE));
        const vec_f f = cvtepi32_ps(c);
        return cvtps_epi32(mul_ps(mul_ps(f, f), set1_ps(1.0f / float(ACT_DIV))));
    }

    ForceInline vec clampL1(vec x) {
        return min_epi32(max_epi32(x, setzero()), set1_epi32(2 * OUT_SCALE));
    }

    ForceInline vec linL1(vec c) {
        return srli_epi32(add_epi32(c, set1_epi32(64)), 7);
    }

    ForceInline vec sqrL1(vec c) {
        const vec_f f = cvtepi32_ps(c);
        return cvtps_epi32(mul_ps(mul_ps(f, f), set1_ps(1.0f / float(4 * ACT_DIV))));
    }

    template <bool side>
    Inline int evaluate(Nnue& nnue, int ply, const BoardState& prev, const BoardState& board) {
        compute<side>(nnue, ply, prev, board);

        const int bkt = BUCKET_IDS[Bitcount(board.occB())];

        alignas(64) uint8_t l1[L1];
        alignas(64) uint8_t l2[L2_IN];
        alignas(64) uint8_t l3[L3];

        const vec zero = setzero();
        const vec qa = set1_epi16(QA);

        {
            vec clamp[2];
            for (int half = 0; half < 2; ++half) {
                const int16_t* src = nnue.accs[ply].vals[half ? !side : side];
                for (int i = 0; i < PW; i += W16) {
                    repeat<2>([&](auto k) { clamp[k] = min_epi16(max_epi16(load(src + i + (k * PW)), zero), qa); });

                    //a * 128 * b / 32768 == a * b / 256 => range is 0 - 254 instead of 0 - 127
                    const vec pairwise = mulhrs_epi16(slli_epi16(clamp[0], 7), clamp[1]);

                    store_h(l1 + half * PW + i, cvtepi16_epi8(pairwise));
                }
            }
        }

        {
            constexpr int OUT = L2 / W32;

            vec l1Sum[OUT * 4]{};
            repeat<OUT>([&](auto k) { l1Sum[k] = load(&net.l1B[bkt][k * W32]); });

            for (int i = 0; i < L1 / 4; i += 4) {
                repeat<4>([&](auto j) {
                    const vec in = set1_epi32(*(const int32_t*)(l1 + 4 * (i + j)));
                    repeat<OUT>([&](auto k) { l1Sum[j * OUT + k] = dpbusd(l1Sum[j * OUT + k], in, load(&net.l1W[bkt][i + j][k * W32])); });
                });
            }

            repeat<OUT>([&](auto k) { 
                const vec clamp = clampL1(add_epi32(add_epi32(l1Sum[k], l1Sum[OUT + k]), add_epi32(l1Sum[2 * OUT + k], l1Sum[3 * OUT + k])));
                store_q(l2 + k * W32, cvtepi32_epi8(linL1(clamp)));
                store_q(l2 + L2 + k * W32, cvtepi32_epi8(sqrL1(clamp)));
            });
        }

        {
            constexpr int OUT = L3 / W32;

            vec l2Sum[OUT];
            repeat<OUT>([&](auto k) { l2Sum[k] = load(&net.l2B[bkt][k * W32]); });

            for (int i = 0; i < L2_IN / 4; ++i) {
                const vec in = set1_epi32(*(const int32_t*)(l2 + 4 * i));
                repeat<OUT>([&](auto k) { l2Sum[k] = dpbusd(l2Sum[k], in, load(net.l2W[bkt][i][k * W32])); });
            }

            repeat<OUT>([&](auto k) { store_q(l3 + k * W32, cvtepi32_epi8(screlu(l2Sum[k]))); });
        }

#if HAS_AVX512
        vec l3Sum = dpbusd(zero, zext(load_h(l3)), zext(load_h(net.l3W[bkt])));
#else
        vec l3Sum = dpbusd(zero, load(l3), load(net.l3W[bkt]));
#endif
        int out = reduce_add_epi32(l3Sum) + net.l3B[bkt];

        return out * SCALE / OUT_SCALE;
    }

    Inline bool loadWeights() {
        std::memcpy(&net, NNUE_WEIGHTS, sizeof(NNUE_WEIGHTS));

        // rearrange weights to be read in the correct order by simd instructions
        int8_t raw1[OUT_BUCKETS][L2][L1];
        int8_t raw2[OUT_BUCKETS][L3][L2_IN];
        std::memcpy(raw1, net.l1W, sizeof(raw1));
        std::memcpy(raw2, net.l2W, sizeof(raw2));

        for (int bkt = 0; bkt < OUT_BUCKETS; ++bkt) {
            for (int o = 0; o < L2; ++o) {
                net.l1B[bkt][o] *= 2; //L1 is /256 instead of /512 due to mulhrs, so we need to double the bias
                for (int i = 0; i < L1; ++i) {
                    net.l1W[bkt][i / 4][o][i & 3] = raw1[bkt][o][i];
                }
            }

            for (int o = 0; o < L3; ++o) {
                for (int i = 0; i < L2_IN; ++i) {
                    net.l2W[bkt][i / 4][o][i & 3] = raw2[bkt][o][i];
                }
            }
        }

        return true;
    }

    Inline void init() {
        loadWeights();
        initRelations();
    }
}