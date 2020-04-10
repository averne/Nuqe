#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <switch.h>

#include "mtp_object.hpp"
#include "mtp_packet.hpp"
#include "mtp_types.hpp"
#include "fs.hpp"
#include "utils.hpp"

namespace nq::mtp {

constexpr inline std::uint32_t root_handle = 0xffffffff;

struct StorageId {
    union {
        std::uint32_t id = 0;
        struct {
            std::uint16_t partition; // Lower 16 bits (little endian)
            std::uint16_t location;  // Higher 16 bits
        };
    };

    constexpr inline StorageId(): id(0) { };
    constexpr inline StorageId(const StorageId &other): id(other.id) { }
    constexpr inline StorageId(std::uint32_t id): id(id) { }
    constexpr inline StorageId(std::uint16_t location, std::uint16_t partition): partition(partition), location(location) { }

    constexpr inline operator std::uint32_t() const {
        return this->id;
    }
};
ASSERT_SIZE(StorageId, 4);

struct StorageInfo {
    StorageType      storage_type       = StorageType::Undefined;
    FilesystemType   filesystem_type    = FilesystemType::Undefined;
    AccessCapability access_capability  = AccessCapability::ReadWrite;
    std::uint64_t    max_capacity       = 0;
    std::uint64_t    free_space         = 0;
    std::uint32_t    free_space_objects = 0xffffffff;
    String           description        = {};
    String           volume_identifier  = {};
};

struct ObjectInfo {
    StorageId        storage_id        = {};
    ObjectFormatCode format            = ObjectFormatCode::Undefined;
    Protection       protection_status = Protection::None;
    std::uint32_t    compressed_size   = 0;
    ObjectFormatCode thumbnail_format  = ObjectFormatCode::Undefined;
    std::uint32_t    thumbnail_size    = 0;
    std::uint32_t    thumbnail_width   = 0;
    std::uint32_t    thumbnail_height  = 0;
    std::uint32_t    image_width       = 0;
    std::uint32_t    image_height      = 0;
    std::uint32_t    image_depth       = 0;
    Object::Handle   parent            = 0;
    AssociationType  association_type  = AssociationType::GenericFolder;
    AssociationDesc  association_desc  = 0;
    std::uint32_t    sequence_number   = 0;
    String           filename          = {};
    DateTime         created           = 0;
    DateTime         modified          = 0;
    String           keywords          = {};

    ObjectInfo() = default;
    ObjectInfo(DataPacket &packet);
    ObjectInfo(StorageId id, const Object &object):
        storage_id(id), format(object.format), compressed_size(object.size), parent(object.parent->handle), filename(object.name) { }

    void push_to(DataPacket &packet);
};

struct Storage {
    fs::Filesystem fs           = {};
    StorageId      id           = 0;
    StorageInfo    storage_info = {};

    inline Storage() = default;

    Storage(const fs::Filesystem &fs, StorageId id, const StorageInfo &storage_info);

    inline ~Storage() {
        this->fs.close();
    }

    inline void update_storage_info() {
        this->storage_info.free_space   = this->fs.free_space();
        this->storage_info.max_capacity = this->fs.total_space();
    }

    std::vector<Object::Handle> cache_directory(Object *object, std::uint32_t depth = 1, std::uint32_t cur_depth = 1);

    ResponseCode get_storage_info(DataPacket &packet);
    ResponseCode get_object_handles(DataPacket &packet, Object *object);
    ResponseCode get_object_info(DataPacket &packet, Object *object);
    ResponseCode get_object(DataPacket &packet, Object *object);
    ResponseCode delete_object(Object *object);
    ResponseCode send_object_info(DataPacket &packet, Object::Handle parent_handle, Object **out_obj);
    ResponseCode send_object(DataPacket &packet, Object *object);
    ResponseCode move_object(Object *object, Object::Handle parent_handle, Object::Handle &new_handle);
    ResponseCode copy_object(Object *object, Object::Handle parent_handle, Object::Handle &new_handle);
    ResponseCode get_partial_object(DataPacket &packet, Object *object, std::size_t offset, std::size_t size);
    ResponseCode get_object_prop_value(DataPacket &packet, Object *object, ObjectPropertyCode property);
    ResponseCode set_object_prop_value(DataPacket &packet, Object *object, ObjectPropertyCode property);
    ResponseCode get_object_prop_list(DataPacket &packet, Object *object,
        ObjectFormatCode format, ObjectPropertyCode prop, std::uint32_t group_code, std::uint32_t depth);

    inline Object *find_handle(Object::Handle handle) {
        if (auto it = this->objects.find(handle); it != this->objects.end())
            return &it->second;
        return nullptr;
    }

    private:
        std::unordered_map<std::string, Object::Handle> known_paths;
        std::unordered_map<Object::Handle, Object> objects;
};

class StorageManager {
    public:
        StorageManager() { }

        inline void add_storage(Storage &&storage) {
            this->storages[storage.id] = std::move(storage);
        }

        ResponseCode find_storage(StorageId id, Storage **storage);
        ResponseCode find_handle(Object::Handle handle, Storage **storage, Object **object);

        ResponseCode get_storage_ids(DataPacket &packet) const;

    private:
        std::unordered_map<std::uint32_t, Storage> storages;
};

} // namespace nq::mtp
