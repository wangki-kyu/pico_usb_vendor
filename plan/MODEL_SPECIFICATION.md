# FOMO 얼굴 감지 모델 - 기술 명세서

**프로젝트:** Face detection - FOMO - Embedded Online Conference  
**모델 파일:** `tflite_learn_958324_7.tflite`  
**용도:** PC 테스트 앱 & Pico 프로젝트 공통 참고  
**마지막 업데이트:** 2026-04-12

---

## 📋 목차

1. [모델 개요](#모델-개요)
2. [입출력 스펙](#입출력-스펙)
3. [전처리 (Preprocessing)](#전처리-preprocessing)
4. [양자화 (Quantization)](#양자화-quantization)
5. [추론 (Inference)](#추론-inference)
6. [FOMO 후처리 (Post-processing)](#fomo-후처리-post-processing)
7. [Blur 처리](#blur-처리)
8. [PC 앱 구현](#pc-앱-구현)
9. [Pico 구현 시 참고사항](#pico-구현-시-참고사항)
10. [트러블슈팅](#트러블슈팅)

---

## 모델 개요

### 모델 타입
- **FOMO (Faster Objects, More Objects)**
- Edge Impulse Studio에서 자동 생성
- 경량 신경망 (39.9 KB)
- 실시간 객체 감지 (8×8 그리드 기반)

### 특징
- ✅ 매우 빠름 (Pico RP2040에서 밀리초 단위)
- ✅ 메모리 효율적
- ❌ 정확한 bbox 미제공 (그리드 셀 위치만)
- ❌ 8×8 격자 단위 위치 정보 (작은 물체 감지 어려움)

### 사용 시나리오
- ✅ 얼굴 감지 후 즉시 blur/마스킹
- ✅ 얼굴 존재 여부만 판단
- ✅ 에너지 효율이 중요한 MCU 기반 시스템
- ❌ 정확한 얼굴 영역이 필요한 경우 (YOLO 권장)

---

## 입출력 스펙

### 입력 (Input)

| 항목 | 값 |
|------|-----|
| **Shape** | `[1, 64, 64, 1]` |
| **배치 크기** | 1 (한 번에 1개 이미지만) |
| **높이** | 64 픽셀 |
| **너비** | 64 픽셀 |
| **채널** | 1 (Grayscale) |
| **데이터 타입** | INT8 (부호있는 8비트 정수) |
| **값 범위** | -128 ~ 127 |
| **색상 공간** | Grayscale (흑백) |

### 출력 (Output)

| 항목 | 값 |
|------|-----|
| **Shape** | `[1, 8, 8, 2]` |
| **배치 크기** | 1 |
| **그리드 높이** | 8 |
| **그리드 너비** | 8 |
| **클래스 수** | 2 (`[background, face]`) |
| **데이터 타입** | INT8 |
| **값 범위** | -128 ~ 127 |

**해석:**
- 8×8 = 64개 그리드 셀
- 각 셀마다 2개 클래스 확률값
- 총 128개 출력값

---

## 전처리 (Preprocessing)

### 목적
원본 이미지 → 모델 입력 형식으로 변환

### 단계별 처리

#### 1️⃣ 이미지 로드
```
입력: 파일 경로 또는 PIL.Image
출력: PIL.Image (RGB 또는 RGBA)
```

**요점:**
- 색상 공간 무관 (자동 변환)
- 이미지 크기 무관 (리사이징함)

```python
# Python (PC 앱)
from PIL import Image
img = Image.open("photo.jpg")  # 어떤 크기든 OK
```

```cpp
// C++ (Pico)
uint8_t* image_data = /* 카메라 입력 */;  // RGB888 형식
int img_w = 640, img_h = 480;  // 어떤 크기든 OK
```

---

#### 2️⃣ 리사이징 (Resizing)

**목적:** 원본 크기 → 64×64로 변환

```
입력: 원본 이미지 (W × H)
출력: 64×64 이미지
```

**Python:**
```python
resized = img.resize((64, 64), Image.LANCZOS)
```

**중요:** 
- LANCZOS 필터 사용 (고품질 다운샘플링)
- 비율 무시 (무조건 64×64로 늘림/줄임)
- 이로 인해 약간의 왜곡 발생 (수용함)

**C/C++ (Pico):**
```cpp
// 간단한 nearest-neighbor 리사이징
uint8_t resized_64x64[64*64];
for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
        int src_x = (x * img_w) / 64;
        int src_y = (y * img_h) / 64;
        resized_64x64[y*64 + x] = image_data[src_y*img_w + src_x];
    }
}
```

---

#### 3️⃣ 컬러 변환 (Color Conversion)

**목적:** RGB → Grayscale (흑백)으로 변환

```
입력: 64×64 RGB 이미지
출력: 64×64 Grayscale 이미지
```

**공식:**
```
Gray = 0.299 × R + 0.587 × G + 0.114 × B
```

**Python:**
```python
gray = resized.convert("L")  # PIL이 자동 계산
```

**C/C++ (Pico):**
```cpp
uint8_t gray_64x64[64*64];
for (int i = 0; i < 64*64; i++) {
    uint8_t r = rgb_data[i*3 + 0];
    uint8_t g = rgb_data[i*3 + 1];
    uint8_t b = rgb_data[i*3 + 2];
    gray_64x64[i] = (299*r + 587*g + 114*b) / 1000;
}
```

**결과:**
- uint8 값 범위: [0, 255]
- 0 = 검정, 255 = 흰색

---

#### 4️⃣ INT8 양자화 변환

**목적:** uint8 [0,255] → int8 [-128,127]로 변환

**공식:**
```
int8_value = uint8_value - 128
```

**예시:**
```
uint8: 0    →  int8: -128
uint8: 128  →  int8: 0
uint8: 255  →  int8: 127
```

**⚠️ 중요: 정수 언더플로우 주의**

**Python (틀린 예):**
```python
# ❌ 잘못됨 - uint8 언더플로우 발생
int8 = (uint8_arr - 128).astype(np.int8)
# uint8(0) - 128 = uint8(128) ← wrap-around!
```

**Python (올바른 예):**
```python
# ✅ 올바름 - int16 경유
int8 = (uint8_arr.astype(np.int16) - 128).astype(np.int8)
# np.uint8(0).astype(np.int16) - 128 = -128 ← 정상
```

**C/C++ (Pico):**
```cpp
int8_t int8_64x64[64*64];
for (int i = 0; i < 64*64; i++) {
    int8_64x64[i] = (int16_t)gray_64x64[i] - 128;
    // int16 사용으로 언더플로우 방지
}
```

---

#### 5️⃣ 텐서 형태 변환 (Tensor Reshape)

**목적:** [64, 64] → [1, 64, 64, 1]로 변환

```
입력: 1D array, 크기 4096 (64×64)
출력: 4D tensor, shape [1, 64, 64, 1]
```

**차원 의미:**
- Dimension 0: 배치 크기 (1 = 1개 이미지)
- Dimension 1: 높이 (64)
- Dimension 2: 너비 (64)
- Dimension 3: 채널 (1 = Grayscale)

**Python:**
```python
tensor = int8_array.reshape(1, 64, 64, 1)
# 메모리 재할당 없음, 메타데이터만 변경
```

**C/C++ (Pico):**
```cpp
// 메모리 구조는 동일, 해석만 변경
// int8_64x64[0] → tensor[0][0][0][0]
// int8_64x64[1] → tensor[0][0][0][1] (X, 다음 행)
// int8_64x64[1] → tensor[0][0][1][0] (O, 다음 픽셀)
```

---

### 전처리 전체 플로우

```
파일 로드 (RGB, 어떤 크기든)
    ↓
64×64 리사이즈 (LANCZOS)
    ↓
Grayscale 변환 (uint8 [0,255])
    ↓
INT8 변환 (int8 [-128,127], int16 경유)
    ↓
Reshape ([1, 64, 64, 1])
    ↓
모델 입력 준비 완료 ✓
```

---

## 양자화 (Quantization)

### 목적
- 모델 크기 감소 (32-bit float → 8-bit int)
- 추론 속도 향상
- 메모리 절약

### 입력 양자화 파라미터

| 파라미터 | 값 | 용도 |
|---------|-----|------|
| **scale** | 1/255 ≈ 0.003921569 | float → int8 변환 계수 |
| **zero_point** | -128 | 0을 대표하는 int8 값 |

**역양자화 공식:**
```
float_value = (int8_value - zero_point) × scale
            = (int8_value - (-128)) × (1/255)
            = (int8_value + 128) / 255
```

**예시:**
```
int8: -128  →  float: 0.0
int8: 0     →  float: 0.502
int8: 127   →  float: 1.0
```

### 출력 양자화 파라미터

| 파라미터 | 값 | 용도 |
|---------|-----|------|
| **scale** | 1/256 = 0.00390625 | int8 → float 변환 계수 |
| **zero_point** | -128 | 0을 대표하는 int8 값 |

**역양자화 공식:**
```
float_value = (int8_value - zero_point) × scale
            = (int8_value - (-128)) × (1/256)
            = (int8_value + 128) / 256
```

**예시:**
```
int8: -128  →  float: 0.0      (배경 100%)
int8: 0     →  float: 0.5      (중립)
int8: 127   →  float: 0.996    (얼굴 ~100%)
```

---

## 추론 (Inference)

### TFLite 인터프리터 실행 흐름

```
1. 인터프리터 로드
2. 입력 텐서 설정
3. invoke() 실행
4. 출력 텐서 읽기
```

### Python 구현

```python
import tensorflow as tf
import numpy as np

# 1. 인터프리터 로드
interpreter = tf.lite.Interpreter(model_path="tflite_learn_958324_7.tflite")
interpreter.allocate_tensors()

# 2. 입출력 텐서 인덱스 획득
inp_details = interpreter.get_input_details()[0]
out_details = interpreter.get_output_details()[0]

inp_index = inp_details["index"]
out_index = out_details["index"]

# 3. 입력 설정 및 추론
input_data = np.array([[[[...]]]], dtype=np.int8)  # [1,64,64,1]
interpreter.set_tensor(inp_index, input_data)
interpreter.invoke()

# 4. 출력 읽기
output_data = interpreter.get_tensor(out_index)  # [1,8,8,2] int8
```

### C/C++ (TensorFlow Lite Micro, Pico)

```cpp
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// 1. 모델 로드 (PROGMEM에서)
extern const unsigned char tflite_model[];
extern const int tflite_model_len;

const tflite::Model* model = tflite::GetModel(tflite_model);
tflite::MicroInterpreter interpreter(
    model,
    resolver,
    tensor_arena,
    kTensorArenaSize,
    error_reporter
);

// 2. 입력 설정
TfLiteTensor* input = interpreter.input(0);
memcpy(input->data.int8, input_data_int8, 64*64);

// 3. 추론 실행
interpreter.Invoke();

// 4. 출력 읽기
TfLiteTensor* output = interpreter.output(0);  // [1,8,8,2] int8
int8_t* face_scores = output->data.int8;
```

---

## FOMO 후처리 (Post-processing)

### 목적
8×8 그리드의 출력 → 원본 이미지에서 얼굴 위치 추출

### 단계 1: 출력 역양자화

**입력:** [1, 8, 8, 2] int8 (raw 모델 출력)  
**출력:** [8, 8] float32 (face 클래스 확률)

```python
# 모델 출력
output_raw = interpreter.get_tensor(output_index)  # [1,8,8,2] int8

# face 클래스 추출 (클래스 1)
face_scores_int8 = output_raw[0, :, :, 1]  # [8,8]

# INT8 → float32 역양자화
# float = (int8 + 128) / 256
face_probs = (face_scores_int8.astype(np.float32) + 128) / 256
```

**결과:**
- Shape: [8, 8]
- 값 범위: [0.0, 1.0] (확률)
- 0.0 = 배경, 1.0 = 얼굴 100% 확신

### 단계 2: 임계값 적용

**입력:** [8, 8] face 확률, threshold=0.5  
**출력:** [N] 활성화된 셀 좌표

```python
threshold = 0.5

# threshold 이상인 셀 찾기
activated = []
for row in range(8):
    for col in range(8):
        if face_probs[row, col] >= threshold:
            activated.append((row, col, face_probs[row, col]))
```

**예시:**
```
face_probs:
  [0.1, 0.2, 0.1, 0.1, ...]
  [0.2, 0.8, 0.9, 0.2, ...]  ← row 1, col 1,2에서 threshold 초과
  [0.1, 0.7, 0.8, 0.1, ...]  ← row 2, col 1,2에서 threshold 초과
  ...

activated: [(1,1,0.8), (1,2,0.9), (2,1,0.7), (2,2,0.8)]
```

### 단계 3: 클러스터링 (BFS)

**목적:** 인접한 셀들을 하나의 얼굴로 병합

**알고리즘:** 체비쇼프 거리(Chebyshev distance) 기반 BFS
- 8방향 연결 (대각선 포함)
- 거리 <= 1인 셀들을 같은 클러스터로 병합

```python
def find_clusters(activated):
    """
    activated: [(row, col, prob), ...]
    returns: [[(row, col, prob), ...], ...]  클러스터 리스트
    """
    clusters = []
    used = set()
    
    for i, (r, c, p) in enumerate(activated):
        if i in used:
            continue
        
        # 새 클러스터 시작
        cluster = [(r, c, p)]
        used.add(i)
        queue = [i]
        
        # BFS
        while queue:
            cur_idx = queue.pop(0)
            cur_r, cur_c, _ = activated[cur_idx]
            
            # 인접한 셀 찾기
            for j, (nr, nc, np_) in enumerate(activated):
                if j not in used:
                    # 체비쇼프 거리
                    if max(abs(nr - cur_r), abs(nc - cur_c)) <= 1:
                        cluster.append((nr, nc, np_))
                        used.add(j)
                        queue.append(j)
        
        clusters.append(cluster)
    
    return clusters
```

**예시:**
```
activated 셀들:
  (1,1), (1,2)
  (2,1), (2,2)

BFS 후:
  Cluster 1: [(1,1), (1,2), (2,1), (2,2)]  ← 하나의 얼굴
```

### 단계 4: 중심점 계산

**목적:** 각 클러스터의 대표 좌표 계산

**방법:** 확률 가중 평균

```python
def calc_cluster_centers(clusters):
    """각 클러스터의 확률 가중 중심점"""
    centers = []
    
    for cluster in clusters:
        rows = [item[0] for item in cluster]
        cols = [item[1] for item in cluster]
        probs = [item[2] for item in cluster]
        
        total_prob = sum(probs)
        
        # 확률 가중 평균
        row_center = sum(r * p for r, p in zip(rows, probs)) / total_prob
        col_center = sum(c * p for c, p in zip(cols, probs)) / total_prob
        
        centers.append((row_center, col_center))
    
    return centers
```

**예시:**
```
Cluster: [(1, 1, 0.8), (1, 2, 0.9), (2, 1, 0.7), (2, 2, 0.8)]

row_center = (1×0.8 + 1×0.9 + 2×0.7 + 2×0.8) / (0.8+0.9+0.7+0.8)
           = 5.0 / 3.2
           = 1.56

col_center = (1×0.8 + 2×0.9 + 1×0.7 + 2×0.8) / 3.2
           = 5.2 / 3.2
           = 1.625

center: (1.56, 1.625)  ← 8×8 그리드 좌표
```

### 단계 5: 그리드 좌표 → 픽셀 좌표 변환

**목적:** 8×8 그리드 좌표 → 원본 이미지 픽셀 좌표로 변환

**공식:**
```
64×64 공간에서의 픽셀 좌표:
  px_64 = col_center × 8 + 4
  py_64 = row_center × 8 + 4
  (8 = 64/8, 4 = 8/2, 그리드 셀의 중심)

원본 이미지 좌표 (스케일 변환):
  px_orig = px_64 × (orig_width / 64)
  py_orig = py_64 × (orig_height / 64)
```

**예시:**
```
그리드 중심: (row=1.56, col=1.625)
64×64 공간:
  px_64 = 1.625 × 8 + 4 = 17
  py_64 = 1.56 × 8 + 4 = 16.48

원본 이미지 (800×600):
  px_orig = 17 × (800/64) = 17 × 12.5 = 212.5
  py_orig = 16.48 × (600/64) = 16.48 × 9.375 = 154.5

결과: (212, 154)  ← 원본에서의 얼굴 중심점
```

---

## Blur 처리

### 목적
감지된 얼굴 영역을 흐릿하게 처리 (프라이버시 보호)

### 문제점
FOMO는 **중심점만 제공**, bbox 미제공

**해결 방법:** 그리드 셀 크기 기반으로 박스 크기 추정

### 박스 크기 계산

**공식:**
```
cell_width = orig_width / 8
cell_height = orig_height / 8

multiplier = 2.5  # 세트

box_half_width = cell_width × multiplier / 2
box_half_height = cell_height × multiplier / 2

x1 = max(0, center_x - box_half_width)
y1 = max(0, center_y - box_half_height)
x2 = min(orig_width, center_x + box_half_width)
y2 = min(orig_height, center_y + box_half_height)
```

**예시:**
```
원본: 800×600
cell_width = 800/8 = 100
cell_height = 600/8 = 75
multiplier = 2.5

box_half_width = 100 × 2.5 / 2 = 125
box_half_height = 75 × 2.5 / 2 = 93.75

중심: (212, 154)
박스: (212-125, 154-93.75) ~ (212+125, 154+93.75)
    = (87, 60) ~ (337, 248)
    = 가로 250px, 세로 187px 박스
```

### multiplier 조정 가이드

| multiplier | 박스 크기 | 용도 |
|-----------|---------|------|
| 1.5 | 작음 | 얼굴이 크게 찍힌 경우 |
| 2.0 | 중간 | 일반적인 경우 |
| 2.5 | 중간-큼 | **기본값** (추천) |
| 3.0 | 큼 | 얼굴이 작게 찍힌 경우 |
| 4.0 | 매우 큼 | 주변까지 blur 필요시 |

### GaussianBlur 적용

**목적:** 박스 영역에 가우시안 필터 적용

**공식:**
```
blur_radius = max(10, min(box_width, box_height) // 5)
```

**Python 구현:**
```python
from PIL import Image, ImageFilter

# 원본 이미지에서 박스 영역 추출
region = image.crop((x1, y1, x2, y2))

# GaussianBlur 적용
# radius가 클수록 강한 blur
radius = max(10, min(x2-x1, y2-y1) // 5)
blurred = region.filter(ImageFilter.GaussianBlur(radius=radius))

# 원본에 다시 붙이기
image.paste(blurred, (x1, y1))
```

**예시:**
```
박스: (87, 60) ~ (337, 248)
너비: 250, 높이: 188
radius = max(10, min(250, 188) // 5)
       = max(10, 188 // 5)
       = max(10, 37)
       = 37
       
→ radius 37로 GaussianBlur 적용 (적당한 blur)
```

### Blur 강도 조정

**약한 blur:**
```python
radius = 10  # 고정값
```

**강한 blur (완전 프라이버시):**
```python
radius = max(box_width, box_height) // 2
```

**자동 조정:**
```python
# 박스 크기에 비례하는 blur
aspect_ratio = max(box_width, box_height) / min(box_width, box_height)
radius = int(min(box_width, box_height) / (5 - aspect_ratio))
```

---

## PC 앱 구현

### 전체 흐름

```
1. 앱 시작
   ↓
2. 이미지 선택
   ↓
3. 전처리 (리사이즈, 회색, INT8 변환)
   ↓
4. 추론 (TFLite Interpreter)
   ↓
5. 후처리 (역양자화, 클러스터링, 좌표 변환)
   ↓
6. Blur 적용
   ↓
7. 결과 표시
```

### 파일별 역할

| 파일 | 역할 |
|------|------|
| `config.py` | 모델 경로, 상수 설정 |
| `model.py` | 전처리, 추론, 후처리 구현 |
| `image_utils.py` | blur 적용, 이미지 표시 |
| `app.py` | GUI (tkinter) |

### 핵심 코드

**전처리:**
```python
# model.py의 preprocess() 함수
def preprocess(self, pil_image):
    resized = pil_image.resize((64, 64), Image.LANCZOS)
    gray = resized.convert("L")
    arr = np.array(gray, dtype=np.uint8)
    int8 = (arr.astype(np.int16) - 128).astype(np.int8)
    return int8.reshape(1, 64, 64, 1)
```

**추론:**
```python
# model.py의 run_inference() 함수
def run_inference(self, input_tensor):
    self.interpreter.set_tensor(self.inp["index"], input_tensor)
    self.interpreter.invoke()
    raw = self.interpreter.get_tensor(self.out["index"])
    face_probs = (raw[0,:,:,1].astype(np.float32) + 128) / 256
    return face_probs
```

**후처리:**
```python
# model.py의 _find_face_clusters() 함수
# BFS 클러스터링 구현
```

**Blur:**
```python
# image_utils.py의 apply_blur() 함수
def apply_blur(image, blur_boxes, blur_radius=None):
    result = image.copy()
    for x1, y1, x2, y2 in blur_boxes:
        region = result.crop((x1, y1, x2, y2))
        radius = blur_radius or max(10, min(x2-x1, y2-y1) // 5)
        blurred = region.filter(ImageFilter.GaussianBlur(radius=radius))
        result.paste(blurred, (x1, y1))
    return result
```

---

## Pico 구현 시 참고사항

### 메모리 제약

| 항목 | 크기 | 비고 |
|------|-----|------|
| 모델 바이너리 | 39.9 KB | PROGMEM에 저장 |
| 입력 버퍼 | 64×64×1 = 4 KB | RAM |
| 출력 버퍼 | 8×8×2 = 128 bytes | RAM |
| 텐서 arena | ~135 KB | Pico에서 충분 |

### 성능 예상

| 단계 | 예상 시간 |
|------|---------|
| 전처리 | 5-10 ms |
| 추론 | 10-30 ms |
| 후처리 | <1 ms |
| **총합** | **15-40 ms** |

→ **충분히 실시간 처리 가능**

### 카메라 입력

**Pico에서 사용 가능한 카메라:**
- OV7670 (VGA, 640×480)
- OV2640 (200만 화소)
- 기타 UVC 웹캠

**처리 흐름:**
```
카메라 → 640×480 RGB
    ↓
선택적 : 이미지 중앙 224×224 crop (비율 유지)
    ↓
64×64 리사이즈
    ↓
Grayscale 변환
    ↓
INT8 변환
    ↓
추론
```

### C/C++ 구현 팁

**1. 고정 크기 배열 사용 (malloc 피하기)**
```cpp
int8_t input_data[1*64*64*1];  // 스택 할당
int8_t output_data[1*8*8*2];
```

**2. 정수 연산만 사용 (부동소수점 피하기)**
```cpp
// ❌ 피하기
float prob = (int8_val + 128.0f) / 256.0f;

// ✅ 권장 (고정소수점)
uint8_t prob_x256 = (int8_val + 128);  // 0-256 스케일
if (prob_x256 >= 128) {  // 0.5 threshold
    // 얼굴 감지
}
```

**3. 빠른 리사이징 (nearest-neighbor)**
```cpp
// 정확한 LANCZOS는 무거움, nearest-neighbor 사용
for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
        int src_x = (x * width) >> 6;   // 곱셈 대신 시프트
        int src_y = (y * height) >> 6;
        input[y*64+x] = src[(src_y * width + src_x) * 3 + 0];
    }
}
```

**4. PROGMEM에 모델 저장 (RAM 절약)**
```cpp
#include "model_data.h"  // xxd로 변환한 헤더

extern const unsigned char tflite_model[];
extern const int tflite_model_len;

// PROGMEM에서 직접 로드
const tflite::Model* model = tflite::GetModel(tflite_model);
```

---

## 트러블슈팅

### PC 앱

| 문제 | 원인 | 해결법 |
|------|------|--------|
| 모델 로드 실패 | 경로 오류 | config.py의 MODEL_PATH 확인 (절대경로 사용) |
| `AssertionError: 입력 shape 오류` | config.py 수정 누락 | INPUT_SIZE 재확인 (이 모델은 64×64) |
| 감지 안됨 | threshold 너무 높음 | threshold 0.3~0.4로 낮춰보기 |
| 오탐이 많음 | threshold 너무 낮음 | threshold 0.6~0.7로 올려보기 |
| `UnicodeEncodeError` | Windows CMD 인코딩 | PowerShell 또는 WSL 사용 |

### Pico 구현

| 문제 | 원인 | 해결법 |
|------|------|--------|
| 메모리 부족 | tensor arena 크기 초과 | 135KB 이상 할당, 다른 전역 변수 축소 |
| 추론 느림 | 카메라 처리 블로킹 | 멀티스레드 또는 interrupt 사용 |
| 부정확한 위치 | 리사이징 방식 차이 | LANCZOS 대신 nearest-neighbor 사용 가능 |
| 비정상 출력값 | INT8 변환 오류 | int16 경유 확인, zero_point (-128) 확인 |

---

## 참고 문서

- **Edge Impulse 공식 문서:** https://edgeimpulse.com/
- **TensorFlow Lite 문서:** https://www.tensorflow.org/lite
- **TensorFlow Lite Micro (Pico):** https://github.com/tensorflow/tflite-micro
- **Pico RP2040 SDK:** https://github.com/raspberrypi/pico-sdk

---

## 버전 이력

| 버전 | 날짜 | 변경사항 |
|------|------|---------|
| 1.0 | 2026-04-12 | 초판 (PC 앱 + Pico 참고사항) |

---

**문서 작성자:** Claude Code  
**최종 검증:** 2026-04-12  
**다음 검토:** 모델 업데이트 시
