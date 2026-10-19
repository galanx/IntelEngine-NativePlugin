#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace LCM {

using Bytes = std::vector<unsigned char>;

std::string sha256(const Bytes& bytes);

class Source {
public:
    explicit Source(const std::filesystem::path& path);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    const Bytes& bytes() const;
    const std::string& hash() const;
    void verify() const;

private:
    struct Impl;
    Impl* impl_;
};

void safePath(const std::filesystem::path& path, bool mayNotExist = false);
bool sameFile(const std::filesystem::path& left, const std::filesystem::path& right);
class Transaction {
public:
    Transaction(std::filesystem::path backupDir, std::filesystem::path outputDir,
                const std::vector<std::filesystem::path>& protectedDirs);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void backup(const Source& ess, const Source& skse);
    void stage(const Bytes& ess, const Bytes& skse, const std::string& report);
    void failure(const std::string& report) noexcept;
    void publish();
    std::filesystem::path backupDir() const;
    std::filesystem::path stagingDir() const;
    std::filesystem::path outputDir() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace LCM
