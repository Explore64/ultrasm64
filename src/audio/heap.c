#include <ultra64.h>

#include "heap.h"
#include "data.h"
#include "load.h"
#include "synthesis.h"
#include "seqplayer.h"
#include "effects.h"
#include "game/game_init.h"
#include "game/main.h"

#define ALIGN16(val) (((val) + 0xF) & ~0xF)

struct PoolSplit {
    u32 wantSeq;
    u32 wantBank;
    u32 wantCustom;
}; // size = 0x10

struct PoolSplit2 {
    u32 wantPersistent;
    u32 wantTemporary;
}; // size = 0x8

#define REVERB_WINDOW_SIZE_MAX 0x1000

#if defined(VERSION_JP) || defined(VERSION_US)
s16 gVolume;
s8 gReverbDownsampleRate;
u8 sReverbDownsampleRateLog; // never read
#endif

struct SoundAllocPool gAudioSessionPool;
struct SoundAllocPool gAudioInitPool;
struct SoundAllocPool gNotesAndBuffersPool;
struct SoundAllocPool gSeqAndBankPool;
struct SoundAllocPool gPersistentCommonPool;
struct SoundAllocPool gTemporaryCommonPool;

struct SoundMultiPool gSeqLoadedPool;
struct SoundMultiPool gBankLoadedPool;

#ifdef VERSION_SH
struct Unk1Pool gUnkPool1;
struct UnkPool gUnkPool2;
struct UnkPool gUnkPool3;
#endif

struct PoolSplit sSessionPoolSplit;
struct PoolSplit2 sSeqAndBankPoolSplit;
struct PoolSplit sPersistentCommonPoolSplit;
struct PoolSplit sTemporaryCommonPoolSplit;

#ifdef VERSION_SH
u8 gUnkLoadStatus[0x40];
#endif
u8 gBankLoadStatus[0x40];
u8 gSeqLoadStatus[0x100];

#if defined(VERSION_EU) || defined(VERSION_SH)
volatile u8 gAudioResetStatus;
u8 gAudioResetPresetIdToLoad;
s32 gAudioResetFadeOutFramesLeft;
#endif

extern s32 gMaxAudioCmds;

#ifdef VERSION_SH
void *get_bank_or_seq_inner(s32 poolIdx, s32 arg1, s32 bankId);
struct UnkEntry *func_sh_802f1ec4(u32 size);
void func_sh_802f2158(struct UnkEntry *entry);
struct UnkEntry *unk_pool2_alloc(u32 size);
void func_sh_802F2320(struct UnkEntry *entry, struct AudioBankSample *sample);
void func_sh_802f23ec(void);

void unk_pools_init(u32 size1, u32 size2);
#endif

#if defined(VERSION_EU)
/**
 * Assuming 'k' in [9, 24],
 * Computes a newton's method step for f(x) = x^k - d
 */
f32 root_newton_step(f32 x, s32 k, f32 d) {
    f32 deg2 = x * x;
    f32 deg4 = deg2 * deg2;
    f32 deg8 = deg4 * deg4;
    s32 degree = k - 9;
    f32 fx;

    f32 deriv = deg8;
    if (degree & 1) {
        deriv *= x;
    }
    if (degree & 2) {
        deriv *= deg2;
    }
    if (degree & 4) {
        deriv *= deg4;
    }
    if (degree & 8) {
        deriv *= deg8;
    }
    fx = deriv * x - d;
    deriv = k * deriv;
    return x - fx / deriv;
}

/**
 * Assuming 'k' in [9, 24],
 * Computes d ^ (1/k)
 *
 * @return the root, or 1.0 if d is 0
 */
f32 kth_root(f32 d, s32 k) {
    f32 root = 1.5f;
    f32 next;
    f32 diff;
    s32 i;
    if (d == 0.0f) {
        root = 1.0f;
    } else {
        for (i = 0; i < 64; i++) {
            if (1) {
            }
            next = root_newton_step(root, k, d);
            diff = next - root;

            if (diff < 0) {
                diff = -diff;
            }

            if (diff < 1e-07) {
                root = next;
                break;
            } else {
                root = next;
            }
        }
    }

    return root;
}

void build_vol_rampings_table(s32 UNUSED unused, s32 len) {
    s32 i;
    s32 step;
    s32 d;
    s32 k = len / 8;

    for (step = 0, i = 0; i < 0x400; step += 32, i++) {
        d = step;
        if (step == 0) {
            d = 1;
        }

        gLeftVolRampings[0][i]  = kth_root(      d, k - 1);
        gRightVolRampings[0][i] = kth_root(1.0f / d, k - 1) * 65536.0f;
        gLeftVolRampings[1][i]  = kth_root(      d, k);
        gRightVolRampings[1][i] = kth_root(1.0f / d, k) * 65536.0f;
        gLeftVolRampings[2][i]  = kth_root(      d, k + 1);
        gRightVolRampings[2][i] = kth_root(1.0f / d, k + 1) * 65536.0f;
    }
}
#endif

void reset_bank_and_seq_load_status(void) {
#ifdef VERSION_SH
    bzero(&gBankLoadStatus, sizeof(gBankLoadStatus));
    bzero(&gUnkLoadStatus, sizeof(gUnkLoadStatus));
    bzero(&gSeqLoadStatus, sizeof(gBankLoadStatus));
#else
    bzero(&gBankLoadStatus, sizeof(gBankLoadStatus)); //Setting this array to zero is equivilent to SOUND_LOAD_STATUS_NOT_LOADED
    bzero(&gSeqLoadStatus, sizeof(gSeqLoadStatus)); //Same dealio
#endif
}

void discard_bank(s32 bankId) {
    s32 i;

    for (i = 0; i < gMaxSimultaneousNotes; i++) {
        struct Note *note = &gNotes[i];

#if defined(VERSION_EU)
        if (note->noteSubEu.bankId == bankId) {
#else
        if (note->bankId == bankId) {
#endif
#ifdef VERSION_SH
            if (note->unkSH34 == NOTE_PRIORITY_DISABLED && note->priority) {
#else
            if (note->priority >= NOTE_PRIORITY_MIN) {
#endif
                note->parentLayer->enabled = FALSE; // is 0x48, should be 0x44
                note->parentLayer->finished = TRUE;
            }
            note_disable(note);
            audio_list_remove(&note->listItem);
            audio_list_push_back(&gNoteFreeLists.disabled, &note->listItem);
        }
    }
}

void discard_sequence(s32 seqId) {
    s32 i;

    for (i = 0; i < SEQUENCE_PLAYERS; i++) {
        if (gSequencePlayers[i].enabled && gSequencePlayers[i].seqId == seqId) {
#if defined(VERSION_EU) || defined(VERSION_SH)
            sequence_player_disable(&gSequencePlayers[i]);
#else
            sequence_player_disable(gSequencePlayers + i);
#endif
        }
    }
}

void *soundAlloc(struct SoundAllocPool *pool, u32 size) {
#if defined(VERSION_EU) || defined(VERSION_SH)
    u8 *start;
    u32 alignedSize = ALIGN16(size);

    start = pool->cur;
    if (start + alignedSize <= pool->start + pool->size) {
        bzero(start, alignedSize);
        pool->cur += alignedSize;
    } else {
        return NULL;
    }
    pool->numAllocatedEntries++;
    return start;
#else
    u8 *start;
    u32 alignedSize = ALIGN16(size);

    start = pool->cur;
    if ((start + alignedSize <= pool->size + pool->start)) {
        bzero(start, alignedSize);
        pool->cur += alignedSize;
    } else {
        return NULL;
    }
    return start;
#endif
}

#ifdef VERSION_SH
void *sound_alloc_uninitialized(struct SoundAllocPool *pool, u32 size) {
    u8 *start;
    u32 alignedSize = ALIGN16(size);

    start = pool->cur;
    if (start + alignedSize <= pool->start + pool->size) {
        pool->cur += alignedSize;
    } else {
        return NULL;
    }

    pool->numAllocatedEntries++;
    return start;
}
#endif

void sound_alloc_pool_init(struct SoundAllocPool *pool, void *memAddr, u32 size) {
    pool->cur = pool->start = (u8 *) ALIGN16((uintptr_t) memAddr);
#ifdef VERSION_SH
    pool->size = size - ((uintptr_t) memAddr & 0xf);
#else
    pool->size = size;
#endif
    pool->numAllocatedEntries = 0;
}

void persistent_pool_clear(struct PersistentPool *persistent) {
    persistent->pool.numAllocatedEntries = 0;
    persistent->pool.cur = persistent->pool.start;
    persistent->numEntries = 0;
}

void temporary_pool_clear(struct TemporaryPool *temporary) {
    temporary->pool.numAllocatedEntries = 0;
    temporary->pool.cur = temporary->pool.start;
    temporary->nextSide = 0;
    temporary->entries[0].ptr = temporary->pool.start;
#if defined(VERSION_EU) || defined(VERSION_SH)
    temporary->entries[1].ptr = temporary->pool.start + temporary->pool.size;
#else
    temporary->entries[1].ptr = temporary->pool.size + temporary->pool.start;
#endif
    temporary->entries[0].id = -1; // should be at 1e not 1c
    temporary->entries[1].id = -1;
}

extern s32 D_SH_80315EE8;
void sound_init_main_pools(s32 sizeForAudioInitPool) {
    sound_alloc_pool_init(&gAudioInitPool, gAudioHeap, sizeForAudioInitPool);
    sound_alloc_pool_init(&gAudioSessionPool, gAudioHeap + sizeForAudioInitPool, gAudioHeapSize - sizeForAudioInitPool);
}

#ifdef VERSION_SH
#define SOUND_ALLOC_FUNC sound_alloc_uninitialized
#else
#define SOUND_ALLOC_FUNC soundAlloc
#endif

void session_pools_init(struct PoolSplit *a) {
    gAudioSessionPool.cur = gAudioSessionPool.start;
    sound_alloc_pool_init(&gNotesAndBuffersPool, SOUND_ALLOC_FUNC(&gAudioSessionPool, a->wantSeq), a->wantSeq);
    sound_alloc_pool_init(&gSeqAndBankPool, SOUND_ALLOC_FUNC(&gAudioSessionPool, a->wantCustom), a->wantCustom);
}

void seq_and_bank_pool_init(struct PoolSplit2 *a) {
    gSeqAndBankPool.cur = gSeqAndBankPool.start;
    sound_alloc_pool_init(&gPersistentCommonPool, SOUND_ALLOC_FUNC(&gSeqAndBankPool, a->wantPersistent), a->wantPersistent);
    sound_alloc_pool_init(&gTemporaryCommonPool, SOUND_ALLOC_FUNC(&gSeqAndBankPool, a->wantTemporary), a->wantTemporary);
}

void persistent_pools_init(struct PoolSplit *a) {
    gPersistentCommonPool.cur = gPersistentCommonPool.start;
    sound_alloc_pool_init(&gSeqLoadedPool.persistent.pool, SOUND_ALLOC_FUNC(&gPersistentCommonPool, a->wantSeq), a->wantSeq);
    sound_alloc_pool_init(&gBankLoadedPool.persistent.pool, SOUND_ALLOC_FUNC(&gPersistentCommonPool, a->wantBank), a->wantBank);
    persistent_pool_clear(&gSeqLoadedPool.persistent);
    persistent_pool_clear(&gBankLoadedPool.persistent);
}

void temporary_pools_init(struct PoolSplit *a) {
    gTemporaryCommonPool.cur = gTemporaryCommonPool.start;
    sound_alloc_pool_init(&gSeqLoadedPool.temporary.pool, SOUND_ALLOC_FUNC(&gTemporaryCommonPool, a->wantSeq), a->wantSeq);
    sound_alloc_pool_init(&gBankLoadedPool.temporary.pool, SOUND_ALLOC_FUNC(&gTemporaryCommonPool, a->wantBank), a->wantBank);
    temporary_pool_clear(&gSeqLoadedPool.temporary);
    temporary_pool_clear(&gBankLoadedPool.temporary);
}
#undef SOUND_ALLOC_FUNC

#ifdef VERSION_SH
void *alloc_bank_or_seq(s32 poolIdx, s32 size, s32 arg3, s32 id) {
#else
void *alloc_bank_or_seq(struct SoundMultiPool *arg0, s32 arg1, s32 size, s32 arg3, s32 id) {
#endif
    // arg3 = 0, 1 or 2?

#ifdef VERSION_SH
    struct SoundMultiPool *arg0;
#define isSound poolIdx
#endif
    struct TemporaryPool *tp;
    struct SoundAllocPool *pool;
    void *ret;
#if defined(VERSION_JP) || defined(VERSION_US)
    u16 UNUSED _firstVal;
    u16 UNUSED _secondVal;
#else
    u16 firstVal;
    u16 secondVal;
#endif
    u32 nullID = -1;
    UNUSED s32 i;
    u8 *table = NULL;
#ifndef VERSION_SH
    u8 isSound = FALSE;
#endif
#if defined(VERSION_JP) || defined(VERSION_US)
    u16 firstVal;
    u16 secondVal;
    u32 bothDiscardable;
    u32 leftDiscardable, rightDiscardable;
    u32 leftNotLoaded, rightNotLoaded;
    u32 leftAvail, rightAvail;
#endif

#ifdef VERSION_SH
    switch (poolIdx) {
        case 0:
            arg0 = &gSeqLoadedPool;
            table = gSeqLoadStatus;
            break;

        case 1:
            arg0 = &gBankLoadedPool;
            table = gBankLoadStatus;
            break;

        case 2:
            table = gUnkLoadStatus;
            break;
    }
#endif

    if (arg3 == 0) {
        tp = &arg0->temporary;
#ifndef VERSION_SH
        if (arg0 == &gSeqLoadedPool) {
            table = gSeqLoadStatus;
            isSound = FALSE;
        } else if (arg0 == &gBankLoadedPool) {
            table = gBankLoadStatus;
            isSound = TRUE;
        }
#endif

#ifdef VERSION_SH
        if (tp->entries[0].id == (s8)nullID) {
            firstVal = SOUND_LOAD_STATUS_NOT_LOADED;
        } else {
            firstVal = table[tp->entries[0].id];
        }
        if (tp->entries[1].id == (s8)nullID) {
            secondVal = SOUND_LOAD_STATUS_NOT_LOADED;
        } else {
            secondVal = table[tp->entries[1].id];
        }
#else
        firstVal  = (tp->entries[0].id == (s8)nullID ? SOUND_LOAD_STATUS_NOT_LOADED : table[tp->entries[0].id]);
        secondVal = (tp->entries[1].id == (s8)nullID ? SOUND_LOAD_STATUS_NOT_LOADED : table[tp->entries[1].id]);
#endif

#if defined(VERSION_JP) || defined(VERSION_US)
        leftNotLoaded = (firstVal == SOUND_LOAD_STATUS_NOT_LOADED);
        leftDiscardable = (firstVal == SOUND_LOAD_STATUS_DISCARDABLE);
        leftAvail = (firstVal != SOUND_LOAD_STATUS_IN_PROGRESS);
        rightNotLoaded = (secondVal == SOUND_LOAD_STATUS_NOT_LOADED);
        rightDiscardable = (secondVal == SOUND_LOAD_STATUS_DISCARDABLE);
        rightAvail = (secondVal != SOUND_LOAD_STATUS_IN_PROGRESS);
        bothDiscardable = (leftDiscardable && rightDiscardable);

        if (leftNotLoaded) {
            tp->nextSide = 0;
        } else if (rightNotLoaded) {
            tp->nextSide = 1;
        } else if (bothDiscardable) {
            // Use the opposite side from last time.
        } else if (firstVal == SOUND_LOAD_STATUS_DISCARDABLE) { // ??! (I blame copt)
            tp->nextSide = 0;
        } else if (rightDiscardable) {
            tp->nextSide = 1;
        } else if (leftAvail) {
            tp->nextSide = 0;
        } else if (rightAvail) {
            tp->nextSide = 1;
        } else {
            // Both left and right sides are being loaded into.
            return NULL;
        }
#else

#ifdef VERSION_SH
        if (poolIdx == 1) {
            if (firstVal == SOUND_LOAD_STATUS_4) {
                for (i = 0; i < gMaxSimultaneousNotes; i++) {
                    if (gNotes[i].bankId == tp->entries[0].id && gNotes[i].noteSubEu.enabled) {
                        break;
                    }
                }
                if (i == gMaxSimultaneousNotes) {
                    if (gBankLoadStatus[tp->entries[0].id] != SOUND_LOAD_STATUS_5) {
                        gBankLoadStatus[tp->entries[0].id] = SOUND_LOAD_STATUS_DISCARDABLE;
                    }
                    firstVal = SOUND_LOAD_STATUS_DISCARDABLE;
                }
            }
            if (secondVal == SOUND_LOAD_STATUS_4) {
                for (i = 0; i < gMaxSimultaneousNotes; i++) {
                    if (gNotes[i].bankId == tp->entries[1].id && gNotes[i].noteSubEu.enabled) {
                        break;
                    }
                }
                if (i == gMaxSimultaneousNotes) {
                    if (gBankLoadStatus[tp->entries[1].id] != SOUND_LOAD_STATUS_5) {
                        gBankLoadStatus[tp->entries[1].id] = SOUND_LOAD_STATUS_DISCARDABLE;
                    }
                    secondVal = SOUND_LOAD_STATUS_DISCARDABLE;
                }
            }
        }
#endif

        if (firstVal == SOUND_LOAD_STATUS_NOT_LOADED) {
            tp->nextSide = 0;
        } else if (secondVal == SOUND_LOAD_STATUS_NOT_LOADED) {
            tp->nextSide = 1;
        } else {
            if ((firstVal == SOUND_LOAD_STATUS_DISCARDABLE) && (secondVal == SOUND_LOAD_STATUS_DISCARDABLE)) {
                // Use the opposite side from last time.
            } else if (firstVal == SOUND_LOAD_STATUS_DISCARDABLE) {
                tp->nextSide = 0;
            } else if (secondVal == SOUND_LOAD_STATUS_DISCARDABLE) {
                tp->nextSide = 1;
            } else {
#ifdef VERSION_EU
                if (firstVal != SOUND_LOAD_STATUS_IN_PROGRESS) {
                    tp->nextSide = 0;
                } else if (secondVal != SOUND_LOAD_STATUS_IN_PROGRESS) {
                    tp->nextSide = 1;
                } else {
                    // Both left and right sides are being loaded into.
                    return NULL;
                }
#else
                if (poolIdx == 0) {
                    if (firstVal == SOUND_LOAD_STATUS_COMPLETE) {
                        for (i = 0; i < SEQUENCE_PLAYERS; i++) {
                            if (gSequencePlayers[i].enabled && gSequencePlayers[i].seqId == tp->entries[0].id) {
                                break;
                            }
                        }
                        if (i == SEQUENCE_PLAYERS) {
                            tp->nextSide = 0;
                            goto out;
                        }
                    }
                    if (secondVal == SOUND_LOAD_STATUS_COMPLETE) {
                        for (i = 0; i < SEQUENCE_PLAYERS; i++) {
                            if (gSequencePlayers[i].enabled && gSequencePlayers[i].seqId == tp->entries[1].id) {
                                break;
                            }
                        }
                        if (i == SEQUENCE_PLAYERS) {
                            tp->nextSide = 1;
                            goto out;
                        }
                    }
                } else if (poolIdx == 1) {
                    if (firstVal == SOUND_LOAD_STATUS_COMPLETE) {
                        for (i = 0; i < gMaxSimultaneousNotes; i++) {
                            if (gNotes[i].bankId == tp->entries[0].id && gNotes[i].noteSubEu.enabled) {
                                break;
                            }
                        }
                        if (i == gMaxSimultaneousNotes) {
                            tp->nextSide = 0;
                            goto out;
                        }
                    }
                    if (secondVal == SOUND_LOAD_STATUS_COMPLETE) {
                        for (i = 0; i < gMaxSimultaneousNotes; i++) {
                            if (gNotes[i].bankId == tp->entries[1].id && gNotes[i].noteSubEu.enabled) {
                                break;
                            }
                        }
                        if (i == gMaxSimultaneousNotes) {
                            tp->nextSide = 1;
                            goto out;
                        }
                    }
                }
                if (tp->nextSide == 0) {
                    if (firstVal == SOUND_LOAD_STATUS_IN_PROGRESS) {
                        if (secondVal != SOUND_LOAD_STATUS_IN_PROGRESS) {
                            tp->nextSide = 1;
                            goto out;
                        }
                    } else {
                        goto out;
                    }
                } else {
                    if (secondVal == SOUND_LOAD_STATUS_IN_PROGRESS) {
                        if (firstVal != SOUND_LOAD_STATUS_IN_PROGRESS) {
                            tp->nextSide = 0;
                            goto out;
                        }
                    } else {
                        goto out;
                    }
                }
                return NULL;
                out:;
#endif
            }
        }
#endif

        pool = &arg0->temporary.pool;
        if (tp->entries[tp->nextSide].id != (s8)nullID) {
            table[tp->entries[tp->nextSide].id] = SOUND_LOAD_STATUS_NOT_LOADED;
            if (isSound == TRUE) {
                discard_bank(tp->entries[tp->nextSide].id);
            }
        }

        switch (tp->nextSide) {
            case 0:
                tp->entries[0].ptr = pool->start;
                tp->entries[0].id = id;
                tp->entries[0].size = size;

                pool->cur = pool->start + size;

#ifdef VERSION_SH
                if (tp->entries[1].id != (s32)nullID)
#endif
                if (tp->entries[1].ptr < pool->cur) {

                    // Throw out the entry on the other side if it doesn't fit.
                    // (possible @bug: what if it's currently being loaded?)
                    table[tp->entries[1].id] = SOUND_LOAD_STATUS_NOT_LOADED;

                    switch (isSound) {
                        case FALSE:
                            discard_sequence(tp->entries[1].id);
                            break;
                        case TRUE:
                            discard_bank(tp->entries[1].id);
                            break;
                    }

                    tp->entries[1].id = (s32)nullID;
#if defined(VERSION_EU) || defined(VERSION_SH)
                    tp->entries[1].ptr = pool->start + pool->size;
#else
                    tp->entries[1].ptr = pool->size + pool->start;
#endif
                }

                ret = tp->entries[0].ptr;
                break;

            case 1:
#if defined(VERSION_SH)
                tp->entries[1].ptr = (u8 *) ((uintptr_t) (pool->start + pool->size - size) & ~0x0f);
#elif defined(VERSION_EU)
                tp->entries[1].ptr = pool->start + pool->size - size - 0x10;
#else
                tp->entries[1].ptr = pool->size + pool->start - size - 0x10;
#endif
                tp->entries[1].id = id;
                tp->entries[1].size = size;

#ifdef VERSION_SH
                if (tp->entries[0].id != (s32)nullID)
#endif
                if (tp->entries[1].ptr < pool->cur) {

                    table[tp->entries[0].id] = SOUND_LOAD_STATUS_NOT_LOADED;

                    switch (isSound) {
                        case FALSE:
                            discard_sequence(tp->entries[0].id);
                            break;
                        case TRUE:
                            discard_bank(tp->entries[0].id);
                            break;
                    }

                    tp->entries[0].id = (s32)nullID;
                    pool->cur = pool->start;
                }

                ret = tp->entries[1].ptr;
                break;

            default:
                return NULL;
        }

        // Switch sides for next time in case both entries are
        // SOUND_LOAD_STATUS_DISCARDABLE.
        tp->nextSide ^= 1;

        return ret;
    }

#if defined(VERSION_EU) || defined(VERSION_SH)
#ifdef VERSION_SH
    ret = sound_alloc_uninitialized(&arg0->persistent.pool, size);
#else
    ret = soundAlloc(&arg0->persistent.pool, arg1 * size);
#endif
    arg0->persistent.entries[arg0->persistent.numEntries].ptr = ret;

    if (ret == NULL)
#else
    arg0->persistent.entries[arg0->persistent.numEntries].ptr = soundAlloc(&arg0->persistent.pool, arg1 * size);

    if (arg0->persistent.entries[arg0->persistent.numEntries].ptr == NULL)
#endif
    {
        switch (arg3) {
            case 2:
#if defined(VERSION_EU)
                return alloc_bank_or_seq(arg0, arg1, size, 0, id);
#elif defined(VERSION_SH)
                return alloc_bank_or_seq(poolIdx, size, 0, id);
#else
                // Prevent tail call optimization.
                ret = alloc_bank_or_seq(arg0, arg1, size, 0, id);
                return ret;
#endif
            case 1:
#ifdef VERSION_SH
            case 0:
#endif
                return NULL;
        }
    }

    // TODO: why is this guaranteed to write <= 32 entries...?
    // Because the buffer is small enough that more don't fit?
    arg0->persistent.entries[arg0->persistent.numEntries].id = id;
    arg0->persistent.entries[arg0->persistent.numEntries].size = size;
#if defined(VERSION_EU) || defined(VERSION_SH)
    return arg0->persistent.entries[arg0->persistent.numEntries++].ptr;
#else
    arg0->persistent.numEntries++; return arg0->persistent.entries[arg0->persistent.numEntries - 1].ptr;
#endif
#ifdef VERSION_SH
#undef isSound
#endif
}

#ifdef VERSION_SH
void *get_bank_or_seq(s32 poolIdx, s32 arg1, s32 id) {
    void *ret;

    ret = unk_pool1_lookup(poolIdx, id);
    if (ret != NULL) {
        return ret;
    }
    if (arg1 == 3) {
        return NULL;
    }
    return get_bank_or_seq_inner(poolIdx, arg1, id);
}
void *get_bank_or_seq_inner(s32 poolIdx, s32 arg1, s32 bankId) {
    u32 i;
    struct SoundMultiPool* loadedPool;
    struct TemporaryPool* temporary;
    struct PersistentPool* persistent;

    switch (poolIdx) {
        case 0:
            loadedPool = &gSeqLoadedPool;
            break;
    }

    temporary = &loadedPool->temporary;
    if (arg1 == 0) {
        if (temporary->entries[0].id == bankId) {
            temporary->nextSide = 1;
            return temporary->entries[0].ptr;
        } else if (temporary->entries[1].id == bankId) {
            temporary->nextSide = 0;
            return temporary->entries[1].ptr;
        } else {
            return NULL;
        }
    }

    persistent = &loadedPool->persistent;
    for (i = 0; i < persistent->numEntries; i++) {
        if (persistent->entries[i].id == bankId) {
            return persistent->entries[i].ptr;
        }
    }

    if (arg1 == 2) {
        return get_bank_or_seq(poolIdx, 0, bankId);
    }
    return NULL;
}
#endif
#ifndef VERSION_SH
void *get_bank_or_seq(struct SoundMultiPool *arg0, s32 arg1, s32 id) {
    u32 i;
    struct TemporaryPool *temporary = &arg0->temporary;

    if (arg1 == 0) {
        // Try not to overwrite sound that we have just accessed, by setting nextSide appropriately.
        if (temporary->entries[0].id == id) {
            temporary->nextSide = 1;
            return temporary->entries[0].ptr;
        } else if (temporary->entries[1].id == id) {
            temporary->nextSide = 0;
            return temporary->entries[1].ptr;
        }
        return NULL;
    } else {
        struct PersistentPool *persistent = &arg0->persistent;
        for (i = 0; i < persistent->numEntries; i++) {
            if (id == persistent->entries[i].id) {
                return persistent->entries[i].ptr;
            }
        }

        if (arg1 == 2) {
            return get_bank_or_seq(arg0, 0, id);
        }
        return NULL;
    }
}
#endif

#if defined(VERSION_EU) || defined(VERSION_SH)
void func_eu_802e27e4_unused(f32 arg0, f32 arg1, u16 *arg2) {
    s32 i;
    f32 tmp[16];

    tmp[0] = (f32) (arg1 * 262159.0f);
    tmp[8] = (f32) (arg0 * 262159.0f);
    tmp[1] = (f32) ((arg1 * arg0) * 262159.0f);
    tmp[9] = (f32) (((arg0 * arg0) + arg1) * 262159.0f);

    for (i = 2; i < 8; i++) {
        //! @bug they probably meant to store the value to tmp[i] and tmp[8 + i]
        arg2[i] = arg1 * tmp[i - 2] + arg0 * tmp[i - 1];
        arg2[8 + i] = arg1 * tmp[6 + i] + arg0 * tmp[7 + i];
    }

    for (i = 0; i < 16; i++) {
        arg2[i] = tmp[i];
    }
}
#endif

#ifdef VERSION_SH
void fill_zero_filter(s16 filter[]) {
    s32 i;
    for (i = 0; i < 8; i++) {
        filter[i] = 0;
    }
}

extern s16 unk_sh_data_3[15 * 8];
extern s16 unk_sh_data_4[15 * 8];
void func_sh_802F0DE8(s16 filter[8], s32 arg1) {
    s32 i;
    s16 *ptr = &unk_sh_data_3[8 * (arg1 - 1)];
    for (i = 0; i < 8; i++) {
        filter[i] = ptr[i];
    }
}

void func_sh_802F0E40(s16 filter[8], s32 arg1) { // Unused
    s32 i;
    s16 *ptr = &unk_sh_data_4[8 * (arg1 - 1)];
    for (i = 0; i < 8; i++) {
        filter[i] = ptr[i];
    }
}

void fill_filter(s16 filter[8], s32 arg1, s32 arg2) {
    s32 i;
    s16 *ptr;
    if (arg1 != 0) {
        func_sh_802F0DE8(filter, arg1);
    } else {
        fill_zero_filter(filter);
    }
    if (arg2 != 0) {
        ptr = &unk_sh_data_4[8 * (arg2 - 1)];
        for (i = 0; i < 8; i++) {
            filter[i] += ptr[i];
        }
    }
}
#endif

void decrease_reverb_gain(void) {
#if defined(VERSION_EU)
    s32 i;
    for (i = 0; i < gNumSynthesisReverbs; i++) {
        gSynthesisReverbs[i].reverbGain -= gSynthesisReverbs[i].reverbGain / 8;
    }
#elif defined(VERSION_JP) || defined(VERSION_US)
    gSynthesisReverb.reverbGain -= gSynthesisReverb.reverbGain / 4;
#else
    s32 i, j;
    s32 v0 = gAudioBufferParameters.presetUnk4 == 2 ? 2 : 1;
    for (i = 0; i < gNumSynthesisReverbs; i++) {
        for (j = 0; j < v0; j++) {
            gSynthesisReverbs[i].reverbGain -= gSynthesisReverbs[i].reverbGain / 3;
        }
    }
#endif
}

#if defined(VERSION_EU) || defined(VERSION_SH)
s32 audio_shut_down_and_reset_step(void) {
    s32 i;
    s32 j;

    switch (gAudioResetStatus) {
        case 5:
            for (i = 0; i < SEQUENCE_PLAYERS; i++) {
                sequence_player_disable(&gSequencePlayers[i]);
            }
            gAudioResetStatus--;
            break;
        case 4:
            for (i = 0; i < gMaxSimultaneousNotes; i++) {
                if (gNotes[i].noteSubEu.enabled && gNotes[i].adsr.state != ADSR_STATE_DISABLED) {
                    gNotes[i].adsr.fadeOutVel = gAudioBufferParameters.updatesPerFrameInv;
                    gNotes[i].adsr.action |= ADSR_ACTION_RELEASE;
                }
            }
            gAudioResetFadeOutFramesLeft = 0;
            gAudioResetStatus--;
            break;
        case 3:
            gAudioResetStatus--;
            break;
        case 2:
            bzero(&gAiBuffers[0][0], (AIBUFFER_LEN * NUMAIBUFFERS));
            gAudioResetStatus--;
#ifdef VERSION_SH
            func_sh_802f23ec();
#endif
            break;
        case 1:
            audio_reset_session();
            bzero(&gAiBuffers[0][0], (AIBUFFER_LEN * NUMAIBUFFERS));
            gAudioResetStatus = 0;
    }
    if (gAudioResetStatus < 3) {
        return 0;
    }
    return 1;
}
#endif

u8 sAudioFirstBoot = 0;
//Separate the reverb settings into their own func. Bit unstable currently, so still only runs at boot.
#if defined(VERSION_EU) || defined(VERSION_SH)
void init_reverb_eu(void)
{
    s16 *mem;
    struct AudioSessionSettingsEU *preset = &gAudioSessionPresets[0];
    struct SynthesisReverb *reverb;
    struct ReverbSettingsEU *reverbSettings;
    s32 i, j;

    // This is called 4 times for numReverbs to work at higher values. This does eat up some memory though.
    for (j = 0; j < 4; j++)
    {
        gSynthesisReverbs[j].useReverb = 0;

        // Both left and right channels are allocated/cleared together, then separated based on the reverb window size
        if (!sAudioFirstBoot)
            gSynthesisReverbs[j].ringBuffer.left = soundAlloc(&gNotesAndBuffersPool, REVERB_WINDOW_SIZE_MAX * 4);
    }
    gNumSynthesisReverbs = preset->numReverbs;
    for (j = 0; j < gNumSynthesisReverbs; j++)
    {
        reverb = &gSynthesisReverbs[j];
        reverbSettings = &sReverbSettings[MIN(gAudioResetPresetIdToLoad+j, (sizeof(sReverbSettings) / sizeof(struct ReverbSettingsEU))-1)];
        reverb->windowSize = reverbSettings->windowSize * 0x40;
        reverb->downsampleRate = reverbSettings->downsampleRate;
        reverb->reverbGain = reverbSettings->gain;
        reverb->useReverb = 8;
        if (reverb->windowSize > REVERB_WINDOW_SIZE_MAX)
            reverb->windowSize = REVERB_WINDOW_SIZE_MAX;

        if (sAudioFirstBoot) {
            bzero(reverb->ringBuffer.left, REVERB_WINDOW_SIZE_MAX * 4);
        }
        else {
            reverb->resampleRate = 0x8000 / reverb->downsampleRate;
            reverb->resampleStateLeft = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            reverb->resampleStateRight = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            reverb->unk24 = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            reverb->unk28 = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            for (i = 0; i < gAudioBufferParameters.updatesPerFrame; i++)
            {
                mem = soundAlloc(&gNotesAndBuffersPool, DEFAULT_LEN_2CH);
                reverb->items[0][i].toDownsampleLeft = mem;
                reverb->items[0][i].toDownsampleRight = mem + DEFAULT_LEN_1CH / sizeof(s16);
                mem = soundAlloc(&gNotesAndBuffersPool, DEFAULT_LEN_2CH);
                reverb->items[1][i].toDownsampleLeft = mem;
                reverb->items[1][i].toDownsampleRight = mem + DEFAULT_LEN_1CH / sizeof(s16);
            }
        }

        reverb->ringBuffer.right = &reverb->ringBuffer.left[reverb->windowSize];
        reverb->nextRingBufferPos = 0;
        reverb->unkC = 0;
        reverb->curFrame = 0;
        reverb->bufSizePerChannel = reverb->windowSize;
        reverb->framesLeftToIgnore = 2;
        if (reverb->downsampleRate != 1) {
            reverb->resampleRate = 0x8000 / reverb->downsampleRate;
            if (sAudioFirstBoot) {
                bzero(reverb->resampleStateLeft, 16 * sizeof(s16));
                bzero(reverb->resampleStateRight, 16 * sizeof(s16));
                bzero(reverb->unk24, 16 * sizeof(s16));
                bzero(reverb->unk28, 16 * sizeof(s16));

                // All reverb downsample buffers are adjacent in memory, so clear them all in a single call
                bzero(reverb->items[0][0].toDownsampleLeft, DEFAULT_LEN_1CH * 4 * gAudioBufferParameters.updatesPerFrame);
            }
        }
    }
}
#else
void init_reverb_us(s32 presetId)
{
    s32 reverbWindowSize;
    s16 *mem;
    s32 i;

    reverbWindowSize = gReverbSettings[presetId].windowSize;
    gReverbDownsampleRate = gReverbSettings[presetId].downsampleRate;

    switch (gReverbDownsampleRate) {
        case 1:
            sReverbDownsampleRateLog = 0;
            break;
        case 2:
            sReverbDownsampleRateLog = 1;
            break;
        case 4:
            sReverbDownsampleRateLog = 2;
            break;
        case 8:
            sReverbDownsampleRateLog = 3;
            break;
        case 16:
            sReverbDownsampleRateLog = 4;
            break;
        default:
            sReverbDownsampleRateLog = 0;
    }

    if (reverbWindowSize == 0) {
        gSynthesisReverb.useReverb = 0;
    } else {
        gSynthesisReverb.useReverb = 8;
        if (reverbWindowSize > REVERB_WINDOW_SIZE_MAX)
            reverbWindowSize = REVERB_WINDOW_SIZE_MAX;

        // Both left and right channels are allocated/cleared together, then separated based on the reverb window size
        if (!sAudioFirstBoot) {
            gSynthesisReverb.ringBuffer.left = soundAlloc(&gNotesAndBuffersPool, REVERB_WINDOW_SIZE_MAX * 2 * sizeof(s16));

            gSynthesisReverb.resampleStateLeft = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            gSynthesisReverb.resampleStateRight = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            gSynthesisReverb.unk24 = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            gSynthesisReverb.unk28 = soundAlloc(&gNotesAndBuffersPool, 16 * sizeof(s16));
            for (i = 0; i < gAudioUpdatesPerFrame; i++) {
                mem = soundAlloc(&gNotesAndBuffersPool, DEFAULT_LEN_2CH);
                gSynthesisReverb.items[0][i].toDownsampleLeft = mem;
                gSynthesisReverb.items[0][i].toDownsampleRight = mem + DEFAULT_LEN_1CH / sizeof(s16);
                mem = soundAlloc(&gNotesAndBuffersPool, DEFAULT_LEN_2CH);
                gSynthesisReverb.items[1][i].toDownsampleLeft = mem;
                gSynthesisReverb.items[1][i].toDownsampleRight = mem + DEFAULT_LEN_1CH / sizeof(s16);
            }
        } else {
            bzero(gSynthesisReverb.ringBuffer.left, REVERB_WINDOW_SIZE_MAX * 2 * sizeof(s16));
        }

        gSynthesisReverb.ringBuffer.right = &gSynthesisReverb.ringBuffer.left[reverbWindowSize];
        gSynthesisReverb.nextRingBufferPos = 0;
        gSynthesisReverb.unkC = 0;
        gSynthesisReverb.curFrame = 0;
        gSynthesisReverb.bufSizePerChannel = reverbWindowSize;
        gSynthesisReverb.reverbGain = gReverbSettings[presetId].gain;
        gSynthesisReverb.framesLeftToIgnore = 2;
        if (gReverbDownsampleRate != 1) {
            gSynthesisReverb.resampleFlags = A_INIT;
            gSynthesisReverb.resampleRate = 0x8000 / gReverbDownsampleRate;
            if (sAudioFirstBoot) {
                bzero(gSynthesisReverb.resampleStateLeft, 16 * sizeof(s16));
                bzero(gSynthesisReverb.resampleStateRight, 16 * sizeof(s16));
                bzero(gSynthesisReverb.unk24, 16 * sizeof(s16));
                bzero(gSynthesisReverb.unk28, 16 * sizeof(s16));

                // All reverb downsample buffers are adjacent in memory, so clear them all in a single call
                bzero(gSynthesisReverb.items[0][0].toDownsampleLeft, DEFAULT_LEN_1CH * 4 * gAudioUpdatesPerFrame);
            }
        }
    }
}
#endif


#if defined(VERSION_JP) || defined(VERSION_US)
void audio_reset_session(struct AudioSessionSettings *preset, s32 presetId) {
    if (sAudioFirstBoot) {
        bzero(&gAiBuffers[0][0], (AIBUFFER_LEN * NUMAIBUFFERS));
        gAudioSessionPool.cur = gAudioSessionPool.start;
        gSeqAndBankPool.cur = gSeqAndBankPool.start;
        gPersistentCommonPool.cur = gPersistentCommonPool.start;
        gTemporaryCommonPool.cur = gTemporaryCommonPool.start;
        persistent_pool_clear(&gSeqLoadedPool.persistent);
        persistent_pool_clear(&gBankLoadedPool.persistent);
        temporary_pool_clear(&gSeqLoadedPool.temporary);
        temporary_pool_clear(&gBankLoadedPool.temporary);
        reset_bank_and_seq_load_status();

        init_reverb_us(presetId);
        bzero(&gAiBuffers[0][0], (AIBUFFER_LEN * NUMAIBUFFERS));
        gAudioFrameCount = 0;
        osRecvMesg(&gGameVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
        bzero(&gAiBuffers[0][0], (AIBUFFER_LEN * NUMAIBUFFERS));
        return;
    }
#else
void audio_reset_session(void) {
    if (sAudioFirstBoot)
    {
        gAudioSessionPool.cur = gAudioSessionPool.start;
        gSeqAndBankPool.cur = gSeqAndBankPool.start;
        gPersistentCommonPool.cur = gPersistentCommonPool.start;
        gTemporaryCommonPool.cur = gTemporaryCommonPool.start;
        persistent_pool_clear(&gSeqLoadedPool.persistent);
        persistent_pool_clear(&gBankLoadedPool.persistent);
        temporary_pool_clear(&gSeqLoadedPool.temporary);
        temporary_pool_clear(&gBankLoadedPool.temporary);
        reset_bank_and_seq_load_status();

        init_reverb_eu();
        return;
    }
    struct AudioSessionSettingsEU *preset = &gAudioSessionPresets[0];
#endif
#if defined(VERSION_JP) || defined(VERSION_US)
    s8 updatesPerFrame;
#endif
    s32 j;
    s32 persistentMem;
    s32 temporaryMem;
    s32 totalMem;
    s32 wantMisc;

    gSampleDmaNumListItems = 0;
#if defined(VERSION_EU) || defined(VERSION_SH)
    gAudioBufferParameters.frequency = preset->frequency;
    gAudioBufferParameters.aiFrequency = osAiSetFrequency(gAudioBufferParameters.frequency);
    gAudioBufferParameters.samplesPerFrameTarget = ALIGN16(gAudioBufferParameters.frequency / gRefreshRate);
    gAudioBufferParameters.minAiBufferLength = gAudioBufferParameters.samplesPerFrameTarget - 0x10;
    gAudioBufferParameters.maxAiBufferLength = gAudioBufferParameters.samplesPerFrameTarget + 0x10;
#ifdef VERSION_SH
    gAudioBufferParameters.updatesPerFrame = (gAudioBufferParameters.samplesPerFrameTarget + 0x10) / 192 + 1;
    gAudioBufferParameters.samplesPerUpdate = (gAudioBufferParameters.samplesPerFrameTarget / gAudioBufferParameters.updatesPerFrame) & -8;
#else
    gAudioBufferParameters.updatesPerFrame = (gAudioBufferParameters.samplesPerFrameTarget + 0x10) / 160 + 1;
    gAudioBufferParameters.samplesPerUpdate = (gAudioBufferParameters.samplesPerFrameTarget / gAudioBufferParameters.updatesPerFrame) & 0xfff8;
#endif
    gAudioBufferParameters.samplesPerUpdateMax = gAudioBufferParameters.samplesPerUpdate + 8;
    gAudioBufferParameters.samplesPerUpdateMin = gAudioBufferParameters.samplesPerUpdate - 8;
    gAudioBufferParameters.resampleRate = 32000.0f / FLOAT_CAST(gAudioBufferParameters.frequency);
#ifdef VERSION_SH
    gAudioBufferParameters.unkUpdatesPerFrameScaled = (1.0f / 256.0f) / gAudioBufferParameters.updatesPerFrame;
#else
    gAudioBufferParameters.unkUpdatesPerFrameScaled = (3.0f / 1280.0f) / gAudioBufferParameters.updatesPerFrame;
#endif
    gAudioBufferParameters.updatesPerFrameInv = 1.0f / gAudioBufferParameters.updatesPerFrame;

    gMaxSimultaneousNotes = preset->maxSimultaneousNotes;
    gVolume = preset->volume;
    gTempoInternalToExternal = (u32) (gAudioBufferParameters.updatesPerFrame * 2880000.0f / gTatumsPerBeat / D_EU_802298D0);

    gAudioBufferParameters.presetUnk4 = preset->unk1;
    gAudioBufferParameters.samplesPerFrameTarget *= gAudioBufferParameters.presetUnk4;
    gAudioBufferParameters.maxAiBufferLength *= gAudioBufferParameters.presetUnk4;
    gAudioBufferParameters.minAiBufferLength *= gAudioBufferParameters.presetUnk4;
    gAudioBufferParameters.updatesPerFrame *= gAudioBufferParameters.presetUnk4;

#ifdef VERSION_SH
    if (gAudioBufferParameters.presetUnk4 >= 2) {
        gAudioBufferParameters.maxAiBufferLength -= 0x10;
    }
    gMaxAudioCmds = gMaxSimultaneousNotes * 0x14 * gAudioBufferParameters.updatesPerFrame + preset->numReverbs * 0x20 + 0x1E0;
#else
    gMaxAudioCmds = gMaxSimultaneousNotes * 0x10 * gAudioBufferParameters.updatesPerFrame + preset->numReverbs * 0x20 + 0x300;
#endif
#else
    gAiFrequency = osAiSetFrequency(preset->frequency);
    gMaxSimultaneousNotes = preset->maxSimultaneousNotes;
    gSamplesPerFrameTarget = ALIGN16(gAiFrequency / 60);

    gVolume = preset->volume;
    gMinAiBufferLength = gSamplesPerFrameTarget - 0x10;
    updatesPerFrame = gSamplesPerFrameTarget / 160 + 1;
    gAudioUpdatesPerFrame = gSamplesPerFrameTarget / 160 + 1;

    // Compute conversion ratio from the internal unit tatums/tick to the
    // external beats/minute (JP) or tatums/minute (US). In practice this is
    // 300 on JP and 14360 on US.
#ifdef VERSION_JP
    gTempoInternalToExternal = updatesPerFrame * 3600 / gTatumsPerBeat;
#else
    gTempoInternalToExternal = (u32)(updatesPerFrame * 2880000.0f / gTatumsPerBeat / 16.713f);
#endif
    gMaxAudioCmds = gMaxSimultaneousNotes * 20 * updatesPerFrame + 320;
#endif

#if defined(VERSION_SH)
    persistentMem = DOUBLE_SIZE_ON_64_BIT(preset->persistentSeqMem + preset->persistentBankMem + preset->unk18 + preset->unkMem28 + 0x10);
    temporaryMem = DOUBLE_SIZE_ON_64_BIT(preset->temporarySeqMem + preset->temporaryBankMem + preset->unk24 + preset->unkMem2C + 0x10);
#elif defined(VERSION_EU)
    persistentMem = DOUBLE_SIZE_ON_64_BIT(preset->persistentSeqMem + preset->persistentBankMem);
    temporaryMem = DOUBLE_SIZE_ON_64_BIT(preset->temporarySeqMem + preset->temporaryBankMem);
#else
    persistentMem = DOUBLE_SIZE_ON_64_BIT(preset->persistentBankMem + preset->persistentSeqMem);
    temporaryMem = DOUBLE_SIZE_ON_64_BIT(preset->temporaryBankMem + preset->temporarySeqMem);
#endif
    totalMem = persistentMem + temporaryMem;
    wantMisc = gAudioSessionPool.size - totalMem - 0x100;
    sSessionPoolSplit.wantSeq = wantMisc;
    sSessionPoolSplit.wantCustom = totalMem;
    session_pools_init(&sSessionPoolSplit);
    sSeqAndBankPoolSplit.wantPersistent = persistentMem;
    sSeqAndBankPoolSplit.wantTemporary = temporaryMem;
    seq_and_bank_pool_init(&sSeqAndBankPoolSplit);
    sPersistentCommonPoolSplit.wantSeq = DOUBLE_SIZE_ON_64_BIT(preset->persistentSeqMem);
    sPersistentCommonPoolSplit.wantBank = DOUBLE_SIZE_ON_64_BIT(preset->persistentBankMem);
    persistent_pools_init(&sPersistentCommonPoolSplit);
    sTemporaryCommonPoolSplit.wantSeq = DOUBLE_SIZE_ON_64_BIT(preset->temporarySeqMem);
    sTemporaryCommonPoolSplit.wantBank = DOUBLE_SIZE_ON_64_BIT(preset->temporaryBankMem);
    temporary_pools_init(&sTemporaryCommonPoolSplit);
#ifdef VERSION_SH
    unk_pools_init(preset->unkMem28, preset->unkMem2C);
#endif
    reset_bank_and_seq_load_status();

#if defined(VERSION_JP) || defined(VERSION_US)
    for (j = 0; j < 2; j++) {
        gAudioCmdBuffers[j] = soundAlloc(&gNotesAndBuffersPool, gMaxAudioCmds * sizeof(u64));
    }
#endif

    gNotes = soundAlloc(&gNotesAndBuffersPool, gMaxSimultaneousNotes * sizeof(struct Note));
    note_init_all();
    init_note_free_list();

#if defined(VERSION_EU) || defined(VERSION_SH)
    gNoteSubsEu = soundAlloc(&gNotesAndBuffersPool, (gAudioBufferParameters.updatesPerFrame * gMaxSimultaneousNotes) * sizeof(struct NoteSubEu));

    for (j = 0; j != 2; j++) {
        gAudioCmdBuffers[j] = soundAlloc(&gNotesAndBuffersPool, gMaxAudioCmds * sizeof(u64));
    }

    init_reverb_eu();
#else
    init_reverb_us(presetId);
#endif

    init_sample_dma_buffers();

#if defined(VERSION_EU)
    build_vol_rampings_table(0, gAudioBufferParameters.samplesPerUpdate);
#endif

#ifdef VERSION_SH
    D_SH_8034F68C = 0;
    D_SH_803479B4 = 4096;
#endif

    osWritebackDCacheAll();

#if defined(VERSION_JP) || defined(VERSION_US)
    if (gAudioLoadLock != AUDIO_LOCK_UNINITIALIZED) {
        gAudioLoadLock = AUDIO_LOCK_NOT_LOADING;
    }
#endif
    sAudioFirstBoot = 1;
}

#ifdef VERSION_SH
void *unk_pool1_lookup(s32 poolIdx, s32 id) {
    s32 i;

    for (i = 0; i < gUnkPool1.pool.numAllocatedEntries; i++) {
        if (gUnkPool1.entries[i].poolIndex == poolIdx && gUnkPool1.entries[i].id == id) {
            return gUnkPool1.entries[i].ptr;
        }
    }
    return NULL;
}

void *unk_pool1_alloc(s32 poolIndex, s32 arg1, u32 size) {
    void *ret;
    s32 pos;

    pos = gUnkPool1.pool.numAllocatedEntries;
    ret = sound_alloc_uninitialized(&gUnkPool1.pool, size);
    gUnkPool1.entries[pos].ptr = ret;
    if (ret == NULL) {
        return NULL;
    }
    gUnkPool1.entries[pos].poolIndex = poolIndex;
    gUnkPool1.entries[pos].id = arg1;
    gUnkPool1.entries[pos].size = size;

#ifdef AVOID_UB
    //! @bug UB: missing return. "ret" is in v0 at this point, but doing an
    //  explicit return uses an additional register.
    return ret;
#endif
}

u8 *func_sh_802f1d40(u32 size, s32 bank, u8 *arg2, s8 medium) {
    struct UnkEntry *ret;

    ret = func_sh_802f1ec4(size);
    if (ret != NULL) {
        ret->bankId = bank;
        ret->dstAddr = arg2;
        ret->medium = medium;
        return ret->srcAddr;
    }
    return NULL;
}
u8 *func_sh_802f1d90(u32 size, s32 bank, u8 *arg2, s8 medium) {
    struct UnkEntry *ret;

    ret = unk_pool2_alloc(size);
    if (ret != NULL) {
        ret->bankId = bank;
        ret->dstAddr = arg2;
        ret->medium = medium;
        return ret->srcAddr;
    }
    return NULL;
}
u8 *func_sh_802f1de0(u32 size, s32 bank, u8 *arg2, s8 medium) { // duplicated function?
    struct UnkEntry *ret;

    ret = unk_pool2_alloc(size);
    if (ret != NULL) {
        ret->bankId = bank;
        ret->dstAddr = arg2;
        ret->medium = medium;
        return ret->srcAddr;
    }
    return NULL;
}
void unk_pools_init(u32 size1, u32 size2) {
    void *mem;

    mem = sound_alloc_uninitialized(&gPersistentCommonPool, size1);
    if (mem == NULL) {
        gUnkPool2.pool.size = 0;
    } else {
        sound_alloc_pool_init(&gUnkPool2.pool, mem, size1);
    }
    mem = sound_alloc_uninitialized(&gTemporaryCommonPool, size2);

    if (mem == NULL) {
        gUnkPool3.pool.size = 0;
    } else {
        sound_alloc_pool_init(&gUnkPool3.pool, mem, size2);
    }

    gUnkPool2.numEntries = 0;
    gUnkPool3.numEntries = 0;
}

struct UnkEntry *func_sh_802f1ec4(u32 size) {
    u8 *temp_s2;
    u8 *phi_s3;
    u8 *memLocation;
    u8 *cur;

    s32 i;
    s32 chosenIndex;

    struct UnkStructSH8034EC88 *unkStruct;
    struct UnkPool *pool = &gUnkPool3;

    u8 *itemStart;
    u8 *itemEnd;

    phi_s3 = pool->pool.cur;
    memLocation = sound_alloc_uninitialized(&pool->pool, size);
    if (memLocation == NULL) {
        cur = pool->pool.cur;
        pool->pool.cur = pool->pool.start;
        memLocation = sound_alloc_uninitialized(&pool->pool, size);
        if (memLocation == NULL) {
            pool->pool.cur = cur;
            return NULL;
        }
        phi_s3 = pool->pool.start;
    }
    temp_s2 = pool->pool.cur;

    chosenIndex = -1;
    for (i = 0; i < D_SH_8034F68C; i++) {
        unkStruct = &D_SH_8034EC88[i];
        if (unkStruct->isFree == FALSE) {
            itemStart = unkStruct->ramAddr;
            itemEnd = unkStruct->ramAddr + unkStruct->sample->size - 1;
            if (itemEnd < phi_s3 && itemStart < phi_s3) {
                continue;

            }
            if (itemEnd >= temp_s2 && itemStart >= temp_s2) {
                continue;

            }

            unkStruct->isFree = TRUE;
        }
    }

    for (i = 0; i < pool->numEntries; i++) {
        if (pool->entries[i].used == FALSE) {
            continue;
        }
        itemStart = pool->entries[i].srcAddr;
        itemEnd = itemStart + pool->entries[i].size - 1;

        if (itemEnd < phi_s3 && itemStart < phi_s3) {
            continue;
        }

        if (itemEnd >= temp_s2 && itemStart >= temp_s2) {
            continue;
        }

        func_sh_802f2158(&pool->entries[i]);
        if (chosenIndex == -1) {
            chosenIndex = i;
        }
    }

    if (chosenIndex == -1) {
        chosenIndex = pool->numEntries++;
    }
    pool->entries[chosenIndex].used = TRUE;
    pool->entries[chosenIndex].srcAddr = memLocation;
    pool->entries[chosenIndex].size = size;

    return &pool->entries[chosenIndex];
}

void func_sh_802f2158(struct UnkEntry *entry) {
    s32 idx;
    s32 seqCount;
    s32 bankId1;
    s32 bankId2;
    s32 instId;
    s32 drumId;
    struct Drum *drum;
    struct Instrument *inst;

    seqCount = gAlCtlHeader->seqCount;
    for (idx = 0; idx < seqCount; idx++) {
        bankId1 = gCtlEntries[idx].bankId1;
        bankId2 = gCtlEntries[idx].bankId2;
        if ((bankId1 != 0xff && entry->bankId == bankId1) || (bankId2 != 0xff && entry->bankId == bankId2) || entry->bankId == 0) {
            if (get_bank_or_seq(1, 2, idx) != NULL) {
                if (IS_BANK_LOAD_COMPLETE(idx) != FALSE) {
                    for (instId = 0; instId < gCtlEntries[idx].numInstruments; instId++) {
                        inst = get_instrument_inner(idx, instId);
                        if (inst != NULL) {
                            if (inst->normalRangeLo != 0) {
                                func_sh_802F2320(entry, inst->lowNotesSound.sample);
                            }
                            if (inst->normalRangeHi != 127) {
                                func_sh_802F2320(entry, inst->highNotesSound.sample);
                            }
                            func_sh_802F2320(entry, inst->normalNotesSound.sample);
                        }
                    }
                    for (drumId = 0; drumId < gCtlEntries[idx].numDrums; drumId++) {
                        drum = get_drum(idx, drumId);
                        if (drum != NULL) {
                            func_sh_802F2320(entry, drum->sound.sample);
                        }
                    }
                }
            }
        }
    }
}

void func_sh_802F2320(struct UnkEntry *entry, struct AudioBankSample *sample) {
    if (sample != NULL && sample->sampleAddr == entry->srcAddr) {
        sample->sampleAddr = entry->dstAddr;
        sample->medium = entry->medium;
    }
}

struct UnkEntry *unk_pool2_alloc(u32 size) {
    void *data;
    struct UnkEntry *ret;
    s32 *numEntries = &gUnkPool2.numEntries;

    data = sound_alloc_uninitialized(&gUnkPool2.pool, size);
    if (data == NULL) {
        return NULL;
    }
    ret = &gUnkPool2.entries[*numEntries];
    ret->used = TRUE;
    ret->srcAddr = data;
    ret->size = size;
    (*numEntries)++;
    return ret;
}

void func_sh_802f23ec(void) {
    s32 i;
    s32 idx;
    s32 seqCount;
    s32 bankId1;
    s32 bankId2;
    s32 instId;
    s32 drumId;
    struct Drum *drum;
    struct Instrument *inst;
    struct UnkEntry *entry; //! @bug: not initialized but nevertheless used

    seqCount = gAlCtlHeader->seqCount;
    for (idx = 0; idx < seqCount; idx++) {
        bankId1 = gCtlEntries[idx].bankId1;
        bankId2 = gCtlEntries[idx].bankId2;
        if ((bankId1 != 0xffu && entry->bankId == bankId1) || (bankId2 != 0xff && entry->bankId == bankId2) || entry->bankId == 0) {
            if (get_bank_or_seq(1, 3, idx) != NULL) {
                if (IS_BANK_LOAD_COMPLETE(idx) != FALSE) {
                    for (i = 0; i < gUnkPool2.numEntries; i++) {
                        entry = &gUnkPool2.entries[i];
                        for (instId = 0; instId < gCtlEntries[idx].numInstruments; instId++) {
                            inst = get_instrument_inner(idx, instId);
                            if (inst != NULL) {
                                if (inst->normalRangeLo != 0) {
                                    func_sh_802F2320(entry, inst->lowNotesSound.sample);
                                }
                                if (inst->normalRangeHi != 127) {
                                    func_sh_802F2320(entry, inst->highNotesSound.sample);
                                }
                                func_sh_802F2320(entry, inst->normalNotesSound.sample);
                            }
                        }
                        for (drumId = 0; drumId < gCtlEntries[idx].numDrums; drumId++) {
                            drum = get_drum(idx, drumId);
                            if (drum != NULL) {
                                func_sh_802F2320(entry, drum->sound.sample);
                            }
                        }
                    }
                }
            }
        }
    }
}
#endif
