#include "error.hpp"
#include "usb.hpp"

#include "mtp_codes.hpp"
#include "mtp_properties.hpp"
#include "mtp_server.hpp"

#define DUMP_DPACKET(p) ({                    \
    DTRACE(&p.header, sizeof(PacketHeader));  \
    DTRACE(p.buffer.data(), p.buffer.size()); \
})

#define SEND_DPACKET(p) DUMP_DPACKET(p), p.send().succeeded() ? ResponseCode::OK : ResponseCode::General_Error

namespace nq::mtp {

namespace {

static inline std::uint16_t standard_version         = 100;        // PTP version: 1.0.0
static inline std::uint32_t vendor_extension_id      = 6;          // MTP id -- Spec specifies 0xffffffff should be used but libmtp warns that this id is usually used by PTP devices
static inline std::uint16_t vendor_extension_version = 110;        // MTP version: 1.1.0
static inline String        mtp_extensions           = {};
static inline std::uint16_t functional_mode          = 0;
static inline String        manufacturer             = u"Nintendo";
static inline String        model                    = u"Switch";
static inline String        version                  = u"Unknown";
static inline String        serial_number            = u"Unknown";

static inline Array<OperationCode> supported_operations = std::array{
    OperationCode::GetDeviceInfo,
    OperationCode::OpenSession,
    OperationCode::CloseSession,
    OperationCode::GetStorageIDs,
    OperationCode::GetStorageInfo,
    OperationCode::GetObjectHandles,
    OperationCode::GetObjectInfo,
    OperationCode::GetObject,
    OperationCode::DeleteObject,
    OperationCode::SendObjectInfo,
    OperationCode::SendObject,
    OperationCode::GetDevicePropDesc,
    OperationCode::GetDevicePropValue,
    OperationCode::MoveObject,
    OperationCode::CopyObject,
    OperationCode::GetPartialObject,
    OperationCode::GetObjectPropsSupported,
    OperationCode::GetObjectPropDesc,
    OperationCode::GetObjectPropValue,
    OperationCode::SetObjectPropValue,
};

static inline Array<EventCode> supported_events = std::array{
    EventCode::Undefined,
};

static inline Array<DevicePropertyCode> supported_device_properties = std::array{
    DevicePropertyCode::Device_Friendly_Name,
    DevicePropertyCode::Synchronization_Partner,
};

static inline Array<ObjectFormatCode> supported_capture_formats = std::array{
    ObjectFormatCode::Undefined,
};

static inline Array<ObjectFormatCode> supported_playback_formats = std::array{
    ObjectFormatCode::Undefined,
    ObjectFormatCode::Association,
};

} // namespace

Result Server::process() {
    RequestPacket request;
    R_TRY_RETURN(request.receive());
    TRACE("Received request: %#x\n", request.header.code);
    DTRACE(&request, request.size());

    ResponsePacket response;
    switch (request.header.type) {
        case PacketType::Command:
            response = this->handle_request(request);
            break;
        case PacketType::Data:
        case PacketType::Response:
        case PacketType::Event:
            ERROR("Received wrong packet type %#x\n", request.header.type);
            break;
        case PacketType::Undefined:
            ERROR("Undefined packet type %#x\n", request.header.type);
            break;
        default:
            ERROR("Unknown packet type %#x\n", request.header.type);
    }

    response.update_header(request);
    TRACE("Sending response %#x\n", response.header.code);
    DTRACE(&response, response.size());

    return response.send();
}

ResponsePacket Server::handle_request(const RequestPacket &request) {
    switch (static_cast<OperationCode>(request.header.code)) {
        case OperationCode::GetDeviceInfo:
            return this->get_device_info(request);
        case OperationCode::OpenSession:
            return this->open_session(request);
        case OperationCode::CloseSession:
            return this->close_session(request);
        case OperationCode::GetStorageIDs:
            return this->get_storage_ids(request);
        case OperationCode::GetStorageInfo:
            return this->get_storage_info(request);
        case OperationCode::GetObjectHandles:
            return this->get_object_handles(request);
        case OperationCode::GetObjectInfo:
            return this->get_object_info(request);
        case OperationCode::GetObject:
            return this->get_object(request);
        case OperationCode::DeleteObject:
            return this->delete_object(request);
        case OperationCode::SendObjectInfo:
            return this->send_object_info(request);
        case OperationCode::SendObject:
            return this->send_object(request);
        case OperationCode::GetDevicePropDesc:
            return this->get_device_prop_desc(request);
        case OperationCode::GetDevicePropValue:
            return this->get_device_prop_value(request);
        case OperationCode::MoveObject:
            return this->move_object(request);
        case OperationCode::CopyObject:
            return this->copy_object(request);
        case OperationCode::GetPartialObject:
            return this->get_partial_object(request);
        case OperationCode::GetObjectPropsSupported:
            return this->get_object_props_supported(request);
        case OperationCode::GetObjectPropDesc:
            return this->get_object_prop_desc(request);
        case OperationCode::GetObjectPropValue:
            return this->get_object_prop_value(request);
        case OperationCode::SetObjectPropValue:
            return this->set_object_prop_value(request);
        default:
            ERROR("Request %#x not implemented\n", request.header.code);
            return ResponseCode::Invalid_TransactionID;
    }
}

ResponsePacket Server::get_device_info(const RequestPacket &request) {
    TRACE("Sending device info:\n");
    auto device_info = DataPacket(
        request,
        info::standard_version,
        info::vendor_extension_id,
        info::vendor_extension_version,
        info::mtp_extensions,
        info::functional_mode,
        info::supported_operations,
        info::supported_events,
        info::supported_device_properties,
        info::supported_capture_formats,
        info::supported_playback_formats,
        info::manufacturer,
        info::model,
        info::version,
        info::serial_number
    );
    return SEND_DPACKET(device_info);
}

ResponsePacket Server::open_session(const RequestPacket &request) {
    TRACE("Opening session (id %d)\n", request.get(0));
    this->session_opened = true;
    return ResponseCode::OK;
}

ResponsePacket Server::close_session(const RequestPacket &request) {
    TRACE("Closing session (id %d)\n", request.get(0));
    this->session_opened = false;
    return ResponseCode::OK;
}

ResponsePacket Server::get_storage_ids(const RequestPacket &request) {
    TRACE("Sending storage ids:\n");
    auto storage_ids = DataPacket(request);
    MTP_TRY_RETURN(this->storage_manager.get_storage_ids(storage_ids));
    return SEND_DPACKET(storage_ids);
}

ResponsePacket Server::get_storage_info(const RequestPacket &request) {
    TRACE("Sending storage info (storage %#010x):\n", request.get(0));

    Storage *storage = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_storage(request.get(0), &storage));

    auto storage_info = DataPacket(request);
    MTP_TRY_RETURN(storage->get_storage_info(storage_info));
    return SEND_DPACKET(storage_info);
}

ResponsePacket Server::get_object_handles(const RequestPacket &request) {
    TRACE("Sending object handles (device %#x, object format %#x, parent %#x)\n", request.get(0), request.get(1), request.get(2));
    if (request.get(1)) {
        ERROR("Filtering by format %#x not supported\n", request.get(1));
        return ResponseCode::Specification_By_Format_Unsupported;
    }

    Storage *storage = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_storage(request.get(0), &storage));

    Object *object = storage->find_handle(request.get(2));
    TRY_RETURNV(object != nullptr, ResponseCode::Invalid_ObjectHandle);

    DataPacket object_handles(request);
    MTP_TRY_RETURN(storage->get_object_handles(object_handles, object));
    return SEND_DPACKET(object_handles);
}

ResponsePacket Server::delete_object(const RequestPacket &request) {
    TRACE("Deleting object (handle %#x, format %#x)\n", request.get(0), request.get(1));
    if (request.get(1))
        return ResponseCode::Specification_By_Format_Unsupported;

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    return storage->delete_object(object);
}

ResponsePacket Server::get_object_info(const RequestPacket &request) {
    TRACE("Sending object info (handle %#x)\n", request.get(0));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    auto object_info = DataPacket(request);
    MTP_TRY_RETURN(storage->get_object_info(object_info, object));
    return SEND_DPACKET(object_info);
}

ResponsePacket Server::get_object(const RequestPacket &request) {
    TRACE("Getting object (handle %#x)\n", request.get(0));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    auto packet = DataPacket(request);
    return storage->get_object(packet, object);
}

ResponsePacket Server::send_object_info(const RequestPacket &request) {
    TRACE("Sending object info (storage %#010x, parent %#x)\n", request.get(0), request.get(1));
    auto packet = DataPacket();
    R_TRY_RETURNV(packet.receive(), ResponseCode::General_Error);

    MTP_TRY_RETURN(this->storage_manager.find_storage(request.get(0), &this->last_sent_storage));

    MTP_TRY_RETURN(this->last_sent_storage->send_object_info(packet, request.get(1), &this->last_sent_object));

    auto response = ResponsePacket(ResponseCode::OK);
    response.set_params(std::array{
        this->last_sent_storage->id.id,
        request.get(1),
        this->last_sent_object->handle,
    });
    return response;
}

ResponsePacket Server::send_object(const RequestPacket &request) {
    TRACE("Sending object (handle %#x)\n", this->last_sent_object->handle);
    auto packet = DataPacket(request);
    return this->last_sent_storage->send_object(packet, this->last_sent_object);
}

ResponsePacket Server::get_device_prop_desc(const RequestPacket &request) {
    TRACE("Sending device prop desc (property %#x):\n", request.get(0));
    auto prop_desc = DataPacket(request);
    MTP_TRY_RETURN(props::get_device_prop_desc(prop_desc, request.get<DevicePropertyCode>(0)));
    return SEND_DPACKET(prop_desc);
}

ResponsePacket Server::get_device_prop_value(const RequestPacket &request) {
    TRACE("Sending device prop value (property %#x):\n", request.get(0));
    auto prop_value = DataPacket(request);
    MTP_TRY_RETURN(props::get_device_prop_value(prop_value, request.get<DevicePropertyCode>(0)));
    return SEND_DPACKET(prop_value);
}

ResponsePacket Server::move_object(const RequestPacket &request) {
    TRACE("Moving object (handle %#x, storage %#010x, parent %#x)\n", request.get(0), request.get(1), request.get(2));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    // Moving objects between stores is not supported
    if (storage->id != request.get(1))
        return ResponseCode::Store_Not_Available;

    Object::Handle parent = request.get(2);
    if (request.get(2) == 0)
        parent = root_handle;

    Object::Handle new_handle;
    auto response = ResponsePacket(storage->move_object(object, parent, new_handle));
    response.set_params(std::array{new_handle});
    return response;
}

ResponsePacket Server::copy_object(const RequestPacket &request) {
    TRACE("Copying object (handle %#x, storage %#010x, parent %#x)\n", request.get(0), request.get(1), request.get(2));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    // Copying objects between stores is not supported
    if (storage->id != request.get(1))
        return ResponseCode::Store_Not_Available;

    Object::Handle parent = request.get(2);
    if (request.get(2) == 0)
        parent = root_handle;

    Object::Handle new_handle;
    auto response = ResponsePacket(storage->copy_object(object, parent, new_handle));
    response.set_params(std::array{new_handle});
    return response;
}

ResponsePacket Server::get_partial_object(const RequestPacket &request) {
    TRACE("Getting partial object (handle %#x, offset %#x, size %#x)\n", request.get(0), request.get(1), request.get(2));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    auto packet = DataPacket(request);
    return storage->get_partial_object(packet, object, request.get(1), request.get(2));
}

ResponsePacket Server::get_object_props_supported(const RequestPacket &request) {
    TRACE("Sending object props supported (format %#x)\n", request.get(0));
    auto props = DataPacket(request);
    MTP_TRY_RETURN(props::get_object_props_supported(props, request.get<ObjectFormatCode>(0)));
    return SEND_DPACKET(props);
}

ResponsePacket Server::get_object_prop_desc(const RequestPacket &request) {
    TRACE("Sending object prop desc (property %#x, format %#x):\n", request.get(0), request.get(1));
    auto prop_desc = DataPacket(request);
    MTP_TRY_RETURN(props::get_object_prop_desc(prop_desc, request.get<ObjectPropertyCode>(0), request.get<ObjectFormatCode>(1)));
    return SEND_DPACKET(prop_desc);
}

ResponsePacket Server::get_object_prop_value(const RequestPacket &request) {
    TRACE("Sending object prop value (handle %#x, prop code %#x)\n", request.get(0), request.get(1));

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    auto prop_value = DataPacket(request);
    MTP_TRY_RETURN(storage->get_object_prop_value(prop_value, object, request.get<ObjectPropertyCode>(1)));
    return SEND_DPACKET(prop_value);
}

ResponsePacket Server::set_object_prop_value(const RequestPacket &request) {
    TRACE("Setting objet prop value (handle %#x, prop code %#x)\n", request.get(0), request.get(1));
    auto prop_value = DataPacket();
    R_TRY_RETURNV(prop_value.receive(), ResponseCode::General_Error);
    DUMP_DPACKET(prop_value);

    Storage *storage = nullptr; Object *object = nullptr;
    MTP_TRY_RETURN(this->storage_manager.find_handle(request.get(0), &storage, &object));

    return storage->set_object_prop_value(prop_value, object, request.get<ObjectPropertyCode>(1));
}

} // namespace nq::mtp
