#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace IntelEngine {

enum class UiHostKind { None, PrismaUI, Magelight };
enum class UiHostEvent { DomReady, Entered, Exited, Refused, Unavailable };
enum class UiOpenResult { Accepted, Pending, Refused };

struct UiListener {
    const char* name;
    void (*callback)(const char* argument);
};

class IUiHost {
public:
    virtual ~IUiHost() = default;
    virtual UiHostKind Kind() const noexcept = 0;
    virtual bool IsAvailable() const noexcept = 0;
    virtual bool Create() = 0;
    virtual UiOpenResult Open() = 0;
    virtual void Close() = 0;
    virtual void Reload() = 0;
    virtual UiOpenResult Service() = 0;
    virtual bool Call(std::string_view name, std::string_view argument) = 0;
    virtual void Shutdown() = 0;
};

using UiHostEventSink = std::function<void(UiHostEvent)>;

// Preference is read once at process selection; adapters retain callback context
// until host callbacks can no longer occur.
std::unique_ptr<IUiHost> CreateUiHost(std::string_view preference,
                                     std::span<const UiListener> listeners,
                                     UiHostEventSink eventSink);

struct UiHostApiResolver {
    void* (*getModuleHandle)(const wchar_t* moduleName);
    void* (*getProcAddress)(void* module, const char* symbolName);
    void* (*requestPrismaApi)();
    bool (*postTask)(void (*callback)(void*), void* user);
};

std::unique_ptr<IUiHost> CreateUiHostWithResolver(std::string_view preference,
                                                 std::span<const UiListener> listeners,
                                                 UiHostEventSink eventSink,
                                                 const UiHostApiResolver& resolver);

// Types the pad as keys (A Enter, B Escape, d-pad and left stick the arrows) on a Magelight view made elsewhere,
// such as the choice popup created through Magelight's Papyrus tier, which has no gamepad call of its own.
// 1 = done, 0 = no gamepad layer (no Magelight, a host before 1.6.0, VR, switched off), -1 = refused by the host.
int SetMagelightViewGamepadKeys(std::uint32_t view);
int SetMagelightViewGamepadKeysWithResolver(std::uint32_t view, const UiHostApiResolver& resolver);

}  // namespace IntelEngine
