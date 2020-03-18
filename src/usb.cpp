#include <cstring>
#include <atomic>
#include <thread>
#include <algorithm>
#include <switch.h>

#include "utils.hpp"

#include "usb.hpp"

using namespace std::chrono_literals;

namespace nq::usb {

UsbDsInterface       *g_interface;
UsbDsEndpoint        *g_endpoint_in, *g_endpoint_out, *g_endpoint_interr;

// Buffers must be page-aligned
alignas(0x1000) std::uint8_t g_endpoint_in_buf[endpoint_buffer_size * num_buffers];
alignas(0x1000) std::uint8_t g_endpoint_out_buf[endpoint_buffer_size * num_buffers];
std::uint8_t g_endpoint_in_cur_buf_idx = 0, g_endpoint_out_cur_buf_idx = 0;

std::atomic<UsbState> g_state = UsbState::Finalized;
std::thread           g_state_thread;
std::atomic_bool      g_state_thread_should_exit = false;

// From libnx usb_comms.c
// For fw >5.x
static inline Result init_usb() {
    u8 iManufacturer, iProduct, iSerialNumber;
    static const u16 supported_langs[] = {0x0409}; // en-us
    R_TRY_RETURN(usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, sizeof(supported_langs) / sizeof(u16)));  // Send language descriptor
    R_TRY_RETURN(usbDsAddUsbStringDescriptor(&iManufacturer, "Nintendo"));                                            // Send manufacturer
    R_TRY_RETURN(usbDsAddUsbStringDescriptor(&iProduct, "Nintendo Switch"));                                          // Send product
    R_TRY_RETURN(usbDsAddUsbStringDescriptor(&iSerialNumber, "SerialNumber"));                                        // Send serial number

    // Send device descriptors
    struct usb_device_descriptor device_descriptor = {
        .bLength                = USB_DT_DEVICE_SIZE,
        .bDescriptorType        = USB_DT_DEVICE,
        .bcdUSB                 = 0x0110,
        .bDeviceClass           = 0x00,
        .bDeviceSubClass        = 0x00,
        .bDeviceProtocol        = 0x00,
        .bMaxPacketSize0        = 0x40,
        .idVendor               = 0x057e,
        .idProduct              = 0x3000,
        .bcdDevice              = 0x0100,
        .iManufacturer          = iManufacturer,
        .iProduct               = iProduct,
        .iSerialNumber          = iSerialNumber,
        .bNumConfigurations     = 0x01,
    };

    // Full Speed is USB 1.1
    R_TRY_RETURN(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device_descriptor));

    // High Speed is USB 2.0
    device_descriptor.bcdUSB = 0x0200;
    R_TRY_RETURN(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device_descriptor));

    // Super Speed is USB 3.0
    device_descriptor.bcdUSB = 0x0300;
    // Upgrade packet size to 512
    device_descriptor.bMaxPacketSize0 = 0x09;
    R_TRY_RETURN(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device_descriptor));

    // Define Binary Object Store
    u8 bos[] = {
        0x05,                       // .bLength
        USB_DT_BOS,                 // .bDescriptorType
        0x16, 0x00,                 // .wTotalLength
        0x02,                       // .bNumDeviceCaps

        // USB 2.0
        0x07,                       // .bLength
        USB_DT_DEVICE_CAPABILITY,   // .bDescriptorType
        0x02,                       // .bDevCapabilityType
        0x02, 0x00, 0x00, 0x00,     // dev_capability_data

        // USB 3.0
        0x0A,                       // .bLength
        USB_DT_DEVICE_CAPABILITY,   // .bDescriptorType
        0x03,                       // .bDevCapabilityType
        0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    R_TRY_RETURN(usbDsSetBinaryObjectStore(bos, sizeof(bos)));

    return Result::success();
}

static inline Result init_mtp_interface() {
    u8 mtp_index;
    R_TRY_RETURN(usbDsAddUsbStringDescriptor(&mtp_index, "MTP"));

    struct usb_interface_descriptor interface_descriptor = {
        .bLength            = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bNumEndpoints      = 3,               // Spec specifies 4 (missing "default" endpoint)
        .bInterfaceClass    = USB_CLASS_IMAGE, // Image
        .bInterfaceSubClass = 1,               // Still image capture device
        .bInterfaceProtocol = 1,               // Use still image protocol
        .iInterface         = mtp_index,       // Use MTP protocol
    };

    // Data-in
    struct usb_endpoint_descriptor endpoint_descriptor_in = {
        .bLength            = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType    = USB_DT_ENDPOINT,
        .bEndpointAddress   = USB_ENDPOINT_IN,
        .bmAttributes       = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize     = 0x40, // Implementation-specific, max 64
    };

    // Data-out
    struct usb_endpoint_descriptor endpoint_descriptor_out = {
        .bLength            = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType    = USB_DT_ENDPOINT,
        .bEndpointAddress   = USB_ENDPOINT_OUT,
        .bmAttributes       = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize     = 0x40, // Implementation-specific, max 64
    };

    // Interrupt
    struct usb_endpoint_descriptor endpoint_descriptor_interr = {
        .bLength            = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType    = USB_DT_ENDPOINT,
        .bEndpointAddress   = USB_ENDPOINT_IN,
        .bmAttributes       = USB_TRANSFER_TYPE_INTERRUPT,
        .wMaxPacketSize     = 0x1c, // Implementation-specific
        .bInterval          = 6,
    };

    struct usb_ss_endpoint_companion_descriptor endpoint_companion = {
        .bLength            = sizeof(struct usb_ss_endpoint_companion_descriptor),
        .bDescriptorType    = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst          = 0x0f,
        .bmAttributes       = 0x00,
        .wBytesPerInterval  = 0x00,
    };

    R_TRY_RETURN(usbDsRegisterInterface(&g_interface));

    interface_descriptor.bInterfaceNumber        = g_interface->interface_index;
    endpoint_descriptor_in.bEndpointAddress     += interface_descriptor.bInterfaceNumber + 1;
    endpoint_descriptor_out.bEndpointAddress    += interface_descriptor.bInterfaceNumber + 1;
    endpoint_descriptor_interr.bEndpointAddress += interface_descriptor.bInterfaceNumber + 2;

    // Full Speed Config
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Full, &interface_descriptor,        USB_DT_INTERFACE_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Full, &endpoint_descriptor_in,      USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Full, &endpoint_descriptor_out,     USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Full, &endpoint_descriptor_interr,  USB_DT_ENDPOINT_SIZE));

    // High Speed Config
    endpoint_descriptor_in.wMaxPacketSize  = 0x200;
    endpoint_descriptor_out.wMaxPacketSize = 0x200;
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_High, &interface_descriptor,        USB_DT_INTERFACE_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_High, &endpoint_descriptor_in,      USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_High, &endpoint_descriptor_out,     USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_High, &endpoint_descriptor_interr,  USB_DT_ENDPOINT_SIZE));

    // Super Speed Config
    endpoint_descriptor_in.wMaxPacketSize  = 0x400;
    endpoint_descriptor_out.wMaxPacketSize = 0x400;
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &interface_descriptor,       USB_DT_INTERFACE_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_descriptor_in,     USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_companion,         USB_DT_SS_ENDPOINT_COMPANION_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_descriptor_out,    USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_companion,         USB_DT_SS_ENDPOINT_COMPANION_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_descriptor_interr, USB_DT_ENDPOINT_SIZE));
    R_TRY_RETURN(usbDsInterface_AppendConfigurationData(g_interface, UsbDeviceSpeed_Super, &endpoint_companion,         USB_DT_SS_ENDPOINT_COMPANION_SIZE));

    // Setup endpoints.
    R_TRY_RETURN(usbDsInterface_RegisterEndpoint(g_interface, &g_endpoint_in,     endpoint_descriptor_in.bEndpointAddress));
    R_TRY_RETURN(usbDsInterface_RegisterEndpoint(g_interface, &g_endpoint_out,    endpoint_descriptor_out.bEndpointAddress));
    R_TRY_RETURN(usbDsInterface_RegisterEndpoint(g_interface, &g_endpoint_interr, endpoint_descriptor_interr.bEndpointAddress));

    return Result::success();
}

static void state_change_func() {
    u32 state;
    auto state_change_event = usbDsGetStateChangeEvent();

    while (!g_state_thread_should_exit) {
        if (R_SUCCEEDED(eventWait(state_change_event, 0)) && R_SUCCEEDED(usbDsGetState(&state))) {
            switch (state) {
                case 0 ... 4:
                case 6:
                    g_state = UsbState::Busy;
                    break;
                case 5:
                    g_state = UsbState::Ready;
                    break;
            }
        }
        eventClear(state_change_event);
        std::this_thread::sleep_for(10ms);
    }
}

static inline bool check_state() {
    return (g_state == UsbState::Initialized) || (g_state == UsbState::Ready);
}

Result initialize() {
    R_TRY_RETURNV(check_state(), 0);

    R_TRY_RETURN(usbDsInitialize());
    R_TRY_RETURN(init_usb());
    R_TRY_RETURN(init_mtp_interface());
    R_TRY_RETURN(usbDsInterface_EnableInterface(g_interface));
    R_TRY_RETURN(usbDsEnable());

    g_state_thread = std::thread(state_change_func);
    g_state = UsbState::Initialized;

    return Result::success();
}

void cancel() {
    usbDsEndpoint_Cancel(g_endpoint_in);
    usbDsEndpoint_Cancel(g_endpoint_out);
}

void finalize() {
    R_TRY_RETURNV(g_state == UsbState::Finalized, );

    g_state_thread_should_exit = true;
    g_state_thread.join();

    cancel();
    usbDsExit();

    g_state = UsbState::Finalized;
}

bool is_connected() {
    return check_state();
}

Result wait_xfer(UsbDsEndpoint *endpoint, std::uint32_t urb_id, std::uint64_t timeout_ns, std::size_t *xferd_size) {
    u32 tmp_xferd;
    UsbDsReportData reportdata;

    Result rc = eventWait(&endpoint->CompletionEvent, timeout_ns);
    if (rc.failed()) {
        // Cancel transaction
        usbDsEndpoint_Cancel(endpoint);
        eventWait(&endpoint->CompletionEvent, UINT64_MAX);
    } else if (xferd_size) {
        R_TRY_RETURN(usbDsEndpoint_GetReportData(endpoint, &reportdata));
        R_TRY_RETURN(usbDsParseReportData(&reportdata, urb_id, NULL, &tmp_xferd));
        *xferd_size = tmp_xferd;
    }

    eventClear(&endpoint->CompletionEvent);
    return rc;
}

Result send(const void *buf, std::size_t size, std::size_t *out) {
    std::uint32_t urb_id;
    std::size_t tmp_xferd = 0;
    *out = 0;
    while (size) {
        auto chunk_size = std::min(size, sizeof(g_endpoint_in_buf));
        buf = std::copy_n(reinterpret_cast<const std::uint8_t *>(buf), chunk_size, g_endpoint_in_buf);
        R_TRY_RETURN(begin_xfer(g_endpoint_in, g_endpoint_in_buf, chunk_size, &urb_id));
        R_TRY_RETURN(wait_xfer(g_endpoint_in, urb_id, UINT64_MAX, &tmp_xferd));
        if (out)
            *out += tmp_xferd;
        size -= tmp_xferd;
        if (tmp_xferd < chunk_size)
            break;
    }
    return Result::success();
}

Result receive(void *buf, std::size_t size, std::size_t *out) {
    std::uint32_t urb_id;
    std::size_t tmp_xferd = 0;
    *out = 0;
    while (size) {
        auto chunk_size = std::min(size, sizeof(g_endpoint_out_buf));
        R_TRY_RETURN(begin_xfer(g_endpoint_out, g_endpoint_out_buf, chunk_size, &urb_id));
        R_TRY_RETURN(wait_xfer(g_endpoint_out, urb_id, UINT64_MAX, &tmp_xferd));
        buf = std::copy_n(g_endpoint_out_buf, tmp_xferd, reinterpret_cast<std::uint8_t *>(buf));
        if (out)
            *out += tmp_xferd;
        size -= tmp_xferd;
        if (tmp_xferd < chunk_size)
            break;
    }
    return Result::success();
}

} // namespace nq::usb
