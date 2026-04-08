#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

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
