
<div align="center">
	<img src="https://i.imgur.com/koqdmmT.png" alt="Crownd logo"> <br> 
	<a href="https://www.youtube.com/watch?v=uWWC-2pPSrE">The King Will Come!</a> 
</div>

# About
This is a UCI compliant chess engine written in c++, with NNUE evaluation.\
The main motivation for this project was to get an introduction to neural networks, but also as round 2 of a chess engine started over 10 years ago that I had given up on due to my limited knowledge for the HCE. "Cui.h", a custom command user interface, is a remnant of that time. \
Crownd is built upon the code of my perft engine,  <a href="https://github.com/thomashuijbregts/chessbit">chessbit</a>, using a tweaked version of its move generator.\
The name crownd is a tribute to my dear mother, whose name is literally that in French, and it works for chess at the same time! The "e" was omitted for style points.

# Strength
Currently waiting for an official rating, but it should be able to give a hard time to very strong engines.

# Build
The code only supports AVX2 and AVX512 architectures, and makes use of Single Instruction Multiple Data, aka SIMD, to accelerate the NNUE inference.

The code supports both Windows and Linux machines.

There are binaries available in the releases (compiled with clang), but if something is missing, you can also use the committed Makefile script and download the network in the releases (nnue_x.bin).\
The network is incorporated in the binary using #embed (normally available with the recent versions of clang, icx and gcc, but not with MSVC), so just place it in the same folder while compiling.

| Arch             | Arg                      | VNNI  | VBMI | Name                          |
|-----------------:|-------------------------:|------:|-----:|------------------------------:|
| -march=x86-64-v3 | /                        | no    | no   | crownd_{ver}_v3.exe           |
| -march=x86-64-v3 | -mavxvnni                | yes   | no   | crownd_{ver}_v3_vnni.exe      |
| -march=x86-64-v4 | /                        | no    | no   | crownd_{ver}_v4.exe           |
| -march=x86-64-v4 | -mavx512vnni             | yes   | no   | crownd_{ver}_v4_vnni.exe      |
| -march=x86-64-v4 | -mavx512vnni -mavx512vbmi| yes   | yes  | crownd_{ver}_v4_vnni_vbmi.exe |
| -march=znver4    | /                        | yes   | yes  | crownd_{ver}_zen4.exe         |
| -march=znver5    | /                        | yes   | yes  | crownd_{ver}_zen5.exe         |

If you're not sure what you can run, I suggest you start from the bottom and work your way up to the top of this list.

# Move generation
Bitboards with pext for the bishop/rook attack tables lookups.\
See <a href="https://github.com/thomashuijbregts/chessbit">chessbit</a> for all the details.\
Several "perft only" tricks and template parameters were removed to accomodate the engine's needs. The BoardState struct was also reworked to fit into 128 bytes to reduce cache pressure.

# Search
Many different pruning methods and heuristics are implemented to improve the Alphabeta algorithm. Most of the logic is implemented in Engine.h.
### Iterative deepening
- Aspiration window
- Time management
### Alphabeta search
- Cuckoo hashing for future repetition detection
- Mate distance pruning
- Internal iterative reductions
- Endgame tablebase probing
- Stability reductions (<- new tech)
- Razoring
- Reverse futility pruning
- Null move pruning with verification search
- ProbCut
- Move loop
   - Staged move generation (noisy, then quiet) 
   - Futility pruning
   - Late move pruning
   - History pruning (quiet and noisy)
   - SEE pruning (quiet and noisy)
   - Singular extensions
   - Late move reductions
### Quiescence search
- Delta pruning
- Move loop
   - Futility pruning
   - SEE pruning
### Transposition table
- 16 bits key storage
- Depth and age based replacement policy
### Lazy SMP
- Voting system to select the best move across threads
### Other
- History: quiet, noisy, continuation and pawn
- Correction history: pawn, non pawn, continuation, minor pieces and major pieces
- Killer moves (2)
- Counter moves

# NNUE
The network is trained on Lc0 data, using 0 WDL and a cosine decay for the learning rate (from 0.001 to 0.000001).\
It uses the HalfKA features, with horizontal mirroring (32 king buckets), as well as threat inputs (called piece relationships - pcRel - in this project), with 75202 total features.

# To do
- A network using exclusively selfplay data
- FRC support
- Better UCI support (decommission custom UI)
- Better search and more optimizations, always

# Credits
- <a href="https://chessprogramming.org">Chess Programming Wiki</a>, an essential source of information for guidance and knowledge for the search function.
- <a href="https://github.com/official-stockfish/Stockfish">Stockfish</a>, used to validate and fix bugs for the search function, as well as some ideas and formulas that were not present on the chess wiki.
- <a href="https://github.com/Yoshie2000/PlentyChess">PlentyChess</a>, used as a second source of inspiration for the search function.
- <a href="https://github.com/jw1912/bullet">Bullet</a>, used for the NNUE training, as well as very useful knowledge on NNUE architectures to get started.
- <a href="https://github.com/Disservin/fastchess">Fastchess</a>, used to validate improvements in selfplay, as well as estimating crownd's strength against other engines.
- <a href="https://87flowers.com/byteboard-attack-tables-1/">87flowers</a>, for the SIMD accelerated threats updates, used in the AVX512 build (requires VBMI).
- <a href="https://github.com/LeelaChessZero/lc0">Lc0</a>, for the data used in the network.
- <a href="https://github.com/AndyGrant/Pyrrhic">Pyrrhic</a>, for easy Syzygy tables integration.