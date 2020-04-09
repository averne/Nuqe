#pragma once

#include <atomic>
#include <string>

#include "mtp_packet.hpp"
#include "mtp_storage.hpp"
#include "mtp_object.hpp"
#include "mtp_types.hpp"

#include "utils.hpp"

namespace nq::mtp {

namespace info {

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

} // namespace info

class Server {
    public:
        Server(const StorageManager &storage_manager): storage_manager(storage_manager) { }

        Result process();

    private:
        ResponsePacket handle_request(const RequestPacket &packet);

    protected:
        ResponsePacket get_device_info(const RequestPacket &request);
        ResponsePacket open_session(const RequestPacket &request);
        ResponsePacket close_session(const RequestPacket &request);
        ResponsePacket get_storage_ids(const RequestPacket &request);
        ResponsePacket get_storage_info(const RequestPacket &request);
        ResponsePacket get_object_handles(const RequestPacket &request);
        ResponsePacket get_object_info(const RequestPacket &request);
        ResponsePacket get_object(const RequestPacket &request);
        ResponsePacket delete_object(const RequestPacket &request);
        ResponsePacket send_object_info(const RequestPacket &request);
        ResponsePacket send_object(const RequestPacket &request);
        ResponsePacket get_device_prop_desc(const RequestPacket &request);
        ResponsePacket get_device_prop_value(const RequestPacket &request);
        ResponsePacket move_object(const RequestPacket &request);
        ResponsePacket copy_object(const RequestPacket &request);
        ResponsePacket get_partial_object(const RequestPacket &request);
        ResponsePacket get_object_props_supported(const RequestPacket &request);
        ResponsePacket get_object_prop_desc(const RequestPacket &request);
        ResponsePacket get_object_prop_value(const RequestPacket &request);
        ResponsePacket set_object_prop_value(const RequestPacket &request);

    private:
        StorageManager storage_manager;

        Storage *last_sent_storage;
        Object  *last_sent_object;

        std::atomic_bool session_opened = false;
};

} // namespace nq::mtp
