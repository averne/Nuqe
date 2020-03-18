#include "mtp_packet.hpp"

namespace nq::mtp {

Result DataPacket::receive() {
    std::size_t received;
    R_TRY_RETURN(usb::receive(this, sizeof(PacketHeader), &received));
    TRY_RETURNV(received == sizeof(PacketHeader), err::FailedUsbReceive);
    this->buffer.resize(this->header.size - sizeof(PacketHeader));
    R_TRY_RETURN(usb::receive(this->buffer.data(), this->buffer.size(), &received));
    return (received == this->buffer.size()) ? Result::success() : err::FailedUsbReceive;
}

Result DataPacket::send() {
    std::size_t sent;
    update_header();
    R_TRY_RETURN(usb::send(this, sizeof(PacketHeader), &sent));
    TRY_RETURNV(sent == sizeof(PacketHeader), err::FailedUsbSend);
    R_TRY_RETURN(usb::send(this->buffer.data(), this->buffer.size(), &sent));
    TRY_RETURNV(sent == this->buffer.size(), err::FailedUsbSend);
    return usb::set_zlt(usb::get_in_endpoint()); // Signal end of transfer (needed when this->buffer.size() & (wMaxPacketSize - 1) == 0)
}

Result DataPacket::stream_from_file(fs::File &file, std::size_t size, std::size_t offset) {
    if (size + sizeof(PacketHeader) >= std::numeric_limits<decltype(PacketHeader::size)>::max())
        this->header.size = 0xffffffff;
    else
        this->header.size = sizeof(PacketHeader) + size;

    DTRACE(&this->header, sizeof(PacketHeader));

    std::size_t sent;
    R_TRY_RETURN(usb::send(this, sizeof(PacketHeader), &sent));
    TRY_RETURNV(sent == sizeof(PacketHeader), err::FailedUsbSend);

    if (size == 0)
        return Result::success();

    constexpr std::size_t chunk_size = usb::endpoint_buffer_size;
    std::uint32_t urb_id;

    usb::snd_dbuf_reset();
    R_TRY_RETURN(usb::set_zlt(usb::get_in_endpoint(), false));
    std::size_t read = file.read(usb::snd_dbuf_get_cur_buf(), chunk_size, offset);
    R_TRY_RETURN(usb::snd_dbuf_begin(read, &urb_id));
    offset += read;

    while (size) {
        usb::snd_dbuf_swap();
        std::size_t tmp_read = file.read(usb::snd_dbuf_get_cur_buf(), chunk_size, offset);
        offset += tmp_read;

        R_TRY_RETURN(usb::snd_dbuf_wait(urb_id, &sent));
        TRY_RETURNV(sent == read, err::FailedUsbSend);
        size -= sent;
        read  = tmp_read;

        R_TRY_RETURN(usb::snd_dbuf_begin(read, &urb_id));
    }

    R_TRY_RETURN(usb::snd_dbuf_wait(urb_id, &sent));
    TRY_RETURNV(sent == read, err::FailedUsbSend);

    return Result::success();
}

Result DataPacket::stream_to_file(fs::File &file, std::size_t size, std::size_t offset) {
    std::size_t received;
    R_TRY_RETURN(usb::receive(this, sizeof(PacketHeader), &received));
    TRY_RETURNV(received == sizeof(PacketHeader), err::FailedUsbReceive);
    DTRACE(&this->header, sizeof(PacketHeader));

    if (size == 0)
        return Result::success();

    constexpr std::size_t chunk_size = usb::endpoint_buffer_size;
    std::uint32_t urb_id;

    usb::rcv_dbuf_reset();
    R_TRY_RETURN(usb::rcv_dbuf_begin(chunk_size, &urb_id));
    R_TRY_RETURN(usb::rcv_dbuf_wait(urb_id, &received));
    size   -= received;

    while (size) {
        void *buf = usb::rcv_dbuf_get_cur_buf();
        usb::rcv_dbuf_swap();
        R_TRY_RETURN(usb::rcv_dbuf_begin(chunk_size, &urb_id));

        file.write(buf, received, offset);
        offset += received;

        R_TRY_RETURN(usb::rcv_dbuf_wait(urb_id, &received));
        size   -= received;
    }

    file.write(usb::rcv_dbuf_get_cur_buf(), received, offset);

    // End of data transfer is indicated by short or null packet
    if (received == chunk_size)
        R_TRY_RETURN(usb::receive(usb::rcv_dbuf_get_cur_buf(), chunk_size, &received));

    return Result::success();
}

} // namespace nq::mtp
