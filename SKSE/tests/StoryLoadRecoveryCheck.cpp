#include "StoryLoadRecovery.h"
#include <iostream>
#include <stdexcept>

int main() {
    auto require = [](bool ok) { if (!ok) throw std::runtime_error("story load recovery gate mismatch"); };
    require(!IntelEngineStoryLoadRecovered(false));
    require(IntelEngineStoryLoadRecovered(true));
    require(IntelEngineStoryLoadRecovered(false));
    for (int load = 0; load < 2; ++load) {
        intelEngineStoryLoadRecovered.store(false);
        require(!IntelEngineStoryLoadRecovered(false));
        require(!IntelEngineStoryLoadRecovered(false));
        require(IntelEngineStoryLoadRecovered(true));
        require(IntelEngineStoryLoadRecovered(false));
    }
    std::cout << "PASS: story arrival gate closed until recovery; repeated reads and successive load resets\n";
}
