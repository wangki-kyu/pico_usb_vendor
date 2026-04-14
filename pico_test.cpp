#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "ws2812.pio.h"
#include "device/usbd_pvt.h"
#include "device/usbd.h"

/* TFLite Micro Inference */
#include "micro-sdk/edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "micro-sdk/edge-impulse-sdk/classifier/ei_classifier_types.h"
#include "micro-sdk/tflite-model/tflite_learn_958324_7.h"
#include "micro-sdk/edge-impulse-sdk/classifier/ei_run_classifier_c.h"

/* TFLite Micro API for direct inference */
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/core/c/common.h"

/* C/C++ compatibility for LED functions */
extern "C"
{
    void led_on(void);
    void led_off(void);
    void led_toggle(void);
    bool led_get_state(void);
    void led_on_with_color(uint32_t color);
}

/* ============================================================================
 * LED Control Configuration
 * ============================================================================ */

// LED pin definition - Waveshare RP2040 Zero uses GPIO16 for ARGB LED
#define LED_PIN 16
#define IS_RGBW false

// LED Color Definitions (WS2812B RGB format)
#define LED_COLOR_RED 0xFF0000    // 빨강
#define LED_COLOR_ORANGE 0xFFA500 // 주황
#define LED_COLOR_YELLOW 0xFFFF00 // 노랑
#define LED_COLOR_GREEN 0x00FF00  // 초록
#define LED_COLOR_BLUE 0x0000FF   // 파랑
#define LED_COLOR_INDIGO 0x4B0082 // 남색
#define LED_COLOR_VIOLET 0x800080 // 보라
#define LED_COLOR_WHITE 0xFFFFFF  // 흰색
#define LED_COLOR_OFF 0x000000    // 검정 (OFF)

// LED state tracker - maintains current LED status for status queries
static volatile bool led_state = false;
static PIO pio = pio0;
static uint sm = 0;

// USB command queue
volatile uint8_t pending_command = 0xFF; // 0xFF = no command pending

// NPU model ready flag
volatile bool model_ready = false;

// NPU image ready flag
volatile bool image_ready = false;

/* ============================================================================
 * TFLite Micro Inference Engine
 * ============================================================================ */

extern uint8_t model_buffer[]; // Model buffer from usb_descriptors.c

// TFLite Micro Interpreter and resolver
tflite::MicroInterpreter *g_interpreter = nullptr;
tflite::AllOpsResolver *g_resolver = nullptr;

// Tensor arena for model inference
#define TENSOR_ARENA_SIZE 150 * 1024 // 256KB tensor arena
alignas(16) static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

// Inference output buffer for [8x8] float32 probabilities
static float inference_output[8 * 8];

// Image buffer for single image processing (64x64x1 int8)
typedef struct
{
    int8_t data[64 * 64];
} image_buffer_t;

// Working buffer for float conversion
static float image_float_buffer[64 * 64];

static ei_impulse_result_t inference_result = {};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/**
 * Initialize TFLite Micro Interpreter
 * Creates and configures the interpreter for model inference
 * @return true if initialization successful, false otherwise
 */
bool initializeInterpreter(void)
{
    // led_on_with_color(LED_COLOR_RED); // Step 1: Started initialization
    // sleep_ms(200);

    static tflite::AllOpsResolver resolver;
    // led_on_with_color(LED_COLOR_ORANGE); // Step 2: Resolver created
    // sleep_ms(200);

    static tflite::MicroInterpreter interpreter(
        tflite::GetModel(tflite_learn_958324_7),
        resolver,
        tensor_arena,
        TENSOR_ARENA_SIZE);
    // led_on_with_color(LED_COLOR_YELLOW); // Step 3: Interpreter created
    // sleep_ms(200);

    g_interpreter = &interpreter;
    g_resolver = &resolver;
    // led_on_with_color(LED_COLOR_GREEN); // Step 4: Pointers assigned
    // sleep_ms(200);

    // Allocate tensors
    if (g_interpreter->AllocateTensors(true) != kTfLiteOk)
    {
        fprintf(stderr, "Error: AllocateTensors failed\n");
        led_on_with_color(LED_COLOR_RED); // Error: AllocateTensors failed
        sleep_ms(500);
        led_off();
        return false;
    }

    // led_on_with_color(LED_COLOR_BLUE); // Step 5: AllocateTensors successful
    // sleep_ms(200);
    // led_off();

    printf("Interpreter initialized successfully\n");
    return true;
}

/**
 * Run TFLite Micro inference on preprocessed INT8 input
 * Input: INT8 array [1, 64, 64, 1] (preprocessed grayscale image)
 * Output: [1, 8, 8, 2] dequantized to float32 probabilities
 * @param input_data Preprocessed INT8 input array pointer (64x64 pixels)
 * @param output_data Output buffer pointer for float32 probabilities [8x8]
 * @return true if inference successful, false otherwise
 */
extern "C"
{
    bool runInference(const int8_t *input_data, float *output_data)
    {
        printf("[INFERENCE] Starting inference...\n");
        fflush(stdout);

        // Step 1: Validate interpreter
        if (!g_interpreter)
        {
            printf("[INFERENCE] ERROR: Interpreter not initialized\n");
            fflush(stdout);
            return false;
        }

        // Step 2: Validate input/output pointers
        if (!input_data || !output_data)
        {
            printf("[INFERENCE] ERROR: Invalid input or output pointer\n");
            fflush(stdout);
            return false;
        }

        // Step 3: Get input tensor
        TfLiteTensor *input_tensor = g_interpreter->input(0);
        if (!input_tensor)
        {
            printf("[INFERENCE] ERROR: Cannot get input tensor\n");
            fflush(stdout);
            return false;
        }

        // Step 4: Copy input data to tensor
        memcpy(input_tensor->data.int8, input_data, 64 * 64 * sizeof(int8_t));
        printf("[INFERENCE] Input data copied (4096 bytes)\n");
        fflush(stdout);

        // Step 5: Run inference
        TfLiteStatus invoke_status = g_interpreter->Invoke();
        if (invoke_status != kTfLiteOk)
        {
            printf("[INFERENCE] ERROR: Invoke failed (status=%d)\n", invoke_status);
            fflush(stdout);
            return false;
        }
        printf("[INFERENCE] Model invoked successfully\n");
        fflush(stdout);

        // Step 6: Get output tensor
        TfLiteTensor *output_tensor = g_interpreter->output(0);
        if (!output_tensor)
        {
            printf("[INFERENCE] ERROR: Cannot get output tensor\n");
            fflush(stdout);
            return false;
        }

        // Step 7: Dequantize INT8 output to float32
        // Formula: float = (int8 + 128) / 256
        int8_t *tensor_output = output_tensor->data.int8;
        printf("[INFERENCE] Processing 64 output values (8x8 grid)...\n");
        fflush(stdout);

        for (int i = 0; i < 8 * 8; i++)
        {
            int8_t raw = tensor_output[i * 2 + 1]; // face class (index 1)
            output_data[i] = (static_cast<float>(raw) + 128.0f) / 256.0f;
        }

        printf("[INFERENCE] SUCCESS: [1,8,8,2] output dequantized to [8,8] float\n");
        fflush(stdout);
        return true;
    }
}

/**
 * Callback function for ei_run_classifier to get image data
 * Converts int8 image data to float for inference
 */
static int get_image_data(size_t offset, size_t length, float *out_ptr)
{
    // Read from pre-converted float buffer
    if (offset + length > 64 * 64)
    {
        return EIDSP_OUT_OF_MEM;
    }
    memcpy(out_ptr, image_float_buffer + offset, length * sizeof(float));
    return EIDSP_OK;
}

/**
 * Send TFLite inference output to host via USB
 * Sends 8x8 float probabilities as uint8 values (0-255 scale)
 * @param output Float array [8x8] with probability values (0.0-1.0)
 */
extern "C"
{
    void send_tflite_inference_results(const float *output)
    {
        uint8_t response[64] = {0};
        // memset(response, 0, 64);
        for (int i = 0; i < 64; i++)
        {
            response[i] = (uint8_t)(output[i] * 255.0f);
        }

        if (!usbd_edpt_busy(0, 0x83))
        {
            usbd_edpt_claim(0, 0x83);
            usbd_edpt_xfer(0, 0x83, response, 64);
        }

        // tud_vendor_n_write(0, response, 64);
        // tud_vendor_n_write_flush(0);
        // printf("[USB] TFLite inference results sent\n");
    }

    void send_tflite_inference_results_test(const float *output)
    {
        uint8_t response[64] = {
            0,
        };
        memset(response, 3, 64);
        // for (int i = 0; i < 64; i++)
        // {
        //     response[i] = (uint8_t)(output[i] * 255.0f);
        // }
        tud_vendor_n_write(0, response, 64);
        tud_vendor_n_write_flush(0);
        // printf("[USB] TFLite inference results sent\n");
    }
}

/**
 * Send inference results (bounding boxes) to host via USB
 */
void send_inference_results(void)
{
    // Protocol: [0x22][count][box1_data][box2_data]...
    // Each box: [x][y][w][h][confidence_int]

    uint8_t response[64]; // Response buffer
    uint16_t idx = 0;

    response[idx++] = 0x22; // Response code
    response[idx++] = inference_result.bounding_boxes_count;

    // Iterate through all detected bounding boxes
    for (uint32_t i = 0; i < inference_result.bounding_boxes_count; i++)
    {
        ei_impulse_result_bounding_box_t box = inference_result.bounding_boxes[i];

        // Pack box data: [x_int][y_int][w_int][h_int][conf_int]
        // Convert float coordinates to int8 (0-255 scale for 64x64 image)
        uint8_t x = (uint8_t)(box.x * 255 / 64);
        uint8_t y = (uint8_t)(box.y * 255 / 64);
        uint8_t w = (uint8_t)(box.width * 255 / 64);
        uint8_t h = (uint8_t)(box.height * 255 / 64);
        uint8_t conf = (uint8_t)(box.value * 255); // confidence 0.0-1.0 → 0-255

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
void run_tflite_inference(volatile int8_t *image_data)
{
    printf("[INFERENCE] Starting inference...\n");

    // Step 1: Convert int8 image data to RGB888-packed float
    // Edge Impulse SDK의 extract_image_features()는 get_data 콜백의 float 값을
    // uint32_t로 재해석하여 R/G/B 채널을 파싱함.
    // → 각 픽셀을 (gray << 16) | (gray << 8) | gray 형태로 패킹 후 float 메모리로 저장.
    for (int i = 0; i < 64 * 64; i++)
    {
        uint8_t gray = (uint8_t)((int16_t)image_data[i] + 128); // int8 → uint8(0~255)
        uint32_t rgb = ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | (uint32_t)gray;
        memcpy(&image_float_buffer[i], &rgb, sizeof(float));
    }
    printf("[INFERENCE] Image converted to float\n");

    // Step 2: Create signal_t structure for ei_run_classifier
    signal_t signal;
    signal.total_length = 64 * 64;
    signal.get_data = &get_image_data;

    // Step 3: Run inference
    printf("[INFERENCE] Running classifier...\n");
    EI_IMPULSE_ERROR err = run_classifier(&signal, &inference_result, false);
    printf("[INFERENCE] Classifier result: %d\n", (int)err);

    if (err != EI_IMPULSE_OK)
    {
        // Handle inference error - return empty results
        printf("[INFERENCE] Error occurred, clearing results\n");
        inference_result.bounding_boxes_count = 0;
    }
    else
    {
        printf("[INFERENCE] Detection count: %d\n", inference_result.bounding_boxes_count);
    }

    // Step 4: Toggle LED to indicate inference completion
    led_toggle();

    // Step 5: Send results back to host
    send_inference_results();
}

/* ============================================================================
 * External Image Buffer (from usb_descriptors.c)
 * ============================================================================ */

extern volatile int8_t *image_data_ptr; // Image buffer from usb_descriptors.c

/* ============================================================================
 * LED Hardware Initialization
 * ============================================================================
 *
 * Initializes GPIO pin for LED control with proper hardware configuration.
 * This function configures the GPIO direction and initial state.
 */
void led_init(void)
{
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
 * Turn on the LED with specified color
 * @param color RGB color in format (R << 16) | (G << 8) | B
 */
extern "C"
{
    void led_on_with_color(uint32_t color)
    {
        pio_sm_put_blocking(pio, sm, color);
        led_state = true;
        sleep_ms(1000);
    }
}

/**
 * Turn on the LED (red color)
 */
void led_on(void)
{
    // WS2812B uses RGB format: (R << 16) | (G << 8) | B
    // Red: (255 << 16) | (0 << 8) | 0 = 0xFF0000
    uint32_t red = (255 << 16) | (0 << 8) | 0;
    pio_sm_put_blocking(pio, sm, red);
    led_state = true;
}

/**
 * Turn off the LED
 */
void led_off(void)
{
    // Black (all off): 0x000000
    pio_sm_put_blocking(pio, sm, 0);
    led_state = false;
}

/**
 * Toggle LED state
 */
void led_toggle(void)
{
    if (led_state)
    {
        led_off();
    }
    else
    {
        led_on();
    }
}

/**
 * Get current LED state
 * @return Current LED state (true = ON, false = OFF)
 */
bool led_get_state(void)
{
    return led_state;
}

/* ============================================================================
 * Memory Usage Monitoring
 * ============================================================================
 *
 * Monitors RAM usage and logs available memory
 */

extern char __StackLimit, __bss_end__;

/**
 * Get free heap memory size
 * @return Free memory in bytes
 */
uint32_t get_free_memory(void)
{
    // Pico: Total RAM is 264KB (0x42000)
    // __bss_end__ points to end of BSS section
    // Stack grows downward from top of RAM
    extern char __StackLimit;
    extern char __bss_end__;

    uint32_t stack_limit = (uint32_t)&__StackLimit;
    uint32_t bss_end = (uint32_t)&__bss_end__;
    uint32_t heap_end = stack_limit;

    return heap_end - bss_end;
}

/**
 * Log memory usage information
 */
void log_memory_usage(void)
{
    uint32_t free_mem = get_free_memory();
    uint32_t used_mem = 264 * 1024 - free_mem;

    printf("[MEMORY] Used: %u KB, Free: %u KB, Total: 264 KB\n",
           used_mem / 1024, free_mem / 1024);
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
void adc_temp_init(void)
{
    adc_init();
    adc_set_temp_sensor_enabled(true);
}

/**
 * Read temperature from internal sensor
 * @return Temperature in Celsius (float)
 */

extern "C"
{
    float adc_get_temperature(void)
    {
        adc_select_input(4); // Select temperature sensor (ADC channel 4)
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
void usb_device_init(void)
{
    tusb_init();
}

/* ============================================================================
 * Temperature Data Transmission via USB
 * ============================================================================ */

// External endpoint number from usb_descriptors.c
#define EPNUM_VENDOR_INT_IN 0x84

/**
 * Send temperature data via Bulk endpoint (polling-based)
 * Protocol: [0xAA][temp_int][temp_frac]
 * Example: 25.5°C → [0xAA][25][50]
 */
extern "C"
{
    void send_temperature_data_bulk(float temperature)
    {
        uint8_t response[3];
        response[0] = 0xAA;                                                  // Temperature response marker
        response[1] = (uint8_t)temperature;                                  // Integer part
        response[2] = (uint8_t)((temperature - (uint8_t)temperature) * 100); // Fractional part (0-99)

        tud_vendor_n_write(0, response, 3);
        tud_vendor_n_write_flush(0);
        // printf("[TEMP] Bulk sent: %.2f°C\n", temperature);
    }

    /**
     * Send temperature data via Interrupt endpoint (periodic, low-latency)
     * Interrupt endpoint provides automatic periodic polling (10ms interval)
     * Ideal for sensor data that needs consistent, low-latency delivery
     */
    void send_temperature_data_interrupt(float temperature)
    {
        static uint8_t data[3];
        data[0] = 0xBB;                                                  // Different marker for interrupt data
        data[1] = (uint8_t)temperature;                                  // Integer part
        data[2] = (uint8_t)((temperature - (uint8_t)temperature) * 100); // Fractional part

        // Use low-level endpoint transfer API for interrupt endpoint

        if (tud_mounted())
        {
            if (!usbd_edpt_busy(0, EPNUM_VENDOR_INT_IN))
            {
                usbd_edpt_claim(0, 0x84);
                usbd_edpt_xfer(0, 0x84, data, sizeof(data));
                printf("[TEMP] Interrupt sent: %.2f°C\n", temperature);
                fflush(stdout);
            }
            else
            {
                printf("EP 0x84 is busy!\n");
                fflush(stdout);
            }
        }
        else
        {
            printf("tud_mounted not yet\n");
            fflush(stdout);            
        }
    }

    /**
     * Wrapper function - can be changed to either bulk or interrupt
     */
    void send_temperature_data(float temperature)
    {
        // Change this to send_temperature_data_bulk() if you prefer bulk endpoint
        send_temperature_data_interrupt(temperature);
    }
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
int main()
{
    printf("Hello from Pico!\n");
    // fflush(stdout);
    // Initialize LED hardware
    led_init();

    // Initialize temperature sensor (ADC Channel 4)
    adc_temp_init();

    tusb_init();
    stdio_init_all();

    led_on_with_color(LED_COLOR_OFF);

    // Initialize TFLite Micro interpreter
    if (initializeInterpreter())
    {
        sleep_ms(500);

        for (int i = 0; i < 3; i++)
        {
            // Turn LED on
            led_on();
            sleep_ms(200); // Wait 200ms with LED on

            // Turn LED off
            led_off();
            sleep_ms(200); // Wait 200ms with LED off
        }
    }

    // ============================================================================
    // LED Self-Test on Startup
    // ============================================================================
    // Perform 3 LED toggles to verify hardware operation at boot time
    // This provides visual confirmation that the LED is functioning correctly

    // LED is now OFF after self-test, ready for USB command control

    // Initialize USB device stack
    usb_device_init();

    // Log initial memory usage
    // log_memory_usage();

    // // led_on_with_color(LED_COLOR_YELLOW);    // YELLOW가 아님

    // // Main processing loop - runs indefinitely
    uint32_t last_memory_log = 0;
    uint32_t last_temp_send = 0;
    const uint32_t TEMP_SEND_INTERVAL_MS = 1000; // Send temperature every 1 second

    while (true)
    {
        // Process USB events (bulk transfers, control transfers, etc.)
        // This must be called frequently to maintain USB communication
        // printf("test\n");
        // fflush(stdout);
        tud_task();

        // Process model ready signal from USB callback
        if (model_ready)
        {
            // Model data has been received and stored in SRAM
            // Load TFLite model from buffer
            led_on();
            model_ready = false;

            // TODO: Initialize TFLite interpreter with model_buffer
            // This would be done here after model is fully received
        }

        // Process pending LED command from USB callback
        if (pending_command != 0xFF)
        {
            uint8_t cmd = pending_command;
            pending_command = 0xFF; // Clear the pending command

            switch (cmd)
            {
            case 0x00: // LED OFF
                led_off();
                break;
            case 0x01: // LED ON
                led_on();
                break;
            case 0x02: // LED TOGGLE
                led_toggle();
                break;
            default:
                break;
            }
        }

        // Send temperature data at regular intervals
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_temp_send >= TEMP_SEND_INTERVAL_MS)
        {
            float temp = adc_get_temperature();
            // printf("%f\n", temp);
            // fflush(stdout);
            send_temperature_data(temp);
            last_temp_send = now;
        }

        // Optional: Reduce CPU load if needed
        sleep_ms(1);
    }

    return 0;
}
