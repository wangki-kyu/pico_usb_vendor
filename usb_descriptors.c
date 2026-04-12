#include "tusb.h"
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * NPU Model Reception State Machine
 * ============================================================================ */

#define MODEL_BUFFER_SIZE (1)  // 60KB for model data

typedef enum {
    MODEL_RX_IDLE = 0,
    MODEL_RX_RECEIVING,
} model_rx_state_t;

static uint8_t model_buffer[MODEL_BUFFER_SIZE];
static volatile model_rx_state_t model_rx_state = MODEL_RX_IDLE;
static volatile uint16_t model_rx_total = 0;
static volatile uint16_t model_rx_offset = 0;

/* ============================================================================
 * Single Image Reception Buffer
 * ============================================================================ */

#define IMAGE_WIDTH   64
#define IMAGE_HEIGHT  64
#define IMAGE_CHANNELS 1
#define IMAGE_SIZE    (IMAGE_WIDTH * IMAGE_HEIGHT * IMAGE_CHANNELS)  // 4096 bytes

// Single image buffer (no ping-pong complexity)
static int8_t image_buffer[IMAGE_SIZE] = {0};

// Make buffer accessible from pico_test.c for inference
extern volatile int8_t* image_data_ptr;
volatile int8_t* image_data_ptr = image_buffer;

extern volatile bool image_ready;  // Will be defined in pico_test.c

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

// Temperature sensor commands
#define TEMP_CMD_READ    0x10  // Read internal temperature sensor

// NPU model commands
#define CMD_MODEL_LOAD   0x20  // Load model data from host
#define CMD_IMAGE_DATA   0x21  // Inference image data

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
extern void led_on_with_color(uint32_t color);

/* ============================================================================
 * External Temperature Sensor Functions (defined in pico_test.c)
 * ============================================================================ */

extern float adc_get_temperature(void);

/* ============================================================================
 * External Model Ready Flag (defined in pico_test.c)
 * ============================================================================ */

extern volatile bool model_ready;

/* ============================================================================
 * Model Reception Helper Functions
 * ============================================================================ */

extern bool runInference(const int8_t *input_data, float *output_data);
extern void send_tflite_inference_results(const float *output);
extern void send_tflite_inference_results_test(const float *output);

/**
 * Handle incoming model data during RECEIVING state
 */
static void handle_model_rx_data(uint8_t itf, const uint8_t* data, uint16_t len) {
    uint16_t remaining = model_rx_total - model_rx_offset;
    uint16_t to_copy = (len < remaining) ? len : remaining;

    memcpy(model_buffer + model_rx_offset, data, to_copy);
    model_rx_offset += to_copy;

    // Check if reception complete
    if (model_rx_offset >= model_rx_total) {
        model_rx_state = MODEL_RX_IDLE;
        model_ready = true;
    }
}

/**
 * Handle model load command (0x20)
 */
static void handle_model_load_cmd(uint8_t itf, const uint8_t* data, uint16_t len) {
    // Parse header: [0x20][size_high][size_low][payload...]
    if (len < 3) {
        return;
    }

    uint16_t total = ((uint16_t)data[1] << 8) | data[2];

    // Validate model size
    if (total == 0 || total > MODEL_BUFFER_SIZE) {
        return;
    }

    // Initialize model reception
    model_rx_total = total;
    model_rx_offset = 0;
    model_rx_state = MODEL_RX_RECEIVING;
    model_ready = false;

    // Copy first payload immediately
    uint16_t first_payload = len - 3;
    if (first_payload > 0) {
        memcpy(model_buffer, data + 3, first_payload);
        model_rx_offset = first_payload;

        // Check if completed in first packet
        if (model_rx_offset >= model_rx_total) {
            model_rx_state = MODEL_RX_IDLE;
            model_ready = true;
        }
    }
}

/**
 * Handle image data for inference (0x21)
 * Simple approach: copy data to buffer and set ready flag
 */
extern void sleep_ms(uint32_t ms);

static void handle_image_data_cmd(uint8_t itf, const uint8_t* data, uint16_t len) {
    // Image data: [0x21][image_data...]

    // Step 1: Called
    // led_on_with_color(0x00FF00);  // Green - function called
    sleep_ms(200);

    if (len < 2) {  // At least command byte + some data
        // led_on_with_color(0xFF0000);  // Red - data too short
        sleep_ms(500);
        return;
    }

    uint16_t image_data_len = len - 1;  // Skip command byte
    const uint8_t* image_data = data + 1;

    // Copy to single buffer
    uint16_t to_copy = (image_data_len < IMAGE_SIZE) ? image_data_len : IMAGE_SIZE;
    memcpy(image_buffer, (int8_t*)image_data, to_copy);

    // Mark as ready for processing
    if (to_copy > 0) {
        // led_on_with_color(0x0000FF);  // Blue - about to set image_ready
        // sleep_ms(200);
        // image_ready = true;
        // led_on_with_color(0xFFFF00);  // Yellow - image_ready set complete
        // sleep_ms(200);
        float output[64]; 
        memset(output, 0, sizeof(output));
        if (runInference((const int8_t*)image_buffer, output)) {
            // led_on();
            output[0] = 0.5f;
            send_tflite_inference_results(output);
        } else {
            led_on_with_color(0x800080);
            send_tflite_inference_results_test(output);
        }
    }
}

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
    // Read from USB buffer to clear hardware buffer and arm OUT endpoint
    uint8_t tmp[64];
    uint16_t len = tud_vendor_n_read(itf, tmp, bufsize);

    // ========== Model Reception State Machine ==========
    if (model_rx_state == MODEL_RX_RECEIVING) {
        handle_model_rx_data(itf, tmp, len);
        return;
    }

    // ========== Command Processing ==========
    uint8_t command = tmp[0];

    // Handle model load command
    if (command == CMD_MODEL_LOAD) {
        handle_model_load_cmd(itf, tmp, len);
    }
    // Handle image data command
    else if (command == CMD_IMAGE_DATA) {
        // led_toggle();
        handle_image_data_cmd(itf, tmp, len);
    }
    // Handle temperature read command
    else if (command == TEMP_CMD_READ) {
        float temperature = adc_get_temperature();

        // Convert to response format: [integer_part, decimal_part]
        // Example: 25.46°C -> [0x19, 0x2E] (25, 46)
        uint8_t temp_int = (uint8_t)temperature;
        uint8_t temp_dec = (uint8_t)((temperature - temp_int) * 100.0f);

        uint8_t response[2] = {temp_int, temp_dec};
        tud_vendor_n_write(itf, response, 2);
        tud_vendor_n_write_flush(itf);
    }
    // Queue LED commands for main loop to process
    // This avoids blocking operations in USB interrupt handler
    else if (command <= 0x03) {  // Valid LED commands: 0x00, 0x01, 0x02, 0x03
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
