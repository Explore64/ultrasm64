#include <ultra64.h>
#include <hvqm2dec.h>

#include "buffers.h"
#ifdef HVQM
#include <hvqm/hvqm.h>
#endif
#include "config.h"
#include "audio/data.h"

ALIGNED8 u8 gDecompressionHeap[0xD000];
#if defined(VERSION_EU)
ALIGNED16 u8 gAudioHeap[DOUBLE_SIZE_ON_64_BIT(AUDIO_HEAP_SIZE) - 0x3800];
#elif defined(VERSION_SH)
ALIGNED16 u8 gAudioHeap[DOUBLE_SIZE_ON_64_BIT(AUDIO_HEAP_SIZE) - 0x4800];
#else
ALIGNED16 u8 gAudioHeap[DOUBLE_SIZE_ON_64_BIT(AUDIO_HEAP_SIZE)];
#endif

ALIGNED8 u8 gIdleThreadStack[THREAD1_STACK];
ALIGNED8 u8 gThread3Stack[THREAD3_STACK];
ALIGNED8 u8 gThread4Stack[THREAD4_STACK];
ALIGNED8 u8 gThread5Stack[THREAD5_STACK];
#if ENABLE_RUMBLE
ALIGNED8 u8 gThread6Stack[THREAD6_STACK];
#endif
ALIGNED8 u8 gThread9Stack[THREAD9_STACK];
// 0x400 bytes
ALIGNED8 u8 gGfxSPTaskStack[SP_DRAM_STACK_SIZE8];
// 0xc00 bytes for f3dex, 0x900 otherwise
ALIGNED8 u8 gGfxSPTaskYieldBuffer[OS_YIELD_DATA_SIZE];
// 0x200 bytes
struct SaveBuffer __attribute__ ((aligned (8))) gSaveBuffer;
// 0x190a0 bytes
struct GfxPool gGfxPools[2];
