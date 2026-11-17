#include "Cui.h"
#include "Uci.h"
#include "Engine.h"
#include "Nnue.h"
#include "Cuckoo.h"

int main() {
    game::setFen(StartPosition);
    nnue::init();
    engine::init();
    cuckoo::init();

    cui::start();
}