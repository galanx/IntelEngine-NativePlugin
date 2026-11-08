#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace SKSE { struct SerializationInterface {
    struct Record { uint32_t type, version; std::vector<uint8_t> data; };
    std::vector<Record> records; size_t next = 0, offset = 0;
    bool GetNextRecordInfo(uint32_t& t, uint32_t& v, uint32_t& n) { if (next == records.size()) return false; auto& r = records[next++]; offset = 0; t = r.type; v = r.version; n = static_cast<uint32_t>(r.data.size()); return true; }
    size_t ReadRecordData(void* out, size_t n) { auto& d = records[next - 1].data; auto count = std::min(n, d.size() - offset); std::memcpy(out, d.data() + offset, count); offset += count; return count; }
}; }
namespace logger { template<class... T> void info(const char*, T&&...) {} template<class... T> void warn(const char*, T&&...) {} }
namespace IntelEngine {
constexpr uint32_t SAVE_ID_RECORD = 'IEID', SAVE_ID_VERSION = 1;
std::string g_currentSaveID;
struct SlotTracker { int loads = 0; static SlotTracker* GetSingleton() { static SlotTracker s; return &s; } void Load(SKSE::SerializationInterface*) { ++loads; } };
#include "LoadCallback.inc"
}
using R = SKSE::SerializationInterface::Record;
static R rec(uint32_t t, uint32_t v, std::vector<uint8_t> d = {}) { return {t, v, d}; }
static std::vector<uint8_t> bytes(uint32_t n, std::string s = {}) { std::vector<uint8_t> d(sizeof(n)); std::memcpy(d.data(), &n, sizeof(n)); d.insert(d.end(), s.begin(), s.end()); return d; }
int main() {
    using namespace IntelEngine; g_currentSaveID = "keep";
    SKSE::SerializationInterface bad{{rec(SAVE_ID_RECORD, 1, {1, 0}), rec(SAVE_ID_RECORD, 1, bytes(4, "xy")), rec(SAVE_ID_RECORD, 1, bytes(0)), rec(SAVE_ID_RECORD, 1, bytes(256)), rec(SAVE_ID_RECORD, 99), rec('IETK', 1)}};
    LoadCallback(&bad); assert(g_currentSaveID == "keep" && SlotTracker::GetSingleton()->loads == 1);
    SKSE::SerializationInterface good{{rec(SAVE_ID_RECORD, 1, bytes(2, "ok")), rec('IETK', 1)}};
    LoadCallback(&good); assert(g_currentSaveID == "ok" && SlotTracker::GetSingleton()->loads == 2);
}
