#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <algorithm>
#include <type_traits>

#include "error.hpp"
#include "fs.hpp"
#include "mtp_codes.hpp"
#include "mtp_types.hpp"
#include "usb.hpp"
#include "utils.hpp"

namespace nq::mtp {

// Packet types as defined in ISO15740
enum class PacketType: std::uint16_t {
    Undefined = 0,
    Command   = 1,
    Data      = 2,
    Response  = 3,
    Event     = 4,
};

// PTP packet as defined in ISO15740
struct PacketHeader {
    std::uint32_t   size           = 0;
    PacketType      type           = PacketType::Undefined;
    TransactionCode code           = 0;
    std::uint32_t   transaction_id = 0;
};
ASSERT_SIZE(PacketHeader, 0xc);
ASSERT_STANDARD_LAYOUT(PacketHeader);

template <std::size_t N>
struct Packet {
    PacketHeader                 header = {};
    std::array<std::uint32_t, N> params = {};

    constexpr inline Packet() = default;

    constexpr inline std::size_t size()        const { return this->header.size; }
    constexpr inline std::size_t params_size() const { return this->header.size - sizeof(PacketHeader); }
    constexpr inline std::size_t params_nb()   const { return this->params_size() / sizeof(decltype(this->params)::value_type); }

    template <typename T = std::uint32_t>
    constexpr inline T get(std::size_t idx) const {
        return static_cast<T>(this->params[idx]);
    }
};

template <std::size_t N>
struct InPacket: public Packet<N> {
    constexpr inline InPacket() = default;

    inline Result receive() {
        std::size_t received;
        R_TRY_RETURN(usb::receive(this, sizeof(InPacket<N>), &received));
        return (received >= sizeof(PacketHeader)) ? Result::success() : err::FailedUsbReceive;
    }
};

template <std::size_t N>
struct OutPacket: public Packet<N> {
    constexpr inline OutPacket() = default;

    inline Result send() const {
        std::size_t sent;
        R_TRY_RETURN(usb::send(this, this->size(), &sent));
        return (sent == this->size()) ? Result::success() : err::FailedUsbSend;
    }
};

struct RequestPacket: public InPacket<5> {
    constexpr inline RequestPacket() = default;
};

struct ResponsePacket: public OutPacket<5> {
    constexpr inline ResponsePacket() {
        this->header.size = sizeof(PacketHeader);
        this->header.type = PacketType::Response;
    };

    constexpr inline ResponsePacket(ResponseCode code): ResponsePacket() {
        update_header(code);
    }

    constexpr inline void update_header(ResponseCode code) {
        this->header.code = static_cast<TransactionCode>(code);
    }

    constexpr inline void update_header(const RequestPacket &request) {
        this->header.transaction_id = request.header.transaction_id;
    }

    template <std::size_t N>
    constexpr inline void set_params(std::array<std::uint32_t, N> params) {
        std::copy(params.begin(), params.end(), this->params.begin());
        this->header.size = params.size() * sizeof(typename decltype(params)::value_type) + sizeof(PacketHeader);
    }
};

struct DataPacket {
    PacketHeader              header = {};
    std::size_t               offset = 0;
    std::vector<std::uint8_t> buffer = {};

    inline DataPacket() = default;

    inline DataPacket(const RequestPacket &request) {
        update_header(request);
    }

    template <typename ...Args>
    inline DataPacket(const RequestPacket &request, Args &&...args) {
        set_data(std::forward<Args>(args)...);
        update_header(request);
    }

    inline void update_header() {
        this->header.size = sizeof(PacketHeader) + this->buffer.size();
    }

    inline void update_header(const RequestPacket &request) {
        update_header();
        this->header.type           = PacketType::Data;
        this->header.code           = request.header.code;
        this->header.transaction_id = request.header.transaction_id;
    }

    template <typename ...Args>
    inline void set_data(Args &&...args) {
        (push(args), ...);
    }

    template <typename T, typename Type = std::remove_reference_t<std::remove_cv_t<T>>,
        std::enable_if_t<!traits::is_mtp_type_v<Type>, int> = 0>
    inline void push(T &&object) {
        if (this->buffer.capacity() < this->buffer.size() + sizeof(Type))
            this->buffer.reserve(this->buffer.size() + sizeof(Type));
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&object), sizeof(Type), std::back_inserter(this->buffer));
    }

    template <typename T>
    inline void push(const Array<T> &arr) {
        if (this->buffer.capacity() < this->buffer.size() + arr.size())
            this->buffer.reserve(this->buffer.size() + arr.size());
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&arr.num_elements), sizeof(Array<T>::num_elements),
            std::back_inserter(this->buffer));
        std::copy_n(reinterpret_cast<const std::uint8_t *>(arr.elements.data()), arr.size() - sizeof(Array<T>::num_elements),
            std::back_inserter(this->buffer));
    }

    inline void push(const String &str) {
        if (this->buffer.capacity() < this->buffer.size() + str.size())
            this->buffer.reserve(this->buffer.size() + str.size());
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&str.num_chars), sizeof(String::num_chars),
            std::back_inserter(this->buffer));
        std::copy_n(reinterpret_cast<const std::uint8_t *>(str.chars.data()), str.size() - sizeof(String::num_chars),
            std::back_inserter(this->buffer));
    }

    inline void push(const DateTime &date) {
        push(date.str);
    }

    inline void push(const std::u16string &str) {
        this->push(String(str));
    }

    inline void push(const char16_t *str) {
        this->push(String(str));
    }

    template <typename T, typename Type = std::remove_reference_t<std::remove_cv_t<T>>,
        std::enable_if_t<!traits::is_mtp_type_v<Type>, int> = 0>
    inline Type pop() {
        Type ret =  *reinterpret_cast<Type *>(this->buffer.data() + this->offset);
        this->offset += sizeof(Type);
        return ret;
    }

    template <typename T, std::enable_if_t<traits::is_array_v<T>, int> = 0>
    inline T pop() {
        auto a = Array<T>(this->buffer.data() + this->offset);
        this->offset += a.size();
        return a;
    }

    inline String pop() {
        auto s = String(this->buffer.data() + this->offset);
        this->offset += s.size();
        return s;
    }

    Result receive();
    Result send();

    Result stream_from_file(fs::File &file, std::size_t size, std::size_t offset = 0);
    Result stream_to_file(fs::File &file, std::size_t size, std::size_t offset = 0);
};

} // namespace nq::mtp
