#include "tusb.h"
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * NPU Model Reception State Machine
 * ============================================================================ */

#define MODEL_BUFFER_SIZE (1) // 60KB for model data

typedef enum
{
    MODEL_RX_IDLE = 0,
    MODEL_RX_RECEIVING,
} model_rx_state_t;

static uint8_t model_buffer[MODEL_BUFFER_SIZE];
static volatile model_rx_state_t model_rx_state = MODEL_RX_IDLE;
static volatile uint16_t model_rx_total = 0;
static volatile uint16_t model_rx_offset = 0;

/* ============================================================================
 * Single Image Reception Buffer & State Machine
 * ============================================================================ */

#define IMAGE_WIDTH 64
#define IMAGE_HEIGHT 64
#define IMAGE_CHANNELS 1
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT * IMAGE_CHANNELS) // 4096 bytes

typedef enum
{
    IMAGE_RX_IDLE = 0,
    IMAGE_RX_RECEIVING,
} image_rx_state_t;

// Single image buffer (no ping-pong complexity)
static int8_t image_buffer[IMAGE_SIZE] = {0};
static volatile uint16_t image_rx_offset = 0; // Track received bytes
static volatile image_rx_state_t image_rx_state = IMAGE_RX_IDLE;

// Make buffer accessible from pico_test.c for inference
extern volatile int8_t *image_data_ptr;
volatile int8_t *image_data_ptr = image_buffer;

extern volatile bool image_ready; // Will be defined in pico_test.c

/* ============================================================================
 * LED Control Protocol Definition
 * ============================================================================
 *
 * Command format: Single byte command
 * Response format: Single byte status
 */

// LED control commands
#define LED_CMD_OFF 0x00    // Turn LED off
#define LED_CMD_ON 0x01     // Turn LED on
#define LED_CMD_TOGGLE 0x02 // Toggle LED state
#define LED_CMD_STATUS 0x03 // Query LED status

// Temperature sensor commands
#define TEMP_CMD_READ 0x10 // Read internal temperature sensor

// NPU model commands
#define CMD_MODEL_LOAD 0x20 // Load model data from host
#define CMD_IMAGE_DATA 0x21 // Inference image data

// Response codes
#define RESP_OK 0x00      // Command executed successfully
#define RESP_INVALID 0xFF // Invalid command received
#define RESP_LED_OFF 0x00 // LED is currently OFF (for STATUS command)
#define RESP_LED_ON 0x01  // LED is currently ON (for STATUS command)

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
static void handle_model_rx_data(uint8_t itf, const uint8_t *data, uint16_t len)
{
    uint16_t remaining = model_rx_total - model_rx_offset;
    uint16_t to_copy = (len < remaining) ? len : remaining;

    memcpy(model_buffer + model_rx_offset, data, to_copy);
    model_rx_offset += to_copy;

    // Check if reception complete
    if (model_rx_offset >= model_rx_total)
    {
        model_rx_state = MODEL_RX_IDLE;
        model_ready = true;
    }
}

/**
 * Handle incoming image data during RECEIVING state
 * All packets: [0x21(CMD)] + [image data 63 bytes]
 */
static void handle_image_rx_data(uint8_t itf, const uint8_t *data, uint16_t len)
{
    // Skip command byte (always 0x21)
    uint16_t image_data_len = len;
    const uint8_t *image_data = data;

    uint16_t remaining = IMAGE_SIZE - image_rx_offset;
    uint16_t to_copy = (image_data_len < remaining) ? image_data_len : remaining;

    memcpy(image_buffer + image_rx_offset, (int8_t *)image_data, to_copy);
    image_rx_offset += to_copy;

    // printf("[USB] Image chunk: %u bytes (total: %u/%u)\n", to_copy, image_rx_offset, IMAGE_SIZE);
    // fflush(stdout);

    // Check if reception complete
    if (image_rx_offset >= IMAGE_SIZE)
    {
        image_rx_state = IMAGE_RX_IDLE;
        printf("[USB] Image complete! Starting inference...\n");
        fflush(stdout);

        float output[64];
        memset(output, 0, sizeof(output));

        if (runInference((const int8_t *)image_buffer, output))
        {
            printf("[INFERENCE] SUCCESS - Results ready\n");
            fflush(stdout);
            output[0] = 0.5f;
            send_tflite_inference_results(output);
        }
        else
        {
            printf("[INFERENCE] FAILED - Running fallback test\n");
            fflush(stdout);
            send_tflite_inference_results_test(output);
        }

        // Reset for next image
        image_rx_offset = 0;
        memset(image_buffer, 0, IMAGE_SIZE);
        printf("[USB] Ready for next image\n");
        fflush(stdout);
    }
}

/**
 * Handle model load command (0x20)
 */
static void handle_model_load_cmd(uint8_t itf, const uint8_t *data, uint16_t len)
{
    // Parse header: [0x20][size_high][size_low][payload...]
    if (len < 3)
    {
        return;
    }

    uint16_t total = ((uint16_t)data[1] << 8) | data[2];

    // Validate model size
    if (total == 0 || total > MODEL_BUFFER_SIZE)
    {
        return;
    }

    // Initialize model reception
    model_rx_total = total;
    model_rx_offset = 0;
    model_rx_state = MODEL_RX_RECEIVING;
    model_ready = false;

    // Copy first payload immediately
    uint16_t first_payload = len - 3;
    if (first_payload > 0)
    {
        memcpy(model_buffer, data + 3, first_payload);
        model_rx_offset = first_payload;

        // Check if completed in first packet
        if (model_rx_offset >= model_rx_total)
        {
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

static void handle_image_data_cmd(uint8_t itf, const uint8_t *data, uint16_t len)
{
    // Image data: [0x21][image_data...]
    printf("[USB] Image chunk received: %u bytes (offset: %u/%u)\n", len, image_rx_offset, IMAGE_SIZE);
    fflush(stdout);

    if (len < 2)
    { // At least command byte + some data
        printf("[USB] ERROR: Chunk too short (len=%u)\n", len);
        fflush(stdout);
        return;
    }

    uint16_t image_data_len = len - 1; // Skip command byte
    const uint8_t *image_data = data + 1;

    // Calculate how much we can copy
    uint16_t remaining = IMAGE_SIZE - image_rx_offset;
    uint16_t to_copy = (image_data_len < remaining) ? image_data_len : remaining;

    // Append to buffer
    memcpy(image_buffer + image_rx_offset, (int8_t *)image_data, to_copy);
    image_rx_offset += to_copy;

    printf("[USB] Chunk copied: %u bytes (total: %u/%u)\n", to_copy, image_rx_offset, IMAGE_SIZE);
    fflush(stdout);

    // Check if we have the complete image
    if (image_rx_offset >= IMAGE_SIZE)
    {
        printf("[USB] Image complete! Starting inference...\n");
        fflush(stdout);

        float output[64];
        memset(output, 0, sizeof(output));

        printf("[INFERENCE] Starting inference on %ux%u image...\n", IMAGE_WIDTH, IMAGE_HEIGHT);
        fflush(stdout);

        if (runInference((const int8_t *)image_buffer, output))
        {
            printf("[INFERENCE] SUCCESS - Results ready\n");
            fflush(stdout);
            output[0] = 0.5f;
            send_tflite_inference_results(output);
        }
        else
        {
            printf("[INFERENCE] FAILED - Running fallback test\n");
            fflush(stdout);
            send_tflite_inference_results_test(output);
        }

        // Reset for next image
        image_rx_offset = 0;
        image_rx_state = IMAGE_RX_IDLE; // Exit receiving state
        memset(image_buffer, 0, IMAGE_SIZE);
        printf("[USB] Ready for next image\n");
        fflush(stdout);
    }
}

/* ============================================================================
 * USB Descriptor
 * ============================================================================ */

// Device Descriptor (Composite Device)
const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xEF,    // Miscellaneous Device (Composite)
    .bDeviceSubClass = 0x02, // Common Class
    .bDeviceProtocol = 0x01, // Interface Association Descriptor
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE,
    .idProduct = 0x4006, // Changed from 0x4005 to force Windows to re-read descriptors
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01};

// Configuration Descriptor
enum
{
    ITF_NUM_CDC_CMD = 0, // CDC Control (Interface 0)
    ITF_NUM_CDC_DATA,    // CDC Data (Interface 1)
    ITF_NUM_VENDOR,      // Vendor (Interface 2)
    ITF_NUM_TOTAL
};

// Endpoint numbers
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82
#define EPNUM_VENDOR_OUT 0x03
#define EPNUM_VENDOR_IN 0x83
#define EPNUM_VENDOR_INT_IN 0x84  // Interrupt endpoint for sensor data (temperature)

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN + 7)  // +7 for interrupt endpoint

const uint8_t desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC first (IAD is automatically included by TUD_CDC_DESCRIPTOR)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CMD, 0, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // 2. Vendor Interface (수동 정의)
    // Interface Descriptor
    9, TUSB_DESC_INTERFACE, ITF_NUM_VENDOR, 0, 3, // bNumEndpoints를 3으로 설정 (Bulk 2 + Int 1)
    TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 0,    // Vendor Class, Subclass, Protocol, String Index

    // Endpoint: Bulk IN
    7, TUSB_DESC_ENDPOINT, EPNUM_VENDOR_IN, TUSB_XFER_BULK, 64, 0, 0,
    // Endpoint: Bulk OUT
    7, TUSB_DESC_ENDPOINT, EPNUM_VENDOR_OUT, TUSB_XFER_BULK, 64, 0, 0,
    // Endpoint: Interrupt IN (온도 데이터용)
    7, TUSB_DESC_ENDPOINT, EPNUM_VENDOR_INT_IN, TUSB_XFER_INTERRUPT, 8, 0, 10
};

// String Descriptor (UTF-16 LE format required by USB spec)

// Language descriptor (Index 0)
static const uint16_t _desc_str_langid[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2)),
    0x0409 // English US
};

// Manufacturer string descriptor (Index 1)
static const uint16_t _desc_str_manufacturer[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * 7)),
    'T', 'e', 's', 't', 'M', 'f', 'g', 0};

// Product string descriptor (Index 2)
static const uint16_t _desc_str_product[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * 15)),
    'P', 'i', 'c', 'o', ' ', 'U', 'S', 'B', ' ', 'D', 'e', 'v', 'i', 'c', 'e', 0};

// Serial number string descriptor (Index 3)
static const uint16_t _desc_str_serial[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * 6)),
    '1', '2', '3', '4', '5', '6', 0};

static const uint16_t *_desc_string_table[] = {
    _desc_str_langid,
    _desc_str_manufacturer,
    _desc_str_product,
    _desc_str_serial};

// ============== USB Callback Functions ==============

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    // Index 0 always returns language descriptor
    if (index == 0)
    {
        return _desc_str_langid;
    }

    // Return string descriptor for valid indices
    if (index < sizeof(_desc_string_table) / sizeof(_desc_string_table[0]))
    {
        return _desc_string_table[index];
    }

    return NULL;
}

// Vendor class callback
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;

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

void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize)
{
    // Read from USB buffer
    uint8_t tmp[64];
    uint16_t len = tud_vendor_n_read(itf, tmp, bufsize);

    // ========== Model Reception State Machine ==========
    if (model_rx_state == MODEL_RX_RECEIVING)
    {
        handle_model_rx_data(itf, tmp, len);
        tud_vendor_n_read(itf, NULL, 0);
        return;
    }

    // ========== Image Reception State Machine ==========
    if (image_rx_state == IMAGE_RX_RECEIVING)
    {
        handle_image_rx_data(itf, tmp, len);
        tud_vendor_n_read(itf, NULL, 0);
        return;
    }

    // ========== Command Processing (only when idle) ==========
    uint8_t command = tmp[0];

    if (command == CMD_MODEL_LOAD)
    {
        handle_model_load_cmd(itf, tmp, len);
    }
    else if (command == CMD_IMAGE_DATA)
    {
        image_rx_state = IMAGE_RX_RECEIVING; // Enter receiving state

        // command 제외 복사
        memcpy(image_buffer, &tmp[1], 63);
        image_rx_offset += 63;
        printf("[USB] Image chunk: %u bytes (total: %u/%u)\n", 63, image_rx_offset, IMAGE_SIZE);
        fflush(stdout);
        // handle_image_rx_data(itf, tmp, len);  // Process first packet
    }
    // Handle temperature read command
    else if (command == TEMP_CMD_READ)
    {
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
    else if (command <= 0x03)
    { // Valid LED commands: 0x00, 0x01, 0x02, 0x03
        pending_command = command;
    }

    // Re-arm the OUT endpoint to receive next transfer
    // This is critical: without this, the callback will only be called once
    tud_vendor_n_read(itf, NULL, 0);
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
void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes)
{
    (void)itf;        // Interface index not used
    (void)sent_bytes; // Sent bytes not used in current implementation

    // TODO: Implement if multi-packet responses or status tracking is needed
}

/* ============================================================================
 * CDC Callbacks
 * ============================================================================ */

// CDC Line Coding Callback (Host->Device)
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    (void)itf;
    // Handle line coding changes if needed (baud rate, data bits, stop bits, parity)
    // Currently not used in this implementation
}

// CDC Control Callback (Host->Device)
bool tud_cdc_control_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)dtr; // Data Terminal Ready
    (void)rts; // Request To Send
    // Handle DTR/RTS state changes if needed
    return true;
}

// CDC RX Callback - received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    // CDC data is available via tud_cdc_n_read()
    // Handle CDC data reception here if needed
}
