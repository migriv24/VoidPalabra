/* src/reticulum/rooted_fs.hpp — microReticulum's files, kept inside the node's folder.
 *
 * On a desktop, microReticulum assumes the process's working directory IS its
 * storage: its stores open "./path_store/", "./known_store/",
 * "./hashlist_store/" and "./cache" relative to wherever the program was
 * started (found 2026-09-23: a transport_identity and a cache folder appeared in
 * this repository's root while the tests ran). A library must not write keys
 * and routing tables into a caller's current folder, and chdir is a process-wide
 * side effect a library must not take either.
 *
 * So the filesystem microReticulum is handed is ROOTED: every relative path it
 * names resolves under the node's storage folder, and absolute paths (the ones
 * we build ourselves) pass through. One adapter instead of a patch per call
 * site, which also covers the next relative path upstream adds. */
#pragma once

#include <microStore/Adapters/UniversalFileSystem.h>
#include <microStore/FileSystem.h>

#include <string>

namespace voidpalabra::reticulum::detail {

class RootedFileSystem : public microStore::FileSystemImpl {
public:
    explicit RootedFileSystem(std::string root) : root_(std::move(root)), inner_(microStore::Adapters::UniversalFileSystem()) {}

    std::string at(const char* path) const {
        std::string s = path ? path : "";
        if (absolute(s)) return s;
        while (s.rfind("./", 0) == 0) s.erase(0, 2);
        if (s.empty() || s == ".") return root_;
        return root_ + "/" + s;
    }

    bool init(bool reformat) override { return inner_.init(reformat); }
    microStore::File open(const char* path, microStore::File::Mode mode, const bool create = false) override {
        return inner_.open(at(path).c_str(), mode, create);
    }
    bool exists(const char* path) override { return inner_.exists(at(path).c_str()); }
    bool remove(const char* path) override { return inner_.remove(at(path).c_str()); }
    bool rename(const char* from, const char* to) override {
        return inner_.rename(at(from).c_str(), at(to).c_str());
    }
    bool mkdir(const char* path) override { return inner_.mkdir(at(path).c_str()); }
    bool rmdir(const char* path) override { return inner_.rmdir(at(path).c_str()); }
    size_t size(const char* path) override { return inner_.size(at(path).c_str()); }
    bool isDirectory(const char* path) override { return inner_.isDirectory(at(path).c_str()); }
    std::list<std::string> listDirectory(const char* path, Callbacks::DirectoryListing cb = nullptr) override {
        return inner_.listDirectory(at(path).c_str(), cb);
    }
    size_t storageSize() override { return inner_.storageSize(); }
    size_t storageAvailable() override { return inner_.storageAvailable(); }

private:
    static bool absolute(const std::string& s) {
        return !s.empty() && (s[0] == '/' || s[0] == '\\' || (s.size() > 1 && s[1] == ':'));
    }
    std::string root_;
    microStore::FileSystem inner_;
};

} // namespace voidpalabra::reticulum::detail
