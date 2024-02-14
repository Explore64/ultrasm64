#ifndef VERSION_SH
#include <ultra64.h>
#include <PR/os.h>

#include "data.h"
#include "external.h"
#include "heap.h"
#include "load.h"
#include "seqplayer.h"

#define ALIGN16(val) (((val) + 0xF) & ~0xF)

struct SharedDma {
    /*0x0*/ u8 *buffer;       // target, points to pre-allocated buffer
    /*0x4*/ uintptr_t source; // device address
    /*0x8*/ u32 bufSize;      // size of buffer (converted from u16 for intentional padding to size 0x10)
    /*0xC*/ u8 reuseIndex;    // position in sSampleDmaReuseQueue1/2, if ttl == 0
    /*   */ // u8 pad[3];
};                            // size = 0x10

// EU only
void port_eu_init(void);

struct Note *gNotes;

struct SequencePlayer gSequencePlayers[SEQUENCE_PLAYERS];
struct SequenceChannel gSequenceChannels[SEQUENCE_CHANNELS];
struct SequenceChannelLayer gSequenceLayers[SEQUENCE_LAYERS];

struct SequenceChannel gSequenceChannelNone;
struct AudioListItem gLayerFreeList;
struct NotePool gNoteFreeLists;

OSMesgQueue gCurrAudioFrameDmaQueue;
OSMesg gCurrAudioFrameDmaMesgBufs[AUDIO_FRAME_DMA_QUEUE_SIZE];
OSIoMesg gCurrAudioFrameDmaIoMesgBufs[AUDIO_FRAME_DMA_QUEUE_SIZE];

OSMesgQueue gAudioDmaMesgQueue;
OSMesg gAudioDmaMesg;
OSIoMesg gAudioDmaIoMesg;

struct SharedDma sSampleDmas[0x60];
u8 sSampleTTLs[0x60 * 4];
u32 gSampleDmaNumListItems; // sh: 0x803503D4
u32 sSampleDmaListSize1; // sh: 0x803503D8

// Circular buffer of DMAs with ttl = 0. tail <= head, wrapping around mod 256.
u8 sSampleDmaReuseQueue1[256];
u8 sSampleDmaReuseQueue2[256];
u8 sSampleDmaReuseQueueTail1;
u8 sSampleDmaReuseQueueTail2;
u8 sSampleDmaReuseQueueHead1; // sh: 0x803505E2
u8 sSampleDmaReuseQueueHead2; // sh: 0x803505E3

// bss correct up to here

ALSeqFile *gSeqFileHeader;
ALSeqFile *gAlCtlHeader;
ALSeqFile *gAlTbl;
u8 *gAlBankSets;
u16 gSequenceCount;

struct CtlEntry *gCtlEntries; // sh: 0x803505F8

#if defined(VERSION_EU)
u32 padEuBss1;
struct AudioBufferParametersEU gAudioBufferParameters;
#elif defined(VERSION_US) || defined(VERSION_JP)
s32 gAiFrequency;
#endif

s32 gMaxAudioCmds;
s32 gMaxSimultaneousNotes;

#if defined(VERSION_EU)
s16 gTempoInternalToExternal;
#else
s32 gSamplesPerFrameTarget;
s32 gMinAiBufferLength;

s16 gTempoInternalToExternal;

s8 gAudioUpdatesPerFrame;
#endif

s8 gSoundMode;

#if defined(VERSION_EU)
s8 gAudioUpdatesPerFrame;
#endif

extern u64 gAudioGlobalsStartMarker;
extern u64 gAudioGlobalsEndMarker;

extern u8 gSoundDataADSR[]; // sound_data.ctl
extern u8 gSoundDataRaw[];  // sound_data.tbl
extern u8 gMusicData[];     // sequences.s
extern u8 gBankSetsData[];  // bank_sets.s

ALSeqFile *get_audio_file_header(s32 arg0);

/**
 * Performs an immediate DMA copy
 */
void audio_dma_copy_immediate(uintptr_t devAddr, void *vAddr, size_t nbytes) {
    osInvalDCache(vAddr, nbytes);
    osPiStartDma(&gAudioDmaIoMesg, OS_MESG_PRI_HIGH, OS_READ, devAddr, vAddr, nbytes,
                 &gAudioDmaMesgQueue);
    osRecvMesg(&gAudioDmaMesgQueue, NULL, OS_MESG_BLOCK);
}

#ifdef VERSION_EU
u8 audioString34[] = "CAUTION:WAVE CACHE FULL %d";
u8 audioString35[] = "BASE %x %x\n";
u8 audioString36[] = "LOAD %x %x %x\n";
u8 audioString37[] = "INSTTOP    %x\n";
u8 audioString38[] = "INSTMAP[0] %x\n";
u8 audioString39[] = "already flags %d\n";
u8 audioString40[] = "already flags %d\n";
u8 audioString41[] = "ERR:SLOW BANK DMA BUSY\n";
u8 audioString42[] = "ERR:SLOW DMA BUSY\n";
u8 audioString43[] = "Check %d  bank %d\n";
u8 audioString44[] = "Cache Check\n";
u8 audioString45[] = "NO BANK ERROR\n";
u8 audioString46[] = "BANK %d LOADING START\n";
u8 audioString47[] = "BANK %d LOAD MISS (NO MEMORY)!\n";
u8 audioString48[] = "BANK %d ALREADY CACHED\n";
u8 audioString49[] = "BANK LOAD MISS! FOR %d\n";
#endif

/**
 * Performs an asynchronus (normal priority) DMA copy
 */
void audio_dma_copy_async(uintptr_t devAddr, void *vAddr, size_t nbytes, OSMesgQueue *queue, OSIoMesg *mesg) {
    osInvalDCache(vAddr, nbytes);
    osPiStartDma(mesg, OS_MESG_PRI_NORMAL, OS_READ, devAddr, vAddr, nbytes, queue);
}

/**
 * Performs a partial asynchronous (normal priority) DMA copy. This is limited
 * to 0x1000 bytes transfer at once.
 */
void audio_dma_partial_copy_async(uintptr_t *devAddr, u8 **vAddr, ssize_t *remaining, OSMesgQueue *queue, OSIoMesg *mesg) {
#if defined(VERSION_EU)
    ssize_t transfer = (*remaining >= 0x1000 ? 0x1000 : *remaining);
#else
    ssize_t transfer = (*remaining < 0x1000 ? *remaining : 0x1000);
#endif
    *remaining -= transfer;
    osInvalDCache(*vAddr, transfer);
    osPiStartDma(mesg, OS_MESG_PRI_NORMAL, OS_READ, *devAddr, *vAddr, transfer, queue);
    *devAddr += transfer;
    *vAddr += transfer;
}

void decrease_sample_dma_ttls() {
    u32 i;

    for (i = 0; i < sSampleDmaListSize1; i++) {
        if (sSampleTTLs[i] != 0) {
            sSampleTTLs[i]--;
            if (sSampleTTLs[i] == 0) {
                sSampleDmas[i].reuseIndex = sSampleDmaReuseQueueHead1;
                sSampleDmaReuseQueue1[sSampleDmaReuseQueueHead1++] = (u8) i;
            }
        }
    }

    for (i = sSampleDmaListSize1; i < gSampleDmaNumListItems; i++) {
        if (sSampleTTLs[i] != 0) {
            sSampleTTLs[i]--;
            if (sSampleTTLs[i] == 0) {
                sSampleDmas[i].reuseIndex = sSampleDmaReuseQueueHead2;
                sSampleDmaReuseQueue2[sSampleDmaReuseQueueHead2++] = (u8) i;
            }
        }
    }
}

void *dma_sample_data(uintptr_t devAddr, u32 size, s32 arg2, u8 *dmaIndexRef) {
    s32 hasDma = FALSE;
    struct SharedDma *dma;
    uintptr_t dmaDevAddr;
    u32 transfer;
    u32 i;
    u32 dmaIndex;
    ssize_t bufferPos;

    if (arg2 != 0 || *dmaIndexRef >= sSampleDmaListSize1) {
        for (i = sSampleDmaListSize1; i < gSampleDmaNumListItems; i++) {
#if defined(VERSION_EU)
            dma = &sSampleDmas[i];
#else
            dma = sSampleDmas + i;
#endif
            bufferPos = devAddr - dma->source;
            if (0 <= bufferPos && (size_t) bufferPos <= dma->bufSize - size) {
                // We already have a DMA request for this memory range.
                if (sSampleTTLs[i] == 0 && sSampleDmaReuseQueueTail2 != sSampleDmaReuseQueueHead2) {
                    // Move the DMA out of the reuse queue, by swapping it with the
                    // tail, and then incrementing the tail.
                    if (dma->reuseIndex != sSampleDmaReuseQueueTail2) {
                        sSampleDmaReuseQueue2[dma->reuseIndex] =
                            sSampleDmaReuseQueue2[sSampleDmaReuseQueueTail2];
                        sSampleDmas[sSampleDmaReuseQueue2[sSampleDmaReuseQueueTail2]].reuseIndex =
                            dma->reuseIndex;
                    }
                    sSampleDmaReuseQueueTail2++;
                }
                sSampleTTLs[i] = 60;
                *dmaIndexRef = (u8) i;
#if defined(VERSION_EU)
                return &dma->buffer[(devAddr - dma->source)];
#else
                return (devAddr - dma->source) + dma->buffer;
#endif
            }
        }

        if (sSampleDmaReuseQueueTail2 != sSampleDmaReuseQueueHead2 && arg2 != 0) {
            // Allocate a DMA from reuse queue 2. This queue can be empty, since
            // TTL 60 is pretty large.
            dmaIndex = sSampleDmaReuseQueue2[sSampleDmaReuseQueueTail2];
            sSampleDmaReuseQueueTail2++;
            dma = sSampleDmas + dmaIndex;
            sSampleTTLs[dmaIndex] = 2;
            hasDma = TRUE;
        }
    } else {
#if defined(VERSION_EU)
        dma = sSampleDmas;
        dma += *dmaIndexRef;
#else
        dma = sSampleDmas + *dmaIndexRef;
#endif
        bufferPos = devAddr - dma->source;
        if (0 <= bufferPos && (size_t) bufferPos <= dma->bufSize - size) {
            // We already have DMA for this memory range.
            if (sSampleTTLs[*dmaIndexRef] == 0) {
                // Move the DMA out of the reuse queue, by swapping it with the
                // tail, and then incrementing the tail.
                if (dma->reuseIndex != sSampleDmaReuseQueueTail1) {
#if defined(VERSION_EU)
                    if (1) {
                    }
#endif
                    sSampleDmaReuseQueue1[dma->reuseIndex] =
                        sSampleDmaReuseQueue1[sSampleDmaReuseQueueTail1];
                    sSampleDmas[sSampleDmaReuseQueue1[sSampleDmaReuseQueueTail1]].reuseIndex =
                        dma->reuseIndex;
                }
                sSampleDmaReuseQueueTail1++;
            }
            sSampleTTLs[*dmaIndexRef] = 2;
#if defined(VERSION_EU)
            return dma->buffer + (devAddr - dma->source);
#else
            return (devAddr - dma->source) + dma->buffer;
#endif
        }
    }

    if (!hasDma) {
        // Allocate a DMA from reuse queue 1. This queue will hopefully never
        // be empty, since TTL 2 is so small.
        dmaIndex = sSampleDmaReuseQueue1[sSampleDmaReuseQueueTail1++];
        dma = sSampleDmas + dmaIndex;
        sSampleTTLs[dmaIndex] = 2;
        hasDma = TRUE;
    }

    transfer = dma->bufSize;
    dmaDevAddr = devAddr & ~0xF;
    dma->source = dmaDevAddr;
#ifdef VERSION_US
    //osInvalDCache(dma->buffer, transfer);
#endif
#if defined(VERSION_EU)
    osPiStartDma(&gCurrAudioFrameDmaIoMesgBufs[gCurrAudioFrameDmaCount++], OS_MESG_PRI_NORMAL,
                     OS_READ, dmaDevAddr, dma->buffer, transfer, &gCurrAudioFrameDmaQueue);
    *dmaIndexRef = dmaIndex;
    return (devAddr - dmaDevAddr) + dma->buffer;
#else
    gCurrAudioFrameDmaCount++;
    osPiStartDma(&gCurrAudioFrameDmaIoMesgBufs[gCurrAudioFrameDmaCount - 1], OS_MESG_PRI_NORMAL,
                 OS_READ, dmaDevAddr, dma->buffer, transfer, &gCurrAudioFrameDmaQueue);
    *dmaIndexRef = dmaIndex;
    return dma->buffer + (devAddr - dmaDevAddr);
#endif
}


void init_sample_dma_buffers(void) {
    s32 i;
    s32 sDmaBufSize;

#if defined(VERSION_EU)
    sDmaBufSize = 0x400;
#else
    sDmaBufSize = 144 * 9;
#endif

#if defined(VERSION_EU)
    for (i = 0; i < gMaxSimultaneousNotes * 3 * gAudioBufferParameters.presetUnk4; i++)
#else
    for (i = 0; i < gMaxSimultaneousNotes * 3; i++)
#endif
    {
        sSampleDmas[gSampleDmaNumListItems].buffer = soundAlloc(&gNotesAndBuffersPool, sDmaBufSize);
        if (sSampleDmas[gSampleDmaNumListItems].buffer == NULL) {
            break;
        }
        sSampleDmas[gSampleDmaNumListItems].bufSize = sDmaBufSize;
        sSampleDmas[gSampleDmaNumListItems].source = 0;
        sSampleTTLs[gSampleDmaNumListItems] = 0;
        gSampleDmaNumListItems++;
    }

    for (i = 0; (u32) i < gSampleDmaNumListItems; i++) {
        sSampleDmaReuseQueue1[i] = (u8) i;
        sSampleDmas[i].reuseIndex = (u8) i;
    }

    for (i = gSampleDmaNumListItems; i < ARRAY_COUNT(sSampleDmaReuseQueue1); i++) {
        sSampleDmaReuseQueue1[i] = 0;
    }

    sSampleDmaReuseQueueTail1 = 0;
    sSampleDmaReuseQueueHead1 = (u8) gSampleDmaNumListItems;
    sSampleDmaListSize1 = gSampleDmaNumListItems;

#if defined(VERSION_EU)
    sDmaBufSize = 0x200;
#else
    sDmaBufSize = 160 * 9;
#endif
    for (i = 0; i < gMaxSimultaneousNotes; i++) {
        sSampleDmas[gSampleDmaNumListItems].buffer = soundAlloc(&gNotesAndBuffersPool, sDmaBufSize);
        if (sSampleDmas[gSampleDmaNumListItems].buffer == NULL) {
            break;
        }
        sSampleDmas[gSampleDmaNumListItems].bufSize = sDmaBufSize;
        sSampleDmas[gSampleDmaNumListItems].source = 0;
        sSampleTTLs[gSampleDmaNumListItems] = 0;
        gSampleDmaNumListItems++;
    }

    for (i = sSampleDmaListSize1; (u32) i < gSampleDmaNumListItems; i++) {
        sSampleDmaReuseQueue2[i - sSampleDmaListSize1] = (u8) i;
        sSampleDmas[i].reuseIndex = (u8)(i - sSampleDmaListSize1);
    }

    // This probably meant to touch the range size1..size2 as well... but it
    // doesn't matter, since these values are never read anyway.
    for (i = gSampleDmaNumListItems; i < ARRAY_COUNT(sSampleDmaReuseQueue2); i++) {
        sSampleDmaReuseQueue2[i] = sSampleDmaListSize1;
    }

    sSampleDmaReuseQueueTail2 = 0;
    sSampleDmaReuseQueueHead2 = gSampleDmaNumListItems - sSampleDmaListSize1;
}

#if defined(VERSION_JP) || defined(VERSION_US)
#else
void patch_sound(UNUSED struct AudioBankSound *sound, UNUSED u8 *memBase, UNUSED u8 *offsetBase) {
    struct AudioBankSample *sample;
    void *patched;
    UNUSED u8 *mem; // unused on US

#define PATCH(x, base) (patched = (void *)((uintptr_t) (x) + (uintptr_t) base))

    if (sound->sample != NULL) {
        sample = sound->sample = PATCH(sound->sample, memBase);
        if (sample->loaded == 0) {
            sample->sampleAddr = PATCH(sample->sampleAddr, offsetBase);
            sample->loop = PATCH(sample->loop, memBase);
            sample->book = PATCH(sample->book, memBase);
            sample->loaded = 1;
        }
#if defined(VERSION_EU)
        else if (sample->loaded == 0x80) {
            PATCH(sample->sampleAddr, offsetBase);
            mem = soundAlloc(&gNotesAndBuffersPool, sample->sampleSize);
            if (mem == NULL) {
                sample->sampleAddr = patched;
                sample->loaded = 1;
            } else {
                audio_dma_copy_immediate((uintptr_t) patched, mem, sample->sampleSize);
                sample->loaded = 0x81;
                sample->sampleAddr = mem;
            }
            sample->loop = PATCH(sample->loop, memBase);
            sample->book = PATCH(sample->book, memBase);
        }
#endif
    }

#undef PATCH
}
#endif

#ifdef VERSION_EU
#define PATCH_SOUND patch_sound
#else
// copt inline of the above
#define PATCH_SOUND(_sound, mem, offset)                                                  \
{                                                                                         \
    struct AudioBankSound *sound = _sound;                                                \
    struct AudioBankSample *sample;                                                       \
    void *patched;                                                                        \
    if ((*sound).sample != (void *) 0)                                                    \
    {                                                                                     \
        patched = (void *)(((uintptr_t)(*sound).sample) + ((uintptr_t)((u8 *) mem)));     \
        (*sound).sample = patched;                                                        \
        sample = (*sound).sample;                                                         \
        if ((*sample).loaded == 0)                                                        \
        {                                                                                 \
            patched = (void *)(((uintptr_t)(*sample).sampleAddr) + ((uintptr_t) offset)); \
            (*sample).sampleAddr = patched;                                               \
            patched = (void *)(((uintptr_t)(*sample).loop) + ((uintptr_t)((u8 *) mem)));  \
            (*sample).loop = patched;                                                     \
            patched = (void *)(((uintptr_t)(*sample).book) + ((uintptr_t)((u8 *) mem)));  \
            (*sample).book = patched;                                                     \
            (*sample).loaded = 1;                                                         \
        }                                                                                 \
    }                                                                                     \
}
#endif

// on US/JP this inlines patch_sound, using some -sopt compiler flag
void patch_audio_bank(struct AudioBank *mem, u8 *offset, u32 numInstruments, u32 numDrums) {
    struct Instrument *instrument;
    struct Instrument **itInstrs;
    struct Instrument **end;
    struct AudioBank *temp;
    u32 i;
    void *patched;
    struct Drum *drum;
    struct Drum **drums;
#if defined(VERSION_EU)
    u32 numDrums2;
#endif

#define BASE_OFFSET_REAL(x, base) (void *)((uintptr_t) (x) + (uintptr_t) base)
#define PATCH(x, base) (patched = BASE_OFFSET_REAL(x, base))
#define PATCH_MEM(x) x = PATCH(x, mem)

#if defined(VERSION_JP) || defined(VERSION_US)
#define BASE_OFFSET(x, base) BASE_OFFSET_REAL(x, base)
#else
#define BASE_OFFSET(x, base) BASE_OFFSET_REAL(base, x)
#endif

    drums = mem->drums;
#if defined(VERSION_JP) || defined(VERSION_US)
    if (drums != NULL && numDrums > 0) {
        mem->drums = (void *)((uintptr_t) drums + (uintptr_t) mem);
        if (numDrums > 0) //! unneeded when -sopt is enabled
        for (i = 0; i < numDrums; i++) {
#else
    numDrums2 = numDrums;
    if (drums != NULL && numDrums2 > 0) {
        mem->drums = PATCH(drums, mem);
        for (i = 0; i < numDrums2; i++) {
#endif
            patched = mem->drums[i];
            if (patched != NULL) {
                drum = PATCH(patched, mem);
                mem->drums[i] = drum;
                if (drum->loaded == 0) {
#if defined(VERSION_JP) || defined(VERSION_US)
                    //! copt replaces drum with 'patched' for these two lines
                    PATCH_SOUND(&(*(struct Drum *)patched).sound, mem, offset);
                    patched = (*(struct Drum *)patched).envelope;
#else
                    patch_sound(&drum->sound, (u8 *) mem, offset);
                    patched = drum->envelope;
#endif
                    drum->envelope = BASE_OFFSET(mem, patched);
                    drum->loaded = 1;
                }

            }
        }
    }

    //! Doesn't affect EU, but required for US/JP
    temp = &*mem;
#if defined(VERSION_JP) || defined(VERSION_US)
    if (numInstruments >= 1)
#endif
    if (numInstruments > 0) {
        //! Doesn't affect EU, but required for US/JP
        struct Instrument **tempInst;
        itInstrs = temp->instruments;
        tempInst = temp->instruments;
        end = numInstruments + tempInst;

#if defined(VERSION_JP) || defined(VERSION_US)
l2:
#else
        do {
#endif
            if (*itInstrs != NULL) {
                *itInstrs = BASE_OFFSET(*itInstrs, mem);
                instrument = *itInstrs;

                if (instrument->loaded == 0) {
                    PATCH_SOUND(&instrument->lowNotesSound, (u8 *) mem, offset);
                    PATCH_SOUND(&instrument->normalNotesSound, (u8 *) mem, offset);
                    PATCH_SOUND(&instrument->highNotesSound, (u8 *) mem, offset);
                    patched = instrument->envelope;
                    instrument->envelope = BASE_OFFSET(mem, patched);
                    instrument->loaded = 1;
                }
            }
            itInstrs++;
#if defined(VERSION_JP) || defined(VERSION_US)
            //! goto generated by copt, required to match US/JP
            if (end != itInstrs) {
                goto l2;
            }
#else
        } while (end != itInstrs);
#endif
    }
#undef PATCH_MEM
#undef PATCH
#undef BASE_OFFSET_REAL
#undef BASE_OFFSET
#undef PATCH_SOUND
}

struct AudioBank *bank_load_immediate(s32 bankId, s32 arg1) {
    u32 buf[4];
    u32 numInstruments, numDrums;
    struct AudioBank *ret;
    u8 *ctlData;
    s32 alloc;

    // (This is broken if the length is 1 (mod 16), but that never happens --
    // it's always divisible by 4.)
    alloc = gAlCtlHeader->seqArray[bankId].len + 0xf;
    alloc = ALIGN16(alloc);
    alloc -= 0x10;
    ctlData = gAlCtlHeader->seqArray[bankId].offset;
    ret = alloc_bank_or_seq(&gBankLoadedPool, 1, alloc, arg1, bankId);
    if (ret == NULL) {
        return NULL;
    }

    audio_dma_copy_immediate((uintptr_t) ctlData, buf, 0x10);
    numInstruments = buf[0];
    numDrums = buf[1];
    audio_dma_copy_immediate((uintptr_t)(ctlData + 0x10), ret, alloc);
    patch_audio_bank(ret, gAlTbl->seqArray[bankId].offset, numInstruments, numDrums);
    gCtlEntries[bankId].numInstruments = (u8) numInstruments;
    gCtlEntries[bankId].numDrums = (u8) numDrums;
    gCtlEntries[bankId].instruments = ret->instruments;
    gCtlEntries[bankId].drums = ret->drums;
    gBankLoadStatus[bankId] = SOUND_LOAD_STATUS_COMPLETE;
    return ret;
}

struct AudioBank *bank_load_async(s32 bankId, s32 arg1, struct SequencePlayer *seqPlayer) {
    u32 numInstruments, numDrums;
    u32 buf[4];
    size_t alloc;
    struct AudioBank *ret;
    u8 *ctlData;
    OSMesgQueue *mesgQueue;

    alloc = gAlCtlHeader->seqArray[bankId].len + 0xf;
    alloc = ALIGN16(alloc);
    alloc -= 0x10;
    ctlData = gAlCtlHeader->seqArray[bankId].offset;
    ret = alloc_bank_or_seq(&gBankLoadedPool, 1, alloc, arg1, bankId);
    if (ret == NULL) {
        return NULL;
    }

    audio_dma_copy_immediate((uintptr_t) ctlData, buf, 0x10);
    numInstruments = buf[0];
    numDrums = buf[1];
    seqPlayer->loadingBankId = (u8) bankId;
#if defined(VERSION_EU)
    gCtlEntries[bankId].numInstruments = numInstruments;
    gCtlEntries[bankId].numDrums = numDrums;
    gCtlEntries[bankId].instruments = ret->instruments;
    gCtlEntries[bankId].drums = 0;
    seqPlayer->bankDmaCurrMemAddr = (u8 *) ret;
    seqPlayer->bankDmaCurrDevAddr = (uintptr_t)(ctlData + 0x10);
    seqPlayer->bankDmaRemaining = alloc;
    if (1) {
    }
#else
    seqPlayer->loadingBankNumInstruments = numInstruments;
    seqPlayer->loadingBankNumDrums = numDrums;
    seqPlayer->bankDmaCurrMemAddr = (u8 *) ret;
    seqPlayer->loadingBank = ret;
    seqPlayer->bankDmaCurrDevAddr = (uintptr_t)(ctlData + 0x10);
    seqPlayer->bankDmaRemaining = alloc;
#endif
    mesgQueue = &seqPlayer->bankDmaMesgQueue;
    osCreateMesgQueue(mesgQueue, &seqPlayer->bankDmaMesg, 1);
#if defined(VERSION_JP) || defined(VERSION_US)
    seqPlayer->bankDmaMesg = NULL;
#endif
    seqPlayer->bankDmaInProgress = TRUE;
    audio_dma_partial_copy_async(&seqPlayer->bankDmaCurrDevAddr, &seqPlayer->bankDmaCurrMemAddr,
                                 &seqPlayer->bankDmaRemaining, mesgQueue, &seqPlayer->bankDmaIoMesg);
    gBankLoadStatus[bankId] = SOUND_LOAD_STATUS_IN_PROGRESS;
    return ret;
}

void *sequence_dma_immediate(s32 seqId, s32 arg1) {
    s32 seqLength;
    void *ptr;
    u8 *seqData;

    seqLength = gSeqFileHeader->seqArray[seqId].len + 0xf;
    seqLength = ALIGN16(seqLength);
    seqData = gSeqFileHeader->seqArray[seqId].offset;
    ptr = alloc_bank_or_seq(&gSeqLoadedPool, 1, seqLength, arg1, seqId);
    if (ptr == NULL) {
        return NULL;
    }

    audio_dma_copy_immediate((uintptr_t) seqData, ptr, seqLength);
    gSeqLoadStatus[seqId] = SOUND_LOAD_STATUS_COMPLETE;
    return ptr;
}

void *sequence_dma_async(s32 seqId, s32 arg1, struct SequencePlayer *seqPlayer) {
    s32 seqLength;
    void *ptr;
    u8 *seqData;
    OSMesgQueue *mesgQueue;

    seqLength = gSeqFileHeader->seqArray[seqId].len + 0xf;
    seqLength = ALIGN16(seqLength);
    seqData = gSeqFileHeader->seqArray[seqId].offset;
    ptr = alloc_bank_or_seq(&gSeqLoadedPool, 1, seqLength, arg1, seqId);
    if (ptr == NULL) {
        return NULL;
    }

    if (seqLength <= 0x40) {
        // Immediately load short sequenece
        audio_dma_copy_immediate((uintptr_t) seqData, ptr, seqLength);
        if (1) {
        }
        gSeqLoadStatus[seqId] = SOUND_LOAD_STATUS_COMPLETE;
    } else {
        audio_dma_copy_immediate((uintptr_t) seqData, ptr, 0x40);
        mesgQueue = &seqPlayer->seqDmaMesgQueue;
        osCreateMesgQueue(mesgQueue, &seqPlayer->seqDmaMesg, 1);
#if defined(VERSION_JP) || defined(VERSION_US)
        seqPlayer->seqDmaMesg = NULL;
#endif
        seqPlayer->seqDmaInProgress = TRUE;
        audio_dma_copy_async((uintptr_t)(seqData + 0x40), (u8 *) ptr + 0x40, seqLength - 0x40, mesgQueue,
                             &seqPlayer->seqDmaIoMesg);
        gSeqLoadStatus[seqId] = SOUND_LOAD_STATUS_IN_PROGRESS;
    }
    return ptr;
}

u8 get_missing_bank(u32 seqId, s32 *nonNullCount, s32 *nullCount) {
    void *temp;
    u32 bankId;
    u16 offset;
    u8 i;
    u8 ret;

    *nullCount = 0;
    *nonNullCount = 0;
#if defined(VERSION_EU)
    offset = ((u16 *) gAlBankSets)[seqId];
    for (i = gAlBankSets[offset++], ret = 0; i != 0; i--) {
        bankId = gAlBankSets[offset++];
#else
    offset = ((u16 *) gAlBankSets)[seqId] + 1;
    for (i = gAlBankSets[offset - 1], ret = 0; i != 0; i--) {
        offset++;
        bankId = gAlBankSets[offset - 1];
#endif

        if (IS_BANK_LOAD_COMPLETE(bankId) == TRUE) {
#if defined(VERSION_EU)
            temp = get_bank_or_seq(&gBankLoadedPool, 2, bankId);
#else
            temp = get_bank_or_seq(&gBankLoadedPool, 2, gAlBankSets[offset - 1]);
#endif
        } else {
            temp = NULL;
        }

        if (temp == NULL) {
            (*nullCount)++;
            ret = bankId;
        } else {
            (*nonNullCount)++;
        }
    }

    return ret;
}

struct AudioBank *load_banks_immediate(s32 seqId, u8 *outDefaultBank) {
    void *ret = NULL;
    u32 bankId = 0;
    u16 offset;
    u8 i;

    offset = ((u16 *) gAlBankSets)[seqId];
#ifdef VERSION_EU
    for (i = gAlBankSets[offset++]; i != 0; i--) {
        bankId = gAlBankSets[offset++];
#else
    offset++;
    for (i = gAlBankSets[offset - 1]; i != 0; i--) {
        offset++;
        bankId = gAlBankSets[offset - 1];
#endif

        if (IS_BANK_LOAD_COMPLETE(bankId) == TRUE) {
#ifdef VERSION_EU
            ret = get_bank_or_seq(&gBankLoadedPool, 2, bankId);
#else
            ret = get_bank_or_seq(&gBankLoadedPool, 2, gAlBankSets[offset - 1]);
#endif
        } else {
            ret = NULL;
        }

        if (ret == NULL) {
            ret = bank_load_immediate(bankId, 2);
        }
    }
    *outDefaultBank = bankId;
    return ret;
}

void preload_sequence(u32 seqId, u8 preloadMask) {
    void *sequenceData;
    u8 temp;

    if (seqId >= gSequenceCount) {
        return;
    }

    gAudioLoadLock = AUDIO_LOCK_LOADING;
    if (preloadMask & PRELOAD_BANKS) {
        load_banks_immediate(seqId, &temp);
    }

    if (preloadMask & PRELOAD_SEQUENCE) {
        // @bug should be IS_SEQ_LOAD_COMPLETE
        if (IS_BANK_LOAD_COMPLETE(seqId) == TRUE) {
            sequenceData = get_bank_or_seq(&gSeqLoadedPool, 2, seqId);
        } else {
            sequenceData = NULL;
        }
        if (sequenceData == NULL && sequence_dma_immediate(seqId, 2) == NULL) {
            gAudioLoadLock = AUDIO_LOCK_NOT_LOADING;
            return;
        }
    }

    gAudioLoadLock = AUDIO_LOCK_NOT_LOADING;
}

void load_sequence_internal(u32 player, u32 seqId, s32 loadAsync);

void load_sequence(u32 player, u32 seqId, s32 loadAsync) {
    if (!loadAsync) {
        gAudioLoadLock = AUDIO_LOCK_LOADING;
    }
    load_sequence_internal(player, seqId, loadAsync);
    if (!loadAsync) {
        gAudioLoadLock = AUDIO_LOCK_NOT_LOADING;
    }
}

void alSeqFileNew(ALSeqFile *file, u8 *base) {
    s32 offset = (s32) base;
    s32 i;
    
    /*
     * patch the file so that offsets are pointers
     */
    for (i = 0; i < file->seqCount; i++) {
        file->seqArray[i].offset = (u8 *)((u8 *)file->seqArray[i].offset + offset);
    }
}

void load_sequence_internal(u32 player, u32 seqId, s32 loadAsync) {
    void *sequenceData;
    struct SequencePlayer *seqPlayer = &gSequencePlayers[player];

    if (seqId >= gSequenceCount) {
        return;
    }

    sequence_player_disable(seqPlayer);
    if (loadAsync) {
        s32 numMissingBanks = 0;
        s32 dummy = 0;
        s32 bankId = get_missing_bank(seqId, &dummy, &numMissingBanks);
        if (numMissingBanks == 1) {
            if (bank_load_async(bankId, 2, seqPlayer) == NULL) {
                return;
            }
            // @bug This should set the last bank (i.e. the first in the JSON)
            // as default, not the missing one. This code path never gets
            // taken, though -- all sequence loading is synchronous.
            seqPlayer->defaultBank[0] = bankId;
        } else {
            if (load_banks_immediate(seqId, &seqPlayer->defaultBank[0]) == NULL) {
                return;
            }
        }
    } else if (load_banks_immediate(seqId, &seqPlayer->defaultBank[0]) == NULL) {
        return;
    }

    seqPlayer->seqId = seqId;
    sequenceData = get_bank_or_seq(&gSeqLoadedPool, 2, seqId);
    if (sequenceData == NULL) {
        if (seqPlayer->seqDmaInProgress) {
            return;
        }
        if (loadAsync) {
            sequenceData = sequence_dma_async(seqId, 2, seqPlayer);
        } else {
            sequenceData = sequence_dma_immediate(seqId, 2);
        }

        if (sequenceData == NULL) {
            return;
        }
    }

    init_sequence_player(player);
    seqPlayer->scriptState.depth = 0;
    seqPlayer->delay = 0;
    seqPlayer->enabled = TRUE;
    seqPlayer->seqData = sequenceData;
    seqPlayer->scriptState.pc = sequenceData;
}

// (void) must be omitted from parameters to fix stack with -framepointer
void audio_init() {
#if defined(VERSION_JP) || defined(VERSION_US)
    u8 buf[0x10];
#endif
    s32 i, j, k;
    UNUSED s32 lim1; // lim1 unused in EU
#if defined(VERSION_EU)
    UNUSED u8 buf[0x10];
    s32 UNUSED lim2, lim3;
    u32 size;
#else
    s32 lim2, lim3;
    u32 size;
#endif
    void *data;
    u64 *ptr64;

    gAudioLoadLock = AUDIO_LOCK_UNINITIALIZED;

#if defined(VERSION_JP) || defined(VERSION_US)

    lim2 = gAudioHeapSize;
    for (i = 0; i <= lim2 / 8 - 1; i++) {
        ((u64 *) gAudioHeap)[i] = 0;
    }


    // It seems boot.s doesn't clear the .bss area for audio, so do it here.
    i = 0;
    lim3 = ((uintptr_t) &gAudioGlobalsEndMarker - (uintptr_t) &gAudioGlobalsStartMarker) / 8;
    ptr64 = &gAudioGlobalsStartMarker;
    for (k = lim3; k >= 0; k--) {
        ptr64[i] = 0;
        i++;
    }

#else
    for (i = 0; i < gAudioHeapSize / 8; i++) {
        ((u64 *) gAudioHeap)[i] = 0;
    }

    // It seems boot.s doesn't clear the .bss area for audio, so do it here.
    lim3 = ((uintptr_t) &gAudioGlobalsEndMarker - (uintptr_t) &gAudioGlobalsStartMarker) / 8;
    ptr64 = &gAudioGlobalsStartMarker;
    for (k = lim3; k >= 0; k--) {
        *ptr64++ = 0;
    }

    D_EU_802298D0 = 20.03042f;
    gRefreshRate = 50;
    port_eu_init();
    if (k) {
    }
#endif

    for (i = 0; i < NUMAIBUFFERS; i++) {
        gAiBufferLengths[i] = 10;
    }

    gAudioFrameCount = 0;
    gAudioTaskIndex = 0;
    gCurrAiBufferIndex = 0;
    gSoundMode = 0;
    gAudioTask = NULL;
    gAudioTasks[0].task.t.data_size = 0;
    gAudioTasks[1].task.t.data_size = 0;
    osCreateMesgQueue(&gAudioDmaMesgQueue, &gAudioDmaMesg, 1);
    osCreateMesgQueue(&gCurrAudioFrameDmaQueue, gCurrAudioFrameDmaMesgBufs, ARRAY_COUNT(gCurrAudioFrameDmaMesgBufs));
    gCurrAudioFrameDmaCount = 0;
    gSampleDmaNumListItems = 0;

    sound_init_main_pools(gAudioInitPoolSize);
    bzero(&gAiBuffers, sizeof(gAiBuffers));
    for (i = 0; i < NUMAIBUFFERS; i++) {
        gAiBuffers[i] = soundAlloc(&gAudioInitPool, AIBUFFER_LEN);
    }

#if defined(VERSION_EU)
    gAudioResetPresetIdToLoad = 0;
    gAudioResetStatus = 1;
    audio_shut_down_and_reset_step();
#else
    audio_reset_session(&gAudioSessionPresets[0], 0);
#endif

    // Load headers for sounds and sequences
    gSeqFileHeader = (ALSeqFile *) buf;
    data = gMusicData;
    audio_dma_copy_immediate((uintptr_t) data, gSeqFileHeader, 0x10);
    gSequenceCount = gSeqFileHeader->seqCount;
#if defined(VERSION_EU)
    size = gSequenceCount * sizeof(ALSeqData) + 4;
    size = ALIGN16(size);
#else
    size = ALIGN16(gSequenceCount * sizeof(ALSeqData) + 4);
#endif
    gSeqFileHeader = soundAlloc(&gAudioInitPool, size);
    audio_dma_copy_immediate((uintptr_t) data, gSeqFileHeader, size);
    alSeqFileNew(gSeqFileHeader, data);

    // Load header for CTL (instrument metadata)
    gAlCtlHeader = (ALSeqFile *) buf;
    data = gSoundDataADSR;
    audio_dma_copy_immediate((uintptr_t) data, gAlCtlHeader, 0x10);
    size = gAlCtlHeader->seqCount * sizeof(ALSeqData) + 4;
    size = ALIGN16(size);
    gCtlEntries = soundAlloc(&gAudioInitPool, gAlCtlHeader->seqCount * sizeof(struct CtlEntry));
    gAlCtlHeader = soundAlloc(&gAudioInitPool, size);
    audio_dma_copy_immediate((uintptr_t) data, gAlCtlHeader, size);
    alSeqFileNew(gAlCtlHeader, data);

    // Load header for TBL (raw sound data)
    gAlTbl = (ALSeqFile *) buf;
    audio_dma_copy_immediate((uintptr_t) data, gAlTbl, 0x10);
    size = gAlTbl->seqCount * sizeof(ALSeqData) + 4;
    size = ALIGN16(size);
    gAlTbl = soundAlloc(&gAudioInitPool, size);
    audio_dma_copy_immediate((uintptr_t) gSoundDataRaw, gAlTbl, size);
    alSeqFileNew(gAlTbl, gSoundDataRaw);

    // Load bank sets for each sequence
    gAlBankSets = soundAlloc(&gAudioInitPool, 0x100);
    audio_dma_copy_immediate((uintptr_t) gBankSetsData, gAlBankSets, 0x100);

    init_sequence_players();
    gAudioLoadLock = AUDIO_LOCK_NOT_LOADING;
    // Should probably contain the sizes of the data banks, but those aren't
    // easily accessible from here.
    osSyncPrintf("---------- Init Completed. ------------\n");
    osSyncPrintf(" Syndrv    :[%6d]\n", gSoundDataRaw - gSoundDataADSR); // gSoundDataADSR
#ifndef VERSION_SH
    osSyncPrintf(" Seqdrv    :[%6d]\n", gBankSetsData - gMusicData); // gMusicData
#else
    osSyncPrintf(" Seqdrv    :[%6d]\n", _assetsSegmentRomEnd - gMusicData); // gMusicData
#endif
    osSyncPrintf(" audiodata :[%6d]\n", gMusicData - gSoundDataRaw); // gSoundDataRaw
    osSyncPrintf("---------------------------------------\n");
}
#endif
