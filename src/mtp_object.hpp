#pragma once

#include <cstdint>
#include <string>

#include "mtp_codes.hpp"
#include "mtp_types.hpp"

namespace nq::mtp {

struct ObjectInfo;

struct Object {
    using Handle = std::uint32_t;

    static inline Handle s_handle = 0;

    ObjectFormatCode format = ObjectFormatCode::Undefined;
    std::size_t      size   = 0;
    String           name   = {};
    std::string      path   = {};
    Handle           handle = s_handle;
    Object          *parent = nullptr;

    static inline Handle new_handle() {
        return (++s_handle == 0) ? 1 : s_handle;
    }

    static String name_from_path(const std::string &path) {
        return path.substr(path.find_last_of("/") + 1);
    }

    inline Object() = default;
    inline Object(const FsDirectoryEntry &entry, std::string &&path, Object *parent):
        format(type(entry)), size(entry.file_size), name(entry.name),
        path(std::move(path)), parent(parent) { }

    Object(const ObjectInfo &info, std::string &&path, Object &parent);

    static constexpr inline ObjectFormatCode type(const FsDirectoryEntry &entry) {
        return (entry.type == FsDirEntryType_Dir) ? ObjectFormatCode::Association : ObjectFormatCode::Undefined;
    }

    inline constexpr bool is_directory() const {
        return this->format == ObjectFormatCode::Association;
    }

    inline constexpr bool is_file() const {
        return this->format == ObjectFormatCode::Undefined;
    }
};

} // namespace nq::mtp
