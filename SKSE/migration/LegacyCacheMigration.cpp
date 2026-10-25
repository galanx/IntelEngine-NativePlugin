// Offline Windows C++17 tool. Link LegacyCacheMigrationIO.cpp and bcrypt; use the
// repository's existing nlohmann headers. Define LCM_CODEC_SHA256 from the actual
// RenderedCacheCodec.h used at build time; --codec verifies it again at execution.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "LegacyCacheMigrationIO.h"
#include "../src/RenderedCacheCodec.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <map>
#include <set>
#include <string_view>

#include <TlHelp32.h>
#include <functional>
#include <shellapi.h>
#ifndef LCM_CODEC_SHA256
#error Define LCM_CODEC_SHA256 as the SHA256 string of the included production header
#endif

namespace LCM {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr const char* Provider = "2fb155b8508e085616c5f72cb8fd2368275557dc56988300b92e540930d1f444";
constexpr std::uint32_t Owner = 0x424510A2, Strv = 0x53545256;
constexpr std::uint32_t Runtime = 0x01064920, Skse = 0x02020060;
// Released 2.2.7/2.2.8 still target 1.6.1170; see upstream skse64_whatsnew.txt.
constexpr std::uint32_t Skse227 = 0x02020070, Skse228 = 0x02020080;
constexpr const char* Provider17104 = "69260a675186cf781a2105002505ab224fee12e84d004a3787670c9d6ce936be";
constexpr std::uint32_t Runtime17104 = 0x01070680, Skse17104 = 0x02030010;
constexpr std::uint32_t RuntimeVR = 0x010400F1, SkseVR = 0x020000C0;
// SKSE64 v2.0.20 skse_version.h MAKE_EXE_VERSION: (major<<24)|(minor<<16)|(build<<4).
constexpr std::uint32_t Runtime1597 = 0x01050610, Skse1597 = 0x02000140;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string hex(std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(text.size() * 2);
    for (unsigned char c : text) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string unhex(const std::string& text) {
    require(text.size() % 2 == 0, "invalid expected hex length");
    std::string result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw std::runtime_error("expected hex must be lowercase hexadecimal");
        };
        result += static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1]));
    }
    return result;
}
std::string_view view(const Bytes& bytes, std::size_t start, std::size_t end) {
    require(start <= end && end <= bytes.size(), "span outside file");
    return {reinterpret_cast<const char*>(bytes.data() + start), end - start};
}
std::uint32_t u32(const Bytes& bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "partial binary header");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset+1]) << 8) |
           (std::uint32_t(bytes[offset+2]) << 16) | (std::uint32_t(bytes[offset+3]) << 24);
}
bool qualifiedRuntime(const Bytes& bytes, const Json& basis) {
    return ((u32(bytes,8) == Skse || u32(bytes,8) == Skse227 || u32(bytes,8) == Skse228) && u32(bytes,12) == Runtime &&
            basis.at("runtime") == "1.6.1170.0" && basis.at("provider_sha256") == Provider) ||
           (u32(bytes,8) == Skse17104 && u32(bytes,12) == Runtime17104 &&
            basis.at("runtime") == "1.7.104.0" && basis.at("provider_sha256") == Provider17104);
}
void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "length patch outside header");
    for (unsigned i = 0; i < 4; ++i) bytes[offset+i] = static_cast<unsigned char>(value >> (8*i));
}
std::uint64_t decimal(std::string_view text) {
    require(!text.empty() && (text.size() == 1 || text.front() != '0'), "noncanonical decimal");
    std::uint64_t result = 0;
    auto parsed = std::from_chars(text.data(), text.data()+text.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size(), "invalid decimal/overflow");
    return result;
}
bool rawDelimiter(unsigned char c) { return c >= 9 && c <= 13; }
void text(std::string_view value) {
    // Strict UTF-8, excluding unqualified C0/DEL bytes. No locale-dependent lexer.
    for (std::size_t i = 0; i < value.size();) {
        unsigned char c = value[i++];
        if (c < 0x80) {
            require(c >= 32 || c == 7 || rawDelimiter(c) || c == 27, "unsupported text control/NUL");
            require(c != 127, "unsupported DEL");
            continue;
        }
        unsigned count; std::uint32_t point, minimum;
        if (c >= 0xc2 && c <= 0xdf) { count=1; point=c & 31; minimum=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count=2; point=c & 15; minimum=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count=3; point=c & 7; minimum=0x10000; }
        else throw std::runtime_error("invalid UTF-8 lead");
        require(count <= value.size()-i, "partial UTF-8");
        while (count--) {
            unsigned char next=value[i++];
            require((next & 0xc0) == 0x80, "invalid UTF-8 continuation");
            point=(point << 6) | (next & 63);
        }
        require(point >= minimum && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff), "invalid UTF-8 scalar");
    }
}
std::string lower(std::string key) {
    for (char& c : key) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    return key;
}
bool target(std::string_view key) {
    return key == "intel_factsrendered" || key == "intel_gossiprendered" || key == "intel_taskhistoryrendered";
}
struct Field {
    std::uint64_t object;
    std::string key;
    std::size_t start, end;
};
struct Parsed {
    std::size_t plugin=0, chunk=0, begin=0, end=0;
    std::uint64_t declared=0, written=0;
    std::vector<Field> fields;
    Json framing=Json::array(), refusals=Json::array();
};
Parsed parse(const Bytes& bytes, bool fixMode = false) {
    require(bytes.size() >= 20 && view(bytes,0,4) == "SKSE", "not an SKSE co-save");
    require(u32(bytes,4) == 1, fixMode ? "unsupported co-save format; expected format 1" : "unsupported co-save version");
    Parsed result;
    std::size_t offset=20;
    const auto plugins=u32(bytes,16);
    require(plugins <= (bytes.size()-20)/12, "impossible plugin count");
    unsigned ownerCount=0;
    unsigned candidates=0;
    for (std::uint32_t i=0; i<plugins; ++i) {
        require(offset <= bytes.size() && bytes.size()-offset >= 12, "partial plugin header");
        const auto owner=u32(bytes,offset), chunks=u32(bytes,offset+4), length=u32(bytes,offset+8);
        if (owner == Owner) require(++ownerCount == 1, "duplicate PapyrusUtil owner");
        std::size_t begin=offset+12;
        require(length <= bytes.size()-begin, "plugin length exceeds file");
        const auto end=begin+length;
        require(chunks <= length/12, "impossible chunk count");
        auto cursor=begin;
        for (std::uint32_t j=0; j<chunks; ++j) {
            require(cursor <= end && end-cursor >= 12, "partial chunk header");
            const auto kind=u32(bytes,cursor), version=u32(bytes,cursor+4), size=u32(bytes,cursor+8);
            const auto start=cursor+12;
            require(size <= end-start, "chunk length exceeds plugin");
            result.framing.push_back({{"owner",owner},{"type",kind},{"version",version},{"header",cursor},{"start",start},{"end",start+size}});
            if (owner == Owner && kind == Strv) {
                require(++candidates == 1, "ambiguous STRV records");
                require(version == 1, "unsupported STRV version");
                result.plugin=offset; result.chunk=cursor; result.begin=start; result.end=start+size;
            }
            cursor=start+size;
        }
        require(cursor == end, "plugin chunk count/length mismatch");
        offset=end;
    }
    require(offset == bytes.size(), "trailing bytes/plugin count mismatch");
    require(ownerCount == 1, "PapyrusUtil owner missing");
    const bool qualifiedHeader=((u32(bytes,8) == Skse || u32(bytes,8) == Skse227 || u32(bytes,8) == Skse228) && u32(bytes,12) == Runtime) ||
        (u32(bytes,8) == Skse17104 && u32(bytes,12) == Runtime17104) ||
        (u32(bytes,8) == SkseVR && u32(bytes,12) == RuntimeVR) ||
        (u32(bytes,8) == Skse1597 && u32(bytes,12) == Runtime1597);
    if (fixMode) require(qualifiedHeader,
        "unsupported co-save runtime; fix accepts Skyrim 1.5.97 / SKSE64 2.0.20 (untested), 1.6.1170 / SKSE64 2.2.6-2.2.8, 1.7.104 and VR 1.4.15 / SKSEVR 2.0.12");
    else if (!qualifiedHeader)
        result.refusals.push_back("unqualified SKSE/runtime header");
    if (fixMode && candidates == 0) {
        // A cache-free copy must not disguise an unknown/mistyped STRV record.
        constexpr std::uint32_t nonScalarStrings[] = {
            0x494E5456, 0x464C4F56, 0x464F5256, // INTV, FLOV, FORV
            0x494E544C, 0x464C4F4C, 0x5354524C, 0x464F524C, 0x504B474F // lists, PKGO
        };
        for (const auto& record : result.framing) if (record.at("owner") == Owner) {
            const auto kind=record.at("type").get<std::uint32_t>();
            const auto version=record.at("version").get<std::uint32_t>();
            require(((kind == 0x4D4F4453 || kind == 0x504C474E) && version == 0) ||
                (version == 1 && std::find(std::begin(nonScalarStrings),std::end(nonScalarStrings),kind) != std::end(nonScalarStrings)),
                "unqualified PapyrusUtil record in cache-free copy");
        }
        return result;
    }
    require(candidates == 1, "qualified owner/STRV missing");
    auto cursor=result.begin;
    auto token=[&]() -> std::pair<std::size_t,std::size_t> {
        require(cursor < result.end && bytes[cursor] != ' ', "empty/partial ASCII-space token");
        const auto start=cursor;
        while (cursor < result.end && bytes[cursor] != ' ') ++cursor;
        const auto end=cursor;
        if (cursor < result.end) {
            ++cursor;
            require(cursor < result.end && bytes[cursor] != ' ', "trailing/repeated separator");
        }
        return {start,end};
    };
    auto number=[&]() { const auto span=token(); return decimal(view(bytes,span.first,span.second)); };
    result.declared=number();
    require(result.declared <= std::numeric_limits<std::uint32_t>::max(), "object count overflow");
    std::map<std::uint64_t,bool> objects;
    while (cursor < result.end) {
        const auto object=number(), entries=number();
        auto [objectState,firstObject]=objects.try_emplace(object,false);
        require(entries > 0 && entries <= (result.end-cursor+1)/4, "invalid entry count");
        const auto form=static_cast<std::uint32_t>(object);
        std::set<std::string> keys;
        bool carriesTarget=false;
        for (std::uint64_t n=0; n<entries; ++n) {
            const auto k=token(), v=token();
            std::string key(view(bytes,k.first,k.second));
            auto folded=lower(key);
            if (folded == "\x1b") folded.clear();
            for (char& c : folded) if (c == '\a') c=' ';
            const bool targetAlias=target(folded);
            if (targetAlias) {
                text(key);
                if (key != folded) result.refusals.push_back("unqualified key text/transport/casing at "+std::to_string(k.first));
                require(keys.insert(folded).second, "duplicate/restored/case-ambiguous key");
            }
            const auto value=view(bytes,v.first,v.second);
            if (targetAlias) {
                text(value);
                carriesTarget=true;
                // No resolution for untouched names; temporary cache targets remain unqualified.
                if (target(key) && (form >> 24) == 0xff)
                    result.refusals.push_back("unqualified temporary cache identity: "+std::to_string(object));
            }
            result.fields.push_back({object,std::move(key),v.first,v.second});
        }
        require(firstObject || (!objectState->second && !carriesTarget), "duplicate serialized object");
        if (carriesTarget) objectState->second=true;
        if (carriesTarget && object != 0 && (object >> 32 != 62 || form == 0))
            result.refusals.push_back("unqualified non-global identity: "+std::to_string(object));
        ++result.written;
        require(result.written <= result.declared, "written count exceeds declaration");
    }
    require(cursor == result.end, "STRV not fully consumed");
    if (result.written != result.declared) result.refusals.push_back("unqualified empty-map count gap");
    return result;
}
void verifyNativeTargets(const Bytes& bytes, const Parsed& parsed) {
    // Mirror PapyrusUtil's native extraction without copying opaque string values.
    struct NativeView : std::streambuf {
        NativeView(const Bytes& input, std::size_t start, std::size_t end) {
            auto* data=reinterpret_cast<char*>(const_cast<unsigned char*>(input.data()));
            const auto nul=std::find(input.begin()+start,input.begin()+end,0);
            setg(data,data+start,data+(nul-input.begin()));
        }
        std::pair<std::size_t,std::size_t> token() {
            while (gptr() != egptr() && (*gptr() == ' ' || rawDelimiter(*gptr()))) gbump(1);
            const auto start=static_cast<std::size_t>(gptr()-eback());
            while (gptr() != egptr() && *gptr() != ' ' && !rawDelimiter(*gptr())) gbump(1);
            return {start,static_cast<std::size_t>(gptr()-eback())};
        }
    } buffer(bytes,parsed.begin,parsed.end);
    std::istream native(&buffer);
    auto expected=parsed.fields.begin();
    auto nextTarget=[&]() {
        while (expected != parsed.fields.end() && !target(expected->key)) ++expected;
    };
    nextTarget();
    if (expected == parsed.fields.end()) return;
    int objects=0;
    native >> objects;
    for (int i=0; native && i<objects; ++i) {
        std::uint64_t object=0;
        int entries=0;
        native >> object >> entries;
        for (int n=0; native && n<entries; ++n) {
            const auto key=buffer.token(), value=buffer.token();
            if (key.first == key.second || value.first == value.second) {
                native.setstate(std::ios::failbit);
                break;
            }
            if (!target(view(bytes,key.first,key.second))) continue;
            require(expected != parsed.fields.end() && expected->object == object &&
                expected->key == view(bytes,key.first,key.second) &&
                expected->start == value.first && expected->end == value.second,
                "ambiguous native target assignment");
            ++expected;
            nextTarget();
        }
    }
    require(expected == parsed.fields.end(), "rendered cache lost under native tokenization");
}
Json inspect(const Bytes& bytes, const Parsed& parsed) {
    Json fields=Json::array();
    for (const auto& f : parsed.fields) {
        Json field={{"object",std::to_string(f.object)},{"start",f.start},{"end",f.end},
            {"target",target(f.key)},{"value_hex",hex(view(bytes,f.start,f.end))}};
        try {
            text(f.key);
            field["key"]=f.key;
        } catch (const std::exception&) {
            field["key_hex"]=hex(f.key);
        }
        fields.push_back(std::move(field));
    }
    return {{"status","inspected_only"},{"source_sha256",sha256(bytes)},{"runtime_header",u32(bytes,12)},
        {"declared",parsed.declared},{"written",parsed.written},{"strv_start",parsed.begin},{"strv_end",parsed.end},
        {"framing",parsed.framing},{"fields",fields},{"refusals",parsed.refusals},
        {"limits","No classification from byte syntax; no form resolution, missing-data recovery or installed acceptance."}};
}
std::string transport(std::string value) {
    if (value.empty()) return "\x1b";
    for (char& c : value) if (c == ' ') c='\a';
    return value;
}
struct Converted { Bytes bytes; Json changes; std::size_t legacy=0; };
bool actorBasis(const Json& basis) {
    const auto scope=basis.at("scope").get<std::string>();
    require(scope == "global" || scope == "actor-in-place", "unqualified scope");
    if (scope == "global") return false;
    require(basis.at("legacy_writer") == "IE-3.5.2" && basis.at("actor_bel_policy") == "space" &&
        basis.at("preserve_rne_name_bel") == true, "missing approved actor transport policy");
    return true;
}
Converted convert(const Bytes& input, const Parsed& parsed, const Json& manifest, bool fixMode = false) {
    const auto inputHash=sha256(input);
    require(parsed.refusals.empty(), "whole-stream qualification refusal: "+parsed.refusals.dump());
    if (!fixMode) {
        require(manifest.at("schema") == 1, "unsupported evidence schema");
        require(manifest.at("skse_sha256") == inputHash, "SKSE evidence hash mismatch");
        require(qualifiedRuntime(input, manifest), "unqualified provider/runtime");
    } else {
        require(manifest.at("schema") == "legacy-policy" && manifest.at("skse_sha256") == inputHash,
            "internal legacy-policy evidence mismatch");
        require(manifest.at("legacy_writer") == "IE-3.5.2" && manifest.at("actor_bel_policy") == "space" &&
            manifest.at("preserve_rne_name_bel") == true, "missing approved legacy policy");
    }
    const bool actor=fixMode || actorBasis(manifest);
    if (fixMode) {
        for (const auto& f : parsed.fields) if (target(f.key))
            require(f.object != 0, "global cache target is not eligible for actor fix policy");
    } else {
        for (const auto& f : parsed.fields) if (target(f.key))
            require(actor ? f.object != 0 : f.object == 0, "identity outside qualified scope");
    }
    if (!fixMode) {
        require(manifest.at("codec_sha256") == LCM_CODEC_SHA256, "unqualified production codec generation");
        require(!manifest.at("qualification_receipt_sha256").get<std::string>().empty(), "missing qualification receipt hash");
    }
    const auto& evidence=manifest.at("fields");
    require(evidence.is_array(), "field manifest is not an array");
    std::size_t total=0;
    for (const auto& f : parsed.fields) if (target(f.key)) ++total;
    require(total == evidence.size(), "manifest must cover every target field");
    std::set<std::size_t> used;
    Converted output{Bytes{},Json::array(),0};
    std::size_t copied=0;
    for (const auto& f : parsed.fields) {
        if (!target(f.key)) continue;
        const Json* proof=nullptr;
        for (std::size_t i=0; i<evidence.size(); ++i) {
            const auto& item=evidence[i];
            if (item.at("object") == std::to_string(f.object) && item.at("key") == f.key) {
                require(!proof && used.insert(i).second, "duplicate field proof");
                proof=&item;
            }
        }
        require(proof != nullptr, "unknown target representation");
        require(proof->at("start") == f.start && proof->at("end") == f.end, "field evidence span mismatch");
        if (fixMode) {
            require(proof->at("representation") == "legacy-policy" && proof->at("original_basis") == "owner-policy" &&
                proof->at("source_sha256") == inputHash, "invalid internal legacy-policy evidence");
        } else require(!proof->at("evidence").get<std::string>().empty(), "missing independent field provenance");
        const auto raw=view(input,f.start,f.end);
        if (actor) for (unsigned char c : raw)
            require(c >= 32 ? c != 127 : c == 7 || c == 10, "actor raw ESC/other-control refused");
        const std::string original=unhex(proof->at("original_hex").get<std::string>());
        text(original);
        const auto representation=proof->at("representation").get<std::string>();
        std::string replacement;
        if (representation == "legacy" || representation == "legacy-policy") {
            if (actor) {
                if (representation == "legacy-policy")
                    require(proof->at("original_basis") == "owner-policy", "missing policy-derived original basis");
                for (unsigned char c : original)
                    require(c >= 32 ? c != 127 : c == 10, "actor original BEL/ESC/other-control refused");
                std::string restored(raw);
                for (char& c : restored) if (c == '\a') c=' ';
                require(restored == original, "actor original bytes disagree with restored span");
            } else {
                require(representation == "legacy", "actor policy outside actor scope");
                require(raw.find('\a') == std::string_view::npos && raw.find('\x1b') == std::string_view::npos, "legacy BEL/ESC collision: recovery refused");
                require(raw == original, "independent original bytes disagree with legacy span");
            }
            replacement=transport(IntelEngine::EncodeRenderedCache(original));
            ++output.legacy;
        } else if (representation == "encoded") {
            require(proof->at("writer_codec_sha256") == LCM_CODEC_SHA256, "encoded writer generation unpinned");
            require(raw == transport(IntelEngine::EncodeRenderedCache(original)), "encoded value disagrees with independently expected original");
            replacement.assign(raw);
        } else throw std::runtime_error("unknown target representation: refused");
        output.bytes.insert(output.bytes.end(),input.begin()+copied,input.begin()+f.start);
        output.bytes.insert(output.bytes.end(),replacement.begin(),replacement.end());
        copied=f.end;
        output.changes.push_back({{"object",std::to_string(f.object)},{"key",f.key},{"input_start",f.start},{"input_end",f.end},
            {"output_start",output.bytes.size()-replacement.size()},{"output_end",output.bytes.size()},
            {"representation",representation},{"original_hex",hex(original)},{"converted_hex",hex(replacement)},
            {"original_sha256",sha256(Bytes(original.begin(),original.end()))},
            {"input_sha256",sha256(Bytes(raw.begin(),raw.end()))},{"converted_sha256",sha256(Bytes(replacement.begin(),replacement.end()))}});
    }
    require(used.size() == evidence.size(), "extraneous field evidence");
    output.bytes.insert(output.bytes.end(),input.begin()+copied,input.end());
    const auto delta=output.bytes.size()-input.size(); // Production encoding never contracts eligible legacy bytes.
    require(output.bytes.size() >= input.size() && delta <= std::numeric_limits<std::uint32_t>::max(), "output length overflow");
    for (auto header : {parsed.plugin+8,parsed.chunk+8}) {
        const auto old=u32(input,header);
        require(delta <= std::numeric_limits<std::uint32_t>::max()-old, "enclosing length overflow");
        put32(output.bytes,header,old+static_cast<std::uint32_t>(delta));
    }
    const auto check=parse(output.bytes,fixMode);
    require(check.refusals.empty() && check.fields.size() == parsed.fields.size() && check.declared == parsed.declared && check.written == parsed.written, "output whole-stream verification failed");
    verifyNativeTargets(output.bytes,check);
    // Reverse each explicit value patch and the two necessary headers. Equality
    // proves every remaining byte, including opaque chunks and trailing names.
    Bytes restored;
    std::size_t cursor=0;
    for (const auto& change : output.changes) {
        auto start=change.at("output_start").get<std::size_t>(), end=change.at("output_end").get<std::size_t>();
        restored.insert(restored.end(),output.bytes.begin()+cursor,output.bytes.begin()+start);
        const auto a=change.at("input_start").get<std::size_t>(), b=change.at("input_end").get<std::size_t>();
        restored.insert(restored.end(),input.begin()+a,input.begin()+b);
        cursor=end;
        const auto converted=view(output.bytes,start,end);
        for (unsigned char c : converted) require(!rawDelimiter(c), "residual native delimiter");
    }
    restored.insert(restored.end(),output.bytes.begin()+cursor,output.bytes.end());
    put32(restored,parsed.plugin+8,u32(input,parsed.plugin+8));
    put32(restored,parsed.chunk+8,u32(input,parsed.chunk+8));
    require(restored == input, "unrelated byte ranges changed");
    return output;
}

struct GameSafetyRefusal : std::runtime_error { using std::runtime_error::runtime_error; };
bool gameRunning(const std::function<bool()>& isGameRunning = {}) {
    if (isGameRunning) {
        if (isGameRunning()) throw GameSafetyRefusal("Skyrim is running; close the game before fixing the save");
        return false;
    }
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot == INVALID_HANDLE_VALUE) throw GameSafetyRefusal("cannot enumerate processes; close other tools and retry");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    bool found=false;
    if (Process32FirstW(snapshot,&entry)) do {
        const std::wstring name(entry.szExeFile);
        if (_wcsicmp(name.c_str(),L"SkyrimSE.exe")==0 || _wcsicmp(name.c_str(),L"Skyrim.exe")==0 ||
            _wcsicmp(name.c_str(),L"SkyrimVR.exe")==0) { found=true; break; }
    } while (Process32NextW(snapshot,&entry));
    const auto error=GetLastError(); CloseHandle(snapshot);
    if (!found && error != ERROR_NO_MORE_FILES) throw GameSafetyRefusal("process enumeration failed; close other tools and retry");
    if (found) throw GameSafetyRefusal("Skyrim is running; close the game before fixing the save");
    return false;
}
Json fixEvidence(const Bytes& bytes, const Parsed& parsed) {
    Json fields=Json::array();
    const auto hash=sha256(bytes);
    for (const auto& f : parsed.fields) if (target(f.key)) {
        const auto raw=view(bytes,f.start,f.end);
        std::string original(raw);
        for (char& c : original) if (c=='\a') c=' ';
        fields.push_back({{"object",std::to_string(f.object)},{"key",f.key},{"start",f.start},{"end",f.end},
            {"source_sha256",hash},{"representation","legacy-policy"},{"original_basis","owner-policy"},
            {"original_hex",hex(original)}});
    }
    require(!fields.empty(), "no eligible legacy cache targets found; original save was not changed");
    return {{"schema","legacy-policy"},{"skse_sha256",hash},{"legacy_writer","IE-3.5.2"},
        {"actor_bel_policy","space"},{"preserve_rne_name_bel",true},{"fields",fields}};
}
void verifyBeforePublish() { gameRunning(); }
int fix(const fs::path& essPath, void (*requireGameClosed)() = verifyBeforePublish) {
    try {
    requireGameClosed();
    require(essPath.extension()==".ess", "fix requires one .ess save; select an ESS file");
    const auto base=fs::absolute(essPath).parent_path()/essPath.stem();
    const auto sksePath=fs::path(base.wstring()+L".skse");
    require(fs::exists(essPath) && fs::exists(sksePath), "complete same-directory, same-stem .ess/.skse pair required");
    require(!sameFile(essPath,sksePath), "ESS and co-save resolve to the same file; choose a valid pair");
    Source ess(essPath), skse(sksePath);
    require(ess.bytes().size()>=13 && view(ess.bytes(),0,13)=="TESV_SAVEGAME", "ESS is empty or lacks Skyrim save signature");
    auto parsed=parse(skse.bytes(),true);
    if (u32(skse.bytes(),12) == RuntimeVR) {
        require(ess.bytes().size() >= 21 && u32(ess.bytes(),13) >= 4 &&
            u32(ess.bytes(),13) <= ess.bytes().size()-17 && u32(ess.bytes(),17) == 12,
            "unsupported VR ESS header; expected TESV_SAVEGAME version 12");
    }
    auto converted=parsed.chunk == 0 ? Converted{skse.bytes(),Json::array(),0} :
        convert(skse.bytes(),parsed,fixEvidence(skse.bytes(),parsed),true);
    const auto backup=fs::path(base.wstring()+L"_IEbackup");
    const auto outputEss=fs::path(base.wstring()+L"_IEconverted.ess");
    const auto outputSkse=fs::path(base.wstring()+L"_IEconverted.skse");
    const auto staging=fs::path(base.wstring()+L"_IEconverted.staging");
    require(!fs::exists(backup) && !fs::exists(outputEss) && !fs::exists(outputSkse) && !fs::exists(staging),
        "backup/output/staging path already exists; move it aside and retry");
    try { publishFixedPair(essPath,ess,skse,converted.bytes,requireGameClosed); }
    catch (const GameSafetyRefusal& e) {
        std::cerr << "Fix refused: " << e.what() << "\nBackup: " << backup.u8string()
            << "\nStaging: " << staging.u8string() << "\nNo converted pair published; originals unchanged.\n";
        return 2;
    }
    catch (const std::exception& e) {
        std::cerr << "I/O/publication failed: " << e.what() << "\nBackup: " << backup.u8string()
            << "\nStaging: " << staging.u8string() << "\nOutputs: " << outputEss.u8string() << " and "
            << outputSkse.u8string() << "\nInspect retained files before retrying; originals were not changed.\n";
        return 3;
    }
    std::size_t facts=0,gossip=0,history=0;
    for (const auto& change : converted.changes) {
        const auto key=change.at("key").get<std::string>();
        if (key=="intel_factsrendered") ++facts;
        else if (key=="intel_gossiprendered") ++gossip;
        else if (key=="intel_taskhistoryrendered") ++history;
    }
    std::size_t names=0;
    for (const auto& field : parsed.fields) if (field.key=="rne_name") ++names;
    if (converted.legacy)
        std::cout << "Fixed " << converted.legacy << " legacy cache value(s): facts " << facts << ", gossip " << gossip
            << ", task history " << history << "; preserved " << names << " serialized name span(s).\n";
    else std::cout << "Copied cache-free pair unchanged; no scalar string record to repair.\n";
    std::cout << "Output: " << outputEss.u8string() << " and " << outputSkse.u8string() << "\nBackup: " << backup.u8string()
        << "\nOriginals unchanged. Use a coherent repaired IE DLL/PEX/prompts set; these repaired save components may not match existing IE data. Never load converted saves under IE 3.5.2.\n";
    return 0;
    } catch (const IOError& e) {
        std::cerr << "Save I/O/verification failed: " << e.what()
            << "\nCheck file permissions and storage, then retry. Originals unchanged.\n";
        return 3;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Save filesystem access failed: " << e.what()
            << "\nCheck paths and storage, then retry. Originals unchanged.\n";
        return 3;
    }
}

#ifndef LCM_NO_MAIN
Json readJson(const Source& source) {
    return Json::parse(source.bytes().begin(),source.bytes().end());
}
int run(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "fix --save FILE.ess\ninspect --skse FILE\nconvert --ess FILE --skse FILE --evidence JSON --qualification JSON --codec HEADER --backup NEW_DIR --output NEW_DIR --protected-dir LIVE_SAVE_DIR [--protected-dir DIR ...] [--archive FILE]\n"
                  << "Offline only. Global independent evidence or explicitly qualified IE-3.5.2 actor-in-place transport policy. No force, installation, loading or runtime form resolution.\n";
        return 0;
    }
    require(argc >= 4, "use --help for exact inspect/convert arguments");
    std::string command=argv[1];
    require(command == "inspect" || command == "convert", "unknown command");
    std::map<std::string,std::string> args;
    std::vector<fs::path> protectedDirs;
    const std::set<std::string> allowed=command == "inspect" ? std::set<std::string>{"--skse"} :
        std::set<std::string>{"--ess","--skse","--evidence","--qualification","--codec","--backup","--output","--archive","--protected-dir"};
    for (int i=2; i<argc; i+=2) {
        require(i+1 < argc && allowed.count(argv[i]), "unknown/missing CLI argument");
        if (std::string(argv[i]) == "--protected-dir") protectedDirs.emplace_back(fs::u8path(argv[i+1]));
        else require(args.emplace(argv[i],argv[i+1]).second, "duplicate CLI argument");
    }
    auto path=[&](const char* name) { require(args.count(name), std::string("missing ")+name); return fs::u8path(args.at(name)); };
    Source skse(path("--skse"));
    if (command == "inspect") { std::cout << inspect(skse.bytes(),parse(skse.bytes())).dump(2) << '\n'; skse.verify(); return 0; }
    Source ess(path("--ess")), evidence(path("--evidence")), qualification(path("--qualification")), codec(path("--codec"));
    require(!sameFile(path("--ess"),path("--skse")), "companion identity alias");
    require(path("--ess").extension() == ".ess" && path("--skse").extension() == ".skse" && path("--ess").stem() == path("--skse").stem() && fs::equivalent(fs::absolute(path("--ess")).parent_path(),fs::absolute(path("--skse")).parent_path()), "matched ESS/SKSE pair required");
    require(!protectedDirs.empty(), "explicit live-save protected directory required");
    protectedDirs.push_back(fs::absolute(path("--ess")).parent_path());
    protectedDirs.push_back(fs::absolute(path("--evidence")).parent_path());
    protectedDirs.push_back(fs::absolute(path("--qualification")).parent_path());
    protectedDirs.push_back(fs::absolute(path("--codec")).parent_path());
    const auto manifest=readJson(evidence), receipt=readJson(qualification);
    require(codec.hash() == LCM_CODEC_SHA256, "build/runtime production codec identity mismatch");
    require(manifest.at("ess_sha256") == ess.hash() && manifest.at("skse_sha256") == skse.hash(), "source pair hash mismatch");
    require(manifest.at("qualification_receipt_sha256") == qualification.hash(), "qualification receipt hash mismatch");
    require(qualifiedRuntime(skse.bytes(), receipt) &&
        receipt.at("owner_authorized_offline") == true && actorBasis(receipt) == actorBasis(manifest), "qualification receipt lacks approved restricted basis");
    Json archiveInfo=nullptr;
    std::unique_ptr<Source> archive;
    if (args.count("--archive")) {
        archive=std::make_unique<Source>(path("--archive"));
        const auto& provenance=manifest.at("archive");
        require(provenance.at("sha256") == archive->hash() && provenance.at("ess_member_sha256") == ess.hash() && provenance.at("skse_member_sha256") == skse.hash(), "archive/pair provenance hash mismatch");
        require(!provenance.at("membership_evidence").get<std::string>().empty(), "missing independently qualified archive membership");
        archiveInfo=provenance;
        archiveInfo["membership_verification"]="external evidence; this tool does not extract archives";
        protectedDirs.push_back(fs::absolute(path("--archive")).parent_path());
    } else require(!manifest.contains("archive"), "archive provenance requires original archive hash verification");
    wchar_t executable[32768];
    const auto executableLength=GetModuleFileNameW(nullptr,executable,32768);
    require(executableLength > 0 && executableLength < 32768, "executing tool path unavailable");
    Source tool(fs::path(std::wstring(executable,executableLength)));
    Transaction transaction(path("--backup"),path("--output"),protectedDirs);
    bool backupsVerified=false;
    try {
        transaction.backup(ess,skse);
        Source backedEss(transaction.backupDir()/"original.ess"), backedSkse(transaction.backupDir()/"original.skse");
        require(backedEss.hash() == ess.hash() && backedSkse.hash() == skse.hash(), "backup reopen hash mismatch");
        backupsVerified=true;
        const auto parsed=parse(backedSkse.bytes());
        const auto converted=convert(backedSkse.bytes(),parsed,manifest);
        Json report={{"schema",1},{"status",converted.legacy ? "verified_offline_conversion" : "no_conversion"},
            {"source_ess_sha256",ess.hash()},{"source_skse_sha256",skse.hash()},
            {"backup_ess_sha256",backedEss.hash()},{"backup_skse_sha256",backedSkse.hash()},
            {"output_ess_sha256",ess.hash()},{"output_skse_sha256",sha256(converted.bytes)},
            {"ess_bytes",ess.bytes().size()},{"source_skse_bytes",skse.bytes().size()},{"output_skse_bytes",converted.bytes.size()},
            {"codec_sha256",codec.hash()},{"qualification_sha256",qualification.hash()},{"evidence_sha256",evidence.hash()},
            {"archive",archiveInfo},{"source_inspection",inspect(backedSkse.bytes(),parsed)},
            {"changes",converted.changes},{"unchanged_ranges_verified",true},
            {"limits","Offline qualified byte rewrite only; serialized identities preserved, no game load, runtime form resolution or existing-save acceptance."}};
        report["tool_sha256"]=tool.hash();
        auto verifySources=[&]() { ess.verify(); skse.verify(); evidence.verify(); qualification.verify(); codec.verify(); tool.verify(); backedEss.verify(); backedSkse.verify(); if (archive) archive->verify(); };
        verifySources();
        if (!converted.legacy) {
            require(converted.bytes == skse.bytes(), "encoded control modified");
            report.erase("output_ess_sha256");
            report.erase("output_skse_sha256");
            report.erase("output_skse_bytes");
            report["published"]=false;
            transaction.failure(report.dump(2));
            std::cout << report.dump(2) << '\n';
            return 0;
        }
        transaction.stage(backedEss.bytes(),converted.bytes,report.dump(2));
        verifySources();
        transaction.publish();
        std::cout << report.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        transaction.failure(Json{{"status","refused"},{"reason",error.what()},
            {"source_ess_sha256",ess.hash()},{"source_skse_sha256",skse.hash()},
            {"backups_verified",backupsVerified},{"evidence_sha256",evidence.hash()},
            {"qualification_sha256",qualification.hash()},{"codec_sha256",codec.hash()},
            {"tool_sha256",tool.hash()},{"archive",archiveInfo},{"published",false}}.dump(2));
        throw;
    }
}
#endif
} // namespace LCM
#ifndef LCM_NO_MAIN
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1])=="fix") {
        int wideCount=0;
        wchar_t** wide=CommandLineToArgvW(GetCommandLineW(),&wideCount);
        if (!wide) return 2;
        int result=2;
        try {
            LCM::require(wideCount==4 && std::wstring(wide[1])==L"fix" && std::wstring(wide[2])==L"--save",
                "usage: fix --save FILE.ess");
            result=LCM::fix(std::filesystem::path(wide[3]));
        } catch (const std::exception& error) {
            std::cerr << "Fix refused: " << error.what() << ". Choose a supported, closed-game save pair; originals were not changed.\n";
        }
        LocalFree(wide);
        return result;
    }
    try { return LCM::run(argc,argv); }
    catch (const std::exception& error) {
        std::cerr << nlohmann::json{{"status","refused"},{"reason",error.what()}}.dump() << '\n';
        return 2;
    }
}
#endif
