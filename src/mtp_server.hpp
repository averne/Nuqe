#pragma once

#include <atomic>
#include <string>

#include "mtp_packet.hpp"
#include "mtp_storage.hpp"
#include "mtp_object.hpp"
#include "mtp_types.hpp"

#include "utils.hpp"

namespace nq::mtp {

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
