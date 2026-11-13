"""Compile the real async political tick against deterministic game/worker queues.
Usage: python RunPoliticalEngineTickCheck.py <vcpkg-triplet-dir> [Papyrus.cpp]
The optional source path permits failing-before checks against a historical source snapshot.
Engine/provider fixtures model thread affinity and save identity, not game behavior.
"""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else repo / 'SKSE/src/Papyrus.cpp'
text = source.read_text(encoding='utf-8')
start = text.index('    nlohmann::json PollAndSyncPE() {')
end = text.index('\n    }\n', text.index('    void BeginAsyncPoliticalTick(RE::StaticFunctionTag*,\n')) + len('\n    }')
with tempfile.TemporaryDirectory(prefix='pe-tick-') as directory:
    work = pathlib.Path(directory)
    cpp = r'''
#include "PoliticalEngineChanges.h"
#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
using nlohmann::json;
static bool gameThread=true, resetInPoll=false;
static std::vector<std::function<void()>> workers, gameTasks;
static void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
namespace RE {
struct StaticFunctionTag {};
struct BSFixedString { std::string value; BSFixedString(const char* s):value(s){} const char* c_str() const{return value.c_str();} };
struct TESQuest { const char* GetFormEditorID() const {return "FixtureQuest";} };
}
namespace logger { template<class... T> void error(T&&...){} }
namespace SKSE {
struct Tasks { void AddTask(std::function<void()> task){gameTasks.push_back(std::move(task));} };
static Tasks* GetTaskInterface(){static Tasks value;return &value;}
}
namespace IntelEngine {
struct SlotTracker {
    std::atomic<unsigned> loadGeneration{1};
    static SlotTracker* GetSingleton(){static SlotTracker value;return &value;}
};
struct News { unsigned load; float time; };
static std::vector<News> news;
static unsigned callbacks=0;
struct FactionPolitics {
    struct Snapshot { float currentGameTime; };
    static FactionPolitics* GetSingleton(){static FactionPolitics value;return &value;}
    bool IsReady() const {return true;}
    Snapshot BuildPoliticsTickSnapshot(float time){Require(gameThread,"engine snapshot on worker");return {time};}
    // The tick enforces the war rule (fix 94) right after the snapshot; it writes engine state, so game thread only.
    int EnforceWarRule(const Snapshot&){Require(gameThread,"war rule enforced on worker");return -1;}
    void SyncPERulers(const json&){} void SyncPESides(const json&){}
    std::string BuildPoliticalContextFromSnapshot(const Snapshot&){Require(!gameThread,"context must stay on worker");return "fixture context";}
    int RecordPoliticalEvent(const std::string&,const std::string&,const std::string&,const std::string&,int,float time){
        Require(gameThread,"PE recording reaches engine state writer on worker");
        news.push_back({SlotTracker::GetSingleton()->loadGeneration.load(),time});return static_cast<int>(news.size());
    }
};
struct PoliticalEngineBridge {
    static PoliticalEngineBridge* GetSingleton(){static PoliticalEngineBridge value;return &value;}
    json Poll(){
        Require(!gameThread,"provider read on game thread");
        if(resetInPoll){++SlotTracker::GetSingleton()->loadGeneration;resetInPoll=false;}
        return json::array({{{"type","pe_visit"},{"text","A public ruler visit."}}});
    }
    json Rulers(){return nullptr;} json RealmSides(){return nullptr;}
};
namespace AsyncDispatch {
void Submit(std::function<void()> task){workers.push_back(std::move(task));}
bool ExecuteQuestFunctionString(const std::string&,const std::string&,const std::string&,const std::string&){Require(gameThread,"VM callback on worker");++callbacks;return true;}
}
namespace Papyrus {
''' + text[start:end] + r'''
}
}
static void Drain(std::vector<std::function<void()>>& queue,bool onGameThread){
    auto pending=std::move(queue);queue.clear();gameThread=onGameThread;
    for(auto& task:pending)task();gameThread=true;
}
int main() try {
    using namespace IntelEngine;
    RE::TESQuest quest;
    auto begin=[&](float time){Papyrus::BeginAsyncPoliticalTick(nullptr,time,&quest,"FixtureScript","Ready");};
    begin(38.f);Drain(workers,false);
    Require(news.empty() && callbacks==0,"worker must not commit news or callbacks");
    Drain(gameTasks,true);
    Require(news.size()==1 && news[0].load==1 && news[0].time==38.f && callbacks==1,"game phase commits current-save news and dispatches callback");
    begin(38.f);++SlotTracker::GetSingleton()->loadGeneration;Drain(workers,false);Drain(gameTasks,true);
    Require(news.size()==1 && callbacks==1,"load before worker drops old tick");
    begin(38.f);resetInPoll=true;Drain(workers,false);Drain(gameTasks,true);
    Require(news.size()==1 && callbacks==1,"load during poll drops old tick");
    begin(38.f);Drain(workers,false);++SlotTracker::GetSingleton()->loadGeneration;Drain(gameTasks,true);
    Require(news.size()==1 && callbacks==1,"load after polling drops queued news and callback");
    begin(17.f);Drain(workers,false);Drain(gameTasks,true);
    Require(news.size()==2 && news.back().load==SlotTracker::GetSingleton()->loadGeneration && news.back().time==17.f && callbacks==2,"new-save tick resumes without old-save facts");
    std::cout<<"PASS actual political tick: worker polling, game-thread news/VM, stale loads before/during/after polling, new-save recovery\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
'''
    (work / 'check.cpp').write_text(cpp, encoding='utf-8')
    subprocess.run(['cl','/nologo','/EHsc','/std:c++latest','/I'+str(repo/'SKSE/src'),
                    '/I'+str(pathlib.Path(sys.argv[1])/'include'),'check.cpp','/Fe:check.exe'],cwd=work,check=True)
    subprocess.run([str(work/'check.exe')],cwd=work,check=True)
