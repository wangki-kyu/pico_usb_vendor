#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

// ============== 포트 모드 설정 (추가) ==============
// 0번 포트를 Device 모드로 사용하겠다는 설정입니다.
#define CFG_TUSB_RHPORT0_MODE      (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// ============== Device 설정 ==============
#define CFG_TUD_ENABLED                 1
#define CFG_TUSB_DEVICE_CONTROL_EP0_SIZE    64

// Vendor 클래스 활성화
#define CFG_TUD_VENDOR                      1

// Vendor 끝점 크기
#define CFG_TUD_VENDOR_EP_BUFSIZE           64

// 다른 클래스는 비활성화
#define CFG_TUD_CDC                         0
#define CFG_TUD_MSC                         0
#define CFG_TUD_HID                         0
#define CFG_TUD_AUDIO                       0
#define CFG_TUD_MIDI                        0
#define CFG_TUD_UVC                         0

#ifdef __cplusplus
}
#endif

#endif
