#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"

// USB Device 초기화
void usb_device_init(void) {
    tusb_init();
}

int main() {
    usb_device_init();

    while (true) {
        tud_task(); // USB 작업 처리 (매우 중요!)
        sleep_ms(1);
    }
}
