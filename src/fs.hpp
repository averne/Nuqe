#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <switch.h>

#include "error.hpp"
#include "utils.hpp"

namespace nq::fs {

class Directory {
    public:
        constexpr inline Directory() = default;
        constexpr inline Directory(const FsDir &handle): handle(handle) { }

        inline Directory(FsFileSystem *fs, const std::string &path) {
            R_TRY_LOG(this->open(fs, path));
        }

        inline Result open(FsFileSystem *fs, const std::string &path) {
            auto tmp = path;
            tmp.reserve(FS_MAX_PATH); // Fails otherwise
            return fsFsOpenDirectory(fs, tmp.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &this->handle);
        }

        inline void close() {
            fsDirClose(&this->handle);
        }

        inline bool is_open() const {
            // TODO: Better heuristic?
            return this->handle.s.session;
        }

        inline std::size_t count() {
            s64 count = 0;
            R_TRY_LOG(fsDirGetEntryCount(&this->handle, &count));
            return count;
        }

        std::vector<FsDirectoryEntry> list() {
            s64 total = 0;
            auto c = this->count();
            auto entries = std::vector<FsDirectoryEntry>(c);
            R_TRY_LOG(fsDirRead(&this->handle, &total, c, entries.data()));
            return entries;
        }

    protected:
        FsDir handle = {};
};

class File {
    public:
        constexpr inline File() = default;
        constexpr inline File(const FsFile &handle): handle(handle) { }

        inline File(FsFileSystem *fs, const std::string &path, u32 mode = FsOpenMode_Read) {
            R_TRY_LOG(this->open(fs, path, mode));
        }

        inline Result open(FsFileSystem *fs, const std::string &path, u32 mode = FsOpenMode_Read) {
            auto tmp = path;
            tmp.reserve(FS_MAX_PATH);
            return fsFsOpenFile(fs, tmp.c_str(), mode, &this->handle);
        }

        inline void close() {
            fsFileClose(&this->handle);
        }

        inline bool is_open() const {
            return this->handle.s.session;
        }

        inline std::size_t size() {
            s64 tmp = 0;
            R_TRY_LOG(fsFileGetSize(&this->handle, &tmp));
            return tmp;
        }

        inline void size(std::size_t size) {
            R_TRY_LOG(fsFileSetSize(&this->handle, static_cast<s64>(size)));
        }

        inline std::size_t read(void *buf, std::size_t size, std::size_t offset = 0) {
            u64 tmp = 0;
            R_TRY_LOG(fsFileRead(&this->handle, static_cast<s64>(offset), buf, static_cast<u64>(size), FsReadOption_None, &tmp));
            return tmp;
        }

        inline void write(const void *buf, std::size_t size, std::size_t offset = 0) {
            R_TRY_LOG(fsFileWrite(&this->handle, static_cast<s64>(offset), buf, size, FsWriteOption_None));
        }

        inline void flush() {
            R_TRY_LOG(fsFileFlush(&this->handle));
        }

    protected:
        FsFile handle = {};
};

class Filesystem {
    public:
        constexpr inline Filesystem() = default;
        constexpr inline Filesystem(const FsFileSystem &handle): handle(handle) { }

        inline Filesystem(FsBisPartitionId id) {
            R_TRY_LOG(fsOpenBisFileSystem(&this->handle, id, ""));
        }

        static inline Filesystem sdmc() {
            Filesystem fs;
            R_TRY_LOG(fsOpenSdCardFileSystem(&fs.handle));
            return fs;
        }

        inline void close() {
            flush();
            fsFsClose(&this->handle);
        }

        inline bool is_open() const {
            return this->handle.s.session;
        }

        inline Result flush() {
            return fsFsCommit(&this->handle);
        }

        inline std::size_t total_space() {
            s64 tmp = 0;
            R_TRY_LOG(fsFsGetTotalSpace(&this->handle, "/", &tmp));
            return tmp;
        }

        inline std::size_t free_space() {
            s64 tmp = 0;
            R_TRY_LOG(fsFsGetFreeSpace(&this->handle, "/", &tmp));
            return tmp;
        }

        inline Result open_directory(Directory &d, const std::string &path) {
            return d.open(&this->handle, path);
        }

        inline Result open_file(File &f, const std::string &path, std::uint32_t mode = FsOpenMode_Read) {
            return f.open(&this->handle, path, mode);
        }

        inline Result create_directory(const std::string &path) {
            return fsFsCreateDirectory(&this->handle, path.c_str());
        }

        inline Result create_file(const std::string &path, std::size_t size = 0) {
            return fsFsCreateFile(&this->handle, path.c_str(), static_cast<s64>(size), 0);
        }

        inline Result copy_file(const std::string &source, const std::string &destination) {
            File source_f, dest_f;
            R_TRY_RETURN(this->open_file(source_f, source) || this->open_file(dest_f, destination, FsOpenMode_Write));

            constexpr std::size_t buf_size = 0x100000; // 1 MiB
            auto buf = std::vector<std::uint8_t>(buf_size);

            std::size_t size = source_f.size(), offset = 0;
            while (size) {
                auto read = source_f.read(static_cast<void *>(buf.data()), buf_size, offset);
                dest_f.write(buf.data(), read, offset);
                offset += read;
                size   -= read;
            }

            source_f.close();
            dest_f.close();

            return Result::success();
        }

        inline FsDirEntryType get_path_type(const std::string &path) {
            FsDirEntryType type;
            R_TRY_LOG(fsFsGetEntryType(&this->handle, path.c_str(), &type));
            return type;
        }

        inline bool is_directory(const std::string &path) {
            return get_path_type(path) == FsDirEntryType_Dir;
        }

        inline bool is_file(const std::string &path) {
            return get_path_type(path) == FsDirEntryType_File;
        }

        inline FsTimeStampRaw get_timestamp(const std::string &path) {
            FsTimeStampRaw ts = {};
            R_TRY_LOG(fsFsGetFileTimeStampRaw(&this->handle, path.c_str(), &ts));
            return ts;
        }

        inline std::uint64_t get_timestamp_created(const std::string &path) {
            return this->get_timestamp(path).created;
        }

        inline std::uint64_t get_timestamp_modified(const std::string &path) {
            return this->get_timestamp(path).modified;
        }

        inline Result move_directory(const std::string &old_path, const std::string &new_path) {
            return fsFsRenameDirectory(&this->handle, old_path.c_str(), new_path.c_str());
        }

        inline Result move_file(const std::string &old_path, const std::string &new_path) {
            return fsFsRenameFile(&this->handle, old_path.c_str(), new_path.c_str());
        }

        inline Result delete_directory(const std::string &path) {
            return fsFsDeleteDirectoryRecursively(&this->handle, path.c_str());
        }

        inline Result delete_file(const std::string &path) {
            return fsFsDeleteFile(&this->handle, path.c_str());
        }

    private:
        FsFileSystem handle = {};
};

} // namespace nq::fs
