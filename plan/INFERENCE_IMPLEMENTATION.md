# FOMO 모델 추론 구현 계획

**모델:** `tflite_learn_958324_7.tflite`  
**작성일:** 2026-04-12  
**대상 파일:** `pico_test.cpp`

---

## 배경 및 핵심 이해

### FOMO는 bbox를 직접 출력하지 않는다

기존에 bbox(x, y, w, h)를 직접 출력하는 모델이라고 생각했으나, FOMO는 다르다.

```
모델 출력: [1, 8, 8, 2] int8
           ↑  ↑  ↑  ↑
           배치 8×8그리드 2클래스(배경/얼굴)
```

각 그리드 셀이 "이 위치에 얼굴이 있는가?"를 나타낸다. bbox는 없고 중심점만 있다.

### Edge Impulse SDK가 후처리를 자동으로 해준다

`run_classifier()`를 호출하면 SDK 내부에서 다음 과정을 자동 처리한다:

```
[1,8,8,2] int8 출력
    ↓
역양자화: (int8 + 128) / 256 → float 확률
    ↓
threshold(0.5) 이상인 셀 활성화
    ↓
BFS 클러스터링 (인접 셀 병합)
    ↓
확률 가중 중심점 계산
    ↓
그리드 셀 크기 기반 박스 추정 (64×64 픽셀 공간)
    ↓
ei_impulse_result_t.bounding_boxes[] 에 저장
```

**→ `pico_test.cpp`에서 별도 FOMO 후처리 코드를 작성할 필요 없음.**  
`run_classifier()` 호출 후 `inference_result.bounding_boxes`를 읽으면 된다.

---

## 현재 코드의 문제점

### 문제 1: 구 모델 헤더 include (빌드 오류 원인)

```cpp
// pico_test.cpp:14 — 현재 (잘못됨)
#include "micro-sdk/tflite-model/tflite_learn_956961_19.h"
// 이 파일은 삭제됨 → 빌드 오류 발생
```

### 문제 2: int8 → float 변환 버그 (추론 오류 원인)

```cpp
// pico_test.cpp:121-124 — 현재 (잘못됨)
image_float_buffer[i] = (float)image_data[i] / 255.0f;
```

`image_data`는 `int8_t` 타입 (범위: -128 ~ 127).  
int8(-128) → float으로 변환하면 **-0.502**가 되어 음수가 됨.  
Edge Impulse SDK는 `get_data` 콜백에서 **0.0 ~ 1.0** 범위 float을 기대함.

---

## 수정 계획

### 수정 1: 모델 헤더 교체

**파일:** `pico_test.cpp:14`

```cpp
// 이전
#include "micro-sdk/tflite-model/tflite_learn_956961_19.h"

// 이후
#include "micro-sdk/tflite-model/tflite_learn_958324_7.h"
```

---

### 수정 2: int8 → float 변환 수정

**파일:** `pico_test.cpp:119-125`  
**함수:** `run_tflite_inference()`

```cpp
// 이전
for (int i = 0; i < 64 * 64; i++) {
    image_float_buffer[i] = (float)image_data[i] / 255.0f;
}

// 이후
for (int i = 0; i < 64 * 64; i++) {
    // int8(-128~127) → int16(0~255) → float(0.0~1.0)
    image_float_buffer[i] = (float)((int16_t)image_data[i] + 128) / 255.0f;
}
```

**변환 예시:**
| int8 입력 | 의미 | float 출력 |
|----------|------|----------|
| -128 | 검정(0) | 0.000 |
| 0 | 중간(128) | 0.502 |
| 127 | 흰색(255) | 1.000 |

---

## 변경하지 않는 것

### 결과 전송 프로토콜 (유지)

현재 `send_inference_results()`의 좌표 스케일링은 유지.

```cpp
uint8_t x    = (uint8_t)(box.x * 255 / 64);   // 64px 공간 → 0~255 범위
uint8_t y    = (uint8_t)(box.y * 255 / 64);
uint8_t w    = (uint8_t)(box.width * 255 / 64);
uint8_t h    = (uint8_t)(box.height * 255 / 64);
uint8_t conf = (uint8_t)(box.value * 255);
```

FOMO 출력에서 `box.x`, `box.y`는 64×64 공간의 픽셀 중심 좌표 (0~63).  
`* 255 / 64`로 0~255 스케일로 변환해 전송.  
호스트에서 `val * 64 / 255`로 역변환하면 원래 좌표 복원 가능.

### `run_classifier()` 호출 방식 (유지)

Edge Impulse SDK가 FOMO 후처리를 내부에서 처리하므로 변경 불필요.

---

## 전체 추론 흐름 (수정 후)

```
[Host PC]
  USB 0x21 + int8 픽셀 데이터 전송
        ↓
[usb_descriptors.c]
  image_buffer에 저장, image_ready = true
        ↓
[pico_test.cpp] main loop
  run_tflite_inference(image_data_ptr)
        ↓
  1. int8 → float 변환
     (int16_t)pixel + 128) / 255.0f  → 0.0~1.0
        ↓
  2. signal_t 구성 (get_image_data 콜백)
        ↓
  3. run_classifier() 호출
     - Edge Impulse DSP 내부 처리 (Grayscale 추출)
     - TFLite Micro 추론 실행 [1,64,64,1] → [1,8,8,2]
     - FOMO 후처리 자동 실행
       (역양자화 → threshold → 클러스터링 → 중심점 → 박스 추정)
        ↓
  4. inference_result.bounding_boxes 읽기
        ↓
  5. send_inference_results()
     0x22 + count + [x,y,w,h,conf]*N 전송
        ↓
[Host PC]
  0x22 응답 수신 → 좌표 파싱 → 화면에 표시
```

---

## 모델 파라미터 참고

| 항목 | 값 |
|------|-----|
| 입력 | [1, 64, 64, 1] int8 Grayscale |
| 출력 | [1, 8, 8, 2] int8 (배경/얼굴) |
| 감지 threshold | 0.5 |
| 최대 감지 수 | 10개 |
| Arena 크기 | 137,804 bytes (~134KB) |
| 모델 바이너리 크기 | ~39.9KB |

---

## 검증 순서

1. **빌드 성공** — 구 모델 헤더 제거로 링크 오류 해소 확인
2. **Pico 플래시** — .uf2 업로드
3. **단위 테스트** — 순수 흰색(0x7F) 이미지 전송 → count == 0 확인
4. **얼굴 감지** — 얼굴 이미지 64×64 int8로 전송 → count > 0, 좌표 범위 확인
5. **비얼굴** — 배경 이미지 전송 → count == 0 확인
