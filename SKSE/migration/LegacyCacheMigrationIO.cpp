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

std::string pathUtf8(const std::wstring& path)
{
    if (path.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<unprintable path>";
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                            result.data(), size, nullptr, nullptr) != size)
        return "<unprintable path>";
    return result;
}

[[noreturn]] void ioFail(const char* operation, const std::wstring& path, DWORD error = GetLastError())
{
    throw IOError(std::string(operation) + " failed for '" + pathUtf8(path) +
                  "' (Windows error " + std::to_string(error) + ")");
}

[[noreturn]] void pathFail(const std::string& reason, const std::wstring& path)
{
    throw IOError(reason + ": " + pathUtf8(path));
}

std::wstring fullPath(const std::filesystem::path& path)
{
    if (path.empty()) pathFail("Empty path", path.native());
    const auto spelling = path.native();
    if (spelling.rfind(L"\\\\?\\", 0) == 0 || spelling.rfind(L"\\\\.\\", 0) == 0)
        pathFail("Extended/device path unsupported; copy both files to a short normal writable physical folder, e.g. C:\\IE-convert", spelling);
    for (const auto& part : path)
        if (part == L"..") pathFail("Unsafe parent path component", spelling);
    try {
        return std::filesystem::absolute(path).lexically_normal().native();
    } catch (const std::filesystem::filesystem_error& error) {
        pathFail("Resolve absolute path (Windows error " + std::to_string(error.code().value()) + ")", spelling);
    } catch (...) {
        pathFail("Invalid path", spelling);
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

bool isReparseOrDirectory(const std::wstring& path, bool& exists, DWORD& missingError)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            exists = false;
            missingError = error;
            return false;
        }
        ioFail("Inspect path", path, error);
    }
    exists = true;
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

void safePathImpl(const std::wstring& full, bool mayNotExist)
{
    const std::filesystem::path p(full);
    std::wstring current = p.root_path().native();
    if (current.empty()) pathFail("Path has no root", full);
    const DWORD rootAttributes = GetFileAttributesW(current.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES) ioFail("Inspect path root", current);
    if (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        pathFail("Linked/cloud-managed path component unsupported; copy both files to a short normal writable physical folder, e.g. C:\\IE-convert", current);
    if (!(rootAttributes & FILE_ATTRIBUTE_DIRECTORY))
        pathFail("Unsafe path root", current);
    for (const auto& item : p.relative_path()) {
        const std::wstring part = item.native();
        if (part.empty() || part == L"." || part == L".." || unsafeComponent(part) ||
            part.find(L':') != std::wstring::npos)
            pathFail("Unsafe path component", current + L"\\" + part);
        current = (std::filesystem::path(current) / part).native();
        bool exists = false;
        DWORD missingError = ERROR_PATH_NOT_FOUND;
        const bool reparse = isReparseOrDirectory(current, exists, missingError);
        if (!exists) {
            if (mayNotExist && current == full) return;
            ioFail("Inspect path component", current, missingError);
        }
        if (reparse)
            pathFail("Linked/cloud-managed path component unsupported; copy both files to a short normal writable physical folder, e.g. C:\\IE-convert", current);
    }
}

HANDLE openHandle(const std::wstring& path, DWORD access, DWORD share, DWORD creation,
                  DWORD flags = FILE_ATTRIBUTE_NORMAL)
{
    HANDLE h = CreateFileW(path.c_str(), access, share, nullptr, creation, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) ioFail("Open", path);
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

BY_HANDLE_FILE_INFORMATION info(HANDLE h, const std::wstring& path)
{
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(h, &result)) ioFail("Inspect open file", path);
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

void seekStart(HANDLE h, const std::wstring& path)
{
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN)) ioFail("Seek", path);
}

Bytes readAll(HANDLE h, const std::wstring& path, std::uint64_t size)
{
    if (size > kMaxSourceBytes || size > std::numeric_limits<std::size_t>::max())
        pathFail("File exceeds the 512 MiB per-file limit", path);
    Bytes bytes(static_cast<std::size_t>(size));
    seekStart(h, path);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD ask = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, kChunkBytes));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + offset, ask, &got, nullptr)) ioFail("Read", path);
        if (got == 0) ioFail("Read", path, ERROR_HANDLE_EOF);
        offset += got;
    }
    unsigned char extra = 0;
    DWORD got = 0;
    if (!ReadFile(h, &extra, 1, &got, nullptr)) ioFail("Check file length while reading", path);
    if (got != 0) pathFail("File changed while reading", path);
    return bytes;
}

std::string hashHandle(HANDLE h, const std::wstring& path)
{
    const auto initial = info(h, path);
    const Bytes bytes = readAll(h, path, fileSize(initial));
    return sha256(bytes);
}

void writeNew(const std::wstring& path, const unsigned char* data, std::size_t size)
{
    Handle h(openHandle(path, GENERIC_WRITE, 0, CREATE_NEW));
    std::size_t offset = 0;
    while (offset < size) {
        const DWORD ask = static_cast<DWORD>(std::min<std::size_t>(size - offset, kChunkBytes));
        DWORD wrote = 0;
        if (!WriteFile(h.value, data + offset, ask, &wrote, nullptr)) ioFail("Write", path);
        if (wrote == 0) pathFail("Write made no progress", path);
        offset += wrote;
    }
    if (!FlushFileBuffers(h.value)) ioFail("Flush", path);
}

void writeNew(const std::wstring& path, const std::string& text)
{
    writeNew(path, reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

void verifyWritten(const std::wstring& path, std::string_view expectedHash)
{
    Handle h(openHandle(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    const auto i = info(h.value, path);
    if ((i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        pathFail("Unexpected staged file type", path);
    if (hashHandle(h.value, path) != expectedHash) pathFail("Staged file verification failed", path);
}

Handle lockDirectory(const std::wstring& path)
{
    Handle handle(openHandle(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT));
    const auto attributes = info(handle.value, path).dwFileAttributes;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        pathFail("Unsafe directory handle", path);
    return handle;
}

std::wstring physicalDirectory(const std::wstring& path)
{
    safePathImpl(path, false);
    const auto handle = lockDirectory(path);
    std::wstring result(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle.value, result.data(),
        static_cast<DWORD>(result.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (length == 0) ioFail("Resolve physical directory", path);
    if (length >= result.size()) pathFail("Resolved physical directory path is too long", path);
    result.resize(length);
    return result;
}

std::wstring physicalDestination(const std::wstring& path)
{
    const std::filesystem::path p(path);
    return (std::filesystem::path(physicalDirectory(p.parent_path().native())) / p.filename()).native();
}

void ensureRegular(HANDLE h, const std::wstring& path)
{
    const auto i = info(h, path);
    if ((i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        pathFail("Regular file required", path);
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
        impl_->handle = Handle(openHandle(impl_->path, GENERIC_READ, FILE_SHARE_READ,
                                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
        ensureRegular(impl_->handle.value, impl_->path);
        impl_->initial = info(impl_->handle.value, impl_->path);
        if (fileSize(impl_->initial) > kMaxSourceBytes)
            pathFail("File exceeds the 512 MiB per-file limit", impl_->path);
        impl_->data = readAll(impl_->handle.value, impl_->path, fileSize(impl_->initial));
        impl_->digest = sha256(impl_->data);
        verify();
    } catch (...) { delete impl_; impl_ = nullptr; throw; }
}

Source::~Source() { delete impl_; }
const Bytes& Source::bytes() const { return impl_->data; }
const std::string& Source::hash() const { return impl_->digest; }

void Source::verify() const
{
    const auto current = info(impl_->handle.value, impl_->path);
    if (!sameIdentity(impl_->initial, current) || fileSize(impl_->initial) != fileSize(current) ||
        CompareFileTime(&impl_->initial.ftLastWriteTime, &current.ftLastWriteTime) != 0)
        pathFail("Source changed", impl_->path);
    const auto path = fullPath(std::filesystem::path(impl_->path));
    safePathImpl(path, false);
    Handle reopened(openHandle(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING));
    ensureRegular(reopened.value, path);
    const auto pathInfo = info(reopened.value, path);
    if (!sameIdentity(impl_->initial, pathInfo) || fileSize(impl_->initial) != fileSize(pathInfo) ||
        CompareFileTime(&impl_->initial.ftLastWriteTime, &pathInfo.ftLastWriteTime) != 0)
        pathFail("Source identity changed", path);
    if (hashHandle(impl_->handle.value, impl_->path) != impl_->digest)
        pathFail("Source content changed", impl_->path);
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
    ensureRegular(ah.value, a); ensureRegular(bh.value, b);
    return sameIdentity(info(ah.value, a), info(bh.value, b));
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
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            pathFail("Output collision at '" + pathUtf8(path.native()) +
                     "'; do not overwrite backups or load incomplete output. Inspect retained artifacts and retry the original pair in a fresh physical folder", path.native());
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            ioFail("Check output collision", path.native(), error);
    }
    const auto parentLock = lockDirectory(parent.native());
    ess.verify(); skse.verify();
    if (!CreateDirectoryW(backup.c_str(), nullptr)) ioFail("Create exclusive backup directory", backup.native());
    const auto backupLock = lockDirectory(backup.native());
    const auto backupEss = backup / L"original.ess", backupSkse = backup / L"original.skse";
    writeNew(backupEss.native(), ess.bytes().data(), ess.bytes().size());
    writeNew(backupSkse.native(), skse.bytes().data(), skse.bytes().size());
    Handle backedEss(openHandle(backupEss.native(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    Handle backedSkse(openHandle(backupSkse.native(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
    ensureRegular(backedEss.value, backupEss.native()); ensureRegular(backedSkse.value, backupSkse.native());
    if (hashHandle(backedEss.value, backupEss.native()) != ess.hash() ||
        hashHandle(backedSkse.value, backupSkse.native()) != skse.hash())
        pathFail("Backup pair verification failed", backup.native());
    if (!SetFileAttributesW(backupEss.c_str(), FILE_ATTRIBUTE_READONLY))
        ioFail("Protect verified backup", backupEss.native());
    if (!SetFileAttributesW(backupSkse.c_str(), FILE_ATTRIBUTE_READONLY))
        ioFail("Protect verified backup", backupSkse.native());
    if (!CreateDirectoryW(staging.c_str(), nullptr)) ioFail("Create exclusive staging directory", staging.native());
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
    ensureRegular(essHandle.value, stagedEss.native()); ensureRegular(skseHandle.value, stagedSkse.native());
    if (hashHandle(essHandle.value, stagedEss.native()) != ess.hash() ||
        hashHandle(skseHandle.value, stagedSkse.native()) != convertedHash)
        pathFail("Staged pair changed", staging.native());
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
        if (!SetFileInformationByHandle(handle, FileRenameInfo, rename,
                                         static_cast<DWORD>(storage.size())))
            ioFail("Publish", destination.native());
    };
    renameNew(essHandle.value, outEss);
    try {
        renameNew(skseHandle.value, outSkse);
    } catch (const std::exception& error) {
        throw IOError(std::string("Incomplete publication: converted ESS exists at '") +
                      pathUtf8(outEss.native()) + "' but SKSE publication failed (" + error.what() +
                      "). Do not load the incomplete output; originals are untouched");
    }
    if (hashHandle(essHandle.value, outEss.native()) != ess.hash() ||
        hashHandle(skseHandle.value, outSkse.native()) != convertedHash)
        pathFail("Published pair verification failed; do not load the output; originals are untouched", parent.native());
    ess.verify(); skse.verify();
    stagingLock = Handle{};
    if (!RemoveDirectoryW(staging.c_str()))
        ioFail("Remove staging directory (both converted outputs are already published)", staging.native());
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
    ensureRegular(manifest.value, child(impl_->staging, L"manifest.json"));
    const auto manifestInfo = info(manifest.value, child(impl_->staging, L"manifest.json"));
    if (fileSize(manifestInfo) != report.size() ||
        readAll(manifest.value, child(impl_->staging, L"manifest.json"), report.size()) !=
            Bytes(report.begin(), report.end()))
        pathFail("Manifest verification failed", child(impl_->staging, L"manifest.json"));
    for (const auto* name : { L"converted.ess", L"converted.skse", L"manifest.json" }) {
        const auto path = child(impl_->staging, name);
        impl_->stagedFiles.emplace_back(openHandle(path,
            GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT));
        ensureRegular(impl_->stagedFiles.back().value, path);
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
