#include "mtp_codes.hpp"
#include "mtp_types.hpp"
#include "mtp_object.hpp"
#include "mtp_packet.hpp"
#include "mtp_properties.hpp"
#include "mtp_storage.hpp"

namespace nq::mtp::props {

ResponseCode get_device_prop_desc(DataPacket &packet, DevicePropertyCode property) {
    switch (property) {
        case DevicePropertyCode::Device_Friendly_Name: {
                DevicePropDesc<String> prop;
                prop.code          = DevicePropertyCode::Device_Friendly_Name;
                prop.type          = TypeCode::STR;
                prop.default_value = dev::device_friendly_name;
                prop.current_value = dev::device_friendly_name;
                prop.push_to(packet);
            } break;
        case DevicePropertyCode::Synchronization_Partner: {
                DevicePropDesc<String> prop;
                prop.code          = DevicePropertyCode::Synchronization_Partner;
                prop.type          = TypeCode::STR;
                prop.default_value = dev::synchronization_partner;
                prop.current_value = dev::synchronization_partner;
                prop.push_to(packet);
            } break;
        default:
            ERROR("Device property desc %#x not implemented\n", property);
            return ResponseCode::DeviceProp_Not_Supported;
    }
    return ResponseCode::OK;
}

ResponseCode get_device_prop_value(DataPacket &packet, DevicePropertyCode property) {
    switch (property) {
        case DevicePropertyCode::Device_Friendly_Name:
            packet.push(dev::device_friendly_name);
            break;
        case DevicePropertyCode::Synchronization_Partner:
            packet.push(dev::synchronization_partner);
            break;
        default:
            ERROR("Device property value %#x not implemented\n", property);
            return ResponseCode::DeviceProp_Not_Supported;
    }
    return ResponseCode::OK;
}

ResponseCode get_object_props_supported(DataPacket &packet, ObjectFormatCode format) {
    Array<ObjectPropertyCode> props;
    if (auto it = obj::supported.find(format); it != obj::supported.end()) {
        props.add(it->second);
    } else {
        ERROR("Object props supported %#x not implemented\n", format);
        return ResponseCode::Operation_Not_Supported;
    }
    packet.push(props);
    return ResponseCode::OK;
}

ResponseCode get_object_prop_desc(DataPacket &packet, ObjectPropertyCode property, ObjectFormatCode format) {
    switch (property) {
        case ObjectPropertyCode::StorageID: {
                ObjectPropDesc<StorageId> prop;
                prop.code          = ObjectPropertyCode::StorageID;
                prop.type          = TypeCode::UINT32;
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Object_Format: {
                ObjectPropDesc<ObjectFormatCode> prop;
                prop.code          = ObjectPropertyCode::Object_Format;
                prop.type          = TypeCode::UINT16;
                prop.default_value = ObjectFormatCode::Undefined;
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Object_Size: {
                ObjectPropDesc<std::uint64_t> prop;
                prop.code          = ObjectPropertyCode::Object_Size;
                prop.type          = TypeCode::UINT64;
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Object_File_Name: {
                ObjectPropDesc<String> prop;
                prop.code          = ObjectPropertyCode::Object_File_Name;
                prop.type          = TypeCode::STR;
                prop.get_set       = 1; // Get/set
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Date_Created: {
                ObjectPropDesc<String> prop;
                prop.code          = ObjectPropertyCode::Date_Created;
                prop.type          = TypeCode::STR;
                prop.form_flag     = Forms::DateTime;
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Date_Modified: {
                ObjectPropDesc<String> prop;
                prop.code          = ObjectPropertyCode::Date_Modified;
                prop.type          = TypeCode::STR;
                prop.form_flag     = Forms::DateTime;
                prop.push_to(packet);
            } break;
        case ObjectPropertyCode::Parent_Object: {
                ObjectPropDesc<Object::Handle> prop;
                prop.code          = ObjectPropertyCode::Parent_Object;
                prop.type          = TypeCode::UINT32;
                prop.push_to(packet);
            } break;
        default:
            ERROR("Object property desc %#x not implemented\n", property);
            return ResponseCode::Operation_Not_Supported;
    }
    return ResponseCode::OK;
}

} // namespace nq::mtp::props
