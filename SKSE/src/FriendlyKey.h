#pragma once
#include <string>
namespace IntelEngine {
inline std::string FriendlyKey(std::string key) {
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (key[i] == '_') key[i] = ' ';
        if ((i == 0 || key[i - 1] == ' ') && key[i] >= 'a' && key[i] <= 'z') key[i] -= 'a' - 'A';
    }
    return key;
}
}
