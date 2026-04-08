#include "tusb.h"

// ============== USB Descriptor ==============

// Device Descriptor
const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCAFE,
    .idProduct          = 0x4005,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Configuration Descriptor
const uint8_t desc_configuration[] = {
    // Configuration Descriptor
    TUD_CONFIG_DESCRIPTOR(
        1,                                  // Configuration number
        1,                                  // Interface count
        0,                                  // String index
        (9 + 9 + 7 + 7),                   // Total length (config + interface + endpoint*2)
        0,                                  // Attributes (Bus Powered)
        100                                 // Max power (200mA)
    ),

    // Interface Descriptor (Vendor)
    TUD_VENDOR_DESCRIPTOR(
        0,                                  // Interface number
        0,                                  // String index
        0x81,                               // IN endpoint
        0x01,                               // OUT endpoint
        64                                  // Max packet size
    )
};

// String Descriptor
const char string_manufacturer[] = "TestMfg";
const char string_product[] = "Pico USB Device";
const char string_serial[] = "123456";

const uint8_t *desc_strings[4] = {
    NULL
};

// ============== USB Callback Functions ==============

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    return NULL;
}

// Vendor class callback
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
    (void) rhport;
    (void) stage;
    (void) request;
    return false;
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    (void) itf;
    (void) buffer;
    (void) bufsize;
}

void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes) {
    (void) itf;
    (void) sent_bytes;
}
