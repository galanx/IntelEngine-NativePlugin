#include "PlayerCellEventSource.h"

#include <stdexcept>

int main() {
    const auto require = [](bool condition) {
        if (!condition) throw std::runtime_error("player cell event source offset check failed");
    };

    // Offsets the game's own PlayerCharacter destructor uses (1.6.1170, 1.7.104), and CommonLib's SE and VR values.
    require(PlayerCellEventSourceOffset(false, 1, 5, 97) == 0x2D0);
    require(PlayerCellEventSourceOffset(false, 1, 6, 353) == 0x2D0);
    require(PlayerCellEventSourceOffset(false, 1, 6, 629) == 0x2D8);
    require(PlayerCellEventSourceOffset(false, 1, 6, 1170) == 0x2D8);
    require(PlayerCellEventSourceOffset(false, 1, 7, 99) == 0x2E0);
    require(PlayerCellEventSourceOffset(false, 1, 7, 104) == 0x2E0);
    require(PlayerCellEventSourceOffset(true, 1, 4, 15) == 0x2E8);
}
