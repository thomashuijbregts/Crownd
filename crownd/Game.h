#ifndef GAME_H
#define GAME_H

#include "BoardState.h"
#include "Zobrist.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace defs;
using namespace bstate;
using std::string;

namespace bstate {
    struct BoardState;
}

namespace engine {
    Inline void clear();
}

namespace game {
    inline int moveCount;
    inline BoardState movesPlayed[4096];
    inline BoardState board;

    Inline void printBoard(U64 bitboard) {
        for (int rank = 0; rank < 8; rank++)
        {
            for (int file = 0; file < 8; file++)
            {
                int square = rank * 8 + file;
                if (!file)
                    printf("  %d ", 8 - rank);

                printf(" %d", GetBit(bitboard, square) ? 1 : 0);
            }
            printf("\n");
        }
        printf("\n     a b c d e f g h\n\n");
        printf("     bitboard: 0x%llx\n\n", bitboard);
    }

    Inline void printBoard(const BoardState& board) {
        printf("\n");

        for (int rank = 0; rank < 8; rank++)
        {
            for (int file = 0; file < 8; file++)
            {
                int square = rank * 8 + file;

                if (!file)
                    printf("  %d ", 8 - rank);

                int piece = -1;
                int side;

                if (GetBit(board.pM(), square)) { piece = p; side = board.side; }
                if (GetBit(board.nM(), square)) { piece = n; side = board.side; }
                if (GetBit(board.bM(), square)) { piece = b; side = board.side; }
                if (GetBit(board.rM(), square)) { piece = r; side = board.side; }
                if (GetBit(board.qM(), square)) { piece = q; side = board.side; }
                if (GetBit(board.kM(), square)) { piece = k; side = board.side; }
                                   
                if (GetBit(board.pE(), square)) { piece = p; side = !board.side; }
                if (GetBit(board.nE(), square)) { piece = n; side = !board.side; }
                if (GetBit(board.bE(), square)) { piece = b; side = !board.side; }
                if (GetBit(board.rE(), square)) { piece = r; side = !board.side; }
                if (GetBit(board.qE(), square)) { piece = q; side = !board.side; }
                if (GetBit(board.kE(), square)) { piece = k; side = !board.side; }

                printf(" %c", (piece == -1) ? '.' : ASCII_PIECES[side][piece]);
            }

            printf("\n");
        }

        printf("\n     a b c d e f g h\n\n");

        printf("     Side:     %s\n", !board.side ? "white" : "black");

        printf("     En Passant:  %s\n", (board.eP != noSquare) ? SQUARE_NAMES[board.eP] : "no");

        printf("     Castling:  %c%c%c%c\n\n",
            (board.casPerms & wk) ? 'K' : '-',
            (board.casPerms & wq) ? 'Q' : '-',
            (board.casPerms & bk) ? 'k' : '-',
            (board.casPerms & bq) ? 'q' : '-');
    }

    ForceInline void makeMove(const BoardState& move) {
        moveCount++;
        movesPlayed[moveCount] = move;
        board = move;
    }

    ForceInline void unmakeMove() {
        moveCount--;
        board = movesPlayed[moveCount];
    }

    Inline void setFen(const char* fen, bool clear = true) {
        U64 pcs[2][6];
        U64 occ[3];

        memset(pcs, 0ULL, sizeof(pcs));
        memset(occ, 0ULL, sizeof(occ));

        moveCount = 0;

        bool side = 0;
        int eP = noSquare;
        int casPerms = 0;

        for (int rank = 0; rank < 8; rank++) {
            for (int file = 0; file < 8; file++) {
                int square = rank * 8 + file;

                if (*fen >= 'a' && *fen <= 'z')
                {
                    int piece = getPieceForCharacter(*fen);
                    SetBit(pcs[black][piece], square);
                    fen++;
                }
                else if (*fen >= 'A' && *fen <= 'Z')
                {
                    int piece = getPieceForCharacter(*fen);
                    SetBit(pcs[white][piece], square);
                    fen++;
                }

                if (*fen >= '0' && *fen <= '9')
                {
                    int offset = *fen - '0';
                    int piece = -1;

                    for (int bb_piece = p; bb_piece <= k; bb_piece++)
                    {
                        if (GetBit(pcs[white][bb_piece], square) || GetBit(pcs[black][bb_piece], square)) {
                            piece = bb_piece;
                        }
                    }

                    if (piece == -1) {
                        file--;
                    }

                    file += offset;

                    fen++;
                }

                if (*fen == '/') {
                    fen++;
                }
            }
        }

        fen++;

        side = (*fen == 'w') ? white : black;

        fen += 2;

        while (*fen != ' ')
        {
            switch (*fen)
            {
            case 'K': casPerms |= wk; break;
            case 'Q': casPerms |= wq; break;
            case 'k': casPerms |= bk; break;
            case 'q': casPerms |= bq; break;
            case '-': break;
            }

            fen++;
        }

        fen++;

        if (*fen != '-')
        {
            int file = fen[0] - 'a';
            int rank = 8 - (fen[1] - '0');
            eP = rank * 8 + file;
            fen += 2;
        }
        else {
            eP = noSquare;
            fen += 1;
        }

        while (*fen == ' ') fen++;

        uint8_t halfClock = 0;
        if (*fen >= '0' && *fen <= '9') {
            int hc = 0;
            while (*fen >= '0' && *fen <= '9') {
                hc = hc * 10 + (*fen - '0');
                fen++;
            }
            halfClock = (uint8_t)hc;
        }

        for (int piece = p; piece <= k; piece++) {
            occ[white] |= pcs[white][piece];
            occ[black] |= pcs[black][piece];
        }

        occ[both] |= occ[white];
        occ[both] |= occ[black];

        U64 king = pcs[side][k];

        U64 checks = 0ULL;

        U64 bitboard = pcs[!side][p];
        Bitloop(bitboard) {
            int sourceSquare = SquareOf(bitboard);

            if (PAWN_CAPTURES[!side][sourceSquare] & king) {
                checks |= (1ULL << sourceSquare);
            }
        }

        bitboard = pcs[!side][n];
        Bitloop(bitboard) {
            int sourceSquare = SquareOf(bitboard);

            if (KNIGHT_ATTACKS[sourceSquare] & king) {
                checks |= (1ULL << sourceSquare);
            }
        }

        bitboard = pcs[!side][b];
        Bitloop(bitboard) {
            int sourceSquare = SquareOf(bitboard);

            if (getBishopAttacks(sourceSquare, occ[both]) & king) {
                checks |= (1ULL << sourceSquare);
            }
        }

        bitboard = pcs[!side][r];
        Bitloop(bitboard) {
            int sourceSquare = SquareOf(bitboard);

            if (getRookAttacks(sourceSquare, occ[both]) & king) {
                checks |= (1ULL << sourceSquare);
            }
        }

        bitboard = pcs[!side][q];
        Bitloop(bitboard) {
            int sourceSquare = SquareOf(bitboard);

            if (getQueenAttacks(sourceSquare, occ[both]) & king) {
                checks |= (1ULL << sourceSquare);
            }
        }

        int kMS = SquareOf(pcs[side][k]);
        int kES = SquareOf(pcs[!side][k]);

        Zobrist pawnKey = 0ULL;
        Zobrist nonPawnKey[2]{ 0ULL, 0ULL };
        Zobrist minorKey = 0ULL;
        Zobrist majorKey = 0ULL;
        Zobrist zobrist = zobrist::init(pcs, side, casPerms, eP, pawnKey, nonPawnKey, minorKey, majorKey);

        board = bstate::BoardState(noSquare, noSquare, pcs[side][p], pcs[side][n], pcs[side][b], pcs[side][r], pcs[side][q], pcs[side][k],
            pcs[!side][p], pcs[!side][n], pcs[!side][b], pcs[!side][r], pcs[!side][q], pcs[!side][k],
            kMS, kES, occ[side], occ[!side],
            checks, casPerms, eP, noPiece, noPiece, 0, noSquare, side, false, halfClock, 0, zobrist);
        board.pawnKey = uint32_t(pawnKey);
        board.nonPawnKey[0] = uint32_t(nonPawnKey[0]);
        board.nonPawnKey[1] = uint32_t(nonPawnKey[1]);
        board.minorKey = uint32_t(minorKey);
        board.majorKey = uint32_t(majorKey);

        movesPlayed[0] = board;

        if (clear) engine::clear();
    }

    Inline string getFen(const BoardState& board, int fullMove = 0) {
        string fen;

        int empty;
        int piece;
        for (int rank = 0; rank < 8; rank++)
        {
            empty = 0;
            for (int file = 0; file < 8; file++)
            {
                int square = rank * 8 + file;

                piece = -1;
                int side;

                if (GetBit(board.pM(), square)) { piece = p; side = board.side; }
                if (GetBit(board.nM(), square)) { piece = n; side = board.side; }
                if (GetBit(board.bM(), square)) { piece = b; side = board.side; }
                if (GetBit(board.rM(), square)) { piece = r; side = board.side; }
                if (GetBit(board.qM(), square)) { piece = q; side = board.side; }
                if (GetBit(board.kM(), square)) { piece = k; side = board.side; }
                                   
                if (GetBit(board.pE(), square)) { piece = p; side = !board.side; }
                if (GetBit(board.nE(), square)) { piece = n; side = !board.side; }
                if (GetBit(board.bE(), square)) { piece = b; side = !board.side; }
                if (GetBit(board.rE(), square)) { piece = r; side = !board.side; }
                if (GetBit(board.qE(), square)) { piece = q; side = !board.side; }
                if (GetBit(board.kE(), square)) { piece = k; side = !board.side; }

                if (piece != -1) {
                    if (empty > 0) {
                        fen += std::to_string(empty);
                    }
                    fen += ASCII_PIECES[side][piece];
                    empty = 0;
                }
                else {
                    empty++;
                }
            }
            if (piece == -1) fen += std::to_string(empty);

            if (rank < 7) fen += "/";
        }

        fen += " ";
        fen += (!board.side ? "w" : "b");
        fen += " ";

        string castling = (board.casPerms & wk) ? "K" : "";
        castling += (board.casPerms & wq) ? "Q" : "";
        castling += (board.casPerms & bk) ? "k" : "";
        castling += (board.casPerms & bq) ? "q" : "";

        fen += (board.casPerms > 0) ? castling : "-";
        fen += " ";
        fen += (board.eP != noSquare) ? SQUARE_NAMES[board.eP] : "-";
        fen += " ";
        fen += std::to_string(board.halfClock);
        fen += " ";
        if (fullMove) fen += std::to_string(fullMove);
        else          fen += std::to_string(moveCount / 2 + 1);

        return fen;
    }
}

#endif