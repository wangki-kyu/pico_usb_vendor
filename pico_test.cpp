#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "ws2812.pio.h"

/* TFLite Micro Inference */
#include "micro-sdk/edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "micro-sdk/edge-impulse-sdk/classifier/ei_classifier_types.h"
#include "micro-sdk/tflite-model/tflite_learn_956961_19.h"
#include "micro-sdk/edge-impulse-sdk/classifier/ei_run_classifier_c.h"

/* C/C++ compatibility for LED functions */
extern "C" {
    void led_on(void);
    void led_off(void);
    void led_toggle(void);
    bool led_get_state(void);
}

/* ============================================================================
 * LED Control Configuration
 * ============================================================================ */

// LED pin definition - Waveshare RP2040 Zero uses GPIO16 for ARGB LED
#define LED_PIN 16
#define IS_RGBW false

// LED state tracker - maintains current LED status for status queries
static volatile bool led_state = false;
static PIO pio = pio0;
static uint sm = 0;

// USB command queue
volatile uint8_t pending_command = 0xFF;  // 0xFF = no command pending

// NPU model ready flag
volatile bool model_ready = false;

// NPU image ready flag
volatile bool image_ready = false;

/* ============================================================================
 * TFLite Micro Inference Engine
 * ============================================================================ */

extern uint8_t model_buffer[];  // Model buffer from usb_descriptors.c

// Image buffer for single image processing (64x64x1 int8)
typedef struct {
    int8_t data[64 * 64];
} image_buffer_t;

// Working buffer for float conversion
static float image_float_buffer[64 * 64];

static ei_impulse_result_t inference_result = {};

/**
 * Callback function for ei_run_classifier to get image data
 * Converts int8 image data to float for inference
 */
static int get_image_data(size_t offset, size_t length, float *out_ptr) {
    // Read from pre-converted float buffer
    if (offset + length > 64 * 64) {
        return EIDSP_OUT_OF_MEM;
    }
    memcpy(out_ptr, image_float_buffer + offset, length * sizeof(float));
    return EIDSP_OK;
}

/**
 * Send inference results (bounding boxes) to host via USB
 */
void send_inference_results(void) {
    // Protocol: [0x22][count][box1_data][box2_data]...
    // Each box: [x][y][w][h][confidence_int]

    uint8_t response[64];  // Response buffer
    uint16_t idx = 0;

    response[idx++] = 0x22;  // Response code
    response[idx++] = inference_result.bounding_boxes_count;

    // Iterate through all detected bounding boxes
    for (uint32_t i = 0; i < inference_result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t box = inference_result.bounding_boxes[i];

        // Pack box data: [x_int][y_int][w_int][h_int][conf_int]
        // Convert float coordinates to int8 (0-255 scale for 64x64 image)
        uint8_t x = (uint8_t)(box.x * 255 / 64);
        uint8_t y = (uint8_t)(box.y * 255 / 64);
        uint8_t w = (uint8_t)(box.width * 255 / 64);
        uint8_t h = (uint8_t)(box.height * 255 / 64);
        uint8_t conf = (uint8_t)(box.value * 255);  // confidence 0.0-1.0 → 0-255

        response[idx++] = x;
        response[idx++] = y;
        response[idx++] = w;
        response[idx++] = h;
        response[idx++] = conf;
    }

    // Send results via USB
    tud_vendor_n_write(0, response, 64);
    tud_vendor_n_write_flush(0);
}

/**
 * Run TFLite inference on the loaded model and image data
 */
void run_tflite_inference(volatile int8_t* image_data) {
    // Step 1: Convert int8 image data to float
    // Normalize int8 [-128, 127] to float [0, 1] or [-1, 1] depending on model
    // for (int i = 0; i < 64 * 64; i++) {
    //     // Convert to 0-1 range (assuming unsigned interpretation)
    //     image_float_buffer[i] = (float)(image_data[i] + 128) / 255.0f;
    // }

    // // Step 2: Create signal_t structure for ei_run_classifier
    // signal_t signal;
    // signal.total_length = 64 * 64;
    // signal.get_data = &get_image_data;

    // // Step 3: Run inference
    // EI_IMPULSE_ERROR err = ei_run_classifier(&signal, &inference_result, false);

    // if (err != EI_IMPULSE_OK) {
    //     // Handle inference error - return empty results
    //     inference_result.bounding_boxes_count = 0;
    // }

    // Step 4: Toggle LED to indicate inference completion
    led_toggle();

    // Step 5: Send results back to host
    send_inference_results();
}

/* ============================================================================
 * External Image Buffer (from usb_descriptors.c)
 * ============================================================================ */

extern volatile int8_t* image_data_ptr;  // Image buffer from usb_descriptors.c

/* ============================================================================
 * LED Hardware Initialization
 * ============================================================================
 *
 * Initializes GPIO pin for LED control with proper hardware configuration.
 * This function configures the GPIO direction and initial state.
 */
void led_init(void) {
    // Initialize WS2812B LED using PIO
    uint offset = pio_add_program(pio, &ws2812_program);

    // Initialize state machine
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, IS_RGBW);

    // Initialize LED to OFF state (black)
    pio_sm_put_blocking(pio, sm, 0);
    led_state = false;
}

/* ============================================================================
 * LED Control Functions
 * ============================================================================ */

/**
 * Turn on the LED (red color)
 */
void led_on(void) {
    // WS2812B uses RGB format: (R << 16) | (G << 8) | B
    // Red: (255 << 16) | (0 << 8) | 0 = 0xFF0000
    uint32_t red = (255 << 16) | (0 << 8) | 0;
    pio_sm_put_blocking(pio, sm, red);
    led_state = true;
}

/**
 * Turn off the LED
 */
void led_off(void) {
    // Black (all off): 0x000000
    pio_sm_put_blocking(pio, sm, 0);
    led_state = false;
}

/**
 * Toggle LED state
 */
void led_toggle(void) {
    if (led_state) {
        led_off();
    } else {
        led_on();
    }
}

/**
 * Get current LED state
 * @return Current LED state (true = ON, false = OFF)
 */
bool led_get_state(void) {
    return led_state;
}

/* ============================================================================
 * Temperature Sensor (ADC Channel 4)
 * ============================================================================
 *
 * RP2040 internal temperature sensor on ADC Channel 4
 * Conversion: Raw ADC value -> Temperature in Celsius
 */

/**
 * Initialize ADC for temperature sensor
 */
void adc_temp_init(void) {
    adc_init();
    adc_set_temp_sensor_enabled(true);
}

/**
 * Read temperature from internal sensor
 * @return Temperature in Celsius (float)
 */

extern "C" {
    float adc_get_temperature(void) {
        adc_select_input(4);  // Select temperature sensor (ADC channel 4)
        uint16_t raw = adc_read();

        // RP2040 temperature sensor conversion formula
        // Voltage = (raw / 4095) * 3.3V
        // Temperature = 27 - (V - 0.706) / 0.001721
        float voltage = (raw / 4095.0f) * 3.3f;
        float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;

        return temperature;
    }
}

/* ============================================================================
 * USB Device Initialization
 * ============================================================================
 *
 * Initializes TinyUSB stack for device mode operation.
 * This enables the Pico to enumerate as a USB device and handle
 * vendor-specific bulk transfers.
 */
void usb_device_init(void) {
    tusb_init();
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================
 *
 * Initializes hardware and runs the main processing loop.
 * The loop continuously:
 * 1. Processes USB tasks (critical for handling bulk transfers)
 * 2. Maintains LED state (LED operations must occur within main context)
 */
int main() {
    // Initialize LED hardware
    led_init();

    // Initialize temperature sensor (ADC Channel 4)
    adc_temp_init();

    // ============================================================================
    // LED Self-Test on Startup
    // ============================================================================
    // Perform 3 LED toggles to verify hardware operation at boot time
    // This provides visual confirmation that the LED is functioning correctly

    sleep_ms(500);

    for (int i = 0; i < 3; i++) {
        // Turn LED on
        led_on();
        sleep_ms(200);  // Wait 200ms with LED on

        // Turn LED off
        led_off();
        sleep_ms(200);  // Wait 200ms with LED off
    }

    // LED is now OFF after self-test, ready for USB command control

    // Initialize USB device stack
    usb_device_init();

    // Main processing loop - runs indefinitely
    while (true) {
        // Process USB events (bulk transfers, control transfers, etc.)
        // This must be called frequently to maintain USB communication
        tud_task();

        // Process model ready signal from USB callback
        if (model_ready) {
            // Model data has been received and stored in SRAM
            // Load TFLite model from buffer
            led_on();
            model_ready = false;

            // TODO: Initialize TFLite interpreter with model_buffer
            // This would be done here after model is fully received
        }

        // Process image data and run inference
        if (image_ready) {
            // Image data received and ready for inference
            run_tflite_inference(image_data_ptr);
            image_ready = false;
        }

        // Process pending LED command from USB callback
        if (pending_command != 0xFF) {
            uint8_t cmd = pending_command;
            pending_command = 0xFF;  // Clear the pending command

            switch (cmd) {
                case 0x00:  // LED OFF
                    led_off();
                    break;
                case 0x01:  // LED ON
                    led_on();
                    break;
                case 0x02:  // LED TOGGLE
                    led_toggle();
                    break;
                default:
                    break;
            }
        }

        // Optional: Reduce CPU load if needed
        sleep_ms(1);
    }

    return 0;
}
