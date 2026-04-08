# Pico LED Control - USB Interface Specification

## Document Overview

This document defines the USB interface protocol between the Pico firmware and the KMDF host driver/application for LED control via bulk transfer.

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
- Suitable for device control without strict timing requirements

---

## 3. LED Control Protocol

### 3.1 Command Format

**Structure:** Single byte command

```
┌─────────────────────┐
│   Command Byte      │
│  (0x00 ~ 0x03)     │
└─────────────────────┘
```

| Command | Hex | Name | Description |
|---------|-----|------|-------------|
| 0 | 0x00 | LED_OFF | Turn off the LED |
| 1 | 0x01 | LED_ON | Turn on the LED |
| 2 | 0x02 | LED_TOGGLE | Toggle LED state (ON→OFF or OFF→ON) |
| 3 | 0x03 | LED_STATUS | Query current LED state |

### 3.2 Response Format

**Structure:** Single byte response

```
┌─────────────────────┐
│   Response Byte     │
│  (0x00/0x01/0xFF)  │
└─────────────────────┘
```

| Response | Hex | Meaning |
|----------|-----|---------|
| OK / LED_OFF | 0x00 | Command succeeded OR LED is currently OFF (for STATUS query) |
| LED_ON | 0x01 | LED is currently ON (for STATUS query) |
| INVALID | 0xFF | Unknown command received - command not processed |

### 3.3 Command-Response Mapping

| Command | Expected Response | LED Result |
|---------|-------------------|------------|
| LED_OFF (0x00) | OK (0x00) | LED turns OFF |
| LED_ON (0x01) | OK (0x00) | LED turns ON |
| LED_TOGGLE (0x02) | OK (0x00) | LED state inverts |
| LED_STATUS (0x03) | 0x00 (OFF) or 0x01 (ON) | No change (query only) |
| Invalid (any other) | INVALID (0xFF) | No change |

---

## 4. Communication Sequence

### 4.1 Typical LED Control Sequence

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

### 4.2 Status Query Sequence

```
Host (Driver/App)                     Pico Device
       │                                   │
       │─── Bulk OUT: 0x03 (STATUS) ──→ │
       │                                   │
       │                                   │ Get LED state
       │                                   │ (0x00 or 0x01)
       │                                   │
       │ ← Bulk IN: [0x00 or 0x01] ──────│
       │                                   │
```

---

## 5. Implementation Requirements

### 5.1 Host Driver Requirements (KMDF)

1. **Endpoint Initialization**
   - Configure OUT endpoint (0x01) for command transmission
   - Configure IN endpoint (0x81) for response reception

2. **Bulk Transfer**
   - Send 1-byte command via bulk OUT
   - Wait for 1-byte response via bulk IN

3. **Timeout Handling**
   - Implement appropriate timeout for bulk transfers
   - Recommended: 1000ms timeout per transfer

4. **Error Handling**
   - Check response byte (0xFF = invalid command)
   - Implement retry logic if needed

### 5.2 Device Firmware (Pico)

- ✓ Receives data on endpoint 0x01
- ✓ Processes command byte
- ✓ Executes LED control (GPIO 25)
- ✓ Sends response on endpoint 0x81
- ✓ Returns 0xFF for invalid commands

---

## 6. Example Usage Scenarios

### 6.1 Turn LED ON

```c
// KMDF Driver / Application
uint8_t cmd = 0x01;  // LED_ON command
uint8_t response;

// Send command via bulk OUT
UsbBulkWrite(endpoint_out, &cmd, 1);

// Receive response via bulk IN
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    // Success - LED is now ON
} else if (response == 0xFF) {
    // Error - invalid command
}
```

### 6.2 Query LED Status

```c
// KMDF Driver / Application
uint8_t cmd = 0x03;  // LED_STATUS command
uint8_t response;

// Send status query
UsbBulkWrite(endpoint_out, &cmd, 1);

// Receive LED state
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    printf("LED is OFF\n");
} else if (response == 0x01) {
    printf("LED is ON\n");
}
```

### 6.3 Toggle LED

```c
// KMDF Driver / Application
uint8_t cmd = 0x02;  // LED_TOGGLE command
uint8_t response;

UsbBulkWrite(endpoint_out, &cmd, 1);
UsbBulkRead(endpoint_in, &response, 1);

if (response == 0x00) {
    // Success - LED state toggled
}
```

---

## 7. Electrical Characteristics

### 7.1 LED Pin (GPIO 25)

| Property | Value | Notes |
|----------|-------|-------|
| GPIO Pin | 25 | Pico onboard LED |
| Logic Level | 3.3V (HIGH) | LED turns ON |
| Logic Level | 0V (LOW) | LED turns OFF |
| LED Color | Green | Standard indicator |
| Current Limiting | Internal resistor | Safe for Pico GPIO |

---

## 8. Timing and Constraints

### 8.1 Transfer Timing

| Item | Value | Description |
|------|-------|-------------|
| Max Packet Size | 64 bytes | Single bulk transfer limit |
| Command Size | 1 byte | Single byte sufficient |
| Response Size | 1 byte | Single byte sufficient |
| Processing Time | < 1ms | Firmware response time |
| Bulk Transfer Speed | Full Speed (12 Mbps) | USB 2.0 Full Speed |

### 8.2 Retry Behavior

- If response timeout occurs: Retry transfer
- Max retries: 3 attempts (configurable)
- Wait time between retries: 100ms (configurable)

---

## 9. Known Limitations & Notes

1. **Single Command** per transfer - Only 1 command byte expected per bulk OUT
2. **No Batching** - Cannot send multiple commands in one packet
3. **Asynchronous Response** - Response sent via separate bulk IN transfer
4. **No Bulk Fragmentation** - Responses are always single byte (< max packet size)
5. **Device Enumeration** - Device may require 1-2 seconds to appear after connection

---

## 10. Debugging & Troubleshooting

### 10.1 Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| No device enumeration | Device not powered/connected | Check USB cable, Pico power |
| 0xFF response always | Invalid command sent | Verify command byte (0x00-0x03) |
| Response timeout | Device not responding | Check firmware build/upload |
| LED not responding | Endpoint not properly configured | Verify endpoint addresses (0x01, 0x81) |

### 10.2 Testing Checklist

- [ ] Device enumerates correctly with VID/PID (0xCAFE/0x4005)
- [ ] Bulk OUT endpoint 0x01 accepts data
- [ ] Bulk IN endpoint 0x81 returns response
- [ ] LED_ON command (0x01) turns LED on
- [ ] LED_OFF command (0x00) turns LED off
- [ ] LED_TOGGLE command (0x02) inverts LED state
- [ ] LED_STATUS command (0x03) returns correct state
- [ ] Invalid command (0xFF) returns error response

---

## 11. Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-04-08 | Initial interface specification |

---

## 12. Contact & Support

For firmware-side details, refer to:
- `pico_test.c` - LED hardware control functions
- `usb_descriptors.c` - USB protocol implementation and callbacks

For driver-side implementation, use this specification as the communication contract.
