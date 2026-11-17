ifeq ($(origin CXX),default)
    CXX = clang++
endif

EXE ?= crownd

SRCS = Main.cpp TranspositionTable.cpp Utils.cpp

ARCH ?= native

PYRRHIC = -x c++ -D_CRT_SECURE_NO_WARNINGS -D_SILENCE_CXX20_ATOMIC_INIT_DEPRECATION_WARNING pyrrhic/tbprobe.c

CXXFLAGS = -std=c++20 -O3 -flto -fno-stack-protector -fexceptions -ffunction-sections -fuse-ld=lld -march=$(ARCH)

.PHONY: all tune clean

all:
	$(CXX) $(CXXFLAGS) $(SRCS) $(PYRRHIC) -o $(EXE).exe

tune:
	$(CXX) $(CXXFLAGS) -DTUNE $(SRCS) $(PYRRHIC) -o $(EXE)_tune.exe