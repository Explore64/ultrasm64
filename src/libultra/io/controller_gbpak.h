#ifndef _CONTROLLER_GBPAK_H
#define _CONTROLLER_GBPAK_H

extern OSTimer __osGbpakTimer;
extern OSMesg __osGbpakTimerMsg;
extern OSMesgQueue __osGbpakTimerQ;

s32 __osGbpakSetBank(OSPfs* pfs, u8 bank);

#endif
