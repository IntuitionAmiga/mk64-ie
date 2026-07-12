#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif
#include <string.h>
#include <PR/ultratypes.h>
#include <PR/os_message.h>
#include <PR/os_pi.h>
#include <PR/os_vi.h>
#include <PR/os_time.h>
#include <PR/libultra.h>

#include <ultra64.h>
#include "save.h"
#include "platform/platform.h"

void* segmented_to_virtual(void* addr);

u64 osClockRate = 62500000;
void n64_memcpy(void* dst, const void* src, size_t size);

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;
    return;
}

s32 osSendMesg(UNUSED OSMesgQueue* mq, UNUSED OSMesg msg, UNUSED s32 flag) {
    return 0;
}

s32 osRecvMesg(UNUSED OSMesgQueue* mq, UNUSED OSMesg* msg, UNUSED s32 flag) {
    return 0;
}

s32 AosSendMesg(OSMesgQueue* mq, OSMesg msg, UNUSED s32 flag) {
    s32 index;
    if (mq->validCount >= mq->msgCount) {
        return -1;
    }

    index = (mq->first + mq->validCount) % mq->msgCount;

    mq->msg[index] = msg;
    mq->validCount++;

    return 0;
}

s32 AosRecvMesg(OSMesgQueue* mq, OSMesg* msg, UNUSED s32 flag) {
    if (mq->validCount == 0) {
        return -1;
    }

    if (msg != NULL) {
        *msg = *(mq->first + mq->msg);
    }

    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    return 0;
}

void osSetTime(UNUSED OSTime time) {
    ;
}

#define N64_NS_PER_TICK 21.91f

// Change me to "float" to trade accuracy for perf
typedef double N64Ticks;

OSTime osGetTime(void) {
    uint64_t ns = platform_time_ns();
    N64Ticks ticks = (N64Ticks) ns / (N64Ticks) N64_NS_PER_TICK;
    return (OSTime) ticks;
}

void osWritebackDCacheAll(void) {
    ;
}

void osWritebackDCache(UNUSED void* a, UNUSED size_t b) {
    ;
}

void osInvalDCache(UNUSED void* a, UNUSED size_t b) {
    ;
}

void osInvalICache(UNUSED void* a, UNUSED size_t b) {
    ;
}

static u32 counter = 0;
static u32 ticked = 0;
u32 osGetCount(void) {
    ticked++;
    counter += 757576;
    counter += ticked & 1;
    return counter;
}

s32 osAiSetFrequency(u32 freq) {
    u32 a1;
    s32 a2;
    u32 D_8033491C;

    D_8033491C = 0x02E6D354;
    a1 = D_8033491C / (float) freq + .5f;

    if (a1 < 0x84) {
        return -1;
    }

    a2 = (a1 / 66) & 0xff;
    if (a2 > 16) {
        a2 = 16;
    }

    return D_8033491C / (s32) a1;
}

/*
 * EEPROM and Controller Pak reimplementation on top of the platform-neutral
 * save/ghost storage contracts (platform/platform.h). The game-facing API
 * stays the libultra one; where the data lives is a backend decision.
 *
 * EEPROM addressing follows the N64 convention: one address selects an
 * 8-byte block, so byte offset = address * 8.
 */

struct state_pak {
    OSPfsState state;
    int in_use;
};

static struct state_pak openFile[16] = { 0 };

static int fileIndex = 0;

s32 osEepromProbe(UNUSED OSMesgQueue* mq) {
    if (!platform_save_probe()) {
        return 0;
    }
    return EEPROM_TYPE_4K;
}

s32 osEepromLongRead(UNUSED OSMesgQueue* mq, unsigned char address, unsigned char* buffer, s32 length) {
    if (!platform_save_read((size_t) address * 8, buffer, length)) {
        return 1;
    }
    return 0;
}

s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer) {
    return osEepromLongRead(mq, address, buffer, 8);
}

s32 osEepromLongWrite(UNUSED OSMesgQueue* mq, unsigned char address, unsigned char* buffer, s32 length) {
    if (!platform_save_write((size_t) address * 8, buffer, length)) {
        return 1;
    }
    return 0;
}

s32 osEepromWrite(OSMesgQueue* mq, unsigned char address, unsigned char* buffer) {
    return osEepromLongWrite(mq, address, buffer, 8);
}

s32 osPfsDeleteFile(UNUSED OSPfs* pfs, UNUSED u16 company_code, UNUSED u32 game_code, UNUSED u8* game_name,
                    UNUSED u8* ext_name) {
    if (!platform_ghost_present()) {
        return PFS_ERR_NOPACK;
    }

    if (!platform_ghost_delete()) {
        return PFS_ERR_ID_FATAL;
    }

    return PFS_NO_ERROR;
}

s32 osPfsReadWriteFile(UNUSED OSPfs* pfs, s32 file_no, u8 flag, int offset, int size_in_bytes, u8* data_buffer) {
    if (!openFile[file_no].in_use) {
        return PFS_ERR_INVALID;
    }

    if (flag == PFS_READ) {
        if (!platform_ghost_load(offset, data_buffer, size_in_bytes)) {
            openFile[file_no].in_use = 0;
            return PFS_ERR_BAD_DATA;
        }
    } else {
        if (!platform_ghost_store(offset, data_buffer, size_in_bytes)) {
            openFile[file_no].in_use = 0;
            return PFS_CORRUPTED;
        }
    }
    return PFS_NO_ERROR;
}

/* Ghost records are a fixed 32 KiB, matching the original controller-pak file. */
#define GHOST_RECORD_SIZE 32768

static uint8_t __attribute__((aligned(32))) tempblock[GHOST_RECORD_SIZE];

s32 osPfsAllocateFile(UNUSED OSPfs* pfs, u16 company_code, u32 game_code, u8* game_name, u8* ext_name,
                      UNUSED int file_size_in_bytes, s32* file_no) {
    if (!platform_ghost_present()) {
        return PFS_NO_PAK_INSERTED;
    }

    memset(tempblock, 0, GHOST_RECORD_SIZE);
    if (!platform_ghost_store(0, tempblock, GHOST_RECORD_SIZE)) {
        return PFS_CORRUPTED;
    }

    *file_no = fileIndex++;
    openFile[*file_no].in_use = 1;
    openFile[*file_no].state.company_code = company_code;
    openFile[*file_no].state.game_code = game_code;
    strcpy(openFile[*file_no].state.game_name, (char*) game_name);
    strcpy(openFile[*file_no].state.ext_name, (char*) ext_name);
    return PFS_NO_ERROR;
}

s32 osPfsIsPlug(UNUSED OSMesgQueue* queue, u8* pattern) {
    *pattern = 0;
    if (platform_ghost_present()) {
        *pattern = 1;
    }
    return 1;
}

s32 osPfsInit(UNUSED OSMesgQueue* queue, OSPfs* pfs, int channel) {
    if (!platform_ghost_present()) {
        return PFS_NO_PAK_INSERTED;
    }
    pfs->queue = queue;
    if (channel != 0) {
        return PFS_NO_PAK_INSERTED;
    }
    pfs->channel = channel;
    pfs->status = 0;
    pfs->status |= PFS_INITIALIZED;
    return PFS_NO_ERROR;
}

s32 osPfsNumFiles(UNUSED OSPfs* pfs, s32* max_files, s32* files_used) {
    *max_files = 16;
    *files_used = fileIndex;
    return 0;
}

s32 osPfsFileState(UNUSED OSPfs* pfs, UNUSED s32 file_no, UNUSED OSPfsState* state) {
    return PFS_NO_ERROR;
}

s32 osPfsFreeBlocks(UNUSED OSPfs* pfs, s32* bytes_not_used) {
    /* Report enough free space for one ghost record when storage exists. */
    *bytes_not_used = platform_ghost_present() ? GHOST_RECORD_SIZE : 0;
    return PFS_NO_ERROR;
}

void __osPfsCloseAllFiles(void) {
    for (size_t i = 0; i < 16; i++) {
        openFile[i].in_use = 0;
    }
}

s32 osPfsFindFile(UNUSED OSPfs* pfs, u16 company_code, u32 game_code, u8* game_name, u8* ext_name, s32* file_no) {
    if (!platform_ghost_present()) {
        return PFS_NO_PAK_INSERTED;
    }

    for (size_t i = 0; i < 16; i++) {
        if (openFile[i].in_use && openFile[i].state.game_code == game_code &&
            openFile[i].state.company_code == company_code &&
            strcmp(openFile[i].state.game_name, (char*) game_name) == 0 &&
            strcmp(openFile[i].state.ext_name, (char*) ext_name) == 0) {
            *file_no = i;
            return PFS_NO_ERROR;
        }
    }

    if (!platform_ghost_load(0, tempblock, 1)) {
        return PFS_ERR_INVALID;
    }

    *file_no = fileIndex++;
    openFile[*file_no].in_use = 1;
    openFile[*file_no].state.company_code = company_code;
    openFile[*file_no].state.game_code = game_code;
    strcpy(openFile[*file_no].state.game_name, (char*) game_name);
    strcpy(openFile[*file_no].state.ext_name, (char*) ext_name);
    return PFS_NO_ERROR;
}
