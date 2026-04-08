#include "tusb.h"
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * LED Control Protocol Definition
 * ============================================================================
 *
 * Command format: Single byte command
 * Response format: Single byte status
 */

// LED control commands
#define LED_CMD_OFF      0x00  // Turn LED off
#define LED_CMD_ON       0x01  // Turn LED on
#define LED_CMD_TOGGLE   0x02  // Toggle LED state
#define LED_CMD_STATUS   0x03  // Query LED status

// Response codes
#define RESP_OK          0x00  // Command executed successfully
#define RESP_INVALID     0xFF  // Invalid command received
#define RESP_LED_OFF     0x00  // LED is currently OFF (for STATUS command)
#define RESP_LED_ON      0x01  // LED is currently ON (for STATUS command)

/* ============================================================================
 * External LED Control Functions (defined in pico_test.c)
 * ============================================================================ */

extern void led_on(void);
extern void led_off(void);
extern void led_toggle(void);
extern bool led_get_state(void);

/* ============================================================================
 * USB Descriptor
 * ============================================================================ */

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

/* ============================================================================
 * USB Vendor Bulk RX Callback
 * ============================================================================
 *
 * Handles incoming vendor-specific bulk transfer from host.
 * Processes LED control commands and sends back response status.
 *
 * Protocol:
 *   - Input: Single byte command (0x00 to 0x03)
 *   - Output: Single byte response (0x00=OK/LED_OFF, 0x01=LED_ON, 0xFF=Invalid)
 *
 * @param itf      Interface index (not used in this implementation)
 * @param buffer   Pointer to received data buffer
 * @param bufsize  Number of bytes received
 */
// External pending command variable
extern volatile uint8_t pending_command;

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    // Explicitly read from buffer to clear hardware buffer and arm OUT endpoint
    uint8_t dummy[64];
    tud_vendor_n_read(itf, dummy, bufsize);

    // Extract command byte from read data
    uint8_t command = dummy[0];

    // Queue command for main loop to process
    // This avoids blocking operations in USB interrupt handler
    if (command <= 0x02) {  // Valid commands: 0x00, 0x01, 0x02
        pending_command = command;
    }
}


/* ============================================================================
 * USB Vendor Bulk TX Callback
 * ============================================================================
 *
 * Called when the bulk IN transfer completes.
 * Currently not used, but kept for future enhancements such as:
 * - Tracking transmission status
 * - Implementing multi-packet responses
 * - Debug logging
 *
 * @param itf        Interface index
 * @param sent_bytes Number of bytes successfully transmitted
 */
void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes) {
    (void) itf;           // Interface index not used
    (void) sent_bytes;    // Sent bytes not used in current implementation

    // TODO: Implement if multi-packet responses or status tracking is needed
}
