# RP2040 TFLite Micro USB 얼굴 감지

USB로 이미지를 받아서 TFLite Micro로 얼굴을 감지하고 결과를 돌려주는 Waveshare RP2040 Zero 기반 프로젝트다.

npu와 같은 ai 가속기처럼 mcu를 활용하려고 했지만 설계 목적과 내부 구조, 그리고 처리하는 연산의 성격면에서 근본적으로 다른 하드웨어라는 것을 알았다. 
pico mcu를 이미 구매했기에 vendor specific interface를 활용해 kmdf로 windows driver를 개발하며 익숙해지기 위한 프로젝트를 하면 좋겠다고 생각하여 시작했다. 

> **[블로그에 개발 여정을 정리해뒀습니다]** Windows Driver부터 USB Composite Device, Interrupt endpoint까지의 삽질 기록

## 시스템 구조

```
┌─────────────┐
│  Host PC    │
│  (Windows)  │
└──────┬──────┘
       │ USB

  kmdf driver 

       │
┌──────▼──────────────────────┐
│  Waveshare RP2040 Zero      │
│                             │
│  • TFLite Micro (추론)       │
│  • TinyUSB (CDC + Vendor)   │
│  • WS2812B ARGB LED         │
│  • 내부 온도 센서             │
└─────────────────────────────┘
```

## 주요 기능

- **이미지 추론**: 64x64 이미지 USB로 수신 → TFLite Micro 추론 → 8x8 float32 확률값 반환
- **USB 통신**: CDC (시리얼) + Vendor (Bulk + Interrupt) 복합 인터페이스
- **온도 전송**: Interrupt endpoint로 1초마다 내부 온도 센서값 전송
- **LED 제어**: USB 명령으로 WS2812B ARGB LED 색상 제어

## 개발 환경

### 필요한 것
- Pico SDK 2.2.0
- TFLite Micro (Edge Impulse SDK)
- Windows WDM Driver 개발 환경 (일반 사용자는 pre-built 드라이버 필요)
- CMake 3.13+

### Pico 빌드

```bash
mkdir build
cd build
cmake ..
makeㄴ
```

### kmdf driver 설치 
https://github.com/wangki-kyu/pico_driver

## 개발 여정

Windows Driver와 pico mcu를 개발하며 겪은 것들을 기록하였습니다. 

[windows driver](https://velog.io/@wang_ki/series/windows-driver)

USB 부분이 생각보다 복잡했는데, 특히 descriptor 순서와 endpoint 할당 순서 때문에 꽤 삽질했습니다.

## 한계점

### Pico MCU 메모리

264KB RAM 안에 TFLite tensor arena + 코드 + USB 스택을 다 집어넣으려니 생각보다 빡빡합니다.

- Tensor arena: 150KB
- USB + TinyUSB: ~30KB
- 코드 + 스택: ~80KB

더 큰 모델을 올리려고 시도했지만 메모리 부족으로 실패했습니다.


## 배운점

- usb를 활용한 mcu와의 통신 
- cdc + vendor interface를 동시에 활용하여 복합장치로 인식 시키기 
- tflite 모델을 edgeimpulse.com에서 생성
- vendor specific interface 활용 
- cdc interface를 활성화하여 실시간 로깅 디버깅 

## 참고

- [Pico C/C++ SDK](https://github.com/raspberrypi/pico-sdk)
- [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro)
- [TinyUSB](https://github.com/hathach/tinyusb)
