// One deterministic standalone behavioral check; includes the actual CLI core.
#define LCM_NO_MAIN
#include "../migration/LegacyCacheMigration.cpp"
#include <fstream>
#include <functional>

namespace {
using namespace LCM;
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
              const std::vector<std::string>& representations={"legacy","legacy","legacy"}) {
    Json fields=Json::array(); std::size_t i=0;
    for (const auto& f : parse(input).fields) if (target(f.key)) {
        fields.push_back({{"object",std::to_string(f.object)},{"key",f.key},{"start",f.start},{"end",f.end},
            {"original_hex",hex(original.at(i))},{"representation",representations.at(i)},
            {"writer_codec_sha256",LCM_CODEC_SHA256},{"evidence","independently specified synthetic input"}});
        ++i;
    }
    return {{"schema",1},{"skse_sha256",sha256(input)},{"provider_sha256",Provider},
        {"runtime","1.6.1170.0"},{"scope","global"},{"codec_sha256",LCM_CODEC_SHA256},
        {"qualification_receipt_sha256",std::string(64,'a')},{"fields",fields}};
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
void filesystemCheck(const fs::path& area, const Bytes& input, const Bytes& converted) {
    require(!fs::exists(area), "self-check area must not exist");
    require(fs::create_directory(area), "cannot create exclusive self-check area");
    const auto source=area/"inputs", destinations=area/"results", protectedDir=area/"protected";
    fs::create_directory(source); fs::create_directory(destinations); fs::create_directory(protectedDir);
    const Bytes ess{'T','E','S','V','_','S','A','V','E','G','A','M','E',0,9,255};
    write(source/"test.ess",ess); write(source/"test.skse",input);
    const auto essHash=sha256(ess), skseHash=sha256(input);
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
    Source finalEss(source/"test.ess"), finalSkse(source/"test.skse");
    require(finalEss.hash()==essHash && finalSkse.hash()==skseHash, "original drift");
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
            std::string("1 0 2 intel_factsrendered qz9 rne_name bad\nvalue"),
            std::string("1 0 2 intel_factsrendered qz9 rne_name bad\avalue")}) {
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
        std::cout << "PASS: exact all-family/literal-percent/control/UTF-8 bytes; mixed/encoded preservation; trailing names/opaque chunks; sentinel/identity/count/case/framing refusals; exclusive locked backup/stage/publication, interrupted/failed publication and unchanged originals.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
