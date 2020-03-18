#pragma once

#include <chrono>
#include <thread>
#include <cstdint>
#include <switch.h>

#include "utils.hpp"

namespace nq::usb {

enum class UsbState {
    Initialized,
    Finalized,
    Busy,
    Ready,
};

// 4 Mib
constexpr std::size_t endpoint_buffer_size = 0x400000;
// Double buffered
constexpr std::uint8_t num_buffers = 2;

// Declared extern for direct access
extern UsbDsEndpoint *g_endpoint_in, *g_endpoint_out, *g_endpoint_interr;
extern std::uint8_t g_endpoint_in_buf[endpoint_buffer_size * num_buffers];
extern std::uint8_t g_endpoint_out_buf[endpoint_buffer_size * num_buffers];
extern std::uint8_t g_endpoint_in_cur_buf_idx, g_endpoint_out_cur_buf_idx;

static inline UsbDsEndpoint *get_in_endpoint() {
    return g_endpoint_in;
}

static inline UsbDsEndpoint *get_out_endpoint() {
    return g_endpoint_out;
}

static inline void *get_in_buffer() {
    return g_endpoint_in_buf;
}

static inline void *get_out_buffer() {
    return g_endpoint_out_buf;
}

Result initialize();
void   cancel();
void   finalize();

bool is_connected();

template <typename Rep, typename Period>
static inline bool wait_ready(std::chrono::duration<Rep, Period> timeout) {
    auto stop_time = std::chrono::system_clock::now() + timeout;
    while (std::chrono::system_clock::now() < stop_time) {
        if (is_connected())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return is_connected();
}

// buf must be page-aligned
Result begin_xfer(UsbDsEndpoint *endpoint, void *buf, std::size_t size, std::uint32_t *urb_id);
Result wait_xfer(UsbDsEndpoint *endpoint, std::uint32_t urb_id, std::uint64_t timeout_ns, std::size_t *xferd_size);

Result send(const void *buf, std::size_t size, std::size_t *out_size);
Result receive(void *buf, std::size_t size, std::size_t *out_size);

inline Result set_zlt(UsbDsEndpoint *endpoint, bool zlt = true) {
    return usbDsEndpoint_SetZlt(endpoint, zlt);
}

static inline void snd_dbuf_reset() {
    g_endpoint_in_cur_buf_idx = 0;
}

static inline void snd_dbuf_swap() {
    g_endpoint_in_cur_buf_idx = (g_endpoint_in_cur_buf_idx + 1) % num_buffers;
}

static inline void *snd_dbuf_get_cur_buf() {
    return &g_endpoint_in_buf[g_endpoint_in_cur_buf_idx * endpoint_buffer_size];
}

static inline Result snd_dbuf_begin(std::size_t size, std::uint32_t *urb_id) {
    return begin_xfer(g_endpoint_in, snd_dbuf_get_cur_buf(), size, urb_id);
}

static inline Result snd_dbuf_wait(std::uint32_t urb_id, std::size_t *sent) {
    return wait_xfer(g_endpoint_in, urb_id, UINT64_MAX, sent);
}

static inline void rcv_dbuf_reset() {
    g_endpoint_out_cur_buf_idx = 0;
}

static inline void rcv_dbuf_swap() {
    g_endpoint_out_cur_buf_idx = (g_endpoint_out_cur_buf_idx + 1) % num_buffers;
}

static inline void *rcv_dbuf_get_cur_buf() {
    return &g_endpoint_out_buf[g_endpoint_out_cur_buf_idx * endpoint_buffer_size];
}

static inline Result rcv_dbuf_begin(std::size_t size, std::uint32_t *urb_id) {
    return begin_xfer(g_endpoint_out, rcv_dbuf_get_cur_buf(), size, urb_id);
}

static inline Result rcv_dbuf_wait(std::uint32_t urb_id, std::size_t *sent) {
    return wait_xfer(g_endpoint_out, urb_id, UINT64_MAX, sent);
}

} // namespace nq::usb
