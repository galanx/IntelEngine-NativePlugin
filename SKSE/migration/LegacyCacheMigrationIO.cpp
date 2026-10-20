#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "LegacyCacheMigrationIO.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

namespace LCM {
namespace {

// ponytail: whole-file snapshots cap each input at 512 MiB; stream if larger saves are qualified.
constexpr std::uint64_t kMaxSourceBytes = 512ull * 1024 * 1024;
constexpr DWORD kChunkBytes = 1024 * 1024;

[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }

std::wstring fullPath(const std::filesystem::path& path)
{
    if (path.empty()) fail("Empty path");
    for (const auto& part : path)
        if (part == L"." || part == L"..") fail("Unsafe path component");
    try {
        auto absolute = std::filesystem::absolute(path).lexically_normal().native();
        if (absolute.rfind(L"\\\\?\\", 0) == 0 || absolute.rfind(L"\\\\.\\", 0) == 0)
            fail("Device path rejected");
        return absolute;
    } catch (const std::runtime_error&) {
        throw;
    } catch (...) {
        fail("Invalid path");
    }
}

bool equalInsensitive(std::wstring_view a, std::wstring_view b)
{
    const int comparison = CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                               static_cast<int>(b.size()), TRUE);
    if (comparison == 0) fail("Path comparison failed");
    return comparison == CSTR_EQUAL;
}
bool unsafeComponent(std::wstring_view part)
{
    if (part.empty() || part.back() == L'.' || part.back() == L' ') return true;
    const auto dot = part.find(L'.');
    const auto base = part.substr(0, dot);
    const auto device = [&](std::wstring_view name) { return equalInsensitive(base, name); };
    if (device(L"CON") || device(L"PRN") || device(L"AUX") || device(L"NUL")) return true;
    return base.size() == 4 &&
           (equalInsensitive(base.substr(0, 3), L"COM") || equalInsensitive(base.substr(0, 3), L"LPT")) &&
           base[3] >= L'1' && base[3] <= L'9';
}

std::vector<std::wstring> components(const std::wstring& path)
{
    std::vector<std::wstring> result;
    std::filesystem::path p(path);
    for (const auto& part : p) {
        const auto s = part.native();
        if (s == L"\\" || s == L"/" || (s.size() == 2 && s[1] == L':')) continue;
        result.push_back(s);
    }
    result.insert(result.begin(), p.root_path().native());
    return result;
}

bool componentPrefix(const std::wstring& parent, const std::wstring& child)
{
    const auto a = components(parent);
    const auto b = components(child);
    if (a.size() > b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!equalInsensitive(a[i], b[i])) return false;
    return true;
}

bool isReparseOrDirectory(const std::wstring& path, bool& exists)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            exists = false;
            return false;
        }
        fail("Cannot inspect path");
    }
    exists = true;
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

void safePathImpl(const std::wstring& full, bool mayNotExist)
{
    const std::filesystem::path p(full);
    std::wstring current = p.root_path().native();
    if (current.empty()) fail("Path has no root");
    const DWORD rootAttributes = GetFileAttributesW(current.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !(rootAttributes & FILE_ATTRIBUTE_DIRECTORY))
        fail("Unsafe path root");
    for (const auto& item : p.relative_path()) {
        const std::wstring part = item.native();
        if (part.empty() || part == L"." || part == L".." || unsafeComponent(part))
            fail("Unsafe path component");
        if (part.find(L':') != std::wstring::npos) fail("Unsafe path component");
        current = (std::filesystem::path(current) / part).native();
        bool exists = false;
        const bool reparse = isReparseOrDirectory(current, exists);
        if (!exists) {
            if (mayNotExist && current == full) return;
            fail("Path does not exist");
        }
        if (reparse) fail("Reparse path rejected");
    }
}

HANDLE openHandle(const std::wstring& path, DWORD access, DWORD share, DWORD creation,
                  DWORD flags = FILE_ATTRIBUTE_NORMAL)
{
    HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, creation, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw IOError("Cannot open path");
    return h;
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
};

BY_HANDLE_FILE_INFORMATION info(HANDLE h)
{
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(h, &result)) throw IOError("Cannot inspect open file");
    return result;
}

bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
{
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

std::uint64_t fileSize(const BY_HANDLE_FILE_INFORMATION& i)
{
    return (static_cast<std::uint64_t>(i.nFileSizeHigh) << 32) | i.nFileSizeLow;
}

void seekStart(HANDLE h)
{
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN)) throw IOError("Cannot seek file");
}

Bytes readAll(HANDLE h, std::uint64_t size)
{
    if (size > kMaxSourceBytes || size > std::numeric_limits<std::size_t>::max()) fail("File too large");
    Bytes bytes(static_cast<std::size_t>(size));
    seekStart(h);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD ask = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, kChunkBytes));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + offset, ask, &got, nullptr) || got == 0) throw IOError("Cannot read file");
        offset += got;
    }
    unsigned char extra = 0;
    DWORD got = 0;
    if (!ReadFile(h, &extra, 1, &got, nullptr) || got != 0) throw IOError("File changed while reading");
    return bytes;
}

std::string hashHandle(HANDLE h)
{
    const auto initial = info(h);
    const Bytes bytes = readAll(h, fileSize(initial));
    return sha256(bytes);
}

void writeNew(const std::wstring& path, const unsigned char* data, std::size_t size)
{
    Handle h(openHandle(path, GENERIC_WRITE, 0, CREATE_NEW));
    std::size_t offset = 0;
    while (offset < size) {
        const DWORD ask = static_cast<DWORD>(std::min<std::size_t>(size - offset, kChunkBytes));
        DWORD wrote = 0;
        if (!WriteFile(h.value, data + offset, ask, &wrote, nullptr) || wrote == 0) fail("Cannot write file");
        offset += wrote;
    }
    if (!FlushFileBuffers(h.value)) fail("Cannot flush file");
}

void writeNew(const std::wstring& path, const std::string& text)
{
    writeNew(path, reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

void verifyWritten(const std::wstring& path, std::string_view expectedHash)
{
    Handle h(openHandle(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    const auto i = info(h.value);
    if ((i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("Unexpected staged file type");
    if (hashHandle(h.value) != expectedHash) fail("Staged file verification failed");
}

Handle lockDirectory(const std::wstring& path)
{
    Handle handle(openHandle(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT));
    const auto attributes = info(handle.value).dwFileAttributes;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("Unsafe directory handle");
    return handle;
}

std::wstring physicalDirectory(const std::wstring& path)
{
    safePathImpl(path, false);
    const auto handle = lockDirectory(path);
    std::wstring result(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle.value, result.data(),
        static_cast<DWORD>(result.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (length == 0 || length >= result.size()) fail("Cannot resolve physical directory");
    result.resize(length);
    return result;
}

std::wstring physicalDestination(const std::wstring& path)
{
    const std::filesystem::path p(path);
    return (std::filesystem::path(physicalDirectory(p.parent_path().native())) / p.filename()).native();
}

void ensureRegular(HANDLE h)
{
    const auto i = info(h);
    if ((i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("Regular file required");
}

std::wstring child(const std::wstring& dir, const wchar_t* name)
{
    return (std::filesystem::path(dir) / name).native();
}

} // namespace

std::string sha256(const Bytes& bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, resultSize = 0;
    std::vector<unsigned char> object, digest(32);
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw IOError("SHA-256 unavailable");
    auto closeAlgorithm = [&] { if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); };
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &resultSize, 0) < 0 || resultSize != sizeof(objectSize)) {
        closeAlgorithm(); throw IOError("SHA-256 unavailable");
    }
    object.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) {
        closeAlgorithm(); throw IOError("SHA-256 unavailable");
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ULONG count = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - offset, kChunkBytes));
        if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + offset), count, 0) < 0) {
            BCryptDestroyHash(hash); closeAlgorithm(); throw IOError("SHA-256 failed");
        }
        offset += count;
    }
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash); closeAlgorithm(); throw IOError("SHA-256 failed");
    }
    BCryptDestroyHash(hash);
    closeAlgorithm();
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned char b : digest) { result.push_back(digits[b >> 4]); result.push_back(digits[b & 15]); }
    return result;
}

struct Source::Impl {
    std::wstring path;
    Handle handle;
    BY_HANDLE_FILE_INFORMATION initial{};
    Bytes data;
    std::string digest;
    std::vector<Handle> ancestors;
};

Source::Source(const std::filesystem::path& path) : impl_(new Impl)
{
    try {
        impl_->path = fullPath(path);
        safePathImpl(impl_->path, false);
        auto parent = std::filesystem::path(impl_->path).parent_path();
        for (auto current = parent.root_path();;) {
            safePathImpl(current.native(), false);
            impl_->ancestors.push_back(lockDirectory(current.native()));
            if (current == parent) break;
            auto relative = parent.lexically_relative(current);
            if (relative.empty()) fail("Invalid source parent");
            current /= *relative.begin();
        }
        impl_->handle = Handle(openHandle(impl_->path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
        ensureRegular(impl_->handle.value);
        impl_->initial = info(impl_->handle.value);
        if (fileSize(impl_->initial) > kMaxSourceBytes) fail("File too large");
        impl_->data = readAll(impl_->handle.value, fileSize(impl_->initial));
        impl_->digest = sha256(impl_->data);
        verify();
    } catch (...) { delete impl_; impl_ = nullptr; throw; }
}

Source::~Source() { delete impl_; }
const Bytes& Source::bytes() const { return impl_->data; }
const std::string& Source::hash() const { return impl_->digest; }

void Source::verify() const
{
    const auto current = info(impl_->handle.value);
    if (!sameIdentity(impl_->initial, current) || fileSize(impl_->initial) != fileSize(current) ||
        CompareFileTime(&impl_->initial.ftLastWriteTime, &current.ftLastWriteTime) != 0)
        throw IOError("Source changed");
    const auto path = fullPath(std::filesystem::path(impl_->path));
    safePathImpl(path, false);
    Handle reopened(openHandle(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING));
    ensureRegular(reopened.value);
    const auto pathInfo = info(reopened.value);
    if (!sameIdentity(impl_->initial, pathInfo) || fileSize(impl_->initial) != fileSize(pathInfo) ||
        CompareFileTime(&impl_->initial.ftLastWriteTime, &pathInfo.ftLastWriteTime) != 0)
        throw IOError("Source identity changed");
    if (hashHandle(impl_->handle.value) != impl_->digest)
        throw IOError("Source content changed");
}

void safePath(const std::filesystem::path& path, bool mayNotExist)
{
    const auto abs = fullPath(path);
    safePathImpl(abs, mayNotExist);
}

bool sameFile(const std::filesystem::path& left, const std::filesystem::path& right)
{
    const auto a = fullPath(left), b = fullPath(right);
    safePathImpl(a, false);
    safePathImpl(b, false);
    Handle ah(openHandle(a, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, OPEN_EXISTING));
    Handle bh(openHandle(b, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, OPEN_EXISTING));
    ensureRegular(ah.value); ensureRegular(bh.value);
    return sameIdentity(info(ah.value), info(bh.value));
}

void publishFixedPair(const std::filesystem::path& save, const Source& ess,
                      const Source& skse, const Bytes& converted, void (*beforePublish)())
{
    namespace fs = std::filesystem;
    const fs::path source(fullPath(save));
    const auto parent = source.parent_path();
    const auto stem = source.stem().native();
    const auto backup = parent / (stem + L"_IEbackup");
    const auto staging = parent / (stem + L"_IEconverted.staging");
    const auto outEss = parent / (stem + L"_IEconverted.ess");
    const auto outSkse = parent / (stem + L"_IEconverted.skse");
    for (const auto& path : {backup, staging, outEss, outSkse}) {
        safePath(path, true);
        if (fs::exists(path)) fail("Backup, staging or converted output already exists");
    }
    const auto parentLock = lockDirectory(parent.native());
    ess.verify(); skse.verify();
    if (!CreateDirectoryW(backup.c_str(), nullptr)) fail("Cannot create exclusive backup directory");
    const auto backupLock = lockDirectory(backup.native());
    const auto backupEss = backup / L"original.ess", backupSkse = backup / L"original.skse";
    writeNew(backupEss.native(), ess.bytes().data(), ess.bytes().size());
    writeNew(backupSkse.native(), skse.bytes().data(), skse.bytes().size());
    Handle backedEss(openHandle(backupEss.native(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    Handle backedSkse(openHandle(backupSkse.native(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    ensureRegular(backedEss.value); ensureRegular(backedSkse.value);
    if (hashHandle(backedEss.value) != ess.hash() || hashHandle(backedSkse.value) != skse.hash())
        fail("Backup pair verification failed");
    if (!SetFileAttributesW(backupEss.c_str(), FILE_ATTRIBUTE_READONLY) ||
        !SetFileAttributesW(backupSkse.c_str(), FILE_ATTRIBUTE_READONLY))
        fail("Cannot protect verified backup");
    if (!CreateDirectoryW(staging.c_str(), nullptr)) fail("Cannot create exclusive staging directory");
    auto stagingLock = lockDirectory(staging.native());
    const auto stagedEss = staging / L"converted.ess", stagedSkse = staging / L"converted.skse";
    writeNew(stagedEss.native(), ess.bytes().data(), ess.bytes().size());
    writeNew(stagedSkse.native(), converted.data(), converted.size());
    const auto convertedHash = sha256(converted);
    verifyWritten(stagedEss.native(), ess.hash());
    verifyWritten(stagedSkse.native(), convertedHash);
    Handle essHandle(openHandle(stagedEss.native(), GENERIC_READ | DELETE, FILE_SHARE_READ,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    Handle skseHandle(openHandle(stagedSkse.native(), GENERIC_READ | DELETE, FILE_SHARE_READ,
                                 OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    ensureRegular(essHandle.value); ensureRegular(skseHandle.value);
    if (hashHandle(essHandle.value) != ess.hash() || hashHandle(skseHandle.value) != convertedHash)
        fail("Staged pair changed");
    ess.verify(); skse.verify();
    if (beforePublish) beforePublish();
    auto renameNew = [](HANDLE handle, const fs::path& destination) {
        const auto name = destination.native();
        const auto bytes = name.size() * sizeof(wchar_t);
        std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + bytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(bytes);
        std::copy(name.begin(), name.end(), rename->FileName);
        return SetFileInformationByHandle(handle, FileRenameInfo, rename,
                                         static_cast<DWORD>(storage.size())) != 0;
    };
    if (!renameNew(essHandle.value, outEss)) fail("ESS publication failed; no complete converted pair was published");
    if (!renameNew(skseHandle.value, outSkse))
        fail("Incomplete publication: converted ESS exists but SKSE rename failed. Do not load it; originals are untouched");
    if (hashHandle(essHandle.value) != ess.hash() || hashHandle(skseHandle.value) != convertedHash)
        fail("Published pair verification failed; do not load it; originals are untouched");
    ess.verify(); skse.verify();
    stagingLock = Handle{};
    if (!RemoveDirectoryW(staging.c_str())) fail("Converted pair published, but empty staging directory could not be removed");
}

struct Transaction::Impl {
    std::wstring backup, output, staging;
    std::vector<Handle> parentLocks;
    Handle backupLock, stagingLock;
    std::vector<Handle> stagedFiles;
    bool backedUp = false, staged = false, published = false;
};

Transaction::Transaction(std::filesystem::path backupDir, std::filesystem::path outputDir,
                         const std::vector<std::filesystem::path>& protectedDirs) : impl_(new Impl)
{
    try {
        impl_->backup = fullPath(backupDir);
        impl_->output = fullPath(outputDir);
        impl_->staging = impl_->output + L".staging";
        if (componentPrefix(impl_->backup, impl_->output) ||
            componentPrefix(impl_->output, impl_->backup) ||
            componentPrefix(impl_->backup, impl_->staging) ||
            componentPrefix(impl_->staging, impl_->backup))
            fail("Destination paths overlap");
        for (const auto& p : { impl_->backup, impl_->output }) {
            for (const auto& part : components(p)) if (equalInsensitive(part, L"Saves")) fail("Unsafe destination");
            const auto parent = std::filesystem::path(p).parent_path().native();
            safePathImpl(parent, false);
            const std::filesystem::path parentPath(parent);
            for (auto current = parentPath.root_path();;) {
                safePathImpl(current.native(), false);
                impl_->parentLocks.push_back(lockDirectory(current.native()));
                if (current == parentPath) break;
                auto relative = parentPath.lexically_relative(current);
                if (relative.empty()) fail("Invalid destination parent");
                current /= *relative.begin();
            }
        }
        const auto physicalBackup = physicalDestination(impl_->backup);
        const auto physicalOutput = physicalDestination(impl_->output);
        const auto physicalStaging = physicalDestination(impl_->staging);
        if (componentPrefix(physicalBackup, physicalOutput) ||
            componentPrefix(physicalOutput, physicalBackup) ||
            componentPrefix(physicalBackup, physicalStaging) ||
            componentPrefix(physicalStaging, physicalBackup))
            fail("Physical destination aliases overlap");
        for (const auto& protectedPath : protectedDirs) {
            const auto protectedFull = physicalDirectory(fullPath(protectedPath));
            for (const auto& destination : { physicalBackup, physicalOutput, physicalStaging }) {
                if (componentPrefix(protectedFull, destination) ||
                    componentPrefix(destination, protectedFull))
                    fail("Destination overlaps physical protected directory");
            }
        }
        safePathImpl(impl_->backup, true);
        safePathImpl(impl_->output, true);
        safePathImpl(impl_->staging, true);
        if (GetFileAttributesW(impl_->output.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            fail("Output already exists or is inaccessible");
        if (CreateDirectoryW(impl_->backup.c_str(), nullptr) == 0) fail("Backup directory unavailable");
        if (CreateDirectoryW(impl_->staging.c_str(), nullptr) == 0) fail("Staging directory unavailable");
        impl_->backupLock = lockDirectory(impl_->backup);
        impl_->stagingLock = Handle(openHandle(impl_->staging, FILE_READ_ATTRIBUTES | DELETE,
            FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS));
    } catch (...) { delete impl_; impl_ = nullptr; throw; }
}

Transaction::~Transaction() { delete impl_; }

void Transaction::backup(const Source& ess, const Source& skse)
{
    if (impl_->backedUp) fail("Backup already written");
    ess.verify(); skse.verify();
    writeNew(child(impl_->backup, L"original.ess"), ess.bytes().data(), ess.bytes().size());
    writeNew(child(impl_->backup, L"original.skse"), skse.bytes().data(), skse.bytes().size());
    verifyWritten(child(impl_->backup, L"original.ess"), ess.hash());
    verifyWritten(child(impl_->backup, L"original.skse"), skse.hash());
    if (!SetFileAttributesW(child(impl_->backup, L"original.ess").c_str(), FILE_ATTRIBUTE_READONLY))
        fail("Cannot protect backup");
    if (!SetFileAttributesW(child(impl_->backup, L"original.skse").c_str(), FILE_ATTRIBUTE_READONLY))
        fail("Cannot protect backup");
    impl_->backedUp = true;
}
void Transaction::failure(const std::string& report) noexcept
{
    try {
        const auto path = child(impl_->backup, L"failure.json");
        writeNew(path, report);
        verifyWritten(path, sha256(Bytes(report.begin(), report.end())));
    } catch (...) {
    }
}

void Transaction::stage(const Bytes& ess, const Bytes& skse, const std::string& report)
{
    if (impl_->staged || impl_->published) fail("Output already staged");
    writeNew(child(impl_->staging, L"converted.ess"), ess.data(), ess.size());
    writeNew(child(impl_->staging, L"converted.skse"), skse.data(), skse.size());
    writeNew(child(impl_->staging, L"manifest.json"), report);
    const auto essHash = sha256(ess), skseHash = sha256(skse);
    verifyWritten(child(impl_->staging, L"converted.ess"), essHash);
    verifyWritten(child(impl_->staging, L"converted.skse"), skseHash);
    Handle manifest(openHandle(child(impl_->staging, L"manifest.json"), GENERIC_READ, FILE_SHARE_READ,
                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    ensureRegular(manifest.value);
    const auto manifestInfo = info(manifest.value);
    if (fileSize(manifestInfo) != report.size() || readAll(manifest.value, report.size()) != Bytes(report.begin(), report.end()))
        fail("Manifest verification failed");
    for (const auto* name : { L"converted.ess", L"converted.skse", L"manifest.json" }) {
        impl_->stagedFiles.emplace_back(openHandle(child(impl_->staging, name),
            GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
        ensureRegular(impl_->stagedFiles.back().value);
    }
    impl_->staged = true;
}

void Transaction::publish()
{
    if (!impl_->staged || impl_->published) fail("Output is not ready");
    // Rename through the exclusively held directory handle; never release its
    // identity for a path-based reopen or replace an existing destination.
    const auto filenameBytes = impl_->output.size() * sizeof(wchar_t);
    std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + filenameBytes);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(filenameBytes);
    std::copy(impl_->output.begin(), impl_->output.end(), rename->FileName);
    // Windows refuses a directory rename with child file handles still open
    // (even when those handles share DELETE). Close only at this commit boundary;
    // the directory identity and all original/source locks remain held.
    impl_->stagedFiles.clear();
    if (!SetFileInformationByHandle(impl_->stagingLock.value, FileRenameInfo,
        rename, static_cast<DWORD>(storage.size())))
        fail("Output publication refused");
    impl_->published = true;
}

std::filesystem::path Transaction::backupDir() const { return std::filesystem::path(impl_->backup); }
std::filesystem::path Transaction::stagingDir() const { return std::filesystem::path(impl_->staging); }
std::filesystem::path Transaction::outputDir() const { return std::filesystem::path(impl_->output); }

} // namespace LCM
