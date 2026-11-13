#include "UiHost.h"

#ifdef UIHOST_OFFLINE_TEST
#include "UiHostTestSupport.h"
#else
#include "Plugin.h"
#endif
#include "MagelightUI_API.h"
#include "PrismaUI_API.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace IntelEngine {
namespace {

constexpr std::uint32_t kMinimumHostVersion = 10701;
constexpr std::uint32_t kMagelightApiVersion = 4;
constexpr std::uint32_t PackedPluginVersion(const char* value)
{
    std::uint32_t parts[3]{};
    std::size_t part = 0;
    for (; *value; ++value) {
        if (*value == '.') {
            if (++part == 3) return 0;
        } else {
            parts[part] = parts[part] * 10 + static_cast<std::uint32_t>(*value - '0');
        }
    }
    return parts[0] * 10000 + parts[1] * 100 + parts[2];
}
constexpr std::uint32_t kPluginVersionNumber = PackedPluginVersion(INTELENGINE_VERSION);

std::string Lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string ErrorText(const MAGELIGHT_API::MagelightApi4* api, MAGELIGHT_API::ModId mod,
                      MAGELIGHT_API::Result result)
{
    std::string message = "result=" + std::to_string(static_cast<int>(result));
    if (api && api->GetLastErrorMessage && mod) {
        if (const char* error = api->GetLastErrorMessage(mod); error && *error) {
            message += ": ";
            message += error;
        }
    }
    return message;
}

struct PrismaContext {
    std::mutex mutex;
    PRISMA_UI_API::IVPrismaUI1* api = nullptr;
    std::span<const UiListener> listeners;
    UiHostEventSink sink;
    bool (*postTask)(void (*)(void*), void*) = nullptr;
    std::uint64_t generation = 1;
    PrismaView view = 0;
    PrismaView earlyDomReadyView = 0;
    bool adopted = false;
    bool domReady = false;
    bool listenersInstalled = false;
    bool requestedOpen = false;
    std::uint64_t requestGeneration = 1;
    bool shuttingDown = false;
    std::condition_variable callbacksDrained;
    std::size_t activeCallbacks = 0;
    const MAGELIGHT_API::MagelightApi4* magelight = nullptr;
};

PrismaContext& ProcessPrismaContext()
{
    static auto* context = new PrismaContext;
    return *context;
}
struct PrismaCallbackGuard {
    explicit PrismaCallbackGuard(PrismaContext& context) : context(context)
    {
        std::lock_guard lock(context.mutex);
        if (!context.shuttingDown) {
            ++context.activeCallbacks;
            active = true;
        }
    }

    ~PrismaCallbackGuard()
    {
        if (!active) return;
        std::lock_guard lock(context.mutex);
        if (--context.activeCallbacks == 0) context.callbacksDrained.notify_all();
    }

    explicit operator bool() const { return active; }

    PrismaContext& context;
    bool active = false;
};

struct PrismaPayload {
    PrismaContext* context;
    std::uint64_t generation;
    PrismaView view;
    PRISMA_UI_API::JSListenerCallback callback = nullptr;
    std::string argument;
    UiHostEvent event = UiHostEvent::DomReady;
    bool domReady = false;
    bool eventOnly = false;
    std::uint64_t requestGeneration = 0;
};
template <std::size_t Index>
void PrismaListener(const char* argument);

void FinishPrismaDomReady(PrismaContext& context, PrismaView view, std::uint64_t generation)
{
    PRISMA_UI_API::IVPrismaUI1* api = nullptr;
    std::span<const UiListener> listeners;
    UiHostEventSink* sink = nullptr;
    bool openRequested = false;
    const MAGELIGHT_API::MagelightApi4* magelight = nullptr;
    std::uint64_t requestGeneration = 0;
    {
        std::lock_guard lock(context.mutex);
        if (context.shuttingDown || generation != context.generation) return;
        if (!context.adopted || view != context.view) {
            context.earlyDomReadyView = view;
            return;
        }
        if (context.domReady) return;
        context.domReady = true;
        api = context.api;
        listeners = context.listeners;
        sink = &context.sink;
        openRequested = context.requestedOpen;
        requestGeneration = context.requestGeneration;
        magelight = context.magelight;
    }
    static constexpr std::array<PRISMA_UI_API::JSListenerCallback, 17> callbacks = {
        &PrismaListener<0>, &PrismaListener<1>, &PrismaListener<2>, &PrismaListener<3>,
        &PrismaListener<4>, &PrismaListener<5>, &PrismaListener<6>, &PrismaListener<7>,
        &PrismaListener<8>, &PrismaListener<9>, &PrismaListener<10>, &PrismaListener<11>,
        &PrismaListener<12>, &PrismaListener<13>, &PrismaListener<14>, &PrismaListener<15>, &PrismaListener<16>
    };
    for (std::size_t i = 0; i < listeners.size() && i < callbacks.size(); ++i)
        api->RegisterJSListener(view, listeners[i].name, callbacks[i]);
    {
        std::lock_guard lock(context.mutex);
        if (context.shuttingDown || generation != context.generation || view != context.view) return;
        context.listenersInstalled = true;
        openRequested = context.requestedOpen;
        requestGeneration = context.requestGeneration;
        if (context.sink) context.sink(UiHostEvent::DomReady);
    }
    if (!openRequested) return;
    const bool conflict = (magelight && magelight->IsUIModeActive && magelight->IsUIModeActive()) ||
                          api->HasAnyActiveFocus();
    const bool focused = !conflict && api->Focus(view, true, false);
    bool requestCurrent = false;
    bool anyRequest = false;
    {
        std::lock_guard lock(context.mutex);
        anyRequest = context.requestedOpen;
        requestCurrent = anyRequest && context.requestGeneration == requestGeneration;
        if (requestCurrent && !focused) {
            context.requestedOpen = false;
            ++context.requestGeneration;
        }
    }
    if (!requestCurrent) {
        if (!anyRequest) {
            api->Unfocus(view);
            api->Hide(view);
        }
        return;
    }
    if (!focused) {
        api->Hide(view);
        if (sink && *sink) (*sink)(UiHostEvent::Refused);
        return;
    }
    if (sink && *sink) (*sink)(UiHostEvent::Entered);
}

void RunPrismaPayload(void* raw)
{
    std::unique_ptr<PrismaPayload> payload(static_cast<PrismaPayload*>(raw));
    auto& context = *payload->context;
    PrismaCallbackGuard callbackGuard(context);
    if (!callbackGuard) return;
    if (payload->domReady) {
        FinishPrismaDomReady(context, payload->view, payload->generation);
        return;
    }
    UiHostEventSink* sink = nullptr;
    PRISMA_UI_API::JSListenerCallback callback = payload->callback;
    {
        std::lock_guard lock(context.mutex);
        if (context.shuttingDown || payload->generation != context.generation ||
            (payload->eventOnly && payload->requestGeneration != context.requestGeneration) ||
            !context.adopted || payload->view != context.view) return;
        if (payload->eventOnly) sink = &context.sink;
        else if (!context.domReady || !context.listenersInstalled || !context.requestedOpen) return;
    }
    if (payload->eventOnly) {
        if (sink && *sink) (*sink)(payload->event);
    } else if (callback) {
        callback(payload->argument.c_str());
    }
}

bool QueuePrismaPayload(PrismaContext& context, PrismaPayload* payload)
{
    bool (*postTask)(void (*)(void*), void*) = nullptr;
    {
        std::lock_guard lock(context.mutex);
        postTask = context.postTask;
    }
    if (postTask && postTask(&RunPrismaPayload, payload)) return true;
    delete payload;
    return false;
}

template <std::size_t Index>
void PrismaListener(const char* argument)
{
    auto& context = ProcessPrismaContext();
    std::uint64_t generation;
    PrismaView view;
    PRISMA_UI_API::JSListenerCallback callback = nullptr;
    {
        std::lock_guard lock(context.mutex);
        if (context.shuttingDown || Index >= context.listeners.size()) return;
        generation = context.generation;
        view = context.view;
        callback = context.listeners[Index].callback;
    }
    auto* payload = new PrismaPayload{&context, generation, view, callback,
                                      argument ? argument : "{}", UiHostEvent::DomReady, false, false};
    if (!QueuePrismaPayload(context, payload))
        logger::warn("[Dashboard] Could not schedule PrismaUI listener callback");
}

class PrismaHost final : public IUiHost {
public:
    PrismaHost(PRISMA_UI_API::IVPrismaUI1* api, std::span<const UiListener> listeners,
               UiHostEventSink sink, const MAGELIGHT_API::MagelightApi4* magelight,
               bool (*postTask)(void (*)(void*), void*))
        : api_(api), magelight_(magelight)
    {
        auto& context = ProcessPrismaContext();
        std::lock_guard lock(context.mutex);
        context.api = api;
        context.listeners = listeners;
        context.sink = std::move(sink);
        context.postTask = postTask;
        context.magelight = magelight;
        context.shuttingDown = false;
    }

    UiHostKind Kind() const noexcept override { return UiHostKind::PrismaUI; }
    bool IsAvailable() const noexcept override { return api_ && view_ != 0; }

    bool Create() override
    {
        if (!api_) return false;
        if (view_ && api_->IsValid(view_)) return true;
        auto& context = ProcessPrismaContext();
        std::uint64_t generation;
        {
            std::lock_guard lock(context.mutex);
            generation = ++context.generation;
            context.domReady = false;
            context.listenersInstalled = false;
            context.adopted = false;
            context.earlyDomReadyView = 0;
        }
        try {
            view_ = api_->CreateView("IntelEngine/dashboard/index.html", &OnDomReady);
            if (!view_) return false;
            api_->SetOrder(view_, 90);
            api_->Hide(view_);
            PrismaView early = 0;
            {
                std::lock_guard lock(context.mutex);
                context.view = view_;
                context.adopted = true;
                early = context.earlyDomReadyView;
            }
            if (early == view_) FinishPrismaDomReady(context, view_, generation);
            return true;
        } catch (...) {
            view_ = 0;
            return false;
        }
    }

    UiOpenResult Open() override
    {
        if (!Create()) return UiOpenResult::Refused;
        auto& context = ProcessPrismaContext();
        const bool competingMagelight = magelight_ && magelight_->IsUIModeActive &&
                                          magelight_->IsUIModeActive();
        if (competingMagelight || api_->HasAnyActiveFocus()) {
            return UiOpenResult::Refused;
        }
        bool ready;
        std::uint64_t requestGeneration;
        {
            std::lock_guard lock(context.mutex);
            requestGeneration = ++context.requestGeneration;
            context.requestedOpen = true;
            ready = context.domReady && context.listenersInstalled;
        }
        api_->Show(view_);
        if (!ready) return UiOpenResult::Accepted;
        if (!api_->Focus(view_, true, false)) {
            api_->Hide(view_);
            {
                std::lock_guard lock(context.mutex);
                context.requestedOpen = false;
                ++context.requestGeneration;
            }
            return UiOpenResult::Refused;
        }
        QueuePrismaEvent(UiHostEvent::Entered, requestGeneration);
        return UiOpenResult::Accepted;
    }

    void Close() override
    {
        if (!api_ || !view_) return;
        auto& context = ProcessPrismaContext();
        bool wasRequested;
        std::uint64_t requestGeneration = 0;
        {
            std::lock_guard lock(context.mutex);
            wasRequested = context.requestedOpen;
            if (wasRequested) requestGeneration = ++context.requestGeneration;
            context.requestedOpen = false;
        }
        try {
            api_->Unfocus(view_);
            api_->Hide(view_);
        } catch (...) {}
        if (wasRequested) QueuePrismaEvent(UiHostEvent::Exited, requestGeneration);
    }

    void Reload() override
    {
        if (!api_ || !view_) return;
        auto& context = ProcessPrismaContext();
        bool wasRequested;
        {
            std::lock_guard lock(context.mutex);
            wasRequested = context.requestedOpen;
            ++context.generation;
            context.domReady = false;
            context.listenersInstalled = false;
            context.adopted = false;
            context.earlyDomReadyView = 0;
        }
        api_->Unfocus(view_);
        api_->Destroy(view_);
        view_ = 0;
        if (Create() && wasRequested) api_->Show(view_);
    }

    UiOpenResult Service() override { return UiOpenResult::Pending; }

    bool Call(std::string_view name, std::string_view argument) override
    {
        if (!IsAvailable()) return false;
        std::string fn(name), arg(argument);
        api_->InteropCall(view_, fn.c_str(), arg.c_str());
        return true;
    }

    void Shutdown() override
    {
        if (!api_ || !view_) return;
        Close();
        auto& context = ProcessPrismaContext();
        {
            std::unique_lock lock(context.mutex);
            context.shuttingDown = true;
            ++context.generation;
            context.adopted = false;
            context.callbacksDrained.wait(lock, [&] { return context.activeCallbacks == 0; });
        }
        api_->Destroy(view_);
        view_ = 0;
    }

private:
    void QueuePrismaEvent(UiHostEvent event, std::uint64_t requestGeneration)
    {
        auto& context = ProcessPrismaContext();
        std::uint64_t generation;
        {
            std::lock_guard lock(context.mutex);
            generation = context.generation;
        }
        auto* payload = new PrismaPayload{&context, generation, view_, nullptr, {}, event, false, true};
        payload->requestGeneration = requestGeneration;
        if (!QueuePrismaPayload(context, payload))
            logger::warn("[Dashboard] Could not schedule PrismaUI host event");
    }

    static void OnDomReady(PrismaView view)
    {
        auto& context = ProcessPrismaContext();
        std::uint64_t generation;
        {
            std::lock_guard lock(context.mutex);
            generation = context.generation;
        }
        auto* payload = new PrismaPayload{&context, generation, view, nullptr, {},
                                          UiHostEvent::DomReady, true, false};
        if (!QueuePrismaPayload(context, payload))
            logger::warn("[Dashboard] Could not schedule PrismaUI DOM-ready callback");
    }

    PRISMA_UI_API::IVPrismaUI1* api_;
    PrismaView view_ = 0;
    const MAGELIGHT_API::MagelightApi4* magelight_ = nullptr;
};

struct MagelightContext {
    std::mutex mutex;
    const MAGELIGHT_API::MagelightApi4* api = nullptr;
    MAGELIGHT_API::ModId mod = 0;
    MAGELIGHT_API::ViewId view = 0;
    std::uint64_t generation = 1;
    bool adopted = false;
    MAGELIGHT_API::ViewId earlyReadyView = 0;
    bool rawReady = false;
    bool domReadyDispatched = false;
    bool listenersInstalled = false;
    bool bootstrapRequested = false;
    bool shuttingDown = false;
    bool dead = false;
    bool openPending = false;
    bool exitPending = false;
    bool reopenPending = false;
    bool pendingReload = false;
    bool pendingDestroy = false;
    bool pendingCreate = false;
    bool forceClosePending = false;
    bool reloadRequested = false;
    MAGELIGHT_API::ViewId destroyingView = 0;
    std::condition_variable callbacksDrained;
    std::size_t activeCallbacks = 0;
    std::vector<UiListener> listeners;
    UiHostEventSink sink;
};

MagelightContext& ProcessContext()
{
    static auto* context = new MagelightContext;
    return *context;
}
struct EventCallbackGuard {
    explicit EventCallbackGuard(MagelightContext& context) : context(context)
    {
        std::lock_guard lock(context.mutex);
        if (!context.shuttingDown) {
            ++context.activeCallbacks;
            active = true;
        }
    }

    ~EventCallbackGuard()
    {
        if (!active) return;
        std::lock_guard lock(context.mutex);
        if (--context.activeCallbacks == 0) context.callbacksDrained.notify_all();
    }

    explicit operator bool() const { return active; }

    MagelightContext& context;
    bool active = false;
};

struct PostedPageCallback {
    MagelightContext* context;
    std::uint64_t generation;
    MAGELIGHT_API::ViewId view;
    std::string argument;
    bool domReady;
    void (*callback)(const char*) = nullptr;
};

void PostPageCallback(void* raw)
{
    std::unique_ptr<PostedPageCallback> payload(static_cast<PostedPageCallback*>(raw));
    auto& context = *payload->context;
    EventCallbackGuard callbackGuard(context);
    if (!callbackGuard) return;
    bool bootstrap = false;
    {
        std::lock_guard lock(context.mutex);
        if (context.shuttingDown || payload->generation != context.generation) return;
        if (payload->domReady) {
            if (!context.adopted) {
                context.earlyReadyView = payload->view;
                return;
            }
            if (payload->view != context.view) return;
            if (!context.rawReady) {
                context.rawReady = true;
                if (context.listenersInstalled && !context.bootstrapRequested) {
                    context.bootstrapRequested = true;
                    bootstrap = true;
                }
            } else if (context.bootstrapRequested) {
                if (context.pendingReload || context.pendingDestroy) return;
                context.domReadyDispatched = true;
                if (context.sink) context.sink(UiHostEvent::DomReady);
            }
        } else {
            if (!context.adopted || payload->view != context.view || !context.listenersInstalled) return;
        }
    }
    if (payload->domReady) {
        if (bootstrap) {
            const auto result = context.api->ReloadView(payload->view);
            if (result != MAGELIGHT_API::Result::Ok) {
                {
                    std::lock_guard lock(context.mutex);
                    if (payload->generation == context.generation) {
                        context.bootstrapRequested = false;
                        if (context.reopenPending) {
                            context.reopenPending = false;
                            if (context.sink) context.sink(UiHostEvent::Refused);
                        }
                    }
                }
                logger::warn("[Dashboard] Magelight bootstrap ReloadView failed: {}",
                             ErrorText(context.api, context.mod, result));
            }
        }
    } else {
        if (payload->callback) payload->callback(payload->argument.c_str());
    }
}

void RawDomReady(MAGELIGHT_API::ViewId view)
{
    auto& context = ProcessContext();
    auto* payload = new PostedPageCallback{};
    payload->context = &context;
    {
        std::lock_guard lock(context.mutex);
        payload->generation = context.generation;
        payload->view = view;
        payload->domReady = true;
    }
    const auto result = context.api->PostGameTask(&PostPageCallback, payload);
    if (result != MAGELIGHT_API::Result::Ok) {
        delete payload;
        logger::warn("[Dashboard] Magelight PostGameTask for DOM-ready failed: {}",
                     ErrorText(context.api, context.mod, result));
    }
}

struct ListenerContext {
    MagelightContext* context;
    std::uint64_t generation;
    const char* name;
    void (*callback)(const char*);
};

// Listener contexts are process-lifetime: the API exposes no unregister operation.
std::vector<ListenerContext*>& ListenerContexts()
{
    static auto* contexts = new std::vector<ListenerContext*>;
    return *contexts;
}

void RawListener(MAGELIGHT_API::ViewId view, const char* argument, void* user)
{
    const auto* callback = static_cast<const ListenerContext*>(user);
    auto* context = callback->context;
    auto* payload = new PostedPageCallback{};
    payload->context = context;
    payload->generation = callback->generation;
    payload->view = view;
    payload->callback = callback->callback;
    payload->argument = argument ? argument : "{}";
    payload->domReady = false;
    const auto result = context->api->PostGameTask(&PostPageCallback, payload);
    if (result != MAGELIGHT_API::Result::Ok) {
        delete payload;
        logger::warn("[Dashboard] Magelight PostGameTask for '{}' failed: {}",
                     callback->name, ErrorText(context->api, context->mod, result));
    }
}

void OnMagelightEvent(const MAGELIGHT_API::EventData* event, void* user)
{
    if (!event || !user) return;
    auto* context = static_cast<MagelightContext*>(user);
    EventCallbackGuard callbackGuard(*context);
    if (!callbackGuard) return;

    const UiHostEventSink* sink = nullptr;
    UiHostEvent translated{};
    bool notify = true;
    {
        std::lock_guard lock(context->mutex);
        if (context->shuttingDown) return;
        if (context->pendingDestroy && event->type == MAGELIGHT_API::Event::ViewDestroyed &&
            event->view == context->destroyingView) {
            context->pendingDestroy = false;
            context->destroyingView = 0;
            context->pendingCreate = true;
            notify = false;
        } else if (event->type == MAGELIGHT_API::Event::RenderDead) {
            context->dead = true;
            context->openPending = false;
            context->exitPending = false;
            context->reopenPending = false;
            context->forceClosePending = true;
            translated = UiHostEvent::Unavailable;
        } else if (event->type == MAGELIGHT_API::Event::ViewLoadFailed &&
                   context->adopted && event->view == context->view) {
            context->dead = true;
            context->openPending = false;
            context->exitPending = false;
            context->reopenPending = false;
            context->forceClosePending = true;
            translated = UiHostEvent::Unavailable;
        } else {
            if (!context->adopted || event->view != context->view) return;
            switch (event->type) {
            case MAGELIGHT_API::Event::UIModeEntered:
                context->openPending = false;
                if (context->exitPending) {
                    context->forceClosePending = true;
                    notify = false;
                } else {
                    translated = UiHostEvent::Entered;
                }
                break;
            case MAGELIGHT_API::Event::UIModeExited:
                context->openPending = false;
                context->exitPending = false;
                context->forceClosePending = true;
                if (context->reopenPending) {
                    notify = false;
                } else {
                    translated = UiHostEvent::Exited;
                }
                break;
            case MAGELIGHT_API::Event::UIModeRefused:
                context->openPending = false;
                if (context->exitPending) {
                    context->exitPending = false;
                    notify = false;
                } else {
                    context->reopenPending = false;
                    translated = UiHostEvent::Refused;
                }
                break;
            default:
                notify = false;
                break;
            }
        }
        if (context->pendingReload &&
            (event->type == MAGELIGHT_API::Event::UIModeExited ||
             event->type == MAGELIGHT_API::Event::UIModeRefused))
            context->reloadRequested = true;
        sink = &context->sink;
    }
    if (notify && sink && *sink) (*sink)(translated);
}

void OnMagelightLog(int level, const char* message, void*)
{
    const char* text = message ? message : "";
    if (level >= 2) logger::error("[Dashboard][Magelight] {}", text);
    else if (level == 1) logger::warn("[Dashboard][Magelight] {}", text);
    else logger::info("[Dashboard][Magelight] {}", text);
}

bool MakeUtf8DataPath(std::string& path)
{
    wchar_t cwd[MAX_PATH]{};
    const DWORD length = GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!length || length >= MAX_PATH) return false;
    std::wstring full(cwd, length);
    full += L"\\Data\\Magelight\\IntelEngine\\dashboard\\index.html";
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full.c_str(), -1,
                                          nullptr, 0, nullptr, nullptr);
    if (!bytes) return false;
    path.resize(static_cast<std::size_t>(bytes));
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full.c_str(), -1,
                             path.data(), bytes, nullptr, nullptr)) return false;
    path.pop_back();
    return true;
}

class MagelightHost final : public IUiHost {
public:
    MagelightHost(const MAGELIGHT_API::MagelightApi4* api, std::span<const UiListener> listeners,
                  UiHostEventSink sink, PRISMA_UI_API::IVPrismaUI1* prisma)
        : context_(ProcessContext()), api_(api), prisma_(prisma)
    {
        std::lock_guard lock(context_.mutex);
        ++context_.generation;
        context_.api = api;
        context_.listeners.assign(listeners.begin(), listeners.end());
        context_.sink = std::move(sink);
        context_.shuttingDown = false;
        context_.adopted = false;
        context_.earlyReadyView = 0;
        context_.rawReady = false;
        context_.domReadyDispatched = false;
        context_.listenersInstalled = false;
        context_.bootstrapRequested = false;
        context_.dead = false;
        context_.openPending = false;
        context_.exitPending = false;
        context_.reopenPending = false;
        context_.pendingReload = false;
        context_.pendingDestroy = false;
        context_.pendingCreate = false;
        context_.forceClosePending = false;
        context_.reloadRequested = false;
    }

    UiHostKind Kind() const noexcept override { return UiHostKind::Magelight; }
    bool IsAvailable() const noexcept override
    {
        std::lock_guard lock(context_.mutex);
        return available_ && !context_.dead;
    }


    bool Create() override
    {
        if (view_) return IsAvailable();
        std::uint64_t generation;
        {
            std::lock_guard lock(context_.mutex);
            if (context_.dead) return false;
            generation = context_.generation;
            context_.earlyReadyView = 0;
        }
        if (!mod_) {
            MAGELIGHT_API::ModDesc modDesc{};
            modDesc.size = sizeof(modDesc);
            modDesc.modId = "IntelEngine";
            modDesc.displayName = "IntelEngine Dashboard";
            modDesc.modVersion = kPluginVersionNumber;
            modDesc.minHostVersion = kMinimumHostVersion;
            modDesc.callbackThread = MAGELIGHT_API::CallbackThread::GameThread;
            modDesc.onEvent = &OnMagelightEvent;
            modDesc.onLog = &OnMagelightLog;
            modDesc.user = &context_;
            modDesc.sessionName = "isolated";
            MAGELIGHT_API::ModId mod{};
            const auto registered = api_->RegisterMod(&modDesc, &mod);
            if (registered != MAGELIGHT_API::Result::Ok) {
                logger::warn("[Dashboard] Magelight RegisterMod failed: {}", ErrorText(api_, mod, registered));
                mod_ = mod;
                CleanupRegistration();
                return false;
            }
            mod_ = mod;
            {
                std::lock_guard lock(context_.mutex);
                context_.mod = mod_;
            }
            const auto policy = api_->SetNetworkPolicy(mod_, MAGELIGHT_API::NetworkPolicy::FileOnly);
            if (policy != MAGELIGHT_API::Result::Ok) {
                logger::warn("[Dashboard] Magelight FileOnly policy failed: {}", ErrorText(api_, mod_, policy));
                CleanupRegistration();
                return false;
            }
        }
        bool dead = false;
        {
            std::lock_guard lock(context_.mutex);
            dead = context_.dead;
        }
        if (dead) {
            CleanupRegistration();
            return false;
        }
        std::string path;
        if (!MakeUtf8DataPath(path)) {
            logger::error("[Dashboard] Cannot construct UTF-8 Magelight dashboard path");
            CleanupRegistration();
            return false;
        }
        MAGELIGHT_API::ViewDesc desc{};
        desc.size = sizeof(desc);
        desc.name = "dashboard";
        desc.htmlPath = path.c_str();
        desc.anchor = MAGELIGHT_API::Anchor::TopLeft;
        desc.fullscreen = true;
        desc.clickThrough = false;
        desc.startVisible = false;
        desc.layer = MAGELIGHT_API::Layer::Panel;
        desc.uiScale = 0.0f;
        desc.onDomReady = &RawDomReady;
        MAGELIGHT_API::ViewId view{};
        const auto created = api_->CreateViewEx(mod_, &desc, &view);
        if (created == MAGELIGHT_API::Result::NotReady) {
            logger::info("[Dashboard] Magelight registered; view creation waits for renderer readiness");
            {
                std::lock_guard lock(context_.mutex);
                available_ = true;
            }
            return true;
        }
        if (created != MAGELIGHT_API::Result::Ok || !view) {
            logger::warn("[Dashboard] Magelight CreateViewEx failed: {}", ErrorText(api_, mod_, created));
            view_ = view;
            CleanupRegistration();
            return false;
        }
        view_ = view;
        EnableGamepad();
        {
            std::lock_guard lock(context_.mutex);
            context_.view = view_;
            context_.adopted = true;
            context_.rawReady = context_.earlyReadyView == view_;
            context_.earlyReadyView = 0;
        }
        for (const auto& listener : context_.listeners) {
            auto* callback = new ListenerContext{&context_, generation, listener.name, listener.callback};
            ListenerContexts().push_back(callback);
            const auto result = api_->RegisterJSListenerEx(view_, listener.name, &RawListener, callback);
            if (result != MAGELIGHT_API::Result::Ok) {
                logger::warn("[Dashboard] Magelight listener '{}' registration failed: {}", listener.name,
                             ErrorText(api_, mod_, result));
                CleanupRegistration();
                return false;
            }
        }
        bool bootstrap = false;
        {
            std::lock_guard lock(context_.mutex);
            context_.listenersInstalled = true;
            if (context_.rawReady && !context_.bootstrapRequested) {
                context_.rawReady = true;
                context_.bootstrapRequested = true;
                bootstrap = true;
            }
        }
        {
            std::lock_guard lock(context_.mutex);
            available_ = true;
        }
        if (bootstrap) {
            const auto result = api_->ReloadView(view_);
            if (result != MAGELIGHT_API::Result::Ok) {
                {
                    std::lock_guard lock(context_.mutex);
                    if (context_.generation == generation) context_.bootstrapRequested = false;
                }
                logger::warn("[Dashboard] Magelight bootstrap ReloadView failed: {}",
                             ErrorText(api_, mod_, result));
            }
        }
        return true;
    }

    UiOpenResult Open() override
    {
        if (!Create()) return UiOpenResult::Refused;
        if (!view_) return UiOpenResult::Refused;
        bool retryBootstrap = false;
        {
            std::lock_guard lock(context_.mutex);
            if (context_.exitPending || context_.pendingReload || context_.pendingDestroy ||
                context_.forceClosePending) {
                context_.reopenPending = true;
                return UiOpenResult::Pending;
            }
            if (context_.openPending) return UiOpenResult::Pending;
            if (!context_.domReadyDispatched &&
                !(context_.rawReady && context_.listenersInstalled && !context_.bootstrapRequested)) {
                context_.reopenPending = true;
                return UiOpenResult::Pending;
            }
            if (context_.rawReady && context_.listenersInstalled && !context_.bootstrapRequested) {
                context_.bootstrapRequested = true;
                context_.reopenPending = true;
                retryBootstrap = true;
            }
        }
        if (retryBootstrap) {
            const auto result = api_->ReloadView(view_);
            if (result != MAGELIGHT_API::Result::Ok) {
                {
                    std::lock_guard lock(context_.mutex);
                    context_.bootstrapRequested = false;
                    context_.reopenPending = false;
                }
                logger::warn("[Dashboard] Magelight bootstrap retry failed: {}",
                             ErrorText(api_, mod_, result));
                return UiOpenResult::Refused;
            }
            return UiOpenResult::Pending;
        }
        if (prisma_ && prisma_->HasAnyActiveFocus()) {
            logger::debug("[Dashboard] Magelight open refused due to PrismaUI focus ownership");
            return UiOpenResult::Refused;
        }
        {
            std::lock_guard lock(context_.mutex);
            context_.openPending = true;
            context_.reopenPending = false;
        }
        const auto result = api_->RequestUIMode(view_, MAGELIGHT_API::kUIModeFlagPause);
        if (result == MAGELIGHT_API::Result::Ok) return UiOpenResult::Pending;
        {
            std::lock_guard lock(context_.mutex);
            context_.openPending = false;
        }
        if (result == MAGELIGHT_API::Result::Busy || result == MAGELIGHT_API::Result::Denied)
            logger::debug("[Dashboard] Magelight UI mode refused due to input ownership");
        else
            logger::warn("[Dashboard] Magelight RequestUIMode refused: {}", ErrorText(api_, mod_, result));
        return UiOpenResult::Refused;
    }

    void Close() override { CloseImpl(false); }

    void Reload() override
    {
        if (!api_ || !view_ || !IsAvailable()) return;
        const bool ownsMode = api_->GetUIModeOwner && api_->GetUIModeOwner() == mod_;
        bool defer = false;
        {
            std::lock_guard lock(context_.mutex);
            if (context_.pendingDestroy) return;
            defer = ownsMode || context_.openPending || context_.exitPending;
            if (defer) context_.pendingReload = true;
        }
        if (defer) {
            CloseImpl(true);
            return;
        }
        ReloadNow();
    }

    UiOpenResult Service() override
    {
        bool close = false;
        bool reload = false;
        bool create = false;
        bool reopen = false;
        {
            std::lock_guard lock(context_.mutex);
            close = std::exchange(context_.forceClosePending, false);
            reload = std::exchange(context_.reloadRequested, false);
            create = context_.pendingCreate && !context_.pendingDestroy;
            if (create) context_.pendingCreate = false;
            reopen = context_.reopenPending && !context_.exitPending &&
                     !context_.pendingReload && !context_.pendingDestroy &&
                     context_.domReadyDispatched;
            if (reopen) context_.reopenPending = false;
        }
        if (close) {
            if (api_->GetUIModeOwner && api_->GetUIModeOwner() == mod_)
                api_->ReleaseUIMode(mod_);
            if (view_) api_->ShowView(view_, false);
        }
        if (reload) ReloadNow();
        else if (create) CreateAfterDestroy();
        if (reopen) return Open();
        return UiOpenResult::Pending;
    }

    bool Call(std::string_view name, std::string_view argument) override
    {
        if (!IsAvailable() || !view_) return false;
        std::string fn(name), arg(argument);
        api_->InteropCall(view_, fn.c_str(), arg.c_str());
        return true;
    }

    void Shutdown() override
    {
        if (!api_) return;
        {
            std::unique_lock lock(context_.mutex);
            context_.shuttingDown = true;
            ++context_.generation;
            context_.callbacksDrained.wait(lock, [&] { return context_.activeCallbacks == 0; });
            available_ = false;
        }
        if (mod_) api_->UnregisterMod(mod_);
        mod_ = 0;
        view_ = 0;
    }

private:
    void CloseImpl(bool preserveReopen)
    {
        if (!api_ || !mod_ || !view_) {
            std::lock_guard lock(context_.mutex);
            if (!preserveReopen) context_.reopenPending = false;
            return;
        }
        const bool ownsMode = api_->GetUIModeOwner && api_->GetUIModeOwner() == mod_;
        {
            std::lock_guard lock(context_.mutex);
            if (!preserveReopen) context_.reopenPending = false;
            if (ownsMode || context_.openPending) context_.exitPending = true;
        }
        const auto released = api_->ReleaseUIMode(mod_);
        if (released != MAGELIGHT_API::Result::Ok && released != MAGELIGHT_API::Result::Denied)
            logger::warn("[Dashboard] Magelight ReleaseUIMode: {}", ErrorText(api_, mod_, released));
        api_->ShowView(view_, false);
    }
    void CleanupRegistration()
    {
        {
            std::unique_lock lock(context_.mutex);
            context_.shuttingDown = true;
            ++context_.generation;
            context_.dead = true;
            context_.adopted = false;
            context_.pendingCreate = false;
            context_.pendingDestroy = false;
            context_.pendingReload = false;
            context_.reloadRequested = false;
            context_.reopenPending = false;
            context_.callbacksDrained.wait(lock, [&] { return context_.activeCallbacks == 0; });
            context_.sink = {};
        }
        if (view_) {
            const auto result = api_->DestroyView(view_);
            if (result != MAGELIGHT_API::Result::Ok)
                logger::warn("[Dashboard] Magelight cleanup DestroyView failed: {}",
                             ErrorText(api_, mod_, result));
        }
        if (mod_) api_->UnregisterMod(mod_);
        view_ = 0;
        mod_ = 0;
        std::lock_guard lock(context_.mutex);
        context_.mod = 0;
        context_.view = 0;
        available_ = false;
    }

    void ReloadNow()
    {
        if (!view_) return;
        {
            std::lock_guard lock(context_.mutex);
            if (context_.pendingDestroy) return;
            context_.pendingReload = false;
            ++context_.generation;
            context_.adopted = false;
            context_.earlyReadyView = 0;
            context_.rawReady = false;
            context_.domReadyDispatched = false;
            context_.listenersInstalled = false;
            context_.bootstrapRequested = false;
            context_.pendingDestroy = true;
            context_.destroyingView = view_;
        }
        const auto destroyed = api_->DestroyView(view_);
        if (destroyed != MAGELIGHT_API::Result::Ok) {
            logger::warn("[Dashboard] Magelight DestroyView before reload failed: {}",
                         ErrorText(api_, mod_, destroyed));
            std::lock_guard lock(context_.mutex);
            context_.pendingDestroy = false;
            context_.pendingCreate = false;
            context_.dead = true;
            available_ = false;
        }
    }

    void CreateAfterDestroy()
    {
        view_ = 0;
        {
            std::lock_guard lock(context_.mutex);
            available_ = false;
            context_.view = 0;
            context_.adopted = false;
            context_.pendingCreate = false;
        }
        if (!Create()) {
            std::lock_guard lock(context_.mutex);
            context_.dead = true;
        }
    }

    MagelightContext& context_;
    const MAGELIGHT_API::MagelightApi4* api_ = nullptr;
    PRISMA_UI_API::IVPrismaUI1* prisma_ = nullptr;
    MAGELIGHT_API::ModId mod_ = 0;
    // Controller: let Magelight give the pad to this view while it is open. In navigation style the host moves a
    // selection between the page's controls (window.magelight.gamepad in the page attaches the panel as a scope):
    // A selects, B goes back or closes, the shoulders turn the pages, the right stick scrolls, and a right-stick
    // click switches to a stick-driven cursor. The layer is absent before Magelight 1.6, in VR (where the hands
    // point at the panel) and when the player switched it off: then nothing changes.
    void EnableGamepad() {
        if (!api_ || !view_ || !api_->SetViewGamepad || !api_->QueryCapability) return;
        if (api_->hostVersionNumber < 10600 || api_->QueryCapability("gamepad") != 1) {
            logger::info("[Dashboard] Magelight controller layer not available (VR, switched off, or an older host)");
            return;
        }
        MAGELIGHT_API::GamepadConfig config{};
        config.size = sizeof(config);
        config.enabled = true;
        config.style = MAGELIGHT_API::kGamepadStyleNavigation;
        config.allowCursor = true;
        config.startInCursorMode = false;
        const auto result = api_->SetViewGamepad(view_, &config);
        if (result == MAGELIGHT_API::Result::Ok)
            logger::info("[Dashboard] Magelight controller navigation enabled");
        else
            logger::info("[Dashboard] Magelight controller navigation refused: {}", ErrorText(api_, mod_, result));
    }
    MAGELIGHT_API::ViewId view_ = 0;
    bool available_ = false;
};

struct Candidate {
    std::unique_ptr<IUiHost> host;
    std::string reason;
    std::string version;
};

const MAGELIGHT_API::MagelightApi4* ResolveMagelightApi(const UiHostApiResolver& resolver,
                                                        std::string* reason = nullptr)
{
    if (!resolver.getModuleHandle || !resolver.getProcAddress) {
        if (reason) *reason = "Magelight resolver unavailable";
        return nullptr;
    }
    auto* module = resolver.getModuleHandle(L"Magelight.dll");
    if (!module) {
        if (reason) *reason = "Magelight.dll absent";
        return nullptr;
    }
    using RequestFn = const void* (*)(std::uint32_t);
    const auto request = reinterpret_cast<RequestFn>(resolver.getProcAddress(module, "Magelight_RequestApi"));
    if (!request) {
        if (reason) *reason = "Magelight_RequestApi export missing";
        return nullptr;
    }
    const auto* api = static_cast<const MAGELIGHT_API::MagelightApi4*>(request(kMagelightApiVersion));
    if (!api) {
        if (reason) *reason = "Magelight API v4 unavailable";
        return nullptr;
    }
    if (api->apiVersion != kMagelightApiVersion) {
        if (reason) *reason = "Magelight API version mismatch";
        return nullptr;
    }
    if (reason) *reason = "detected";
    return api;
}

bool IsUsableMagelight(const MAGELIGHT_API::MagelightApi4* api, std::string& reason)
{
    if (!api) return false;
    if (api->hostVersionNumber < kMinimumHostVersion) {
        reason = "Magelight host below required 1.7.1 (10701)";
        return false;
    }
    if (!api->PostGameTask) {
        reason = "Magelight safe PostGameTask unavailable";
        return false;
    }
    reason = "usable";
    return true;
}


void* WinGetModuleHandle(const wchar_t* name) { return GetModuleHandleW(name); }
void* WinGetProcAddress(void* module, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}
void* RequestPrismaApi()
{
    return PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1);
}
bool PostSkseTask(void (*callback)(void*), void* user)
{
    auto* task = SKSE::GetTaskInterface();
    if (!task) return false;
    task->AddTask([callback, user] { callback(user); });
    return true;

}
} // namespace

std::unique_ptr<IUiHost> CreateUiHost(std::string_view preference,
                                      std::span<const UiListener> listeners,
                                      UiHostEventSink eventSink)
{
    const UiHostApiResolver resolver{&WinGetModuleHandle, &WinGetProcAddress, &RequestPrismaApi, &PostSkseTask};
    return CreateUiHostWithResolver(preference, listeners, std::move(eventSink), resolver);
}

std::unique_ptr<IUiHost> CreateUiHostWithResolver(std::string_view preference,
                                                 std::span<const UiListener> listeners,
                                                 UiHostEventSink eventSink,
                                                 const UiHostApiResolver& resolver)
{
    std::string requested = Lower(preference);
    if (requested != "auto" && requested != "magelight" && requested != "prismaui") {
        logger::warn("[Dashboard] Unknown ui.host '{}'; using auto", requested);
        requested = "auto";
    }
    const bool preferPrisma = requested == "prismaui";
    std::string magelightReason;
    const auto* magelightProbe = ResolveMagelightApi(resolver, &magelightReason);
    const auto detectedMagelightVersion = magelightProbe ?
        std::to_string(magelightProbe->hostVersionNumber) : "unavailable";
    const MAGELIGHT_API::MagelightApi4* magelightApi = magelightProbe;
    if (!IsUsableMagelight(magelightProbe, magelightReason)) magelightApi = nullptr;
    PRISMA_UI_API::IVPrismaUI1* prisma = nullptr;
    std::string prismaReason = "PrismaUI unavailable";
    if (resolver.requestPrismaApi) {
        try {
            prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(resolver.requestPrismaApi());
            if (prisma) prismaReason = "usable";
        } catch (...) {
            prismaReason = "PrismaUI API acquisition failed";
        }
    }
    auto tryMagelight = [&]() -> Candidate {
        if (!magelightApi) return {{}, magelightReason, "unavailable"};
        return {std::make_unique<MagelightHost>(magelightApi, listeners, eventSink, prisma), "usable",
                std::to_string(magelightApi->hostVersionNumber)};
    };
    if (preferPrisma && prisma) {
        auto host = std::make_unique<PrismaHost>(prisma, listeners, eventSink, magelightProbe, resolver.postTask);
        if (host->Create()) {
            logger::info("[Dashboard] host preference={}, selected=PrismaUI, magelightVersion={}, reason=explicit preference",
                         requested, detectedMagelightVersion);
            return host;
        }
        prismaReason = "PrismaUI view creation failed";
    }
    if (!preferPrisma) {
        auto magelight = tryMagelight();
        if (magelight.host && magelight.host->Create()) {
            logger::info("[Dashboard] host preference={}, selected=Magelight, version={}, reason=preferred usable host",
                         requested, magelight.version);
            return std::move(magelight.host);
        }
        if (magelight.host) magelightReason = "Magelight registration or view creation failed";
    }
    if (prisma && !preferPrisma) {
        auto host = std::make_unique<PrismaHost>(prisma, listeners, eventSink, magelightProbe, resolver.postTask);
        if (host->Create()) {
            logger::info("[Dashboard] host preference={}, selected=PrismaUI, magelightVersion={}, reason={}",
                         requested, detectedMagelightVersion, magelightReason);
            return host;
        }
        prismaReason = "PrismaUI view creation failed";
    }
    if (preferPrisma) {
        auto magelight = tryMagelight();
        if (magelight.host && magelight.host->Create()) {
            logger::info("[Dashboard] host preference={}, selected=Magelight, version={}, reason=preferred PrismaUI unavailable: {}",
                         requested, magelight.version, prismaReason);
            return std::move(magelight.host);
        }
        if (magelight.host) magelightReason = "Magelight registration or view creation failed";
    }
    logger::info("[Dashboard] host preference={}, selected=None, magelightVersion={}, reason={}; {}",
                 requested, detectedMagelightVersion, magelightReason, prismaReason);
    return {};
}

} // namespace IntelEngine
