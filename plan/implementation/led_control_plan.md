# LED Control Implementation Plan

## Overview
KMDF 드라이버의 IOCTL 명령을 받아 Pico의 bulk endpoint를 통해 LED를 제어하는 기능 구현

## Architecture

```
KMDF Driver (IOCTL) 
    ↓ (USB Bulk OUT)
Pico USB Endpoint (0x01)
    ↓
LED Control Handler
    ↓
GPIO (LED Pin)
```

## Step 1: Pico LED 핀 확인
- **Pico (일반)**: GPIO 25 (내장 LED)
- **Pico W**: GPIO 0 (LED_PIN, 다른 핸들링 필요)
- rp2040

## Step 2: Pico 펌웨어 구현

### 2.1 GPIO 초기화 함수 추가
```c
void led_init(void) {
    // GPIO 25를 출력으로 설정 (Pico 기본)
    // 또는 Pico W인 경우 별도 초기화
}
```

### 2.2 Bulk RX 콜백 구현
`tud_vendor_rx_cb()` 함수에서:
- 수신한 데이터 읽기
- 명령 파싱 (예: 0x01 = LED ON, 0x00 = LED OFF)
- GPIO 제어
- 응답 데이터 전송 (옵션)

### 2.3 명령 프로토콜 정의
| 명령 | 값 | 설명 |
|------|-----|------|
| LED_OFF | 0x00 | LED 끄기 |
| LED_ON  | 0x01 | LED 켜기 |
| LED_TOGGLE | 0x02 | LED 토글 |
| STATUS | 0x03 | LED 상태 조회 |

## Step 3: 구현 단계

1. **GPIO 초기화** - `led_init()` 함수 추가
2. **명령 처리** - `tud_vendor_rx_cb()` 구현
3. **응답 전송** - `tud_vendor_write()` 로 응답 데이터 전송
4. **메인 루프** - 타이밍 확인, 필요시 `sleep_ms()` 추가

## Step 4: 테스트

### Pico 테스트
1. LED 정상 제어 확인
2. USB bulk 데이터 수신 확인
3. 응답 데이터 전송 확인

### KMDF 통합 테스트
1. IOCTL → USB Bulk 전송 확인
2. LED 제어 동작 확인
3. 응답 수신 확인

## Files to Modify/Create

- `pico_test.c` - main 함수, LED 초기화 호출
- `usb_descriptors.c` - `tud_vendor_rx_cb()` 구현 (LED 제어 로직)
- CMakeLists.txt - GPIO 라이브러리 의존성 확인

## Notes

- Pico는 USB와 로직이 분리되어 있으므로, `tud_task()` 루프 중에 GPIO 상태 변경 가능
- KMDF 드라이버 측에서는 bulk OUT으로 명령 전송, IN으로 응답 수신
- 프로토콜 문서화 필요 (KMDF 드라이버와 동일한 형식 사용)


### 추가 구현 내용 
현업에서의 펌웨어 느낌을 최대한 살려서 구현해줘 
그리고 각 로직에 영어로 주석을 자세히 달아줬으면 해 