#ifndef ASSISTANT_TEST_PLATFORM_H
#define ASSISTANT_TEST_PLATFORM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef int VPADChan;
typedef int VPADReadError;
typedef int KPADChan;
typedef int KPADError;
enum { VPAD_CHAN_0, WPAD_CHAN_0 = 0, WPAD_CHAN_3 = 3, VPAD_READ_SUCCESS = 0,
    VPAD_VALID = 0, KPAD_ERROR_OK = 0, VPAD_TP_854X480 = 2, OS_SHAREDDATATYPE_FONT_STANDARD = 0 };
enum { VPAD_BUTTON_A = 1, VPAD_BUTTON_B = 2, VPAD_BUTTON_X = 4, VPAD_BUTTON_UP = 8,
    VPAD_BUTTON_DOWN = 16, VPAD_BUTTON_LEFT = 32, VPAD_BUTTON_RIGHT = 64, VPAD_BUTTON_Y = 128 };
enum { WPAD_BUTTON_A = 1, WPAD_BUTTON_B = 2, WPAD_BUTTON_1 = 4, WPAD_BUTTON_UP = 8,
    WPAD_BUTTON_DOWN = 16, WPAD_BUTTON_LEFT = 32, WPAD_BUTTON_RIGHT = 64, WPAD_BUTTON_2 = 128 };
enum { WPAD_CLASSIC_BUTTON_A = 1, WPAD_CLASSIC_BUTTON_B = 2, WPAD_CLASSIC_BUTTON_X = 4,
    WPAD_CLASSIC_BUTTON_UP = 8, WPAD_CLASSIC_BUTTON_DOWN = 16, WPAD_CLASSIC_BUTTON_LEFT = 32, WPAD_CLASSIC_BUTTON_RIGHT = 64, WPAD_CLASSIC_BUTTON_Y = 128 };
enum { WPAD_PRO_BUTTON_A = 1, WPAD_PRO_BUTTON_B = 2, WPAD_PRO_BUTTON_X = 4,
    WPAD_PRO_BUTTON_UP = 8, WPAD_PRO_BUTTON_DOWN = 16, WPAD_PRO_BUTTON_LEFT = 32, WPAD_PRO_BUTTON_RIGHT = 64, WPAD_PRO_BUTTON_Y = 128 };
enum { WPAD_EXT_CLASSIC = 2, WPAD_EXT_MPLUS_CLASSIC = 7, WPAD_EXT_PRO_CONTROLLER = 31 };
typedef struct { uint16_t x, y, touched, validity; } VPADTouchData;
typedef struct { uint32_t trigger; VPADTouchData tpNormal; } VPADStatus;
typedef struct { uint32_t trigger; int extensionType; struct { uint32_t trigger; } classic, pro; } KPADStatus;
typedef int MCPError;
typedef uint32_t MCPAppType;
typedef uint32_t MCPDeviceType;
typedef struct __attribute__((packed)) { uint64_t titleId; uint32_t groupId; char path[56]; int appType;
    uint16_t titleVersion; uint64_t osVersion; uint32_t sdkVersion; char indexedDevice[10]; uint8_t unknown; } MCPTitleListType;

#ifdef __cplusplus
extern "C" {
#endif
int VPADRead(VPADChan chan, VPADStatus *status, uint32_t count, VPADReadError *error);
int KPADReadEx(KPADChan chan, KPADStatus *status, uint32_t count, KPADError *error);
void VPADGetTPCalibratedPointEx(VPADChan chan, int resolution, VPADTouchData *result, const VPADTouchData *raw);
void OSGetSharedData(int type, int flags, void **data, uint32_t *size);
void *OSAllocFromSystem(size_t size, int alignment);
void OSFreeToSystem(void *pointer);
MCPError MCP_Open(void);
MCPError MCP_Close(int handle);
MCPError MCP_TitleCount(int handle);
MCPError MCP_TitleList(int handle, uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
MCPError MCP_TitleListByAppType(int handle, MCPAppType app_type, uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
MCPError MCP_TitleListByDeviceType(int handle, MCPDeviceType device_type, uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
MCPError MCP_TitleListByDevice(int handle, const char *device, uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
#ifdef __cplusplus
}
#endif
#endif
