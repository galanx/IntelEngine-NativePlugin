"""Offline behavioral check for the production dashboard UI hosts and manager callbacks.

Run from the repository root with: python SKSE/tests/RunDashboardHostCheck.py
Requires clang++ and the project nlohmann-json headers (vcpkg).
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKSE = ROOT / "SKSE"
SRC = SKSE / "src"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    quote = None
    escaped = False
    line_comment = False
    block_comment = False
    i = brace
    while i < len(source):
        c = source[i]
        n = source[i + 1] if i + 1 < len(source) else ""
        if line_comment:
            if c == "\n":
                line_comment = False
        elif block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 1
        elif quote:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == quote:
                quote = None
        elif c == "/" and n == "/":
            line_comment = True
            i += 1
        elif c == "/" and n == "*":
            block_comment = True
            i += 1
        elif c in ('"', "'"):
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
        i += 1
    raise ValueError(f"unterminated method: {signature}")


manager = (SRC / "DashboardUIManager.cpp").read_text(encoding="utf-8")
has_generate = "    void DashboardUIManager::OnGenerateDirectorStatic(" in manager
listener_count = 15 + int(has_generate)
listener_start = manager.index("        static const UiListener listeners[] = {")
listener_end = manager.index("        };", listener_start)
listener_entries = manager[manager.index("{", listener_start) + 1:listener_end]
for index, callback in enumerate((
    "OnCloseStatic", "OnRequestRefreshStatic", "OnReloadUIStatic",
    "OnCancelTaskStatic", "OnCancelScheduleStatic", "OnCancelQuestStatic",
    "OnToggleStoryTypeStatic", "OnChangeSettingStatic", "OnRemovePackagesStatic",
    "OnChangePluginConfigStatic", "OnDispatchStoryStatic", "OnDispatchNpcSocialStatic",
    "OnDispatchPoliticsStatic", "OnExecuteActionStatic", "OnToggleActionStatic",
    "OnGenerateDirectorStatic",
)):
    listener_entries = listener_entries.replace(callback, f"listener{index}")
callbacks = [method(manager, sig) for sig in (
    "    static std::optional<std::uint32_t> ReadFormID(",
    "    void DashboardUIManager::Toggle()",
    "    void DashboardUIManager::RequestClose()",
    "    void DashboardUIManager::OnHostEvent(",
    "    void DashboardUIManager::PushSlotData()",
    "    void DashboardUIManager::SendSlotData(",
    "    void DashboardUIManager::PushFullState(",
    "    void DashboardUIManager::ProcessFullState(",
    "    void DashboardUIManager::FlushState(",
    "    void DashboardUIManager::OnCloseStatic(",
    "    void DashboardUIManager::OnRequestRefreshStatic(",
    "    void DashboardUIManager::OnReloadUIStatic(",
    "    void DashboardUIManager::OnCancelTaskStatic(",
    "    void DashboardUIManager::OnCancelScheduleStatic(",
    "    void DashboardUIManager::OnCancelQuestStatic(",
    "    void DashboardUIManager::OnToggleStoryTypeStatic(",
    "    void DashboardUIManager::OnChangeSettingStatic(",
    "    void DashboardUIManager::OnRemovePackagesStatic(",
    "    void DashboardUIManager::OnChangePluginConfigStatic(",
    "    void DashboardUIManager::OnDispatchStoryStatic(",
    "    void DashboardUIManager::OnDispatchNpcSocialStatic(",
    "    void DashboardUIManager::OnDispatchPoliticsStatic(",
    "    void DashboardUIManager::OnExecuteActionStatic(",
    "    void DashboardUIManager::OnToggleActionStatic(",
    "    void DashboardUIManager::OnGenerateDirectorStatic(",
    "    void DashboardUIManager::LoadActionYAMLs()",
    "    void DashboardUIManager::ReloadView()",
    "    void DashboardUIManager::SendModEvent(",
    "    void DashboardUIManager::HotkeyPollLoop()",
    ) if has_generate or "OnGenerateDirectorStatic" not in sig]

with tempfile.TemporaryDirectory(prefix="dashboard-host-") as temp:
    work = pathlib.Path(temp)
    (work / "actions").mkdir()
    (work / "actions/report.yaml").write_bytes(
        (ROOT / "SKSE/Plugins/SkyrimNet/external/galanx.intelengine/actions/reportplayerconduct.yaml").read_bytes())
    (work / "UiHostTestSupport.h").write_text(r'''#pragma once
#define INTELENGINE_VERSION "3.6.2"
#include "Windows.h"
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
namespace SKSE {
using PluginHandle = std::uint32_t;
struct TaskInterface { template<class F> void AddTask(F&& f) { f(); } };
inline TaskInterface* GetTaskInterface() { static TaskInterface task; return &task; }
}
#include "PrismaUI_API.h"
namespace logger {
inline std::vector<std::string> infoMessages;
inline std::vector<std::string> generateMessages;
template<class... T> void info(const char* message, T&&... args) {
    infoMessages.emplace_back(message);
    if (std::string(message).find("[Dashboard] Generate ") == 0) {
        std::ostringstream line; line << message; ((line << ' ' << args), ...);
        generateMessages.push_back(line.str());
    }
}
template<class... T> void warn(const char*, T&&...) {}
template<class... T> void error(const char*, T&&...) {}
template<class... T> void debug(const char*, T&&...) {}
}
''', encoding="utf-8")
    (work / "Windows.h").write_text(r'''#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#define GetModuleHandleW TestGetModuleHandleW
#define GetProcAddress TestGetProcAddress
#define GetCurrentDirectoryW TestGetCurrentDirectoryW
#define WideCharToMultiByte TestWideCharToMultiByte
#define GetForegroundWindow TestGetForegroundWindow
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define GetCurrentProcessId TestGetCurrentProcessId
#define GetAsyncKeyState TestGetAsyncKeyState
using HMODULE = void*;
using HWND = void*;
using DWORD = unsigned long;
using UINT = unsigned int;
using BOOL = int;
using LPCWCH = const wchar_t*;
using LPSTR = char*;
using LPCCH = const char*;
using LPWSTR = wchar_t*;
constexpr int MAX_PATH = 260;
constexpr int VK_CONTROL=0x11, VK_SHIFT=0x10, VK_MENU=0x12;
constexpr UINT CP_UTF8 = 65001;
constexpr DWORD WC_ERR_INVALID_CHARS = 0x80;
extern "C" HMODULE GetModuleHandleW(const wchar_t*);
extern "C" void* GetProcAddress(HMODULE, const char*);
extern "C" DWORD GetCurrentDirectoryW(DWORD, LPWSTR);
extern "C" int WideCharToMultiByte(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, BOOL*);
extern "C" HWND GetForegroundWindow();
extern "C" DWORD GetWindowThreadProcessId(HWND, DWORD*);
extern "C" DWORD GetCurrentProcessId();
extern "C" short GetAsyncKeyState(int);

''', encoding="utf-8")
    vcpkg = pathlib.Path(os.environ.get("VCPKG_ROOT", "")) if os.environ.get("VCPKG_ROOT") else None
    include_candidates = [SKSE / "vcpkg_installed" / "x64-windows" / "include"]
    if len(sys.argv) > 1:
        include_candidates.insert(0, pathlib.Path(sys.argv[1]) / "include")
    if vcpkg:
        include_candidates.append(vcpkg / "installed" / "x64-windows" / "include")
    include_candidates += [pathlib.Path("C:/vcpkg/installed/x64-windows/include"),
                           pathlib.Path("C:/Users/vector/vcpkg/installed/x64-windows/include")]
    json_include = next((p for p in include_candidates if (p / "nlohmann/json.hpp").exists()), None)
    if json_include is None:
        raise FileNotFoundError("nlohmann/json.hpp not found; set VCPKG_ROOT to the project's vcpkg root")

    cpp = work / "check.cpp"
    cpp.write_text(r'''#ifdef NDEBUG
#undef NDEBUG
#endif
#include "UiHostTestSupport.h"
#include "Windows.h"
#include "UiHost.h"
#include "PrismaUI_API.h"
#include "MagelightUI_API.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <limits>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iterator>

#include <chrono>
namespace FakeWin {
std::wstring cwd = L"C:\\Games\\Skyrim\\雪";
}
extern "C" DWORD GetCurrentDirectoryW(DWORD cap, LPWSTR out) {
    if (FakeWin::cwd.size() + 1 > cap) return static_cast<DWORD>(FakeWin::cwd.size() + 1);
    std::copy(FakeWin::cwd.begin(), FakeWin::cwd.end(), out);
    out[FakeWin::cwd.size()] = 0;
    return static_cast<DWORD>(FakeWin::cwd.size());
}
extern "C" int WideCharToMultiByte(UINT cp, DWORD, LPCWCH in, int chars, LPSTR out, int bytes, LPCCH, BOOL*) {
    if (cp != CP_UTF8 || chars != -1) return 0;
    std::string s;
    for (const wchar_t* p = in; *p; ++p) {
        const unsigned cpv = static_cast<unsigned>(*p);
        if (cpv <= 0x7f) s.push_back(static_cast<char>(cpv));
        else if (cpv <= 0x7ff) { s.push_back(char(0xc0 | (cpv >> 6))); s.push_back(char(0x80 | (cpv & 63))); }
        else { s.push_back(char(0xe0 | (cpv >> 12))); s.push_back(char(0x80 | ((cpv >> 6) & 63))); s.push_back(char(0x80 | (cpv & 63))); }
    }
    if (!out) return static_cast<int>(s.size() + 1);
    if (bytes < static_cast<int>(s.size() + 1)) return 0;
    std::memcpy(out, s.data(), s.size()); out[s.size()] = 0;
    return static_cast<int>(s.size() + 1);
}
extern "C" HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
extern "C" void* GetProcAddress(HMODULE, const char*) { return nullptr; }
std::atomic<bool>* stopOnForeground=nullptr;
extern "C" HWND GetForegroundWindow() { if (stopOnForeground) stopOnForeground->store(true); return reinterpret_cast<HWND>(1); }
extern "C" DWORD GetWindowThreadProcessId(HWND,DWORD* process) { *process=1; return 1; }
extern "C" DWORD GetCurrentProcessId() { return 1; }
extern "C" short GetAsyncKeyState(int) { return 0; }

using namespace IntelEngine;
struct ListenerCall { std::string name, argument; };
struct EventCall { std::string name, str; float number; };
std::vector<ListenerCall> listenerCalls;
std::vector<EventCall> eventCalls;
int callbackHits = 0, routedListener = -1;
void route(int id,const char* arg) { ++callbackHits; routedListener=id; listenerCalls.push_back({std::to_string(id),arg?arg:""}); }
#define ROUTE(N) void listener##N(const char* a){route(N,a);}
ROUTE(0) ROUTE(1) ROUTE(2) ROUTE(3) ROUTE(4) ROUTE(5) ROUTE(6) ROUTE(7)
ROUTE(8) ROUTE(9) ROUTE(10) ROUTE(11) ROUTE(12) ROUTE(13) ROUTE(14) ROUTE(15)
#undef ROUTE
namespace IntelEngine::SkyrimNetAPI {
inline bool (*SendCustomPromptToLLM)(const char*, const char*, const char*,
    std::function<void(const char*, int)>) = nullptr;
}
std::function<void(const char*, int)> directorCompletion;
bool fakeDirectorSend(const char*, const char*, const char*, std::function<void(const char*, int)> cb) {
    directorCompletion = std::move(cb); return true;
}
const std::array<UiListener, ''' + str(listener_count) + r'''> listeners{{
''' + listener_entries + r'''
}};
const std::array<std::string_view, ''' + str(listener_count) + r'''> expectedNames{
''' + ",\n".join(f'"{name}"' for name in (
    "onCloseDashboard", "onRequestRefresh", "onReloadUI",
    "onDashboard_cancelTask", "onDashboard_cancelSchedule", "onDashboard_cancelQuest",
    "onDashboard_toggleStoryType", "onDashboard_changeSetting", "onDashboard_removePackages",
    "onDashboard_changePluginConfig", "onDashboard_dispatchStory", "onDashboard_dispatchNpcSocial",
    "onDashboard_dispatchPolitics", "onDashboard_executeAction", "onDashboard_toggleAction",
    *(["onDashboard_generateDirector"] if has_generate else []),
)) + r'''};

struct FakePrisma final : PRISMA_UI_API::IVPrismaUI1 {
    PrismaView view=91; bool focusResult=true, anyFocus=false, valid=true, focusPause=false;
    int order=0, shown=0, hidden=0, focused=0, unfocused=0, destroyed=0;
    std::vector<std::string> names, calls;
    std::vector<PRISMA_UI_API::JSListenerCallback> callbacks;
    PRISMA_UI_API::OnDomReadyCallback dom=nullptr;
    PrismaView CreateView(const char*,PRISMA_UI_API::OnDomReadyCallback cb) noexcept override { dom=cb; return view; }
    void Invoke(PrismaView,const char*,PRISMA_UI_API::JSCallback=nullptr) noexcept override {}
    void InteropCall(PrismaView,const char* fn,const char* arg) noexcept override { calls.push_back(std::string(fn)+":"+arg); }
    void RegisterJSListener(PrismaView,const char* n,PRISMA_UI_API::JSListenerCallback cb) noexcept override { names.emplace_back(n); callbacks.push_back(cb); }
    bool HasFocus(PrismaView) noexcept override { return focused != 0; }
    bool Focus(PrismaView,bool pause=false,bool disable=false) noexcept override { focusPause=pause; assert(!disable); ++focused; return focusResult; }
    void Unfocus(PrismaView) noexcept override { ++unfocused; focused=0; }
    void Show(PrismaView) noexcept override { ++shown; }
    void Hide(PrismaView) noexcept override { ++hidden; }
    bool IsHidden(PrismaView) noexcept override { return hidden != 0; }
    int GetScrollingPixelSize(PrismaView) noexcept override { return 0; }
    void SetScrollingPixelSize(PrismaView,int) noexcept override {}
    bool IsValid(PrismaView) noexcept override { return valid; }
    void Destroy(PrismaView) noexcept override { ++destroyed; }
    void SetOrder(PrismaView,int n) noexcept override { order=n; }
    int GetOrder(PrismaView) noexcept override { return order; }
    void CreateInspectorView(PrismaView) noexcept override {}
    void SetInspectorVisibility(PrismaView,bool) noexcept override {}
    bool IsInspectorVisible(PrismaView) noexcept override { return false; }
    void SetInspectorBounds(PrismaView,float,float,unsigned,unsigned) noexcept override {}
    bool HasAnyActiveFocus() noexcept override { return anyFocus; }
};
FakePrisma prisma;
const MAGELIGHT_API::MagelightApi4* activeApi=nullptr;
bool modulePresent=true, exportPresent=true, prismaPresent=true, deferPrismaPosts=false;
std::vector<std::pair<void (*)(void*),void*>> prismaPosts;
const void* requestMagelight(std::uint32_t version) { assert(version==4); return activeApi; }
void* fakeModule(const wchar_t* name) { return modulePresent && std::wcscmp(name,L"Magelight.dll")==0 ? reinterpret_cast<void*>(1) : nullptr; }
void* fakeProc(void*,const char* name) { return exportPresent && std::strcmp(name,"Magelight_RequestApi")==0 ? reinterpret_cast<void*>(&requestMagelight) : nullptr; }
void* fakePrisma() { return prismaPresent ? &prisma : nullptr; }
bool fakePost(void (*fn)(void*),void* user) {
    if (deferPrismaPosts) prismaPosts.emplace_back(fn,user);
    else fn(user);
    return true;
}
void drainPrismaPosts() {
    auto posts=std::move(prismaPosts); prismaPosts.clear();
    for (auto [fn,user]:posts) fn(user);
}
UiHostApiResolver resolver{fakeModule,fakeProc,fakePrisma,fakePost};

struct FakeMagelight {
    MAGELIGHT_API::MagelightApi4 api{};
    MAGELIGHT_API::ModDesc modDesc{};
    MAGELIGHT_API::ViewDesc viewDesc{};
    std::vector<std::pair<std::string, MAGELIGHT_API::JsListenerFn4>> listeners;
    std::vector<void*> listenerUsers;
    std::vector<std::string> calls;
    std::vector<std::pair<MAGELIGHT_API::GameTaskFn,void*>> posts;
    std::vector<MAGELIGHT_API::EventData> events;
    MAGELIGHT_API::DomReadyFn dom=nullptr;
    MAGELIGHT_API::EventFn event=nullptr;
    void* eventUser=nullptr;
    MAGELIGHT_API::Result registerResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result policyResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result createResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result listenerResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result requestResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result reloadResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::Result destroyResult=MAGELIGHT_API::Result::Ok;
    MAGELIGHT_API::ModId mod=17, owner=0;
    MAGELIGHT_API::ViewId view=29;
    std::uint32_t flags=0;
    int registered=0, unregistered=0, created=0, destroyed=0, reloads=0, requestCalls=0, releases=0, hidden=0;
    std::vector<std::string> uiActions;
    std::vector<std::size_t> listenerCountsAtReload;
    bool runPosts=false, domDuringCreate=false, renderDeadOnRegister=false;
    std::string capturedPath;
    static FakeMagelight* self;
    FakeMagelight(std::uint32_t version=10701) {
        self=this;
        api.apiVersion=4; api.hostVersion="test"; api.hostVersionNumber=version;
        api.RegisterMod=RegisterMod; api.UnregisterMod=UnregisterMod; api.CreateViewEx=CreateView;
        api.DestroyView=DestroyView; api.ReloadView=ReloadView; api.RegisterJSListenerEx=RegisterListener;
        api.RequestUIMode=RequestMode; api.ReleaseUIMode=ReleaseMode; api.GetUIModeOwner=GetOwner;
        api.SetNetworkPolicy=SetPolicy; api.PostGameTask=PostTask; api.GetLastErrorMessage=GetError;
        api.InteropCall=Interop; api.ShowView=Show;
    }
    static MAGELIGHT_API::Result RegisterMod(const MAGELIGHT_API::ModDesc* d, MAGELIGHT_API::ModId* m) {
        auto& f=*self; f.modDesc=*d; f.event=d->onEvent; f.eventUser=d->user; ++f.registered;
        if (f.registerResult==MAGELIGHT_API::Result::Ok) *m=f.mod;
        if (f.renderDeadOnRegister) {
            MAGELIGHT_API::EventData e{MAGELIGHT_API::Event::RenderDead,0,0,0,"dead"};
            f.event(&e,f.eventUser);
        }
        return f.registerResult;
    }
    static void UnregisterMod(MAGELIGHT_API::ModId) { ++self->unregistered; }
    static MAGELIGHT_API::Result SetPolicy(MAGELIGHT_API::ModId,MAGELIGHT_API::NetworkPolicy p) {
        assert(p==MAGELIGHT_API::NetworkPolicy::FileOnly); return self->policyResult;
    }
    static MAGELIGHT_API::Result CreateView(MAGELIGHT_API::ModId,const MAGELIGHT_API::ViewDesc* d,MAGELIGHT_API::ViewId* v) {
        auto& f=*self; f.viewDesc=*d; f.capturedPath=d->htmlPath; ++f.created; f.dom=d->onDomReady;
        if (f.createResult==MAGELIGHT_API::Result::Ok) *v=f.view;
        if (f.domDuringCreate && f.dom) f.dom(f.view);
        return f.createResult;
    }
    static MAGELIGHT_API::Result DestroyView(MAGELIGHT_API::ViewId) { ++self->destroyed; return self->destroyResult; }
    static MAGELIGHT_API::Result ReloadView(MAGELIGHT_API::ViewId) { ++self->reloads; self->listenerCountsAtReload.push_back(self->listeners.size()); return self->reloadResult; }
    static MAGELIGHT_API::Result RegisterListener(MAGELIGHT_API::ViewId,const char* n,MAGELIGHT_API::JsListenerFn4 cb,void* u) {
        self->listeners.emplace_back(n,cb); self->listenerUsers.push_back(u); return self->listenerResult;
    }
    static MAGELIGHT_API::Result RequestMode(MAGELIGHT_API::ViewId,std::uint32_t fl) {
        auto& f=*self; f.flags=fl; ++f.requestCalls; f.uiActions.push_back("request");
        if (f.requestResult==MAGELIGHT_API::Result::Ok) f.owner=f.mod;
        return f.requestResult;
    }
    static MAGELIGHT_API::Result ReleaseMode(MAGELIGHT_API::ModId mod) {
        ++self->releases; self->uiActions.push_back("release");
        if (self->owner && self->owner!=mod) return MAGELIGHT_API::Result::Denied;
        self->owner=0; return MAGELIGHT_API::Result::Ok;
    }
    static MAGELIGHT_API::ModId GetOwner() { return self->owner; }
    static MAGELIGHT_API::Result PostTask(MAGELIGHT_API::GameTaskFn fn,void* u) {
        if (self->runPosts) fn(u); else self->posts.emplace_back(fn,u);
        return MAGELIGHT_API::Result::Ok;
    }
    static const char* GetError(MAGELIGHT_API::ModId) { return "mock"; }
    static void Interop(MAGELIGHT_API::ViewId,const char* fn,const char* arg) { self->calls.push_back(std::string(fn)+":"+arg); }
    static void Show(MAGELIGHT_API::ViewId,bool visible) { if (!visible) { ++self->hidden; self->uiActions.push_back("hide"); } }
    void drain() { while(!posts.empty()) { auto p=posts.front(); posts.erase(posts.begin()); p.first(p.second); } }
    void fire(MAGELIGHT_API::Event type, MAGELIGHT_API::ViewId target=0) { MAGELIGHT_API::EventData e{type,target,0,0,""}; event(&e,eventUser); }
};
FakeMagelight* FakeMagelight::self=nullptr;

void selectTest() {
    FakeMagelight f(10700); activeApi=&f.api;
    auto h=CreateUiHostWithResolver("AUTO",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    f.api.hostVersionNumber=10701; prisma.names.clear();
    h=CreateUiHostWithResolver("auto",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::Magelight); h->Shutdown();
    for (auto pref : {"magelight","MAGELIGHT","unknown",""}) {
        prisma.names.clear(); h=CreateUiHostWithResolver(pref,listeners,[](UiHostEvent){},resolver);
        assert(h && h->Kind()==UiHostKind::Magelight); h->Shutdown();
    }
    f.api.hostVersionNumber=10600;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    f.api.hostVersionNumber=10601;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    f.api.hostVersionNumber=10701; prisma.view=0;
    h=CreateUiHostWithResolver("prismaui",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::Magelight); h->Shutdown();
    prisma.view=91; prisma.anyFocus=true; prisma.names.clear();
    h=CreateUiHostWithResolver("prismaui",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); assert(h->Open()==UiOpenResult::Refused); h->Shutdown(); prisma.anyFocus=false;
    modulePresent=false; h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    modulePresent=true; exportPresent=false;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    exportPresent=true; activeApi=nullptr;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    activeApi=&f.api; f.api.apiVersion=3;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI); h->Shutdown();
    f.api.apiVersion=4;
    f.api.hostVersionNumber=10700;
    prismaPresent=false; modulePresent=false;
    h=CreateUiHostWithResolver("auto",listeners,[](UiHostEvent){},resolver);
    assert(!h); // neither host must leave the dashboard unavailable
    modulePresent=true;
    h=CreateUiHostWithResolver("auto",listeners,[](UiHostEvent){},resolver);
    assert(!h); // a below-floor host alone is still unavailable
    f.api.hostVersionNumber=10701;
    h=CreateUiHostWithResolver("prismaui",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::Magelight); h->Shutdown();
    f.api.PostGameTask=nullptr;
    h=CreateUiHostWithResolver("auto",listeners,[](UiHostEvent){},resolver);
    assert(!h); // missing safe handoff cannot enable the dashboard
    f.api.PostGameTask=FakeMagelight::PostTask; prismaPresent=true;
}
void prismaTest() {
    FakeMagelight f; activeApi=&f.api;
    prisma.names.clear(); prisma.callbacks.clear(); prisma.calls.clear();
    prisma.view=91; prisma.anyFocus=false; prisma.focused=0; prisma.dom=nullptr;
    std::vector<UiHostEvent> events;
    auto h=CreateUiHostWithResolver("PrismaUI",listeners,[&](UiHostEvent e){events.push_back(e);},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI && prisma.order==90);
    assert(prisma.hidden>0 && prisma.names.empty());
    prisma.dom(prisma.view);
    assert(prisma.names.size()==listeners.size() && prisma.callbacks.size()==listeners.size());
    for(size_t i=0;i<listeners.size();++i) assert(prisma.names[i]==listeners[i].name);
    assert(events.size()==1 && events[0]==UiHostEvent::DomReady);
    assert(h->Open()==UiOpenResult::Accepted && prisma.focusPause);
    assert(events.size()==2 && events.back()==UiHostEvent::Entered);
    for(size_t i=0;i<listeners.size();++i) { callbackHits=0; prisma.callbacks[i]("route"); assert(callbackHits==1 && routedListener==static_cast<int>(i)); }
    assert(h->Call("updateFullState","{}") && h->Call("updateSlots","[]"));
    assert(prisma.calls.size()==2 && prisma.calls[0].starts_with("updateFullState:") && prisma.calls[1].starts_with("updateSlots:"));
    const auto beforeReopen=events.size();
    deferPrismaPosts=true;
    h->Close(); assert(h->Open()==UiOpenResult::Accepted);
    deferPrismaPosts=false; drainPrismaPosts();
    assert(events.size()==beforeReopen+1 && events.back()==UiHostEvent::Entered);
    h->Close(); assert(events.back()==UiHostEvent::Exited);
    h->Shutdown();
}
void magelightTest() {
    FakeMagelight f; activeApi=&f.api;
    std::vector<UiHostEvent> events;
    f.domDuringCreate=true; f.runPosts=true;
    auto h=CreateUiHostWithResolver("magelight",listeners,[&](UiHostEvent e){events.push_back(e);},resolver);
    f.runPosts=false;
    assert(h && h->Kind()==UiHostKind::Magelight && h->IsAvailable());
    assert(f.reloads==1);
    assert(f.registered==1 && f.created==1 && f.viewDesc.fullscreen && !f.viewDesc.startVisible);
    assert(f.capturedPath.find("雪")!=std::string::npos);
    assert(f.listeners.size()==listeners.size());
    for (size_t i=0;i<listeners.size();++i) assert(f.listeners[i].first==listeners[i].name);
    assert(h->Call("updateFullState","{}") && h->Call("updateSlots","[]"));
    assert(f.calls.size()==2 && f.calls[0].starts_with("updateFullState:") && f.calls[1].starts_with("updateSlots:"));
    f.dom(f.view); f.drain();
    assert(f.reloads==1 && events.size()==1 && events.back()==UiHostEvent::DomReady);
    f.fire(MAGELIGHT_API::Event::ViewDomReady,f.view); assert(f.reloads==1 && events.size()==1);
    for (size_t i=0;i<listeners.size();++i) {
        callbackHits=0; f.listeners[i].second(f.view,"route",f.listenerUsers[i]); f.drain();
        assert(callbackHits==1 && routedListener==static_cast<int>(i));
        assert(listenerCalls.back().argument=="route");
    }
    f.requestResult=MAGELIGHT_API::Result::Busy;
    assert(h->Open()==UiOpenResult::Refused);
    f.requestResult=MAGELIGHT_API::Result::Ok;
    assert(h->Open()==UiOpenResult::Pending && f.flags==MAGELIGHT_API::kUIModeFlagPause);
    assert(h->Open()==UiOpenResult::Pending); // one queued entry, not two
    const auto beforeCancelledEntry=events.size();
    h->Close(); f.owner=f.mod;
    assert(h->Open()==UiOpenResult::Pending); // defer reopen until the old mode exits
    f.fire(MAGELIGHT_API::Event::UIModeEntered,f.view);
    h->Service();
    assert(events.size()==beforeCancelledEntry && f.releases>=1 && f.hidden>=1);
    prisma.anyFocus=true;
    f.fire(MAGELIGHT_API::Event::UIModeExited,f.view);
    assert(h->Service()==UiOpenResult::Refused); // contended deferred reopen is not left requested
    prisma.anyFocus=false; f.owner=0;
    assert(h->Open()==UiOpenResult::Pending);
    h->Service();
    f.fire(MAGELIGHT_API::Event::UIModeEntered,f.view); assert(events.back()==UiHostEvent::Entered);
    h->Close(); f.fire(MAGELIGHT_API::Event::UIModeExited,f.view); h->Service(); assert(events.back()==UiHostEvent::Exited);
    f.fire(MAGELIGHT_API::Event::UIModeRefused, f.view); assert(events.back()==UiHostEvent::Refused);
    assert(h->Open()==UiOpenResult::Pending);
    f.fire(MAGELIGHT_API::Event::UIModeEntered,f.view);
    f.owner=f.mod; h->Close(); f.fire(MAGELIGHT_API::Event::UIModeExited,f.view);
    const auto actionStart=f.uiActions.size();
    assert(h->Open()==UiOpenResult::Pending);
    assert(h->Service()==UiOpenResult::Pending);
    assert(f.uiActions.size()==actionStart+2 && f.uiActions[actionStart]=="hide" &&
           f.uiActions[actionStart+1]=="request");
    f.fire(MAGELIGHT_API::Event::UIModeEntered,f.view);
    assert(events.back()==UiHostEvent::Entered);
    h->Close(); f.fire(MAGELIGHT_API::Event::UIModeExited,f.view); h->Service();
    assert(f.requestCalls==5 && f.listenerCountsAtReload.size()==1 && f.listenerCountsAtReload[0]==listeners.size());
    const auto staleListener=f.listeners[0].second;
    void* staleUser=f.listenerUsers[0];
    f.domDuringCreate=false;
    h->Reload();
    h->Service();
    callbackHits=0; staleListener(f.view,"stale",staleUser); f.drain();
    assert(callbackHits==0);
    f.fire(MAGELIGHT_API::Event::ViewDestroyed,f.view);
    h->Service();
    assert(f.created==2 && f.listeners.size()==listeners.size()*2);
    callbackHits=0; staleListener(f.view,"stale-after-reload",staleUser); f.drain();
    assert(callbackHits==0);
    f.dom(f.view); f.drain();
    assert(f.reloads==2 && f.listenerCountsAtReload.size()==2 && f.listenerCountsAtReload[1]==listeners.size()*2);
    f.dom(f.view); f.drain();
    assert(events.back()==UiHostEvent::DomReady && f.reloads==2);
    f.fire(MAGELIGHT_API::Event::RenderDead, 0); assert(events.back()==UiHostEvent::Unavailable && !h->IsAvailable());
    h->Shutdown();
}
void materializationTest() {
    FakeMagelight f; activeApi=&f.api;
    std::vector<UiHostEvent> events;
    auto h=CreateUiHostWithResolver("magelight",listeners,[&](UiHostEvent e){events.push_back(e);},resolver);
    assert(h && h->Kind()==UiHostKind::Magelight);
    assert(h->Open()==UiOpenResult::Pending); h->Service();
    assert(f.reloads==0 && f.requestCalls==0 && events.empty());
    f.dom(f.view+1); f.drain();
    assert(f.reloads==0 && events.empty()); // unrelated readiness cannot materialize this view
    f.reloadResult=MAGELIGHT_API::Result::NotReady;
    f.dom(f.view); f.drain();
    assert(f.reloads==1 && events.size()==1 && events.back()==UiHostEvent::Refused);
    h->Service(); h->Service();
    assert(f.reloads==1 && f.requestCalls==0); // no automatic retry spin after bootstrap failure
    f.reloadResult=MAGELIGHT_API::Result::Ok;
    assert(h->Open()==UiOpenResult::Pending && f.reloads==2);
    assert(events.size()==1 && f.requestCalls==0); // ReloadView Ok is not DOM-ready
    f.dom(f.view); f.drain();
    assert(events.back()==UiHostEvent::DomReady);
    h->Service(); assert(f.requestCalls==1);
    f.fire(MAGELIGHT_API::Event::UIModeEntered,f.view);
    assert(events.back()==UiHostEvent::Entered);
    f.owner=99; h->Close();
    assert(f.owner==99); // this dashboard must not release another mod's mode
    h->Shutdown();
}

void failCleanupTest() {
    FakeMagelight f; activeApi=&f.api;
    f.createResult=MAGELIGHT_API::Result::NotReady;
    auto h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::Magelight && h->IsAvailable());
    assert(f.registered==1 && f.created==1 && f.unregistered==0);
    f.createResult=MAGELIGHT_API::Result::Ok; assert(h->Create() && h->IsAvailable());
    h->Shutdown();
    FakeMagelight failed; activeApi=&failed.api;
    failed.registerResult=MAGELIGHT_API::Result::Internal;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI);
    failed.registerResult=MAGELIGHT_API::Result::Ok; failed.policyResult=MAGELIGHT_API::Result::Denied;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI && failed.unregistered==1);
    failed.policyResult=MAGELIGHT_API::Result::Ok; failed.createResult=MAGELIGHT_API::Result::FileNotFound;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI && failed.unregistered>=2);
    failed.createResult=MAGELIGHT_API::Result::Ok; failed.listenerResult=MAGELIGHT_API::Result::Unsupported;
    h=CreateUiHostWithResolver("magelight",listeners,[](UiHostEvent){},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI && failed.destroyed>=1);
    failed.listenerResult=MAGELIGHT_API::Result::Ok; failed.renderDeadOnRegister=true;
    std::vector<UiHostEvent> earlyEvents;
    h=CreateUiHostWithResolver("magelight",listeners,[&](UiHostEvent e){earlyEvents.push_back(e);},resolver);
    assert(h && h->Kind()==UiHostKind::PrismaUI && failed.unregistered>=3);
    assert(!earlyEvents.empty() && earlyEvents.back()==UiHostEvent::Unavailable);
}

// Manager fixture below receives exact production method definitions extracted by Python.
namespace IntelEngine {
static constexpr auto kPollIntervalMs=std::chrono::milliseconds(50);
static constexpr auto kKeyPressCooldownMs=std::chrono::milliseconds(300);
static constexpr int kModCtrl=1,kModShift=2,kModAlt=4;
static constexpr const char* kActionYamlDirectory="actions";
struct FakeHost final : IUiHost {
    std::vector<std::pair<std::string,std::string>> calls;
    std::function<void()> onClose;
    int openCalls=0;
    UiHostKind Kind() const noexcept override { return UiHostKind::Magelight; }
    UiOpenResult serviceResult=UiOpenResult::Pending;
    bool IsAvailable() const noexcept override { return true; }
    bool Create() override { return true; }
    UiOpenResult Open() override { ++openCalls; return UiOpenResult::Pending; }
    void Close() override { if(onClose) onClose(); }
    void Reload() override {}
    bool Call(std::string_view n,std::string_view a) override { calls.emplace_back(n,a); return true; }
    UiOpenResult Service() override { return serviceResult; }
    void Shutdown() override {}
};
struct ConfigStub {
    int Reloads=0; std::string section,key,yaml;
    int GetHotkey() const{return 7;} int GetModifiers() const{return 0;}
    void Reload(){++Reloads;}
    bool SetHotkey(int){return true;} bool SetModifiers(int){return true;}
    bool WriteYamlValue(const std::string& s,const std::string& k,const std::string& v){section=s;key=k;yaml=v;return true;}
};
struct PoliticsStub {
    bool IsEnabled()const{return true;} int GetTickIntervalHours()const{return 6;}
    int GetMaxRelationChangePerTick()const{return 15;} int GetMaxActiveWars()const{return 2;}
    bool SetEnabled(bool){return true;} bool SetTickIntervalHours(int){return true;}
    bool SetMaxRelationChangePerTick(int){return true;} bool SetMaxActiveWars(int){return true;}
};
struct SlotStub { std::string SerializeToJson()const{return "[slot]";} };
class DashboardConfig { public: static ConfigStub* GetSingleton(){static ConfigStub c;return &c;} void Reload(){} };
class FactionPolitics { public: static PoliticsStub* GetSingleton(){static PoliticsStub p;return &p;} };
class SlotTracker { public: static SlotStub* GetSingleton(){static SlotStub s;return &s;} };

class NPCIndex { public:
    static NPCIndex* GetSingleton(){static NPCIndex index;return &index;}
    std::vector<std::string> names;
    std::size_t GetNPCCount() const{return names.size();}
    std::vector<std::string> GetKnownNames() const {auto out=names;std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;}
};
namespace SkyrimNetAPI { inline std::string (*GetPluginConfigValue)(const char*,const char*,const char*)=nullptr; }
namespace RE {
struct Calendar {
    static Calendar* GetSingleton(){static Calendar c;return &c;}
    float GetCurrentGameTime() const{return 3.5f;}
    unsigned GetYear() const{return 201;} unsigned GetMonth() const{return 7;}
    float GetDay() const{return 20.f;}
    float GetHour() const{return 12.f;}
};
struct Worldspace {};
struct Cell { bool interior=false; bool IsInteriorCell() const { return interior; } };
class Actor { public:
    struct NiPoint3 { float x=0,y=0,z=0; };
    struct Base { bool unique=false; bool IsUnique()const{return unique;} } base;
    bool dead=false,disabled=false,loaded=true; Cell* cell=nullptr; Worldspace* world=nullptr;
    unsigned id=0; const char* name=""; NiPoint3 position{};
    bool IsDead()const{return dead;} bool IsDisabled()const{return disabled;} bool Is3DLoaded()const{return loaded;}
    Cell* GetParentCell()const{return cell;} Worldspace* GetWorldspace()const{return world;}
    NiPoint3 GetPosition()const{return position;} Base* GetActorBase(){return &base;}
    unsigned GetFormID()const{return id;} const char* GetDisplayFullName()const{return name;}
};
class PlayerCharacter : public Actor { public: static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;} };
class UI { public: static UI* GetSingleton(){return nullptr;} bool GameIsPaused()const{return false;} };
}
namespace ProcessUtils {
inline std::vector<RE::Actor*> actors;
template<class F> void ForEachLoadedActor(F&& callback) { for(auto* actor:actors) callback(actor); }
}
namespace SKSE {
using ::SKSE::GetTaskInterface;
struct ModCallbackEvent { std::string eventName,strArg; float numArg=0; void* sender=nullptr; };
struct EventSource { void SendEvent(ModCallbackEvent* e){eventCalls.push_back({e->eventName,e->strArg,e->numArg});} };
inline EventSource* GetModCallbackEventSource(){static EventSource source;return &source;}
}
class DashboardUIManager {
public:
    static DashboardUIManager* GetSingleton(){static DashboardUIManager m;return &m;}
    std::unique_ptr<IUiHost> host_;
    std::atomic<bool> acceptingCallbacks_{true},available_{true},isOpen_{false},domReady_{false};
    std::atomic<bool> stopPolling_{false};
    bool shuttingDown_=false,requestedOpen_=false,openedEventSent_=false,pendingRefresh_=false;
    bool IsOpen() const { return isOpen_.load(); }
    bool pendingOpen_=false,pendingClose_=false,pendingReload_=false,pendingFlush_=false,pendingFullStateSet_=false;
    bool gameTaskQueued_=false,gameTaskRetry_=false,pendingSlotPayloadSet_=false,pendingFullStatePayloadSet_=false;
    std::uint64_t generation_=0,intentGeneration_=0,pendingCloseIntentGeneration_=0,expectedExitIntentGeneration_=0;
    bool expectedExitPending_=false;
    std::uint64_t slotPayloadGeneration_=0,fullStatePayloadGeneration_=0;
    std::string pendingFullState_,pendingSlotPayload_,pendingFullStatePayload_;
    std::atomic_size_t knownNpcsSize_{static_cast<std::size_t>(-1)};
    std::vector<std::pair<std::uint64_t,std::string>> pendingDirectorResults_;
    std::mutex mutex_,actionCacheMutex_; std::condition_variable pollCondition_;
    struct ActionParam {std::string name,description;std::vector<std::string> enumValues;};
    struct ActionMeta {std::string name,fileName,description;bool enabled=true;std::vector<ActionParam> params;};
    std::vector<ActionMeta> actionCache_; bool actionCacheLoaded_=true;
    void LoadActionYAMLs();
    void SendModEvent(const std::string&,const std::string& = "",float=0.f);
    void Toggle(); void RequestClose(); void OnHostEvent(UiHostEvent,std::uint64_t=0);
    void PushSlotData(); void PushFullState(const std::string&); void SendSlotData(std::uint64_t); void ProcessFullState(const std::string&,std::uint64_t);
    void FlushState(std::uint64_t); void HotkeyPollLoop();
    void ReloadView();
    static void OnCloseStatic(const char*); static void OnRequestRefreshStatic(const char*); static void OnReloadUIStatic(const char*);
    static void OnCancelTaskStatic(const char*); static void OnCancelScheduleStatic(const char*); static void OnCancelQuestStatic(const char*);
    static void OnToggleStoryTypeStatic(const char*); static void OnChangeSettingStatic(const char*); static void OnRemovePackagesStatic(const char*);
    static void OnDispatchStoryStatic(const char*); static void OnDispatchNpcSocialStatic(const char*); static void OnDispatchPoliticsStatic(const char*); static void OnExecuteActionStatic(const char*);
    static void OnChangePluginConfigStatic(const char*); static void OnToggleActionStatic(const char*);
    static void OnGenerateDirectorStatic(const char*);
};
}
''' + "namespace IntelEngine {\n" + "\n".join(callbacks) + (
    "\nvoid DashboardUIManager::OnGenerateDirectorStatic(const char*) {}\n" if not has_generate else ""
) + r'''
void testManager() {
    auto* m=DashboardUIManager::GetSingleton(); m->host_=std::make_unique<FakeHost>();
    eventCalls.clear();
    m->Toggle(); assert(m->requestedOpen_ && m->pendingOpen_);
    m->Toggle();
    assert(!m->requestedOpen_ && m->pendingClose_);
    m->OnHostEvent(UiHostEvent::Entered);
    assert(!m->isOpen_ && eventCalls.empty()); // a cancelled late entry cannot reopen or notify
    m->Toggle();
    m->OnHostEvent(UiHostEvent::DomReady);
    assert(eventCalls.empty()); // DOM-first must wait for actual entry
    m->OnHostEvent(UiHostEvent::Entered);
    assert(m->isOpen_ && eventCalls.size()==1 && eventCalls.back().name=="IntelEngine_DashboardOpened");
    m->OnHostEvent(UiHostEvent::DomReady);
    assert(eventCalls.size()==2 && eventCalls.back().name=="IntelEngine_DashboardRefresh");
    auto* host=static_cast<FakeHost*>(m->host_.get());
    SkyrimNetAPI::SendCustomPromptToLLM = fakeDirectorSend;
    const char* storyRequest = R"({"requestId":"bad","form":"story","fields":{"npcName":"Brey","storyType":"seek_player","narration":""},"touched":{},"schema":{"npcName":{"type":"text"},"storyType":{"type":"select","options":["seek_player"]},"narration":{"type":"text"}},"variants":{},"options":{"npcs":["Brey"],"factions":[]}})";
    auto drainDirector = [&] { m->stopPolling_=false; stopOnForeground=&m->stopPolling_; m->HotkeyPollLoop(); stopOnForeground=nullptr; };
    auto generate = [&] { DashboardUIManager::OnGenerateDirectorStatic(storyRequest); };
    DashboardUIManager::OnGenerateDirectorStatic(R"({"requestId":"audit","form":"story","outcome":{"ok":true,"filled":["npcName"],"blank":["narration"],"applied":["narration"],"rejected":{"enemyType":"Invalid option"}}})");
    assert(logger::generateMessages.size()==1);
    assert(logger::generateMessages.back().find(R"("rejected":{"enemyType":"Invalid option"})") != std::string::npos);
    assert(logger::generateMessages.back().find(R"("applied":["narration"])") != std::string::npos);
    assert(!directorCompletion);
    generate();
    for (const char* reply : {"```json\n{\"narration\":\"Wrapped narrative\"}\n```",
                              "Here is the draft:\n{\"narration\":\"Wrapped narrative\"}"}) {
        directorCompletion(reply,1); drainDirector();
        auto wrapped=nlohmann::json::parse(host->calls.back().second);
        assert(wrapped["ok"] && wrapped["fields"]["narration"]=="Wrapped narrative");
        assert(logger::generateMessages.back().find(reply) != std::string::npos);
        generate();
    }
    if (directorCompletion) directorCompletion("not-json",1);
    drainDirector();
    auto resultCount = [&] { return std::count_if(host->calls.begin(),host->calls.end(),
        [](const auto& call){return call.first=="directorGenerateResult";}); };
    assert(resultCount()==3);
    auto failed=nlohmann::json::parse(host->calls.back().second);
    assert(!failed["ok"] && failed["error"].get<std::string>().find("not-json") != std::string::npos);
    assert(logger::generateMessages.back().find("not-json") != std::string::npos);
    generate();
    if (directorCompletion) directorCompletion("service unavailable",0);
    drainDirector();
    assert(resultCount()==4);
    auto serviceFailure=nlohmann::json::parse(host->calls.back().second);
    assert(!serviceFailure["ok"] && serviceFailure["error"]=="Generation failed");
    generate();
    directorCompletion(R"({"narration":"Keep valid narrative","unexpected":false})",1); drainDirector();
    auto partialResult=nlohmann::json::parse(host->calls.back().second);
    assert(partialResult["ok"] && partialResult["fields"]["narration"]=="Keep valid narrative");
    const auto beforeStale=resultCount();
    generate();
    auto staleCompletion=std::move(directorCompletion);
    m->RequestClose(); m->Toggle();
    m->OnHostEvent(UiHostEvent::DomReady); m->OnHostEvent(UiHostEvent::Entered);
    staleCompletion(R"({"narration":"late"})",1); drainDirector();
    assert(resultCount()==beforeStale);
    assert(std::none_of(eventCalls.begin(),eventCalls.end(),[](const auto& e) {
        return e.name=="IntelEngine_DashboardDispatchStory" ||
               e.name=="IntelEngine_DashboardDispatchNpcSocial" ||
               e.name=="IntelEngine_DashboardDispatchPolitics";
    }));
    host->calls.clear(); m->stopPolling_=false;
    std::cout << "PASS director_generate_failure_and_stale_close_reopen\n";
    const auto generation=m->generation_;
    m->PushSlotData(); assert(m->pendingFlush_); m->pendingFlush_=false;
    m->SendSlotData(generation); assert(m->pendingSlotPayloadSet_);
    m->PushFullState("{\"ready\":true}");
    assert(m->pendingFullStateSet_ && m->pendingFullState_=="{\"ready\":true}");
    NPCIndex::GetSingleton()->names={"Zelda","Brey","Brey","Aela"};
    m->ProcessFullState("{\"ready\":true}",generation); assert(m->pendingFullStatePayloadSet_);
    auto fullPayload=nlohmann::json::parse(m->pendingFullStatePayload_);
    assert(fullPayload["knownNpcs"]==nlohmann::json::array({"Aela","Brey","Zelda"}));
    assert(fullPayload["knownNpcs"].size()==3 && fullPayload["knownNpcs"].dump().size()==23);
    m->ProcessFullState("{\"ready\":true}",generation);
    assert(nlohmann::json::parse(m->pendingFullStatePayload_)["knownNpcs"]==fullPayload["knownNpcs"]);
    m->pendingFullStatePayloadSet_=false;
    m->ProcessFullState("{\"ready\":true}",generation);
    assert(!nlohmann::json::parse(m->pendingFullStatePayload_).contains("knownNpcs"));
    NPCIndex::GetSingleton()->names.push_back("New Name");
    m->ProcessFullState("{\"ready\":true}",generation);
    assert(nlohmann::json::parse(m->pendingFullStatePayload_)["knownNpcs"]==nlohmann::json::array({"Aela","Brey","New Name","Zelda"}));
    m->pendingFullStatePayloadSet_=false;
    RE::Cell exterior, neighboringExterior, distantExterior, interior{true}, otherInterior{true};
    RE::Worldspace world, otherWorld;
    auto* player=RE::PlayerCharacter::GetSingleton();
    player->cell=&exterior; player->world=&world; player->position={0,0,0};
    RE::Actor guard, citizen, named, dead, disabled, unloaded, neighboring, boundary, distant, remoteWorld, interiorActor, otherCellActor, noCell;
    guard.id=0xFF000001; guard.name="Whiterun Guard"; guard.cell=&exterior; guard.world=&world; guard.position={1,0,0};
    citizen.id=0xFE000801; citizen.name="Mod Citizen"; citizen.cell=&exterior; citizen.world=&world;
    named.id=0x4E013BF1; named.name="Brey"; named.base.unique=true; named.cell=&exterior; named.world=&world;
    dead.dead=true; dead.cell=&exterior; dead.world=&world;
    disabled.disabled=true; disabled.cell=&exterior; disabled.world=&world;
    unloaded.loaded=false; unloaded.cell=&exterior; unloaded.world=&world;
    neighboring.id=0x10000001; neighboring.name="Neighbor"; neighboring.cell=&neighboringExterior; neighboring.world=&world; neighboring.position={3999,0,0};
    boundary.id=0x10000002; boundary.name="Boundary"; boundary.cell=&neighboringExterior; boundary.world=&world; boundary.position={4000,0,0};
    distant.id=0x10000003; distant.name="Distant"; distant.cell=&distantExterior; distant.world=&world; distant.position={4001,0,0};
    remoteWorld.id=0x10000004; remoteWorld.name="Other World"; remoteWorld.cell=&exterior; remoteWorld.world=&otherWorld; remoteWorld.position={2,0,0};
    interiorActor.id=0x10000005; interiorActor.name="Interior"; interiorActor.cell=&interior; interiorActor.world=&world;
    otherCellActor.id=0x10000006; otherCellActor.name="Other Interior Cell"; otherCellActor.cell=&otherInterior; otherCellActor.world=&world;
    ProcessUtils::actors={nullptr,player,&guard,&citizen,&named,&dead,&disabled,&unloaded,&neighboring,&boundary,&distant,&remoteWorld,&interiorActor,&otherCellActor,&noCell};
    m->ProcessFullState("{\"ready\":true}",generation);
    const auto actorPayload=nlohmann::json::parse(m->pendingFullStatePayload_)["loadedNpcs"];
    assert(actorPayload==nlohmann::json::array({
        {{"formId",0xFF000001u},{"name","Whiterun Guard"}},
        {{"formId",0xFE000801u},{"name","Mod Citizen"}},
        {{"formId",0x4E013BF1u},{"name","Brey"}},
        {{"formId",0x10000001u},{"name","Neighbor"}},
        {{"formId",0x10000002u},{"name","Boundary"}}}));
    m->LoadActionYAMLs();
    m->ProcessFullState("{\"ready\":true}",generation);
    const auto actionsPayload=nlohmann::json::parse(m->pendingFullStatePayload_)["actions"];
    assert(actionsPayload[0]["name"]=="ReportPlayerConduct");
    assert(actionsPayload[0]["params"][1]["enum"]==nlohmann::json::array({"strongly_positive","positive","negative","strongly_negative"}));
    player->cell=&interior;
    RE::Actor sameInterior, differentInterior;
    sameInterior.id=0x10000007; sameInterior.name="Same Interior"; sameInterior.cell=&interior; sameInterior.position={4000,0,0};
    differentInterior.id=0x10000008; differentInterior.name="Different Interior"; differentInterior.cell=&otherInterior;
    ProcessUtils::actors={&sameInterior,&differentInterior};
    m->ProcessFullState("{\"ready\":true}",generation);
    const auto interiorPayload=nlohmann::json::parse(m->pendingFullStatePayload_)["loadedNpcs"];
    assert(interiorPayload==nlohmann::json::array({{{"formId",0x10000007u},{"name","Same Interior"}}}));
    player->cell=nullptr;
    ProcessUtils::actors={&guard};
    m->ProcessFullState("{\"ready\":true}",generation);
    assert(nlohmann::json::parse(m->pendingFullStatePayload_)["loadedNpcs"].empty());
    ProcessUtils::actors.clear();
    stopOnForeground=&m->stopPolling_;
    m->HotkeyPollLoop();
    stopOnForeground=nullptr;
    assert(host->calls.size()==2 && host->calls[0].first=="updateSlots" && host->calls[1].first=="updateFullState");
    const auto staleIntent=m->intentGeneration_;
    m->Toggle(); m->Toggle();
    const int opensBefore=host->openCalls;
    host->onClose=[m]{ m->OnHostEvent(UiHostEvent::Exited); m->OnHostEvent(UiHostEvent::DomReady); };
    m->stopPolling_=false; stopOnForeground=&m->stopPolling_; m->HotkeyPollLoop(); stopOnForeground=nullptr;
    host->onClose={};
    assert(host->openCalls==opensBefore+1 && m->requestedOpen_);
    m->OnHostEvent(UiHostEvent::Entered); assert(m->isOpen_);
    m->OnHostEvent(UiHostEvent::Exited,staleIntent);
    assert(m->isOpen_ && m->requestedOpen_); // an old exit cannot close the reopened request
    m->OnHostEvent(UiHostEvent::Exited); assert(!m->isOpen_ && !m->requestedOpen_);
    m->domReady_=false; eventCalls.clear();
    m->Toggle(); m->OnHostEvent(UiHostEvent::Entered);
    assert(m->isOpen_ && eventCalls.empty()); // entry-first waits for this document before notifying
    m->OnHostEvent(UiHostEvent::DomReady);
    assert(eventCalls.size()==1 && eventCalls.back().name=="IntelEngine_DashboardOpened");
    m->OnHostEvent(UiHostEvent::Exited);
    m->Toggle(); m->OnHostEvent(UiHostEvent::Entered);
    assert(eventCalls.back().name=="IntelEngine_DashboardOpened");
    m->OnHostEvent(UiHostEvent::Refused); assert(!m->isOpen_ && !m->requestedOpen_);
    m->Toggle(); m->OnHostEvent(UiHostEvent::Unavailable); assert(!m->available_);
    m->available_=true;
    eventCalls.clear();
    host->serviceResult=UiOpenResult::Refused;
    m->stopPolling_=false; m->Toggle();
    stopOnForeground=&m->stopPolling_; m->HotkeyPollLoop(); stopOnForeground=nullptr;
    assert(!m->requestedOpen_ && !m->isOpen_);
    host->serviceResult=UiOpenResult::Pending;
    DashboardUIManager::OnCancelTaskStatic(R"({"slot":3})");
    DashboardUIManager::OnCancelScheduleStatic(R"({"slot":4})");
    DashboardUIManager::OnCancelQuestStatic(nullptr);
    DashboardUIManager::OnToggleStoryTypeStatic(R"({"type":"war","enabled":false})");
    DashboardUIManager::OnChangeSettingStatic(R"({"key":"x","value":4.5})");
    DashboardUIManager::OnRemovePackagesStatic(R"({"formId":2147483726})");
    assert(eventCalls.size()==6);
    assert(eventCalls[0].name=="IntelEngine_DashboardCancelTask" && eventCalls[0].number==3.0f);
    assert(eventCalls[1].name=="IntelEngine_DashboardCancelSchedule" && eventCalls[1].number==4.0f);
    assert(eventCalls[2].name=="IntelEngine_DashboardCancelQuest");
    assert(eventCalls[3].str=="war" && eventCalls[3].number==0);
    assert(eventCalls[4].str=="x" && eventCalls[4].number==4.5f);
    assert(eventCalls[5].str=="2147483726");
    eventCalls.clear();
    DashboardUIManager::OnRemovePackagesStatic(R"({"formId":0})");
    DashboardUIManager::OnRemovePackagesStatic(R"({"formId":4294967296})");
    assert(eventCalls.empty());
    for (const std::uint32_t id : {0x4E013BF1u,0xFE000801u,0xFF0012C7u}) {
        const auto arg=std::string("{\"formId\":")+std::to_string(id)+"}";
        DashboardUIManager::OnRemovePackagesStatic(arg.c_str());
    }
    for (const std::uint32_t id : {0x4E013BF1u,0xFE000801u,0xFF0012C7u}) {
        const auto arg=std::string("{\"npcFormId\":")+std::to_string(id)+",\"actionName\":\"x\"}";
        DashboardUIManager::OnExecuteActionStatic(arg.c_str());
    }
    assert(eventCalls.size()==6);
    for (int i=0;i<3;++i) assert(eventCalls[i].name=="IntelEngine_DashboardRemovePackages" && eventCalls[i].str==std::to_string(std::array<std::uint32_t,3>{0x4E013BF1u,0xFE000801u,0xFF0012C7u}[i]));
    for (int i=0;i<3;++i) assert(eventCalls[i+3].name=="IntelEngine_DashboardExecuteAction" && nlohmann::json::parse(eventCalls[i+3].str)["npcFormId"]==std::to_string(std::array<std::uint32_t,3>{0x4E013BF1u,0xFE000801u,0xFF0012C7u}[i]));
    eventCalls.clear();
    DashboardUIManager::OnDispatchStoryStatic(R"({"npcName":"A","storyType":"rumor","narration":"hi","subject":"x"})");
    DashboardUIManager::OnDispatchNpcSocialStatic(R"({"npc1Name":"A","npc2Name":"B","socialType":"chat","narration":"hi"})");
    DashboardUIManager::OnDispatchPoliticsStatic(R"({"factionA":"A","factionB":"B","factionALabel":"Imperial Legion","factionBLabel":"Stormcloaks","eventType":"pact","eventLabel":"Alliance pact","description":"x","relationDelta":-4})");
    DashboardUIManager::OnExecuteActionStatic(R"({"npcFormId":4294967295,"actionName":"follow","params":{"n":4,"s":"x"}})");
    assert(eventCalls.size()==4);
    assert(eventCalls[0].name=="IntelEngine_DashboardDispatchStory");
    assert(nlohmann::json::parse(nlohmann::json::parse(eventCalls[0].str)["response"].get<std::string>())["subject"]=="x");
    assert(eventCalls[1].name=="IntelEngine_DashboardDispatchNpcSocial");
    assert(nlohmann::json::parse(nlohmann::json::parse(eventCalls[1].str)["response"].get<std::string>())["npc2"]=="B");
    assert(eventCalls[2].name=="IntelEngine_DashboardDispatchPolitics");
    assert(nlohmann::json::parse(eventCalls[2].str)["event_label"]=="Alliance pact");
    assert(nlohmann::json::parse(eventCalls[2].str)["event_type"]=="pact");
    assert(nlohmann::json::parse(eventCalls[2].str)["faction_a_label"]=="Imperial Legion");
    assert(nlohmann::json::parse(eventCalls[2].str)["faction_b_label"]=="Stormcloaks");
    DashboardUIManager::OnDispatchPoliticsStatic(R"({"factionA":"A","factionB":"B","eventType":"pact","description":"x","relationDelta":-4})");
    assert(nlohmann::json::parse(eventCalls.back().str)["event_label"]=="pact");
    assert(nlohmann::json::parse(eventCalls.back().str)["faction_a_label"]=="A");
    assert(nlohmann::json::parse(eventCalls.back().str)["faction_b_label"]=="B");
    assert(eventCalls[3].name=="IntelEngine_DashboardExecuteAction");
    assert(nlohmann::json::parse(eventCalls[3].str)["npcFormId"]=="4294967295");
    eventCalls.clear();
    DashboardUIManager::OnChangePluginConfigStatic(R"({"path":"llm.system_prompt","value":"a\\b\"c"})");
    const auto* config=DashboardConfig::GetSingleton();
    assert(config->section=="llm" && config->key=="system_prompt" && config->yaml==R"("a\\b\"c")");
    assert(eventCalls.size()==1 && eventCalls.back().name=="IntelEngine_DashboardRefresh");
    std::filesystem::create_directory(kActionYamlDirectory);
    const std::string actionPath=std::string(kActionYamlDirectory)+"/check.yaml";
    std::ofstream(actionPath) << "name: Check\nparams:\n  enabled: nested\nenabled: true\nother: keep";
    m->actionCache_={{"Check","check","",true,{}}}; eventCalls.clear();
    DashboardUIManager::OnToggleActionStatic(R"({"name":"Check","enabled":false})");
    std::ifstream actionFile(actionPath);
    const std::string actionText((std::istreambuf_iterator<char>(actionFile)),{});
    assert(actionText=="name: Check\nparams:\n  enabled: nested\nenabled: false\nother: keep");
    assert(!m->actionCache_[0].enabled && eventCalls.size()==1 &&
           eventCalls.back().name=="IntelEngine_DashboardRefresh");
    DashboardUIManager::OnCloseStatic(nullptr); assert(!m->requestedOpen_ && m->pendingClose_);
    DashboardUIManager::OnReloadUIStatic(nullptr); assert(m->pendingReload_);
    eventCalls.clear(); DashboardUIManager::OnRequestRefreshStatic(nullptr);
    assert(eventCalls.size()==1 && eventCalls[0].name=="IntelEngine_DashboardRefresh");
    m->acceptingCallbacks_=false; eventCalls.clear(); DashboardUIManager::OnCancelQuestStatic(nullptr); assert(eventCalls.empty());
}
void testLifecycleLogs() {
    auto* m=DashboardUIManager::GetSingleton();
    m->host_=std::make_unique<FakeHost>();
    m->shuttingDown_=false; m->available_=true; m->requestedOpen_=false;
    m->isOpen_=false; m->domReady_=true; m->openedEventSent_=false;
    m->expectedExitPending_=false;
    logger::infoMessages.clear();
    auto countLog=[](std::string_view expected) {
        return std::count_if(logger::infoMessages.begin(),logger::infoMessages.end(),
            [expected](const std::string& message){return message==expected;});
    };

    m->Toggle(); m->OnHostEvent(UiHostEvent::Entered);
    assert(countLog("[Dashboard] Opened")==1);
    m->OnHostEvent(UiHostEvent::Entered);
    assert(countLog("[Dashboard] Opened")==1);

    logger::infoMessages.clear();
    m->Toggle();
    assert(countLog("[Dashboard] Closed (cause=hotkey)")==1);

    logger::infoMessages.clear();
    m->requestedOpen_=true; m->isOpen_=true;
    DashboardUIManager::OnCloseStatic(nullptr);
    assert(countLog("[Dashboard] Closed (cause=page close)")==1);
    DashboardUIManager::OnCloseStatic(nullptr);
    assert(countLog("[Dashboard] Closed (cause=page close)")==1);

    logger::infoMessages.clear();
    m->requestedOpen_=true; m->isOpen_=true;
    m->OnHostEvent(UiHostEvent::Exited);
    assert(countLog("[Dashboard] Closed (cause=host exit; Esc/host reason unavailable)")==1);
    m->OnHostEvent(UiHostEvent::Exited);
    assert(countLog("[Dashboard] Closed (cause=host exit; Esc/host reason unavailable)")==1);

    logger::infoMessages.clear();
    m->requestedOpen_=true;
    m->OnHostEvent(UiHostEvent::Refused);
    assert(countLog("[Dashboard] Open refused")==1);
    m->OnHostEvent(UiHostEvent::Refused);
    assert(countLog("[Dashboard] Open refused")==1);
}
}
int main() {
    for (std::size_t i=0; i<listeners.size(); ++i)
        assert(listeners[i].name == expectedNames[i]);
    selectTest(); prismaTest(); magelightTest(); materializationTest(); failCleanupTest(); IntelEngine::testManager(); IntelEngine::testLifecycleLogs();
    std::cout << "PASS dashboard host selection, adapters, lifecycle, routing, and manager callbacks\n";
}
''', encoding="utf-8")

    exe = work / "check.exe"
    cmd = ["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
           "-DUIHOST_OFFLINE_TEST", "-DMAGELIGHT_API_NO_LOADER",
           "-I", str(work), "-I", str(json_include), "-I", str(SRC),
           str(cpp), str(SRC / "UiHost.cpp"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
