#include "mtp_object.hpp"
#include "mtp_packet.hpp"
#include "mtp_storage.hpp"
#include "mtp_types.hpp"

namespace nq::mtp {

ObjectInfo::ObjectInfo(DataPacket &packet) {
    this->storage_id        = packet.pop<StorageId>();
    this->format            = packet.pop<ObjectFormatCode>();
    this->protection_status = packet.pop<Protection>();
    this->compressed_size   = packet.pop<std::uint32_t>();
    this->thumbnail_format  = packet.pop<ObjectFormatCode>();
    this->thumbnail_size    = packet.pop<std::uint32_t>();
    this->thumbnail_width   = packet.pop<std::uint32_t>();
    this->thumbnail_height  = packet.pop<std::uint32_t>();
    this->image_width       = packet.pop<std::uint32_t>();
    this->image_height      = packet.pop<std::uint32_t>();
    this->image_depth       = packet.pop<std::uint32_t>();
    this->parent            = packet.pop<Object::Handle>();
    this->association_type  = packet.pop<AssociationType>();
    this->association_desc  = packet.pop<AssociationDesc>();
    this->sequence_number   = packet.pop<std::uint32_t>();
    this->filename          = packet.pop();
    this->created.str       = packet.pop();
    this->modified.str      = packet.pop();
    this->keywords          = packet.pop();
}

void ObjectInfo::push_to(DataPacket &packet) {
    packet.buffer.reserve(packet.buffer.size() + sizeof(ObjectInfo) +
        this->filename.size() + this->created.str.size() + this->modified.str.size() + this->keywords.size());
    packet.set_data(
        this->storage_id,       this->format,           this->protection_status, this->compressed_size,
        this->thumbnail_format, this->thumbnail_size,   this->thumbnail_width,   this->thumbnail_height,
        this->image_width,      this->image_height,     this->image_depth,       this->parent,
        this->association_type, this->association_desc, this->sequence_number,   this->filename,
        this->created.str,      this->modified.str,     this->keywords
    );
}

Storage::Storage(const fs::Filesystem &fs, StorageId id, const StorageInfo &storage_info):
        fs(fs), id(id), storage_info(storage_info) {
    // Register root object
    Object root_obj;
    root_obj.handle = root_handle;
    root_obj.format = ObjectFormatCode::Association;
    root_obj.name   = u"";
    root_obj.path   = "/";
    root_obj.size   = 0;
    this->known_paths[root_obj.path] = root_handle;
    this->objects[root_handle] = std::move(root_obj);

    update_storage_info();
}

std::vector<Object::Handle> Storage::cache_directory(Object *object, std::uint32_t depth, std::uint32_t cur_depth) {
    std::vector<Object::Handle> handles;

    if (depth == 0) {
        handles.push_back(object->handle);
        return handles;
    }

    fs::Directory dir;
    R_TRY_RETURNV(this->fs.open_directory(dir, object->path.c_str()), handles);
    SCOPE_GUARD([&dir] { dir.close(); });

    auto entries = dir.list();
    handles.reserve(entries.size());

    for (auto &&entry: entries) {
        Object::Handle handle;
        auto path = object->path + entry.name;
        if (auto obj = this->known_paths.find(path); obj != this->known_paths.end()) {
            // Object was already cached, return existing handle
            handle = obj->second;
        } else {
            // Object wasn't cached, register path + object
            handle = Object::new_handle();
            this->known_paths[path] = handle;

            // Add slash terminator for directories
            if (entry.type == FsDirEntryType_Dir)
                path += '/';

            auto new_object = Object(entry, std::move(path), object);

            this->objects[handle] = std::move(new_object);
        }

        if (cur_depth == depth)
            handles.push_back(handle);

        if (cur_depth < depth) {
            if (auto obj = this->objects[handle]; obj.is_directory()) {
                auto o = this->cache_directory(&obj, depth, cur_depth + 1);
                handles.reserve(handles.size() + o.size());
                handles.insert(handles.end(), o.begin(), o.end());
            }
        }
    }

    return handles;
}

ResponseCode Storage::get_storage_info(DataPacket &packet) {
    this->update_storage_info();
    packet.set_data(
        this->storage_info.storage_type, this->storage_info.filesystem_type,  this->storage_info.access_capability,
        this->storage_info.max_capacity, this->storage_info.free_space,       this->storage_info.free_space_objects,
        this->storage_info.description,  this->storage_info.volume_identifier
    );
    return ResponseCode::OK;
}

ResponseCode Storage::get_object_handles(DataPacket &packet, Object *object) {
    TRACE("Listing directory %s\n", object->path.c_str());
    packet.push(Array<Object::Handle>(this->cache_directory(object)));
    return ResponseCode::OK;
}

ResponseCode Storage::get_object_info(DataPacket &packet, Object *object) {
    TRACE("Getting infos for %s\n", object->path.c_str());

    auto info = ObjectInfo(this->id, *object);

    // Query timestamp
    // TODO: Should be cached with object?
    if (object->is_file()) {
        auto timestamp = this->fs.get_timestamp(object->path);
        info.created  = timestamp.created;
        info.modified = timestamp.modified;
    }

    info.push_to(packet);
    return ResponseCode::OK;
}

ResponseCode Storage::get_object(DataPacket &packet, Object *object) {
    TRACE("Getting object %s (size: %#x)\n", object->path.c_str(), object->size);
    fs::File f;
    R_TRY_RETURNV(this->fs.open_file(f, object->path), ResponseCode::Access_Denied);
    SCOPE_GUARD([&f]() { f.close(); });
    R_TRY_RETURNV(packet.stream_from_file(f, object->size), ResponseCode::Incomplete_Transfer);
    return ResponseCode::OK;
}

ResponseCode Storage::delete_object(Object *object) {
    TRACE("Deleting object %s\n", object->path.c_str());

    if (object->is_file())
        R_TRY_RETURNV(this->fs.delete_file(object->path.c_str()), ResponseCode::Object_WriteProtected);
    else
        R_TRY_RETURNV(this->fs.delete_directory(object->path.c_str()), ResponseCode::Object_WriteProtected);

    return ResponseCode::OK;
}

ResponseCode Storage::send_object_info(DataPacket &packet, Object::Handle parent_handle, Object **out_obj) {
    auto  info        = ObjectInfo(packet);
    auto &parent      = this->objects[parent_handle];
    auto  destination = parent.path + to_utf8(info.filename.chars);

    Object::new_handle();
    auto obj = Object(info, std::move(destination), parent);

    if (obj.is_file())
        R_TRY_LOG(this->fs.create_file(obj.path, obj.size));
    else
        R_TRY_LOG(this->fs.create_directory(obj.path));

    TRACE("Adding object %s (type %d, size %#lx)\n", obj.path.c_str(), obj.type, obj.size);
    this->known_paths[obj.path] = obj.handle;

    if (info.format == ObjectFormatCode::Association)
        obj.path += '/';
    *out_obj = &this->objects.insert_or_assign(obj.handle, std::move(obj)).first->second;

    return ResponseCode::OK;
}

ResponseCode Storage::send_object(DataPacket &packet, Object *object) {
    TRACE("Sending object %s (size: %#x)\n", object->path.c_str(), object->size);
    fs::File f;
    R_TRY_RETURNV(this->fs.open_file(f, object->path, FsOpenMode_Write), ResponseCode::Access_Denied);
    SCOPE_GUARD([&f]() { f.close(); });
    R_TRY_RETURNV(packet.stream_to_file(f, object->size), ResponseCode::Incomplete_Transfer);
    return ResponseCode::OK;
}

ResponseCode Storage::move_object(Object *object, Object::Handle parent_handle, Object::Handle &new_handle) {
    auto old_path = std::move(object->path);

    auto it = this->objects.find(parent_handle);
    if (it == this->objects.end())
        return ResponseCode::Invalid_ObjectHandle;

    auto &parent = it->second;
    object->path = parent.path + to_utf8(object->name.chars);
    TRACE("Moving object %s to %s\n", old_path.c_str(), object->path.c_str());

    if (object->is_file())
        R_TRY_RETURNV(this->fs.move_file(old_path, object->path), ResponseCode::General_Error);
    else
        R_TRY_RETURNV(this->fs.move_directory(old_path, object->path), ResponseCode::General_Error);

    new_handle = object->handle;

    return ResponseCode::OK;
}

ResponseCode Storage::copy_object(Object *object, Object::Handle parent_handle, Object::Handle &new_handle) {
    auto it = this->objects.find(parent_handle);
    if (it == this->objects.end())
        return ResponseCode::Invalid_ObjectHandle;

    auto destination = it->second.path + to_utf8(object->name.chars);
    TRACE("Copying object %s to %s\n", object->path.c_str(), destination.c_str());

    auto new_object   = Object(*object);
    new_object.handle = Object::new_handle();
    new_object.path   = std::move(destination);

    if (new_object.is_file()) {
        R_TRY_LOG(this->fs.create_file(new_object.path, new_object.size));
        R_TRY_RETURNV(this->fs.copy_file(object->path, new_object.path), ResponseCode::Store_Not_Available);
    } else {
        R_TRY_LOG(this->fs.create_directory(new_object.path));
    }

    TRACE("Adding object %s, type %d, size %#lx\n", new_object.path.c_str(), new_object.type, new_object.size);
    this->known_paths[new_object.path] = new_object.handle;
    new_handle = this->objects.insert_or_assign(new_object.handle, std::move(new_object)).first->first;

    return ResponseCode::OK;
}

ResponseCode Storage::get_partial_object(DataPacket &packet, Object *object, std::size_t offset, std::size_t size) {
    TRACE("Getting partial object %s (offset; %#x, size: %#x)\n", object->path.c_str(), offset, size);
    fs::File f;
    R_TRY_RETURNV(this->fs.open_file(f, object->path), ResponseCode::Access_Denied);
    SCOPE_GUARD([&f]() { f.close(); });
    R_TRY_RETURNV(packet.stream_from_file(f, size, offset), ResponseCode::Incomplete_Transfer);
    return ResponseCode::OK;
}

ResponseCode Storage::get_object_prop_value(DataPacket &packet, Object *object, ObjectPropertyCode property) {
    TRACE("Getting prop value for object %s\n", object->path.c_str());
    switch (property) {
        case ObjectPropertyCode::StorageID:
            packet.push(this->id);
            break;
        case ObjectPropertyCode::Object_Format:
            packet.push(object->format);
            break;
        case ObjectPropertyCode::Object_Size:
            if (object->is_directory())
                return ResponseCode::Invalid_ObjectPropCode;
            packet.push(static_cast<std::uint64_t>(object->size));
            break;
        case ObjectPropertyCode::Object_File_Name:
            packet.push(object->name);
            break;
        case ObjectPropertyCode::Date_Created:
            if (object->is_directory())
                return ResponseCode::Invalid_ObjectPropCode;
            packet.push(DateTime(this->fs.get_timestamp_created(object->path)));
            break;
        case ObjectPropertyCode::Date_Modified:
            if (object->is_directory())
                return ResponseCode::Invalid_ObjectPropCode;
            packet.push(DateTime(this->fs.get_timestamp_modified(object->path)));
            break;
        case ObjectPropertyCode::Parent_Object:
            if (object->handle == root_handle)
                packet.push(0);
            else
                packet.push(object->parent->handle);
            break;
        default:
            ERROR("Object prop value %#x not implemented\n", property);
            return ResponseCode::Invalid_ObjectPropCode;
    }
    return ResponseCode::OK;
}

ResponseCode Storage::set_object_prop_value(DataPacket &packet, Object *object, ObjectPropertyCode property) {
    TRACE("Setting prop value for object %s\n", object->path.c_str());
    switch (property) {
        case ObjectPropertyCode::Object_File_Name: {
                auto old_path = std::move(object->path);
                object->name  = packet.pop();
                object->path  = object->parent->path + to_utf8(object->name.chars);

                TRACE("Changing object name to %s\n", object->path.c_str());
                if (object->is_file())
                    this->fs.move_file(old_path, object->path);
                else
                    this->fs.move_directory(old_path, object->path);
            } break;
        default:
            ERROR("Object prop value %#x not implemented\n", property);
            return ResponseCode::Invalid_ObjectPropCode;
    }
    return ResponseCode::OK;
}

ResponseCode Storage::get_object_prop_list(DataPacket &packet, Object *object,
        ObjectFormatCode format, ObjectPropertyCode prop, std::uint32_t group_code, std::uint32_t depth) {
    constexpr auto all_props   = static_cast<ObjectPropertyCode>(0xffffffff);
    constexpr auto all_formats = static_cast<ObjectFormatCode>(0);

    if (group_code)
        return ResponseCode::Specification_By_Group_Unsupported;

    auto handles = this->cache_directory(object, depth);
    packet.buffer.reserve(0x10 * handles.size());

    std::uint32_t nb_props = 0;
    packet.push(0u); // Reserve nb props

    for (auto &&handle: handles) {
        auto &obj = this->objects[handle];

        if ((format != all_formats) && (obj.format != format))
            continue;

#define PUSH_PROP(property, type, item, cond)                                           \
    if ((cond) && ((prop == all_props) || (prop == ObjectPropertyCode::property))) {    \
        ++nb_props;                                                                     \
        packet.push(obj.handle);                                                        \
        packet.push(ObjectPropertyCode::property);                                      \
        packet.push(TypeCode::type);                                                    \
        packet.push(item);                                                              \
    }
        PUSH_PROP(StorageID, UINT32, this->id, true);
        PUSH_PROP(Object_Format, UINT16, obj.format, true);
        PUSH_PROP(Object_File_Name, STR, obj.name, true);
        PUSH_PROP(Parent_Object, UINT32, obj.parent->handle, true);
        PUSH_PROP(Object_Size, UINT64, obj.size, obj.is_file());
        PUSH_PROP(Date_Created, STR, DateTime(this->fs.get_timestamp_created(obj.path)), obj.is_file());
        PUSH_PROP(Date_Modified, STR, DateTime(this->fs.get_timestamp_modified(obj.path)), obj.is_file());
#undef PUSH_PROP
    }

    *reinterpret_cast<std::uint32_t *>(packet.buffer.begin().base()) = nb_props;

    return ResponseCode::OK;
}

ResponseCode StorageManager::find_storage(StorageId id, Storage **storage) {
    if (auto it = this->storages.find(id); it != this->storages.end()) {
        *storage = &it->second;
        return ResponseCode::OK;
    }
    return ResponseCode::Invalid_StorageID;
}

ResponseCode StorageManager::find_handle(Object::Handle handle, Storage **storage, Object **object) {
    for (auto &&s: this->storages) {
        if (auto *obj = s.second.find_handle(handle); obj) {
            *storage = &s.second;
            *object = obj;
            return ResponseCode::OK;
        }
    }
    return ResponseCode::Invalid_ObjectHandle;
}

ResponseCode StorageManager::get_storage_ids(DataPacket &packet) const {
    Array<StorageId> ids;
    for (auto &&s: this->storages)
        ids.add(s.first);
    packet.push(ids);
    return ResponseCode::OK;
}

} // namespace nq::mtp
