# Pico LED Control & Temperature Sensor - Extended USB Interface Specification

## Document Overview

This document defines the extended USB interface protocol between the Pico firmware and the KMDF host driver/application for LED control and temperature sensor reading via bulk transfer.

**Note:** This is an extended version including temperature sensor functionality. Legacy LED-only commands (0x00-0x03) remain fully compatible.

---

## 1. USB Device Information

### 1.1 Basic Device Properties

| Property | Value | Description |
|----------|-------|-------------|
| Vendor ID | 0xCAFE | Vendor identifier |
| Product ID | 0x4005 | Product identifier |
| Device Class | 0x00 (Vendor) | Vendor-defined class |
| Device Version | 0x0100 | Device firmware version |

### 1.2 String Descriptors

| Index | Content |
|-------|---------|
| 1 | "TestMfg" (Manufacturer) |
| 2 | "Pico USB Device" (Product) |
| 3 | "123456" (Serial Number) |

---

## 2. USB Endpoints

### 2.1 Endpoint Configuration

| Endpoint | Direction | Type | Max Packet Size | Purpose |
|----------|-----------|------|-----------------|---------|
| 0x01 | OUT | Bulk | 64 bytes | Command reception from host |
| 0x81 | IN | Bulk | 64 bytes | Response transmission to host |

### 2.2 Transfer Type

- **Bulk Transfer** for reliable command/response communication
- Asynchronous operation (non-isochronous)
- Suitable for device control and sensor reading without strict timing requirements

---

## 3. Extended LED Control Protocol

### 3.1 LED Command Format

**Structure:** Single byte command (0x00 - 0x0F reserved for LED)

```
┌─────────────────────┐
│   Command Byte      │
│  (0x00 ~ 0x03)     │
└─────────────────────┘
```

| Command | Hex | Name | Description |
|---------|-----|------|-------------|
| LED_OFF | 0x00 | Turn off the LED | LED turns OFF |
| LED_ON | 0x01 | Turn on the LED | LED turns ON |
| LED_TOGGLE | 0x02 | Toggle LED state | Toggle between ON and OFF |
| LED_STATUS | 0x03 | Query current LED state | Returns 0x00 (OFF) or 0x01 (ON) |

### 3.2 LED Response Format

**Structure:** Single byte response

```
┌─────────────────────┐
│   Response Byte     │
│  (0x00/0x01/0xFF)  │
└─────────────────────┘
```

| Response | Hex | Meaning |
|----------|-----|---------|
| OK / LED_OFF | 0x00 | Command succeeded OR LED is currently OFF |
| LED_ON | 0x01 | LED is currently ON (for LED_STATUS query) |
| INVALID | 0xFF | Unknown command received |

### 3.3 LED Command-Response Mapping

| Command | Expected Response | LED Result |
|---------|-------------------|------------|
| LED_OFF (0x00) | OK (0x00) | LED turns OFF |
| LED_ON (0x01) | OK (0x00) | LED turns ON |
| LED_TOGGLE (0x02) | OK (0x00) | LED state inverts |
| LED_STATUS (0x03) | 0x00 (OFF) or 0x01 (ON) | No change (query only) |

---

## 4. Temperature Sensor Protocol (NEW)

### 4.1 Onboard Temperature Sensor Specification

| Property | Value | Description |
|----------|-------|-------------|
| Sensor Type | ADC Channel 4 | Internal temperature sensor |
| RP2040 ADC | 12-bit | 0-4095 raw value range |
| Temperature Range | -20°C to +85°C | Typical operating range |
| Accuracy | ±4°C | Typical accuracy |
| Sampling Method | Raw ADC → Temperature conversion | Hardware-based reading |

### 4.2 Temperature Command Format

**Structure:** Single byte command (0x10 reserved for temperature)

```
┌──────────────────────────┐
│   Command Byte: 0x10     │
│   (TEMP_READ command)    │
└──────────────────────────┘
```

| Command | Hex | Name | Description |
|---------|-----|------|-------------|
| TEMP_READ | 0x10 | Read Temperature | Read internal temperature sensor |

### 4.3 Temperature Response Format

**Structure:** Multi-byte response with temperature data

**Option A: Fixed-point format (Recommended for precision)**

```
┌──────────────────────────────────┐
│ Byte 0: Integer Part (0-85)      │
│ Byte 1: Decimal Part (0-99)      │
│         Represents hundredths    │
│         of degree (0.01°C)       │
├──────────────────────────────────┤
│ Total: 2 bytes                   │
│ Range: 0.00°C to 85.99°C         │
└──────────────────────────────────┘
```

**Example responses:**
```
Request:  0x10 (TEMP_READ)
Response: 0x19 0x2E = 25.46°C
Response: 0x0F 0x00 = 15.00°C
Response: 0x23 0x50 = 35.80°C (where 50 in decimal is 0x32)
```

**Option B: Integer-only format (Simplified)**

```
┌──────────────────────────────────┐
│ Byte 0: Temperature (°C)         │
│         0-85 (integer only)      │
├──────────────────────────────────┤
│ Total: 1 byte                    │
│ Range: 0°C to 85°C               │
│ Resolution: 1°C                  │
└──────────────────────────────────┘
```

**Example responses:**
```
Request:  0x10 (TEMP_READ)
Response: 0x19 = 25°C
Response: 0x0F = 15°C
Response: 0x23 = 35°C
```

### 4.4 Temperature Response Mapping

| Command | Response Format | Meaning |
|---------|-----------------|---------|
| TEMP_READ (0x10) | 2 bytes: [Integer, Decimal] | Temperature value (Option A) |
| TEMP_READ (0x10) | 1 byte: [Integer] | Temperature value (Option B) |
| Invalid Command | 0xFF | Unknown command received |

---

## 5. Communication Sequence

### 5.1 LED Control Sequence (Existing)

```
Host (Driver/App)                     Pico Device
       │                                   │
       │─── Bulk OUT: 0x01 (LED ON) ──→ │
       │                                   │
       │                                   │ Process command
       │                                   │ led_on()
       │                                   │
       │ ← Bulk IN: 0x00 (OK) ───────────│
       │                                   │
```

### 5.2 Temperature Query Sequence (NEW)

```
Host (Driver/App)                     Pico Device
       │                                   │
       │─── Bulk OUT: 0x10 (TEMP_READ)─→ │
       │                                   │
       │                                   │ Read ADC Channel 4
       │                                   │ Convert to temperature
       │                                   │ Prepare response
       │                                   │
       │ ← Bulk IN: [Temp Data] ────────│
       │   (2 bytes: int.decimal)       │
       │   or (1 byte: integer)         │
       │                                   │
```

### 5.3 LED Status Query Sequence

```
Host (Driver/App)                     Pico Device
       │                                   │
       │─── Bulk OUT: 0x03 (LED_STATUS)──→ │
       │                                   │
       │                                   │ Get LED state
       │                                   │
       │ ← Bulk IN: [0x00 or 0x01] ──────│
       │                                   │
```

---

## 6. Implementation Requirements

### 6.1 Host Driver Requirements (KMDF)

#### For LED Control
1. Send 1-byte command (0x00-0x03)
2. Receive 1-byte response (0x00, 0x01, or 0xFF)
3. Parse response and handle accordingly

#### For Temperature Reading (NEW)
1. Send 0x10 (TEMP_READ command)
2. **Receive 2 bytes** (if Option A) or **1 byte** (if Option B)
3. Parse temperature value
4. Convert to display format

#### Common Requirements
- Endpoint initialization (OUT: 0x01, IN: 0x81)
- Timeout handling (recommended: 1000ms per transfer)
- Error handling (check for 0xFF response)
- Retry logic (max 3 attempts)

### 6.2 Device Firmware (Pico)

**Existing (LED Control):**
- ✓ Receives data on endpoint 0x01
- ✓ Processes LED commands (0x00-0x03)
- ✓ Controls GPIO 25
- ✓ Sends response on endpoint 0x81

**New (Temperature Sensor):**
- [ ] Initialize ADC Channel 4 (temperature sensor)
- [ ] Add temperature read command handler (0x10)
- [ ] Convert ADC raw value to temperature
- [ ] Apply calibration if needed
- [ ] Send temperature response (2 or 1 byte)

---

## 7. Example Usage Scenarios

### 7.1 Turn LED ON

```c
// KMDF Driver / Application
uint8_t cmd = 0x01;  // LED_ON command
uint8_t response;

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    printf("LED turned ON\n");
} else if (response == 0xFF) {
    printf("Error: Invalid command\n");
}
```

### 7.2 Read Temperature (Option A: Decimal Format)

```c
// KMDF Driver / Application
uint8_t cmd = 0x10;  // TEMP_READ command
uint8_t response[2];

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, response, 2);  // Read 2 bytes

uint8_t temp_int = response[0];
uint8_t temp_dec = response[1];

printf("Temperature: %d.%02d°C\n", temp_int, temp_dec);
// Example output: Temperature: 25.46°C
```

### 7.3 Read Temperature (Option B: Integer Format)

```c
// KMDF Driver / Application
uint8_t cmd = 0x10;  // TEMP_READ command
uint8_t response;

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, &response, 1);  // Read 1 byte

printf("Temperature: %d°C\n", response);
// Example output: Temperature: 25°C
```

### 7.4 Query LED Status

```c
// KMDF Driver / Application
uint8_t cmd = 0x03;  // LED_STATUS command
uint8_t response;

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    printf("LED is OFF\n");
} else if (response == 0x01) {
    printf("LED is ON\n");
}
```

### 7.5 Toggle LED

```c
// KMDF Driver / Application
uint8_t cmd = 0x02;  // LED_TOGGLE command
uint8_t response;

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    printf("LED toggled\n");
}
```

---

## 8. Complete Command Reference

### 8.1 All Supported Commands

| Hex | Name | Input | Output | Description |
|-----|------|-------|--------|-------------|
| 0x00 | LED_OFF | 1 byte (0x00) | 0x00 / 0xFF | Turn off LED |
| 0x01 | LED_ON | 1 byte (0x01) | 0x00 / 0xFF | Turn on LED |
| 0x02 | LED_TOGGLE | 1 byte (0x02) | 0x00 / 0xFF | Toggle LED |
| 0x03 | LED_STATUS | 1 byte (0x03) | 0x00 or 0x01 | Get LED state |
| 0x10 | TEMP_READ | 1 byte (0x10) | 2 bytes [int, dec] | Read temperature |
| Others | RESERVED | - | 0xFF | Reserved for future use |

### 8.2 Response Summary

| Response | Hex | Used By | Meaning |
|----------|-----|---------|---------|
| OK | 0x00 | LED_OFF, LED_ON, LED_TOGGLE | Command executed successfully |
| LED_OFF_STATE | 0x00 | LED_STATUS | LED is currently OFF |
| LED_ON_STATE | 0x01 | LED_STATUS | LED is currently ON |
| TEMP_INTEGER | 0-85 | TEMP_READ | Temperature integer part |
| TEMP_DECIMAL | 0-99 | TEMP_READ | Temperature decimal part (0.01°C) |
| INVALID_CMD | 0xFF | All | Unknown or invalid command |

---

## 9. Electrical & Sensor Characteristics

### 9.1 LED Pin (GPIO 25)

| Property | Value | Notes |
|----------|-------|-------|
| GPIO Pin | 25 | Pico onboard LED |
| Logic Level | 3.3V (HIGH) | LED turns ON |
| Logic Level | 0V (LOW) | LED turns OFF |
| LED Color | Green | Standard indicator |
| Current Limiting | Internal resistor | Safe for Pico GPIO |

### 9.2 Temperature Sensor (ADC Channel 4)

| Property | Value | Notes |
|----------|-------|-------|
| ADC Channel | 4 | Internal temperature sensor |
| Sensing Method | Thermal diode | RP2040 internal |
| Temperature Range | -20°C to +85°C | Typical |
| Accuracy | ±4°C | Typical variance |
| Update Rate | Real-time sampling | On-demand reading |
| ADC Resolution | 12-bit | 4096 levels |
| Conversion Formula | See Section 10 | Calibration details |

---

## 10. Temperature Sensor Technical Details

### 10.1 ADC to Temperature Conversion

**RP2040 Temperature Sensor Conversion:**

The Pico SDK provides built-in temperature reading functions:

```c
// Pico SDK function (firmware implementation)
float adc_read_temperature(void);
```

**Calibration Constants (RP2040 specific):**
- Reference voltage: 3.3V
- ADC resolution: 12-bit (0-4095)
- Temperature coefficient: Approximately 0.5 ADC units per °C

**Simplified conversion:**
```
Raw ADC Value → 12-bit ADC reading
                 ↓
            Apply calibration
                 ↓
            Temperature (°C)
```

### 10.2 Implementation Notes

1. **Sampling:** ADC Channel 4 can be sampled on-demand
2. **Averaging:** Optional filtering for noise reduction (not required)
3. **Frequency:** No minimum time between reads
4. **Power:** Temperature sensor always available when USB is powered
5. **Accuracy:** Accept ±4°C variance in ambient temperature readings

---

## 11. Timing and Constraints

### 11.1 Transfer Timing

| Item | Value | Description |
|------|-------|-------------|
| Max Packet Size | 64 bytes | Single bulk transfer limit |
| LED Command Size | 1 byte | Single byte |
| LED Response Size | 1 byte | Single byte |
| TEMP Command Size | 1 byte | Single byte (0x10) |
| TEMP Response Size | 2 bytes (Option A) or 1 byte (Option B) | Temperature data |
| Processing Time (LED) | < 1ms | Immediate GPIO operation |
| Processing Time (TEMP) | < 50ms | ADC sampling time |
| Bulk Transfer Speed | Full Speed (12 Mbps) | USB 2.0 Full Speed |

### 11.2 Retry Behavior

- If response timeout occurs: Retry transfer
- Max retries: 3 attempts (configurable)
- Wait time between retries: 100ms (configurable)
- Recommended timeout per transfer: 1000ms

---

## 12. Protocol Backward Compatibility

### 12.1 Compatibility Guarantees

- **LED commands (0x00-0x03):** Fully backward compatible
- **Response format:** 1-byte responses for LED commands unchanged
- **New commands (0x10+):** Do not affect existing LED functionality
- **Graceful degradation:** Older drivers can ignore TEMP_READ (0x10)

### 12.2 Version Negotiation (Future Enhancement)

For future protocol versions, consider adding:
```c
0xFE = GET_PROTOCOL_VERSION
Response: [Major, Minor] (e.g., 0x01 0x00 for v1.0)
```

---

## 13. Known Limitations & Notes

1. **Single Command per transfer** - Only 1 command byte expected
2. **No Batching** - Cannot send multiple commands in one packet
3. **Asynchronous Response** - Response sent via separate bulk IN transfer
4. **Temperature Accuracy** - ±4°C inherent accuracy (RP2040 spec)
5. **Device Enumeration** - May require 1-2 seconds to appear after connection
6. **ADC Sampling** - Temperature read reflects current chip temperature
7. **No Real-time Guarantee** - Bulk transfers are not isochronous
8. **Single Response Buffer** - Response overwritten on next command

---

## 14. Debugging & Troubleshooting

### 14.1 Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| No device enumeration | Device not powered/connected | Check USB cable, Pico power |
| 0xFF response always | Invalid command sent | Verify command byte (0x00-0x03, 0x10) |
| Response timeout | Device not responding | Check firmware build/upload |
| LED not responding | Endpoint not properly configured | Verify endpoint addresses |
| Temperature always 0 | ADC not initialized | Check firmware initialization |
| Temperature fluctuates | Normal ADC noise | Apply averaging filter on host |

### 14.2 Testing Checklist

- [ ] Device enumerates correctly (VID: 0xCAFE, PID: 0x4005)
- [ ] Bulk OUT endpoint 0x01 accepts data
- [ ] Bulk IN endpoint 0x81 returns response
- [ ] LED_OFF (0x00) turns LED off
- [ ] LED_ON (0x01) turns LED on
- [ ] LED_TOGGLE (0x02) inverts LED state
- [ ] LED_STATUS (0x03) returns correct state (0x00 or 0x01)
- [ ] TEMP_READ (0x10) returns 2-byte temperature (or 1-byte, Option B)
- [ ] Temperature values are within expected range (-20°C to +85°C)
- [ ] Invalid commands (0xFF, etc.) return 0xFF error response
- [ ] Multiple consecutive reads work correctly
- [ ] LED and temperature commands can be mixed

---

## 15. Example Configuration Structure (C Header for Driver)

```c
// Pico LED Control & Temperature USB Interface
// usb_interface_config.h

#ifndef USB_INTERFACE_CONFIG_H
#define USB_INTERFACE_CONFIG_H

#include <stdint.h>

// USB Device Identifiers
#define USB_VENDOR_ID           0xCAFE
#define USB_PRODUCT_ID          0x4005

// USB Endpoints
#define USB_ENDPOINT_OUT        0x01
#define USB_ENDPOINT_IN         0x81

// LED Commands
#define LED_CMD_OFF             0x00
#define LED_CMD_ON              0x01
#define LED_CMD_TOGGLE          0x02
#define LED_CMD_STATUS          0x03

// Temperature Commands
#define TEMP_CMD_READ           0x10

// Response Codes
#define RESP_OK                 0x00
#define RESP_LED_OFF            0x00
#define RESP_LED_ON             0x01
#define RESP_INVALID            0xFF

// Packet Sizes
#define LED_COMMAND_SIZE        1
#define LED_RESPONSE_SIZE       1
#define TEMP_COMMAND_SIZE       1
#define TEMP_RESPONSE_SIZE_OPT_A 2  // [integer, decimal]
#define TEMP_RESPONSE_SIZE_OPT_B 1  // [integer only]

// Transfer Timing
#define USB_TRANSFER_TIMEOUT_MS 1000
#define USB_RETRY_MAX           3
#define USB_RETRY_DELAY_MS      100

#endif // USB_INTERFACE_CONFIG_H
```

---

## 16. Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-04-08 | Initial LED control interface specification |
| 2.0 | 2026-04-08 | Extended with temperature sensor (TEMP_READ command) |

---

## 17. Contact & Support

For firmware-side implementation details, refer to:
- `pico_test.c` - LED hardware control functions
- `usb_descriptors.c` - USB protocol implementation
- Future: `temperature_sensor.c` - Temperature sensor ADC implementation

For driver-side implementation, use this extended specification as the communication contract.

---

## Appendix A: ASCII Protocol Diagram

```
┌─────────────────────────────────────────────┐
│          USB Protocol Overview              │
├─────────────────────────────────────────────┤
│                                             │
│  HOST (KMDF Driver / Application)           │
│         │                                   │
│         │ Bulk OUT (0x01)                   │
│         │ Command: [0x00-0x03, 0x10]        │
│         ↓                                   │
│  ┌─────────────────────────────────────┐   │
│  │      PICO USB Device                │   │
│  │   (RP2040 Microcontroller)          │   │
│  ├─────────────────────────────────────┤   │
│  │ • LED Control (GPIO 25)             │   │
│  │ • Temperature Sensor (ADC Ch4)      │   │
│  └─────────────────────────────────────┘   │
│         ↑                                   │
│         │ Bulk IN (0x81)                    │
│         │ Response: [Data]                  │
│         │                                   │
│  HOST (KMDF Driver / Application)           │
│                                             │
└─────────────────────────────────────────────┘
```

---

**End of Extended USB Interface Specification v2.0**
