# USB Device "Busy" Issue 해결: Descriptor 포맷 수정

## 문제 상황

**증상:**
- USB 장치를 호스트에 연결했을 때 인식되지 않음
- VM에서 USB passthrough를 통해 사용할 때 "Device Busy" 에러 발생
- lsusb나 dmesg에서 enumeration 실패 메시지 확인

**근본 원인:**
USB 호스트는 기본 enumeration 과정에서 device의 string descriptor(제조사명, 제품명, 시리얼번호 등)를 요청합니다. 
이때 descriptor 데이터의 포맷이 USB 스펙을 준수하지 않으면, 호스트의 USB 스택이 올바른 응답을 받지 못해 계속해서 재시도하게 되고, 
이로 인해 device가 "busy" 상태에 머물러 있게 됩니다.

특히 **가상머신의 USB passthrough 환경**에서는 이 문제가 더 심각하게 나타나는데, 
호스트의 USB 드라이버가 더 엄격하게 descriptor 포맷을 검증하기 때문입니다.

---

## 수정 사항

### 1. String Descriptor 포맷 변경

**Before (잘못된 포맷):**
```c
const char string_manufacturer[] = "TestMfg";
const char string_product[] = "Pico USB Device";
const char string_serial[] = "123456";

const uint8_t *desc_strings[4] = {
    NULL
};
```

**After (USB 스펙 준수):**
```c
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
```

### 2. Descriptor Callback 함수 개선

**Before:**
```c
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    return NULL;
}
```

**After:**
```c
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
```

---

## 핵심 변경 사항 상세 설명

### USB String Descriptor 포맷

USB 스펙에 따르면, string descriptor는 다음 구조를 따릅니다:

```
Byte 0      : Descriptor Length (2 + string length in bytes)
Byte 1      : Descriptor Type (0x03 for STRING)
Byte 2+     : String Data in UTF-16 LE format
```

**구체적인 예:**
```
Manufacturer String "TestMfg" (7 characters)
├─ Byte 0: 0x10 (16 bytes total: 2 header + 7*2 for UTF-16)
├─ Byte 1: 0x03 (TUSB_DESC_STRING)
└─ Bytes 2-15: 'T'(0x54,0x00), 'e'(0x65,0x00), 's'(0x73,0x00), 't'(0x74,0x00), 
               'M'(0x4D,0x00), 'f'(0x66,0x00), 'g'(0x67,0x00)
```

코드에서 `(TUSB_DESC_STRING << 8) | (2 + 2*7)` 는 정확히 이를 구성합니다:
- `TUSB_DESC_STRING << 8` = `0x0300` (타입 필드)
- `(2 + 2*7)` = `0x10` (길이 필드)
- 합치면: `0x0310` (uint16_t로 해석)

### UTF-16 LE 형식의 중요성

일반 ASCII 문자열을 그대로 사용하면 안 되는 이유:
- USB 호스트가 string을 UTF-16 LE로 파싱 시도 → 포맷 불일치 → 에러
- 각 문자가 2바이트(UTF-16) 또는 4바이트(BOM 포함 시)로 표현되어야 함

예를 들어, "Test"는:
- **잘못된 포맷**: `0x54 0x65 0x73 0x74` (ASCII)
- **올바른 포맷**: `0x54 0x00 0x65 0x00 0x73 0x00 0x74 0x00` (UTF-16 LE)

코드에서 각 문자를 쉼표로 분리하여 배열 형태로 표현한 것이 바로 이것입니다:
```c
'T', 'e', 's', 't', ... // 각 문자가 uint16_t 배열의 원소
```

### Descriptor Callback의 올바른 구현

USB 호스트가 descriptor를 요청할 때:
1. **Index 0 요청** → language descriptor 반환 (필수)
2. **Index 1-3 요청** → 해당하는 string descriptor 반환
3. **범위 밖의 Index** → NULL 반환 (host는 이를 "미지원"으로 해석)

이전 코드에서는 항상 NULL을 반환했기 때문에, 호스트는 string descriptor를 받지 못하고 
이를 enumeration 실패의 신호로 받아들여 계속 재시도했습니다.

---

## 왜 VM 환경에서 더 심했나?

### 호스트의 USB 스택 차이

1. **Native USB (물리 USB 포트 직접 연결)**
   - OS 수준의 USB 드라이버가 어느 정도 에러를 용인할 수 있음
   - 재시도 로직이 상대적으로 길 수 있음

2. **VM의 USB Passthrough**
   - VM 하이퍼바이저가 USB 프로토콜을 엄격하게 검증
   - USB 스펙 비준수 → 즉시 "Device Busy" 또는 enumeration timeout
   - 가상 USB 컨트롤러의 리소스 제한으로 재시도가 제한될 수 있음

따라서 VM 환경에서는 descriptor 포맷 오류가 native 환경보다 훨씬 빠르게 "busy" 상태로 나타나게 됩니다.

---

## 검증 방법

수정 후 다음 명령어로 device가 올바르게 인식되는지 확인:

```bash
# Linux/macOS
lsusb -v  # Vendor/Product ID로 검색 (0xCAFE:0x4005)

# dmesg로 enumeration 성공 확인
dmesg | tail -20

# Windows PowerShell (VM의 경우)
Get-PnpDevice | Where-Object {$_.FriendlyName -like "*Pico*"}
```

**성공 시나리오:**
- Device가 USB Hub에 나타남
- Manufacturer: "TestMfg"
- Product: "Pico USB Device"
- Serial: "123456"
- 상태: "OK" 또는 "Ready"

---

## 핵심 교훈

✅ **USB Descriptor는 스펙을 정확히 준수해야 함**
- String descriptor는 **UTF-16 LE** 포맷 필수
- **Proper header** (length + descriptor type) 필수
- 각 string descriptor는 **null-terminated** (0x0000)

✅ **Descriptor Callback은 모든 요청에 응답해야 함**
- Index 0 (language) 요청에는 반드시 응답
- 범위 내의 모든 string index에 응답
- 범위 밖의 요청에는 NULL로 응답

✅ **VM 환경에서는 스펙 준수가 더욱 중요함**
- 물리 환경보다 더 엄격한 검증
- 에러 재현이 일관성 있음

---

## 참고 자료

- [USB Device Class Definition for Communications Devices](https://www.usb.org/sites/default/files/documents/usb_class_definitions.pdf)
- [TinyUSB Documentation](https://docs.tinyusb.org/)
- [USB 2.0 Specification - Chapter 9](https://www.usb.org/document-library/usb-20-specification)
