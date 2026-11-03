#include <stdint.h>
typedef int32_t FSAClientHandle;
typedef struct { uint32_t flags; } FSAStat;
#define FS_ERROR_OK 0
#define FS_ERROR_NOT_FOUND (-0x30017)
#define FS_STAT_DIRECTORY 0x80000000u
int32_t Mocha_InitLibrary(void);
int32_t Mocha_DeInitLibrary(void);
int32_t Mocha_UnlockFSClientEx(int32_t);
int32_t FSAInit(void);
FSAClientHandle FSAAddClient(void *);
int32_t FSADelClient(FSAClientHandle);
int32_t FSAGetStat(FSAClientHandle, const char *, FSAStat *);
