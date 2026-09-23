#include "../PICO-OpenXR-Demos/app/src/main/cpp/arxvr_qa_input.h"
#include <cassert>
#include <cstdio>

int main() {
    ArxVrQaInput command;
    assert(ParseArxVrQaInput("1 .4 0 1 0 0 0 .2 -.2 -.4 0 0 512", command));
    assert(command.sequence == 1 && command.moveY == 1 && command.buttons == 512);
    assert(!ParseArxVrQaInput("1 3 0 1 0 0 0 .2 -.2 -.4 0 0 512", command));
    assert(!ParseArxVrQaInput("1 .4 0 2.75 0 0 0 .2 -.2 -.4 0 0 0", command));
    assert(!ParseArxVrQaInput("1 .4 0 1 nan 0 0 .2 -.2 -.4 0 0 0", command));
    assert(!ParseArxVrQaInput("1 .4 0 1 0 0 0 .8 -.8 -.8 0 0 0", command));
    assert(!ParseArxVrQaInput("1 .4 0 1 0 0 0 .2 -.2 -.4 0 0 65535", command));
    assert(!ParseArxVrQaInput("1 .4 0 1 0 0 0 .2 -.2 -.4 0 0 512 junk", command));
    assert(!ParseArxVrQaInput("0", command));
    assert(command.sequence == 1 && command.moveY == 1 && command.buttons == 512);
    std::puts("PASS: QA commands reject out-of-range axes, long pulses, invalid poses and malformed input");
}
