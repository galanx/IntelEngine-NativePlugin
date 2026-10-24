// One deterministic standalone behavioral check; includes the actual CLI core.
#define LCM_NO_MAIN
#include "../migration/LegacyCacheMigration.cpp"
#include <fstream>
#include <functional>

namespace {
using namespace LCM;
std::filesystem::path publicationCollision;
void createPublicationCollision() {
    const auto backup = publicationCollision.parent_path()/"test_IEbackup/original.skse";
    require(SetFileAttributesW(backup.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"cannot probe backup locking");
    const auto writer=CreateFileW(backup.c_str(),GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if (writer!=INVALID_HANDLE_VALUE) CloseHandle(writer);
    require(writer==INVALID_HANDLE_VALUE,"verified backup is writable during publication");
    require(SetFileAttributesW(backup.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"cannot restore backup protection");
    std::ofstream stream(publicationCollision,std::ios::binary);
    const char marker[]="competitor";
    stream.write(marker,sizeof(marker)-1);
    require(static_cast<bool>(stream),"cannot create publication competitor");
}
void word(Bytes& bytes, std::uint32_t value) {
    for (unsigned i=0; i<4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8*i)));
}
Bytes fixture(const std::string& payload) {
    Bytes bytes{'S','K','S','E'};
    for (auto v : {1u,Skse,Runtime,2u,Owner,2u,static_cast<unsigned>(29+payload.size()),Strv,1u,static_cast<unsigned>(payload.size())}) word(bytes,v);
    bytes.insert(bytes.end(),payload.begin(),payload.end());
    for (auto v : {0x54455354u,9u,5u}) word(bytes,v);
    bytes.insert(bytes.end(),{0,255,9,32,27}); // Opaque, unrelated chunk.
    for (auto v : {0x12345678u,1u,15u,0x41424344u,0u,3u}) word(bytes,v);
    bytes.insert(bytes.end(),{1,2,3});
    return bytes;
}
Json evidence(const Bytes& input, const std::vector<std::string>& original,
              const std::vector<std::string>& representations={"legacy","legacy","legacy"},
              const std::string& scope="global") {
    Json fields=Json::array(); std::size_t i=0;
    for (const auto& f : parse(input).fields) if (target(f.key)) {
        fields.push_back({{"object",std::to_string(f.object)},{"key",f.key},{"start",f.start},{"end",f.end},
            {"original_hex",hex(original.at(i))},{"representation",representations.at(i)},
            {"writer_codec_sha256",LCM_CODEC_SHA256},{"evidence","independently specified synthetic input"}});
        ++i;
    }
    Json manifest={{"schema",1},{"skse_sha256",sha256(input)},{"provider_sha256",Provider},
        {"runtime","1.6.1170.0"},{"scope",scope},{"codec_sha256",LCM_CODEC_SHA256},
        {"qualification_receipt_sha256",std::string(64,'a')},{"fields",fields}};
    if (scope=="actor-in-place") {
        manifest["legacy_writer"]="IE-3.5.2";
        manifest["actor_bel_policy"]="space";
        manifest["preserve_rne_name_bel"]=true;
    }
    return manifest;
}
void refuses(const std::function<void()>& action, const std::string& reason) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("accepted unsafe case: "+reason);
}
void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream stream(path,std::ios::binary); stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    require(static_cast<bool>(stream), "self-check fixture write failed");
}
unsigned gameChecks=0;
void gameStartsBeforePublication() {
    gameRunning([] { return ++gameChecks==2; });
}
void vrCheck(const fs::path& area) {
    fs::create_directory(area);
    const auto actor=std::to_string((std::uint64_t{62}<<32)|0x123456);
    const auto payload="1 "+actor+" 6 intel_factsrendered facts\a\xC3\xA9\n%0A%25"
        " intel_gossiprendered heard\nshared intel_taskhistoryrendered task\nline rne_name Stored\aName"
        " intel_target The\aBannered\aMare%0A intel_lingerdestname Destination\a%25";
    auto input=fixture(payload);
    put32(input,8,SkseVR); put32(input,12,RuntimeVR);
    auto expected=fixture("1 "+actor+" 6 intel_factsrendered facts\a\xC3\xA9%0A%250A%2525"
        " intel_gossiprendered heard%0Ashared intel_taskhistoryrendered task%0Aline rne_name Stored\aName"
        " intel_target The\aBannered\aMare%0A intel_lingerdestname Destination\a%25");
    put32(expected,8,SkseVR); put32(expected,12,RuntimeVR);
    require(inspect(input,parse(input))["refusals"].empty(),"real VR tuple refused inspection");
    // Expert mode must not borrow either flat provider's qualification.
    for (const auto* provider : {Provider,Provider17104}) {
        auto proof=evidence(input,{"facts \xC3\xA9\n%0A%25","heard\nshared","task\nline"},
            {"legacy","legacy","legacy"},"actor-in-place");
        proof["runtime"]="1.4.15.0"; proof["provider_sha256"]=provider;
        refuses([&] { convert(input,parse(input),proof); },"unqualified VR expert provider");
    }
    Bytes ess{'T','E','S','V','_','S','A','V','E','G','A','M','E'};
    word(ess,84); word(ess,12); // Synthetic version-12 header; not a loadable save.
    ess.resize(17+84);
    auto reject=[&](const Bytes& badEss, const Bytes& badSkse) {
        write(area/"test.ess",badEss); write(area/"test.skse",badSkse);
        refuses([&] { fix(area/"test.ess"); },"VR unsafe pair");
        require(std::distance(fs::directory_iterator(area),fs::directory_iterator{})==2,
            "VR refusal created artifacts");
        Source e(area/"test.ess"), s(area/"test.skse");
        require(e.bytes()==badEss && s.bytes()==badSkse,"VR refusal changed originals");
    };
    for (const auto tuple : {std::pair{SkseVR,0x010400F0u},std::pair{Skse,RuntimeVR},
                            std::pair{SkseVR,Runtime},std::pair{SkseVR,RuntimeVR+1}}) {
        auto bad=input; put32(bad,8,tuple.first); put32(bad,12,tuple.second);
        require(!parse(bad).refusals.empty(),"mixed/unknown VR tuple accepted");
        reject(ess,bad);
    }
    for (const auto offset : {4u,36u,40u}) {
        auto bad=input; put32(bad,offset,0xffffffff);
        reject(ess,bad);
    }
    for (const auto& payload : {
        "2 "+actor+" 1 intel_factsrendered gap",
        "1 "+actor+" 2 intel_factsrendered x Intel_FactsRendered y",
        "1 "+actor+" 1 intel_factsrendered bad\tcontrol",
        "1 "+std::to_string((std::uint64_t{62}<<32)|0xff000001)+" 1 intel_factsrendered temporary"}) {
        auto bad=fixture(payload); put32(bad,8,SkseVR); put32(bad,12,RuntimeVR);
        reject(ess,bad);
    }
    auto badEss=ess; put32(badEss,17,11); reject(badEss,input);
    badEss=ess; put32(badEss,13,85); reject(badEss,input);
    badEss=ess; badEss.resize(20); reject(badEss,input);
    badEss=ess; badEss[0]='X'; reject(badEss,input);
    write(area/"test.ess",ess); write(area/"test.skse",input);
    require(fix(area/"test.ess")==0,"VR fix failed");
    Source e(area/"test.ess"), s(area/"test.skse");
    Source oe(area/"test_IEconverted.ess"), os(area/"test_IEconverted.skse");
    Source be(area/"test_IEbackup/original.ess"), bs(area/"test_IEbackup/original.skse");
    require(e.bytes()==ess && oe.bytes()==ess && be.bytes()==ess &&
        s.bytes()==input && bs.bytes()==input && os.bytes()==expected,
        "VR fix changed ESS, source, backup, names, opaque bytes or expected cache bytes");
    const auto seArea=area/"se-1597";
    fs::create_directory(seArea);
    auto seInput=input, seExpected=expected;
    // Independent header words from SKSE64 v2.0.20, not the production constants.
    for (auto* bytes : {&seInput,&seExpected}) {
        put32(*bytes,8,0x02000140); put32(*bytes,12,0x01050610);
    }
    require(parse(seInput).refusals.empty(),"1.5.97 tuple refused inspection");
    for (const auto* provider : {Provider,Provider17104}) {
        auto proof=evidence(seInput,{"facts \xC3\xA9\n%0A%25","heard\nshared","task\nline"},
            {"legacy","legacy","legacy"},"actor-in-place");
        proof["runtime"]="1.5.97.0"; proof["provider_sha256"]=provider;
        refuses([&] { convert(seInput,parse(seInput),proof); },"unqualified 1.5.97 expert provider");
    }
    for (const auto tuple : {std::pair{SkseVR,0x01050610u},std::pair{0x02000140u,Runtime},
                            std::pair{0x02000140u,0x01050600u}}) {
        auto bad=seInput; put32(bad,8,tuple.first); put32(bad,12,tuple.second);
        refuses([&] { parse(bad,true); },"mixed/unknown 1.5.97 tuple");
    }
    write(seArea/"test.ess",ess); write(seArea/"test.skse",seInput);
    require(fix(seArea/"test.ess")==0,"synthetic 1.5.97 fix failed");
    Source seEss(seArea/"test.ess"), seSkse(seArea/"test.skse");
    Source seOutputEss(seArea/"test_IEconverted.ess"), seOutputSkse(seArea/"test_IEconverted.skse");
    Source seBackupEss(seArea/"test_IEbackup/original.ess"), seBackupSkse(seArea/"test_IEbackup/original.skse");
    require(seEss.bytes()==ess && seOutputEss.bytes()==ess && seBackupEss.bytes()==ess &&
        seSkse.bytes()==seInput && seBackupSkse.bytes()==seInput && seOutputSkse.bytes()==seExpected,
        "1.5.97 repair changed source, backup, ESS, opaque bytes, names or non-cache destinations");
    const auto emptyArea=area/"cache-free";
    fs::create_directory(emptyArea);
    Bytes empty{'S','K','S','E'};
    for (auto v : {1u,SkseVR,RuntimeVR,1u,Owner,1u,13u,0x494E5456u,1u,1u}) word(empty,v);
    empty.push_back('0'); // Recognized INTV v1, no scalar strings.
    auto badEmpty=empty; put32(badEmpty,36,2);
    refuses([&] { parse(badEmpty,true); },"unknown cache-free provider record version");
    badEmpty=empty; put32(badEmpty,32,0x12345678);
    refuses([&] { parse(badEmpty,true); },"unknown cache-free provider record");
    badEmpty=empty; put32(badEmpty,20,0x12345678);
    refuses([&] { parse(badEmpty,true); },"cache-free missing provider");
    write(emptyArea/"test.ess",ess); write(emptyArea/"test.skse",empty);
    require(fix(emptyArea/"test.ess")==0,"cache-free fix failed");
    Source emptyEss(emptyArea/"test_IEconverted.ess"), emptySkse(emptyArea/"test_IEconverted.skse");
    Source emptyBackup(emptyArea/"test_IEbackup/original.skse");
    require(emptyEss.bytes()==ess && emptySkse.bytes()==empty && emptyBackup.bytes()==empty,
        "cache-free pair was not copied unchanged");
}
void filesystemCheck(const fs::path& area, const Bytes& input, const Bytes& converted) {
    require(!fs::exists(area), "self-check area must not exist");
    require(fs::create_directory(area), "cannot create exclusive self-check area");
    const auto source=area/"inputs", destinations=area/"results", protectedDir=area/"protected";
    fs::create_directory(source); fs::create_directory(destinations); fs::create_directory(protectedDir);
    const Bytes ess{'T','E','S','V','_','S','A','V','E','G','A','M','E',0,9,255};
    write(source/"test.ess",ess); write(source/"test.skse",input);
    const auto essHash=sha256(ess), skseHash=sha256(input);
    const auto vr=source/"vr";
    fs::create_directory(vr);
    auto vrInput=fixture("1 "+std::to_string((std::uint64_t{62}<<32)|0x123456)
        +" 1 intel_factsrendered legacy");
    put32(vrInput,12,0x010400f0); // Incorrect/mixed tuple, not the real SKSEVR identity.
    write(vr/"test.ess",ess); write(vr/"test.skse",vrInput);
    const auto vrParsed=parse(vrInput);
    require(!inspect(vrInput,vrParsed)["refusals"].empty(),
        "VR inspect did not report the runtime refusal");
    const auto vrEvidence=evidence(vrInput,{"legacy"},{"legacy"},"actor-in-place");
    refuses([&] { convert(vrInput,vrParsed,vrEvidence); }, "VR expert conversion");
    refuses([&] { fix(vr/"test.ess"); }, "VR fix");
    require(std::distance(fs::directory_iterator(vr),fs::directory_iterator{})==2,
        "VR refusal wrote a backup, staging or output file");
    Source vrEss(vr/"test.ess"), vrSkse(vr/"test.skse");
    require(vrEss.hash()==essHash && vrSkse.hash()==sha256(vrInput),
        "VR refusal changed the original pair");
    vrCheck(source/"vr-supported");
    {
        Source e(source/"test.ess"), s(source/"test.skse");
        require(e.hash()==essHash && s.hash()==skseHash, "source hash mismatch");
        require(sameFile(source/"test.ess",source/"test.ess"), "same file identity missed");
        fs::create_hard_link(source/"test.ess",source/"alias.ess");
        require(sameFile(source/"test.ess",source/"alias.ess"), "hardlink identity missed");
        refuses([&] { Transaction t(destinations/"bad-backup",protectedDir/"bad-output",{source,protectedDir}); },"protected destination");
        refuses([&] { Transaction t(destinations/"same",destinations/"same",{source}); },"same output/backup");
        fs::create_directory(destinations/"existing");
        refuses([&] { Transaction t(destinations/"existing-backup",destinations/"existing",{source}); },"existing output");
        {
            Transaction t(destinations/"backup",destinations/"output",{source,protectedDir});
            require(!MoveFileExW(t.backupDir().c_str(),(destinations/"moved-backup").c_str(),0),
                "empty backup directory identity is not pinned");
            t.backup(e,s); t.stage(ess,converted,"{\"status\":\"verified_test\"}");
            // The stage is held against writes through verification; Windows
            // requires closing child handles only at the rename commit boundary.
            const auto handle=CreateFileW((t.stagingDir()/"converted.skse").c_str(),GENERIC_WRITE,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            require(handle == INVALID_HANDLE_VALUE, "staged file can be mutated");
            e.verify(); s.verify(); t.publish();
        }
        Source oe(destinations/"output/converted.ess"), os(destinations/"output/converted.skse");
        Source be(destinations/"backup/original.ess"), bs(destinations/"backup/original.skse");
        require(oe.hash()==essHash && os.hash()==sha256(converted), "published pair changed");
        require(be.hash()==essHash && bs.hash()==skseHash, "backup pair changed");
        require(GetFileAttributesW((destinations/"backup/original.skse").c_str()) & FILE_ATTRIBUTE_READONLY, "backup not readonly");
        {
            Transaction t(destinations/"aborted-backup",destinations/"aborted",{source});
            t.backup(e,s); t.stage(ess,converted,"{}");
            t.failure("{\"status\":\"aborted_test\"}");
        }
        require(!fs::exists(destinations/"aborted") && fs::exists(destinations/"aborted.staging/converted.skse"), "unfinished stage published");
        {
            Transaction t(destinations/"race-backup",destinations/"race",{source});
            t.backup(e,s); t.stage(ess,converted,"{}");
            fs::create_directory(destinations/"race");
            refuses([&] { t.publish(); },"publication target appeared");
        }
        e.verify(); s.verify();
        const auto handle=CreateFileW((source/"test.skse").c_str(),GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        require(handle == INVALID_HANDLE_VALUE, "source write lock ineffective");
    }
    publicationCollision=source/"test_IEconverted.skse";
    {
        Source e(source/"test.ess"), s(source/"test.skse");
        refuses([&] { publishFixedPair(source/"test.ess",e,s,converted,createPublicationCollision); },
            "second-output collision after ESS rename");
    }
    require(fs::exists(source/"test_IEconverted.ess") && fs::exists(source/"test_IEconverted.skse") &&
        fs::exists(source/"test_IEbackup/original.ess") && fs::exists(source/"test_IEconverted.staging"),
        "incomplete publication artifacts were not retained");
    {
        std::ifstream stream(publicationCollision,std::ios::binary);
        std::string marker((std::istreambuf_iterator<char>(stream)),{});
        require(marker=="competitor","no-replace publication overwrote competitor output");
    }
    Source finalEss(source/"test.ess"), finalSkse(source/"test.skse");
    require(finalEss.hash()==essHash && finalSkse.hash()==skseHash, "original drift");
    const auto guarded=source/"guarded.ess";
    write(guarded,ess);
    write(source/"guarded.skse",fixture("1 "+std::to_string((std::uint64_t{62}<<32)|0x123456)
        +" 1 intel_factsrendered text"));
    require(fix(guarded,gameStartsBeforePublication)==2,"final running-game refusal has wrong exit code");
    require(!fs::exists(source/"guarded_IEconverted.ess") &&
        fs::exists(source/"guarded_IEbackup/original.ess"),"final game guard published output");
    const auto locked=CreateFileW(guarded.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    require(locked!=INVALID_HANDLE_VALUE,"cannot create source I/O failure");
    const auto status=fix(guarded,[] {});
    CloseHandle(locked);
    require(status==3,"source I/O failure has wrong exit code");
}
}
int main(int argc, char** argv) {
    try {
        using namespace LCM;
        require(argc==2, "provide one unused scratch directory; retained evidence is not auto-deleted");
        const std::vector<std::string> originals{"qz7\n%0A\t\\n", "gz9\v\f\r\xC3\xA9", "hz2\r\n%25\"x\""};
        const std::vector<std::string> encoded{"qz7%0A%250A%09\\n", "gz9%0B%0C%0D\xC3\xA9", "hz2%0D%0A%2525\"x\""};
        auto payload=[&](const std::vector<std::string>& values) {
            return "1 0 4 intel_factsrendered "+values[0]+" intel_gossiprendered "+values[1]+" intel_taskhistoryrendered "+values[2]+" rne_name qzName9";
        };
        const auto input=fixture(payload(originals)), expected=fixture(payload(encoded));
        auto manifest=evidence(input,originals);
        const auto result=convert(input,parse(input),manifest);
        const auto nonCache=fixture("1 0 3 intel_factsrendered a\nb intel_target The\aBannered\aMare%0A rne_name Stored\aName");
        const auto nonCacheExpected=fixture("1 0 3 intel_factsrendered a%0Ab intel_target The\aBannered\aMare%0A rne_name Stored\aName");
        require(convert(nonCache,parse(nonCache),evidence(nonCache,{"a\nb"},{"legacy"})).bytes==nonCacheExpected,
            "global non-cache BEL/percent bytes were interpreted");
        const auto actor1=(std::uint64_t{62}<<32)|0x00123456, actor2=(std::uint64_t{62}<<32)|0x00765432;
        const std::vector<std::string> actorOriginals{"facts with space\nline","gossip with space\nline","task history\nline"};
        const std::vector<std::string> actorEncoded{"facts\awith\aspace%0Aline","gossip\awith\aspace%0Aline","task\ahistory%0Aline"};
        auto actorPayload=[&](std::uint64_t first, std::uint64_t second, const std::vector<std::string>& values) {
            return "3 "+std::to_string(first)+" 2 intel_factsrendered "+values[0]+" intel_gossiprendered "+values[1]
                +" "+std::to_string(second)+" 4 intel_taskhistoryrendered "+values[2]+" rne_name qz\aName9"
                +" intel_target The\aBannered\aMare%0A intel_lingerdestname Destination\a%25"
                +" "+std::to_string((std::uint64_t{62}<<32)|0xff000dc9)+" 1 rne_name preserved";
        };
        std::vector<std::string> actorRaw;
        for (const auto& value : actorOriginals) actorRaw.push_back(transport(value));
        const auto actorInput=fixture(actorPayload(actor1,actor2,actorRaw));
        const auto actorExpected=fixture(actorPayload(actor1,actor2,actorEncoded));
        auto actorManifest=evidence(actorInput,actorOriginals,{"legacy","legacy","legacy"},"actor-in-place");
        require(convert(actorInput,parse(actorInput,true),fixEvidence(actorInput,parse(actorInput,true)),true).bytes==actorExpected,
            "owner-policy fix conversion bytes differ");
        for (const auto tuple : {std::pair{Skse,Runtime},std::pair{Skse17104,Runtime17104},
                                std::pair{SkseVR,RuntimeVR},std::pair{Skse1597,Runtime1597}}) {
            auto runtimeInput=actorInput, runtimeExpected=actorExpected;
            for (auto* bytes : {&runtimeInput,&runtimeExpected}) {
                put32(*bytes,8,tuple.first); put32(*bytes,12,tuple.second);
            }
            require(convert(runtimeInput,parse(runtimeInput,true),fixEvidence(runtimeInput,parse(runtimeInput,true)),true).bytes==runtimeExpected,
                "supported fix runtime rejected");
        }
        auto otherBuild=actorInput; put32(otherBuild,8,0x12345678);
        refuses([&] { parse(otherBuild,true); }, "unsupported fix SKSE header");
        auto crossedBuild=actorInput; put32(crossedBuild,8,Skse17104);
        refuses([&] { parse(crossedBuild,true); }, "cross-runtime fix SKSE header");
        const auto noTargets=fixture("1 0 1 unrelated x");
        refuses([&] { fixEvidence(noTargets,parse(noTargets,true)); }, "zero legacy targets");
        auto wrongStrv=actorInput; put32(wrongStrv,32,0x12345678);
        refuses([&] { parse(wrongStrv,true); }, "wrong fix STRV");
        auto wrongOwner=actorInput; put32(wrongOwner,20,0x12345678);
        refuses([&] { parse(wrongOwner,true); }, "wrong fix owner");
        auto wrongRuntime=actorInput; put32(wrongRuntime,12,0x01064000);
        refuses([&] { parse(wrongRuntime,true); }, "unsupported fix runtime");
        refuses([&] { gameRunning([] { return true; }); }, "running game seam");
        require(!gameRunning([] { return false; }), "closed-game seam");
        refuses([&] { gameRunning([] { throw std::runtime_error("enumeration failed"); return false; }); },
            "process enumeration failure seam");
        const auto actorConverted=convert(actorInput,parse(actorInput),actorManifest);
        require(actorConverted.bytes==actorExpected && actorConverted.legacy==3, "actor-in-place exact bytes or family coverage differs");
        for (std::size_t i=0; i<actorOriginals.size(); ++i)
            require(actorConverted.changes[i]["original_hex"]==hex(actorOriginals[i]),
                "actor production conversion did not preserve exact original bytes");
        const auto actorParsed=parse(actorExpected);
        require(actorParsed.fields.size()==7 && actorParsed.fields[0].object==actor1 && actorParsed.fields[1].object==actor1
            && actorParsed.fields[2].object==actor2 && actorParsed.fields[3].object==actor2
            && actorParsed.fields[0].key=="intel_factsrendered" && actorParsed.fields[1].key=="intel_gossiprendered"
            && actorParsed.fields[2].key=="intel_taskhistoryrendered" && actorParsed.fields[3].key=="rne_name"
            && view(actorExpected,actorParsed.fields[3].start,actorParsed.fields[3].end)=="qz\aName9"
            && view(actorExpected,actorParsed.fields[4].start,actorParsed.fields[4].end)=="The\aBannered\aMare%0A"
            && view(actorExpected,actorParsed.fields[5].start,actorParsed.fields[5].end)=="Destination\a%25"
            && actorParsed.fields[6].object==((std::uint64_t{62}<<32)|0xff000dc9)
            && actorParsed.fields[6].key=="rne_name"
            && view(actorExpected,actorParsed.fields[6].start,actorParsed.fields[6].end)=="preserved",
            "actor identity/key/rne_name BEL preservation changed");
        auto input17104=actorInput, expected17104=actorExpected;
        for (auto* bytes : {&input17104,&expected17104}) {
            put32(*bytes,8,Skse17104); put32(*bytes,12,Runtime17104);
        }
        auto proof17104=evidence(input17104,actorOriginals,{"legacy","legacy","legacy"},"actor-in-place");
        proof17104["runtime"]="1.7.104.0"; proof17104["provider_sha256"]=Provider17104;
        require(convert(input17104,parse(input17104),proof17104).bytes==expected17104,
            "qualified 1.7.104 actor conversion differs");
        for (const auto field : {"runtime","provider_sha256"}) {
            auto mismatch=proof17104; mismatch[field]=actorManifest[field];
            refuses([&] { convert(input17104,parse(input17104),mismatch); },
                "runtime/provider relabeling accepted");
        }
        auto mismatchedHeader=input17104; put32(mismatchedHeader,8,Skse);
        require(!parse(mismatchedHeader).refusals.empty(), "cross-runtime SKSE header accepted");
        auto unknownRuntime=input17104; put32(unknownRuntime,12,Runtime17104+1);
        require(!parse(unknownRuntime).refusals.empty(), "unknown runtime accepted");
        auto policyManifest=actorManifest;
        policyManifest["fields"][0]["representation"]="legacy-policy";
        policyManifest["fields"][0]["original_basis"]="owner-policy";
        require(convert(actorInput,parse(actorInput),policyManifest).bytes==actorExpected, "owner-policy actor proof changed output");
        auto badActor=policyManifest; badActor["fields"][0].erase("original_basis");
        refuses([&] { convert(actorInput,parse(actorInput),badActor); }, "missing owner-policy original basis");
        badActor=actorManifest; badActor.erase("actor_bel_policy");
        refuses([&] { convert(actorInput,parse(actorInput),badActor); }, "missing actor BEL policy");
        badActor=actorManifest; badActor["fields"][0]["original_hex"]=hex("facts\a with space\nline");
        refuses([&] { convert(actorInput,parse(actorInput),badActor); }, "actor original raw BEL mismatch");
        badActor=actorManifest; badActor.erase("preserve_rne_name_bel");
        refuses([&] { convert(actorInput,parse(actorInput),badActor); }, "missing rne_name BEL preservation policy");
        badActor=actorManifest; badActor["legacy_writer"]="unknown";
        refuses([&] { convert(actorInput,parse(actorInput),badActor); }, "unqualified actor legacy writer");
        for (const auto& control : {std::string("\x1b",1),std::string("\x01",1),std::string("\x7f",1),
                                    std::string("\t",1),std::string("\r",1),std::string("\v",1),std::string("\f",1)}) {
            auto unsafeActor=actorRaw;
            unsafeActor[0]="facts"+control+"line";
            const auto blob=fixture(actorPayload(actor1,actor2,unsafeActor));
            refuses([&] {
                const auto proof=evidence(blob,{"facts"+control+"line",actorOriginals[1],actorOriginals[2]},
                    {"legacy","legacy","legacy"},"actor-in-place");
                convert(blob,parse(blob),proof);
            }, "actor raw ESC/C0/DEL");
            refuses([&] { convert(blob,parse(blob,true),fixEvidence(blob,parse(blob,true)),true); },
                "fix raw ESC/C0/DEL");
        }
        for (const auto invalid : {std::uint64_t{62}<<32, (std::uint64_t{62}<<32)|0xff000001}) {
            const auto blob=fixture(actorPayload(invalid,actor2,actorRaw));
            const auto parsed=parse(blob);
            require(!parsed.refusals.empty(), "actor zero/temporary typed identity was not classified");
            const auto proof=evidence(blob,actorOriginals,{"legacy","legacy","legacy"},"actor-in-place");
            refuses([&] { convert(blob,parsed,proof); }, "actor zero/temporary typed identity");
            refuses([&] { convert(blob,parse(blob,true),fixEvidence(blob,parse(blob,true)),true); },
                "fix temporary cache target");
        }
        auto actorEsc=actorRaw; actorEsc[0]="\x1b";
        const auto actorEscInput=fixture(actorPayload(actor1,actor2,actorEsc));
        auto actorEscProof=evidence(actorEscInput,{"",actorOriginals[1],actorOriginals[2]},
            {"encoded","legacy","legacy"},"actor-in-place");
        refuses([&] { convert(actorEscInput,parse(actorEscInput),actorEscProof); },
            "actor encoded empty/ESC control bypass");
        require(result.bytes==expected && result.legacy==3, "all-family whole-file conversion differs from independent bytes");
        const auto known=evidence(expected,originals,{"encoded","encoded","encoded"});
        require(convert(expected,parse(expected),known).bytes==expected, "encoded control double-encoded");
        auto mixedValues=originals; mixedValues[0]=encoded[0];
        const auto mixed=fixture(payload(mixedValues));
        const auto mixedProof=evidence(mixed,originals,{"encoded","legacy","legacy"});
        require(convert(mixed,parse(mixed),mixedProof).bytes==expected, "mixed representation changed known encoded field");
        for (const auto& boundary : std::vector<std::pair<std::string,std::string>>{
            {"","\x1b"},{"\a","%07"},{"\x1b","%1B"},{"  qz7  ","\a\aqz7\a\a"}}) {
            auto values=originals, before=originals, after=encoded;
            values[0]=boundary.first; before[0]=after[0]=boundary.second;
            const auto blob=fixture(payload(before));
            require(convert(blob,parse(blob),evidence(blob,values,{"encoded","legacy","legacy"})).bytes
                == fixture(payload(after)), "pinned encoded space/empty/control boundary changed");
        }
        auto bad=manifest; bad["fields"][0]["original_hex"]="78";
        refuses([&] { convert(input,parse(input),bad); },"wrong original");
        bad=known; bad["fields"][0]["writer_codec_sha256"]="unknown";
        refuses([&] { convert(expected,parse(expected),bad); },"unpinned encoded generation");
        bad=manifest; bad["fields"][0]["representation"]="unknown";
        refuses([&] { convert(input,parse(input),bad); },"unknown representation");
        bad=manifest; bad["fields"].erase(1);
        refuses([&] { convert(input,parse(input),bad); },"incomplete family coverage");
        for (const std::string& sentinel : {std::string("x\ay"),std::string("\x1b")}) {
            auto values=originals; values[0]=sentinel;
            const auto blob=fixture(payload(values));
            refuses([&] { convert(blob,parse(blob),evidence(blob,values)); },"legacy BEL/ESC");
        }
        for (const auto& unsafe : {
            std::string("2 0 1 intel_factsrendered qz9"), // Count gap.
            std::string("1 1 1 intel_factsrendered qz9"), // Unqualified fixed form.
            std::string("1 4278190081 1 intel_factsrendered qz9"), // Temporary-like.
            std::string("1 18446744073709551615 1 intel_factsrendered qz9"),
            std::string("1 0 1 Intel_FactsRendered qz9"),
            std::string("1 0 2 intel_factsrendered qz9 \x1b value"),
            std::string("1 0 2 intel_factsrendered qz9 rne_name bad\nvalue")}) {
            const auto blob=fixture(unsafe);
            const auto p=parse(blob);
            require(!p.refusals.empty(), "unqualified whole-stream case missed");
            refuses([&] { convert(blob,p,Json::object()); },"unqualified whole-stream case");
        }
        for (const auto& malformed : {
            "1 0 1 key", "1 0 2 key value", "1 0 2 key value KEY other", "2 0 1 k v 0 1 q z",
            "0 0 1 k v", "1 18446744073709551616 1 k v", "1 0 1 k v ", "1  0 1 k v", "01 0 1 k v"}) {
            refuses([&] { parse(fixture(malformed)); },malformed);
        }
        require(parse(fixture("1 0 1 k v")).fields.size()==1, "minimal valid token count rejected");
        for (const auto& invalid : {std::string("x\0y",3),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3)}) {
            auto values=originals; values[0]=invalid;
            refuses([&] { parse(fixture(payload(values))); },"unsupported text");
        }
        for (std::size_t size=0; size<input.size(); ++size) {
            Bytes truncated(input.begin(),input.begin()+size);
            refuses([&] { parse(truncated); },"truncated binary");
        }
        for (auto offset : {4u,16u,24u,28u,36u,40u}) {
            auto broken=input; put32(broken,offset,0xffffffff);
            refuses([&] { parse(broken); },"malformed binary count/version/length");
        }
        auto trailing=input; trailing.push_back(0);
        refuses([&] { parse(trailing); },"trailing binary bytes");
        filesystemCheck(fs::u8path(argv[1]),input,expected);
        std::cout << "PASS: legacy-policy fix evidence, synthetic 1.5.97 / SKSE64 2.0.20 and SE/AE/VR co-save tuples, VR ESS v12; process guard seam, wrong runtime/owner/STRV refusals; exact conversion/name preservation/control and temporary-identity refusals; global/expert conversion; locked backups/staging/no-replace publication including retained incomplete pair and unchanged originals.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
