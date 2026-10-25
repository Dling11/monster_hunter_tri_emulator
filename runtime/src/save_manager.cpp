#include "save_manager.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <thread>
#endif

namespace mh3_save_manager {
namespace {
std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S") << '-' << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}
bool HasFiles(const fs::path& folder) noexcept {
    std::error_code ec;
    if (!fs::is_directory(folder, ec) || ec) return false;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code fileError;
        if (it->is_regular_file(fileError) && !fileError) return true;
        it.increment(ec);
    }
    return false;
}
bool CopyContents(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    if (!fs::is_directory(source, ec) || ec) return false;
    fs::create_directories(destination, ec);
    if (ec) return false;
    fs::directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code copyError;
        fs::copy(it->path(), destination / it->path().filename(),
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyError);
        if (copyError) return false;
        it.increment(ec);
    }
    return !ec;
}
fs::path UniqueFolder(const fs::path& parent, const std::string& prefix) {
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) return {};
    const std::string base = prefix + Timestamp();
    for (unsigned suffix = 0; suffix < 1000; ++suffix) {
        const fs::path candidate = parent / (suffix == 0 ? base : base + '-' + std::to_string(suffix));
        ec.clear();
        if (fs::exists(candidate, ec)) {
            if (ec) return {};
            continue;
        }
        ec.clear();
        if (fs::create_directory(candidate, ec)) return candidate;
        if (ec && ec != std::errc::file_exists) return {};
    }
    return {};
}
fs::path SourceDataDirectory(const fs::path& selected) {
    std::error_code ec;
    if (!fs::is_directory(selected, ec) || ec) return {};
    const fs::path candidates[] = {
        selected / "data",
        selected / "title" / "00010000" / "524d4850" / "data",
        selected / "title" / "00010000" / "524D4850" / "data",
        selected / "Wii" / "title" / "00010000" / "524d4850" / "data",
        selected / "Wii" / "title" / "00010000" / "524D4850" / "data",
        selected / "User" / "Wii" / "title" / "00010000" / "524d4850" / "data"
    };
    for (const auto& candidate : candidates) if (HasFiles(candidate)) return candidate;
    if (selected.filename() == "data" && HasFiles(selected)) return selected;
    if (fs::is_regular_file(selected / "nocopy/data00", ec) && !ec) return selected;
    return {};
}
bool SameContents(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    fs::recursive_directory_iterator it(source, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec) || ec) return false;
        if (!it->is_regular_file(ec)) continue;
        const auto target = destination / it->path().lexically_relative(source);
        const auto size = it->file_size(ec);
        if (ec || fs::file_size(target, ec) != size || ec) return false;
        std::ifstream a(it->path(), std::ios::binary), b(target, std::ios::binary);
        if (!a || !b) return false;
        std::array<char, 16384> first{}, second{};
        while (a) {
            a.read(first.data(), first.size()); b.read(second.data(), second.size());
            if (a.gcount() != b.gcount() || std::memcmp(first.data(), second.data(), size_t(a.gcount()))) return false;
        }
        if (a.bad() || b.bad()) return false;
    }
    return !ec;
}
std::string CopyStamp(const fs::path& folder) {
    const auto name = folder.filename().string();
    const size_t prefix = name.rfind("Backup-", 0) == 0 ? 7 : name.rfind("MH3-RMHP08-", 0) == 0 ? 11 : 0;
    if (!prefix || name.size() < prefix + 19) return {};
    const auto stamp = name.substr(prefix, 19);
    for (size_t i = 0; i < stamp.size(); ++i) {
        if (i == 8 || i == 15) { if (stamp[i] != '-') return {}; }
        else if (stamp[i] < '0' || stamp[i] > '9') return {};
    }
    if (name.size() > prefix + 19) {
        if (name[prefix + 19] != '-' || name.size() == prefix + 20) return {};
        for (size_t i = prefix + 20; i < name.size(); ++i) if (name[i] < '0' || name[i] > '9') return {};
    }
    std::tm value{}; std::istringstream input(stamp.substr(0, 15));
    input >> std::get_time(&value, "%Y%m%d-%H%M%S");
    if (input.fail()) return {};
    return stamp;
}
std::vector<fs::path> ManagedBackups(const fs::path& root) {
    std::vector<fs::path> copies;
    std::error_code ec;
    fs::directory_iterator it(root, ec), end;
    std::error_code rootError; const auto resolvedRoot=fs::weakly_canonical(root,rootError);
    if(rootError)return copies;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (it->is_symlink(entryError) || entryError || !it->is_directory(entryError) || entryError) continue;
        const auto resolved=fs::weakly_canonical(it->path(),entryError);
        if(entryError || resolved.parent_path()!=resolvedRoot)continue;
        if (it->path().filename().string().rfind("Backup-", 0) == 0 && !CopyStamp(it->path()).empty() && HasFiles(it->path()/"data"))
            copies.push_back(it->path());
    }
    std::sort(copies.begin(), copies.end(), [](const auto& a, const auto& b) {
        const auto first = CopyStamp(a), second = CopyStamp(b);
        if (first != second) return first > second;
        // Same-millisecond collision suffixes are internal identifiers, not UI names.
        const auto suffix = [](const fs::path& p) { const auto s=p.filename().string(); return s.size()>27 ? std::strtoul(s.c_str()+27,nullptr,10) : 0UL; };
        return suffix(a) > suffix(b);
    });
    return copies;
}
void RemoveTree(const fs::path& path) noexcept {
    std::error_code ec;
    fs::remove_all(path, ec);
}
void RenameFolder(const fs::path& source, const fs::path& destination, std::error_code& ec) {
    for (unsigned attempt = 0;; ++attempt) {
        fs::rename(source, destination, ec);
#ifdef _WIN32
        // Windows scanners can briefly hold a newly copied directory. Retry only
        // access/sharing/lock failures; other failures retain the rollback path.
        if (!ec || attempt == 19 || (ec != std::errc::permission_denied && ec.value() != 32 && ec.value() != 33)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
#else
        return;
#endif
    }
}
}

fs::path ResolveSaveDataDirectory(const fs::path& nandRoot) {
    const fs::path candidates[] = {
        nandRoot / "title" / "00010000" / "524d4850" / "data",
        nandRoot / "title" / "00010000" / "524D4850" / "data",
        nandRoot / "Wii" / "title" / "00010000" / "524d4850" / "data",
        nandRoot / "Wii" / "title" / "00010000" / "524D4850" / "data",
        nandRoot / "User" / "Wii" / "title" / "00010000" / "524d4850" / "data"
    };
    for (const auto& candidate : candidates) if (HasFiles(candidate)) return candidate;
    // Preserve an existing empty save directory as well; this matters on case-sensitive filesystems.
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && !ec) return candidate;
    }
    return candidates[0];
}
bool HasSaveData(const fs::path& saveData) noexcept { return HasFiles(saveData); }
bool HasPendingImport(const fs::path& pendingRoot) noexcept { return HasFiles(pendingRoot / "data"); }

Result CreateBackup(const fs::path& saveData, const fs::path& backupsRoot) {
    if (!HasFiles(saveData)) return {false, {}, "no-save-data"};
    const auto destination = UniqueFolder(backupsRoot, "Backup-");
    if (destination.empty()) return {false, {}, "backup-folder-create-failed"};
    if (!CopyContents(saveData, destination / "data") || !SameContents(saveData, destination / "data")) {
        RemoveTree(destination);
        return {false, {}, "backup-copy-failed"};
    }
    // Do not rotate an older copy until the new copy has passed byte verification.
    auto copies = ManagedBackups(backupsRoot);
    copies.erase(std::remove(copies.begin(),copies.end(),destination),copies.end());
    for (size_t i = 2; i < copies.size(); ++i) {
        std::error_code removeError; fs::remove_all(copies[i], removeError);
        if (removeError) return {true, destination, "cleanup-incomplete"};
    }
    return {true, destination, {}};
}
Result ExportSave(const fs::path& saveData, const fs::path& exportParent) {
    if (!HasFiles(saveData)) return {false, {}, "no-save-data"};
    std::error_code ec;
    if (!fs::is_directory(exportParent, ec) || ec) return {false, {}, "export-parent-missing"};
    const auto bundle = UniqueFolder(exportParent, "MH3-RMHP08-");
    if (bundle.empty()) return {false, {}, "export-folder-create-failed"};
    // Keep the standard Dolphin/Cemu title layout so an exported folder can be re-imported.
    if (!CopyContents(saveData, bundle / "title" / "00010000" / "524d4850" / "data")) {
        RemoveTree(bundle);
        return {false, {}, "export-copy-failed"};
    }
    std::ofstream readme(bundle / "README.txt", std::ios::binary | std::ios::trunc);
    readme << "MH Tri - Rowell / exported game save\n"
              "Game: European Monster Hunter Tri (RMHP08)\n\n"
              "This copies progress already saved inside Tri, not a running session.\n"
              "Transfer this entire folder to MH3_Rowell_Dev/imports on the other device.\n"
              "Open Gear > Saves > Restore / import > Other saves and select this copy.\n"
              "Confirm Restore and restart, then use Tri's normal Continue option.\n";
    if (!readme) {
        RemoveTree(bundle);
        return {false, {}, "readme-write-failed"};
    }
    return {true, bundle, {}};
}
Result StageImport(const fs::path& selectedFolder, const fs::path& pendingRoot) {
    const auto source = SourceDataDirectory(selectedFolder);
    if (source.empty()) return {false, {}, "invalid-import-folder"};
    const auto parent = pendingRoot.parent_path();
    const auto transactionTag = Timestamp();
    const auto fresh = parent / ("PendingImport.new-" + transactionTag);
    const auto old = parent / ("PendingImport.previous-" + transactionTag);
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) return {false, {}, "pending-folder-create-failed"};
    if (fs::exists(fresh, ec) || ec || fs::exists(old, ec) || ec) return {false, {}, "pending-transaction-name-collision"};
    if (!CopyContents(source, fresh / "data")) {
        RemoveTree(fresh);
        return {false, {}, "pending-copy-failed"};
    }
    std::ofstream manifest(fresh / "README.txt", std::ios::binary | std::ios::trunc);
    manifest << "Staged saved progress for MH Tri - Rowell.\n"
                "A confirmed restore is applied before the game starts.\n"
                "The current saved progress is backed up before replacement.\n";
    manifest.close();
    if (!manifest) {
        RemoveTree(fresh);
        return {false, {}, "pending-manifest-write-failed"};
    }
    const bool hadPending = fs::exists(pendingRoot, ec);
    if (ec) { RemoveTree(fresh); return {false, {}, "pending-check-failed"}; }
    if (hadPending) {
        RenameFolder(pendingRoot, old, ec);
        if (ec) { RemoveTree(fresh); return {false, {}, "pending-rename-failed"}; }
    }
    RenameFolder(fresh, pendingRoot, ec);
    if (ec) {
        if (hadPending) {
            std::error_code restoreError;
            RenameFolder(old, pendingRoot, restoreError);
        }
        RemoveTree(fresh);
        return {false, {}, "pending-commit-failed"};
    }
    RemoveTree(old);
    return {true, pendingRoot, {}};
}
Result ApplyPendingImport(const fs::path& saveData, const fs::path& pendingRoot, const fs::path& backupsRoot) {
    const auto staged = pendingRoot / "data";
    if (!HasFiles(staged)) return {true, {}, "no-pending-import"};
    if (HasFiles(saveData)) {
        const auto backup = CreateBackup(saveData, backupsRoot);
        if (!backup.success) return {false, {}, "automatic-backup-failed"};
    }
    std::error_code ec;
    fs::create_directories(saveData.parent_path(), ec);
    if (ec) return {false, {}, "save-parent-create-failed"};
    const auto transactionTag = Timestamp();
    const auto fresh = saveData.parent_path() / (saveData.filename().string() + ".mh3-new-" + transactionTag);
    const auto old = saveData.parent_path() / (saveData.filename().string() + ".mh3-old-" + transactionTag);
    if (fs::exists(fresh, ec) || ec || fs::exists(old, ec) || ec) return {false, {}, "save-transaction-name-collision"};
    if (!CopyContents(staged, fresh) || !SameContents(staged, fresh)) {
        RemoveTree(fresh);
        return {false, {}, "import-copy-failed"};
    }
    const bool hadCurrent = fs::exists(saveData, ec);
    if (ec) { RemoveTree(fresh); return {false, {}, "save-check-failed"}; }
    if (hadCurrent) {
        RenameFolder(saveData, old, ec);
        if (ec) { RemoveTree(fresh); return {false, {}, "save-rename-failed"}; }
    }
    RenameFolder(fresh, saveData, ec);
    if (ec) {
        if (hadCurrent) {
            std::error_code restoreError;
            RenameFolder(old, saveData, restoreError);
        }
        RemoveTree(fresh);
        return {false, {}, "import-commit-failed"};
    }
    RemoveTree(old);
    RemoveTree(pendingRoot);
    return {true, saveData, {}};
}

std::string CopyCreatedTime(const fs::path& folder) {
    const auto stamp = CopyStamp(folder);
    if (stamp.empty()) return "Creation time not recorded";
    std::tm value{}; std::istringstream input(stamp.substr(0,15));
    input >> std::get_time(&value, "%Y%m%d-%H%M%S");
    std::ostringstream out; out << std::put_time(&value, "%d %b %Y, %I:%M:%S %p") << " (local time)";
    return out.str();
}
std::vector<SaveCopy> ListSaveCopies(const fs::path& userDir, bool backups) {
    std::vector<SaveCopy> copies;
    if (backups) {
        for (const auto& folder : ManagedBackups(userDir/"saves")) copies.push_back({folder,CopyCreatedTime(folder),"Backup"});
    } else for (const auto& kind : {"imports", "exports"}) {
        std::error_code ec; fs::directory_iterator it(userDir/kind,ec), end;
        for (;!ec && it!=end;it.increment(ec)) {
            std::error_code entryError;
            if (it->is_symlink(entryError) || entryError || !it->is_directory(entryError) || entryError) continue;
            if (!SourceDataDirectory(it->path()).empty()) copies.push_back({it->path(),CopyCreatedTime(it->path()),kind});
        }
    }
    if (!backups) std::sort(copies.begin(),copies.end(),[](const SaveCopy& a,const SaveCopy& b) {
        const auto first=CopyStamp(a.folder),second=CopyStamp(b.folder);
        if(first!=second)return first>second;
        return a.folder.generic_string()<b.folder.generic_string();
    });
    return copies;
}
Result QueueRestore(const fs::path& selectedFolder, const fs::path& userDir) {
    const auto pending=userDir/"PendingRestore";
    auto result=StageImport(selectedFolder,pending);
    if (!result.success) return result;
    if (!SameContents(SourceDataDirectory(selectedFolder),pending/"data")) {
        RemoveTree(pending);return {false,{},"restore-copy-verification-failed"};
    }
    std::ofstream marker(pending/"restart-request.txt",std::ios::binary|std::ios::trunc);
    marker << "MH_TRI_ROWELL_RESTORE_1\n";marker.close();
    if (!marker) {RemoveTree(pending);return {false,{},"restore-request-write-failed"};}
    return {true,pending,{}};
}
Result CompleteQueuedRestore(const fs::path& userDir) {
    const auto pending=userDir/"PendingRestore";
    std::ifstream marker(pending/"restart-request.txt");std::string signature;std::getline(marker,signature);marker.close();
    if(signature!="MH_TRI_ROWELL_RESTORE_1")return {true,{},"no-queued-restore"};
    const auto result=HasPendingImport(pending)?ApplyPendingImport(ResolveSaveDataDirectory(userDir/"nand"),pending,userDir/"saves"):
        Result{false,{},"queued-restore-is-empty"};
    if(!result.success) {
        // Keep staged data for recovery, but do not repeat a failed request on every boot.
        std::error_code ec;fs::remove(pending/"restart-request.txt",ec);
    }
    std::ofstream status(userDir/"restore-status.txt",std::ios::trunc);
    status << (result.success ? "Restored saved progress. Use Tri's Continue option." : "Restore failed: "+result.detail) << '\n';
    return result;
}
}
