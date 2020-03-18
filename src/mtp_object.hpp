#pragma once

#include <cstdint>
#include <string>

#include "mtp_codes.hpp"
#include "mtp_types.hpp"

namespace nq::mtp {

struct ObjectInfo;

struct Object {
    enum class Type {
        File,
        Directory,
    };

    using Handle = std::uint32_t;

    static inline Handle s_handle = 0;

    Type        type   = Type::File;
    std::size_t size   = 0;
    String      name   = {};
    std::string path   = {};
    Handle      handle = s_handle;
    Object     *parent = nullptr;

    static inline Handle new_handle() {
        return (++s_handle == 0) ? 1 : s_handle;
    }

    static String name_from_path(const std::string &path) {
        return path.substr(path.find_last_of("/") + 1);
    }

    inline Object() = default;
    inline Object(const FsDirectoryEntry &entry, const std::string &path, Object &parent):
        type(format(entry)), size(entry.file_size), name(entry.name),
        path(std::move(path)), parent(&parent) { }

    Object(const ObjectInfo &info, const std::string &path, Object &parent);

    inline ObjectFormatCode format() const {
        return (this->type == Type::File) ? ObjectFormatCode::Undefined : ObjectFormatCode::Association;
    }

    static constexpr inline Type format(const FsDirectoryEntry &entry) {
        return (entry.type == FsDirEntryType_Dir) ? Type::Directory : Type::File;
    }

    static constexpr inline Type format(ObjectFormatCode code) {
        return (code == ObjectFormatCode::Association) ? Type::Directory : Type::File;
    }
};

} // namespace nq::mtp
