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

// String Descriptor (UTF-16 LE format required by USB spec)

// Language descriptor (Index 0)
static const uint16_t _desc_str_langid[] = {
    (uint16_t) ((TUSB_DESC_STRING << 8) | (2 + 2)),
    0x0409  // English US
};

// Manufacturer string descriptor (Index 1)
static const uint16_t _desc_str_manufacturer[] = {
    (uint16_t) ((TUSB_DESC_STRING << 8) | (2 + 2*7)),
    'T', 'e', 's', 't', 'M', 'f', 'g', 0
};

// Product string descriptor (Index 2)
static const uint16_t _desc_str_product[] = {
    (uint16_t) ((TUSB_DESC_STRING << 8) | (2 + 2*15)),
    'P', 'i', 'c', 'o', ' ', 'U', 'S', 'B', ' ', 'D', 'e', 'v', 'i', 'c', 'e', 0
};

// Serial number string descriptor (Index 3)
static const uint16_t _desc_str_serial[] = {
    (uint16_t) ((TUSB_DESC_STRING << 8) | (2 + 2*6)),
    '1', '2', '3', '4', '5', '6', 0
};

static const uint16_t *_desc_string_table[] = {
    _desc_str_langid,
    _desc_str_manufacturer,
    _desc_str_product,
    _desc_str_serial
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

    // Index 0 always returns language descriptor
    if (index == 0) {
        return _desc_str_langid;
    }

    // Return string descriptor for valid indices
    if (index < sizeof(_desc_string_table) / sizeof(_desc_string_table[0])) {
        return _desc_string_table[index];
    }

    return NULL;
}

// Vendor class callback
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
    (void) rhport;
    (void) stage;
    (void) request;

    // For now, acknowledge control requests but don't process them
    // Return true if handled, false if not
    // Device enumeration doesn't strictly require vendor control transfers,
    // so returning false is acceptable for basic enumeration
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
