#include <RE/Skyrim.h>
#include <REL/Relocation.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace {
    // Only the vptr is consumed by the production wrapper; never construct an engine object.
    struct Reference {
        std::uintptr_t* vtable;
        RE::TESObjectCELL* parent;
        unsigned setters;
    };

    RE::TESObjectCELL* Getter(const RE::TESObjectREFR* self) {
        return reinterpret_cast<const Reference*>(self)->parent;
    }

    // Getter-shaped trap avoids undefined missing-argument behavior on a bad slot.
    RE::TESObjectCELL* SetterTrap(const RE::TESObjectREFR* self) {
        auto* ref = const_cast<Reference*>(reinterpret_cast<const Reference*>(self));
        ++ref->setters;
        ref->parent = nullptr;
        return nullptr;
    }
}

int main(int argc, char** argv) {
    const bool wrong = argc == 2 && std::strcmp(argv[1], "--wrong-slot") == 0;
    const std::array versions{REL::Version{1, 5, 97, 0}, REL::Version{1, 6, 1170, 0}, REL::Version{1, 4, 15, 0}};
    for (const auto& version : versions) {
        if (!REL::Module::mock(version)) return 2;
        const bool vr = REL::Module::IsVR();
        std::array<std::uintptr_t, 0x9B> table;
        table.fill(reinterpret_cast<std::uintptr_t>(&SetterTrap));
        table[(vr ? 0x98 : 0x97) + (wrong ? 1 : 0)] = reinterpret_cast<std::uintptr_t>(&Getter);
        std::uintptr_t cellStorage = 0;
        auto* cell = reinterpret_cast<RE::TESObjectCELL*>(&cellStorage);
        Reference ref{table.data(), cell, 0};
        const auto* engineRef = reinterpret_cast<const RE::TESObjectREFR*>(&ref);
        const auto* result = engineRef->GetSaveParentCell();
        if (result != cell || ref.parent != cell || ref.setters != 0) {
            std::printf("FAIL %s getter: result=%p parent=%p setters=%u\n", vr ? "VR" : "SE/AE", result, ref.parent, ref.setters);
            return 1;
        }
        std::printf("PASS %s getter slot 0x%X: expected cell, no setter, unchanged parent\n", vr ? "VR" : "SE/AE", vr ? 0x98 : 0x97);
    }
}
