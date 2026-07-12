#ifndef GCC
#define D_800DC510_AS_U16
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <PR/os.h>
#include <PR/ucode.h>
#include <macros.h>
#include <decode.h>
#include <mk64.h>
#include <course.h>

#include "profiler.h"
#include "main.h"
#include "racing/memory.h"
#include "racing/course_metadata.h"
#include "menus.h"
#include <segments.h>
#include <common_structs.h>
#include <defines.h>
#include "buffers.h"
#include "camera.h"
#include "profiler.h"
#include "race_logic.h"
#include "skybox_and_splitscreen.h"
#include "render_objects.h"
#include "effects.h"
#include "code_80281780.h"
#include "audio/external.h"
#include "code_800029B0.h"
#include "code_80280000.h"
#include "podium_ceremony_actors.h"
#include "menu_items.h"
#include "code_80057C60.h"
#include "profiler.h"
#include "player_controller.h"
#include "render_player.h"
#include "render_courses.h"
#include "actors.h"
#include "staff_ghosts.h"
#include <debug.h>
#include "crash_screen.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void func_80091B78(void);
void audio_init(void);
void create_debug_thread(void);
void start_debug_thread(void);
struct SPTask* create_next_audio_frame_task(void);

struct VblankHandler* gVblankHandler1 = NULL;
struct VblankHandler* gVblankHandler2 = NULL;

struct SPTask* gActiveSPTask = NULL;
struct SPTask* sCurrentAudioSPTask = NULL;
struct SPTask* sCurrentDisplaySPTask = NULL;
struct SPTask* sNextAudioSPTask = NULL;
struct SPTask* sNextDisplaySPTask = NULL;

struct Controller gControllers[NUM_PLAYERS];
struct Controller* gControllerOne = &gControllers[0];
struct Controller* gControllerTwo = &gControllers[1];
struct Controller* gControllerThree = &gControllers[2];
struct Controller* gControllerFour = &gControllers[3];
struct Controller* gControllerFive = &gControllers[4]; // All physical controllers combined.`
struct Controller* gControllerSix = &gControllers[5];
struct Controller* gControllerSeven = &gControllers[6];
struct Controller* gControllerEight = &gControllers[7];

Player gPlayers[NUM_PLAYERS];
Player* gPlayerOne = &gPlayers[0];
Player* gPlayerTwo = &gPlayers[1];
Player* gPlayerThree = &gPlayers[2];
Player* gPlayerFour = &gPlayers[3];
Player* gPlayerFive = &gPlayers[4];
Player* gPlayerSix = &gPlayers[5];
Player* gPlayerSeven = &gPlayers[6];
Player* gPlayerEight = &gPlayers[7];

Player* gPlayerOneCopy = &gPlayers[0];
Player* gPlayerTwoCopy = &gPlayers[1];
//UNUSED Player* gPlayerThreeCopy = &gPlayers[2];
//UNUSED Player* gPlayerFourCopy = &gPlayers[3];

//UNUSED s32 D_800FD850[3];
struct GfxPool* gGfxPool;

//UNUSED s32 gfxPool_padding; // is this necessary?
struct VblankHandler gGameVblankHandler;
struct VblankHandler sSoundVblankHandler;
OSMesgQueue gDmaMesgQueue, gGameVblankQueue, gGfxVblankQueue, unused_gMsgQueue, gIntrMesgQueue, gSPTaskMesgQueue;
OSMesgQueue sSoundMesgQueue;
OSMesg sSoundMesgBuf[1];
OSMesg gDmaMesgBuf[1], gGameMesgBuf;
OSMesg gGfxMesgBuf[1];
//UNUSED OSMesg D_8014F010, D_8014F014;
OSMesg gIntrMesgBuf[16], gSPTaskMesgBuf[16];
OSMesg gMainReceivedMesg;
OSIoMesg gDmaIoMesg;
OSMesgQueue gSIEventMesgQueue;
OSMesg gSIEventMesgBuf[3];

OSContStatus gControllerStatuses[4];
OSContPad gControllerPads[4];
u8 gControllerBits;
// Contains a 32x32 grid of indices into gCollisionIndices containing indices into gCollisionMesh
CollisionGrid gCollisionGrid[1024];
u16 gNumActors;
u16 gMatrixObjectCount;
s32 gTickSpeed;
f32 D_80150118;

u16 wasSoftReset;
u16 D_8015011E;

s32 D_80150120;
s32 gGotoMode;
UNUSED s32 D_80150128;
UNUSED s32 D_8015012C;
f32 gCameraZoom[4]; // look like to be the fov of each character
UNUSED s32 D_80150140;
UNUSED s32 D_80150144;
f32 gScreenAspect;
f32 gCourseFarPersp;
f32 gCourseNearPersp;
UNUSED f32 D_80150154;

struct D_80150158 gD_80150158[16];
uintptr_t gSegmentTable[16];
Gfx* gDisplayListHead;

struct SPTask* gGfxSPTask;
s32 D_801502A0;
s32 D_801502A4;
u16* gPhysicalFramebuffers[3];
uintptr_t gPhysicalZBuffer = &gZBuffer;
UNUSED u32 D_801502B8;
UNUSED u32 D_801502BC;
Mat4 D_801502C0;

s32 padding[2048];

u16 D_80152300[4];
u16 D_80152308;

OSMesg gPIMesgBuf[32];
OSMesgQueue gPIMesgQueue;

s32 gGamestate = 0xFFFF;
// D_800DC510 is externed as an s32 in other files. D_800DC514 is only used in main.c, likely a developer mistake.
u16 D_800DC510 = 0;
u16 D_800DC514 = 0;
u16 creditsRenderMode = 0; // Renders the whole track. Displays red if used in normal race mode.
u16 gDemoMode = DEMO_MODE_INACTIVE;
u16 gEnableDebugMode = ENABLE_DEBUG_MODE;
s32 gGamestateNext = 7; // = COURSE_DATA_MENU?;
UNUSED s32 D_800DC528 = 1;
s32 gActiveScreenMode = SCREEN_MODE_1P;
s32 gScreenModeSelection = SCREEN_MODE_1P;
UNUSED s32 D_800DC534 = 0;
s32 gPlayerCountSelection1 = 2;

s32 gModeSelection = GRAND_PRIX;
s32 D_800DC540 = 0;
s32 D_800DC544 = 0;
s32 gCCSelection = CC_50;
s32 gGlobalTimer = 0;
UNUSED s32 D_800DC550 = 0;
UNUSED s32 D_800DC554 = 0;
UNUSED s32 D_800DC558 = 0;
// Framebuffer rendering values (max 3)
u16 sRenderedFramebuffer = 0;
u16 sRenderingFramebuffer = 0;
UNUSED u16 D_800DC564 = 0;
s32 D_800DC568 = 0;
s32 D_800DC56C[8] = { 0 };
s16 sNumVBlanks = 0;
UNUSED s16 D_800DC590 = 0;
f32 gVBlankTimer = 0.0f;
f32 gCourseTimer = 0.0f;
int inited = 0;

#include "gfx/gfx_pc.h"

int ever_loaded_save_yet = 0;

/*
 * Backend selection is a platform decision. No graphics or window backend
 * exists in this branch; a real backend is registered in a later stage.
 * gfx_init fails explicitly while these are NULL.
 */
static struct GfxWindowManagerAPI *wm_api = NULL;
static struct GfxRenderingAPI *rendering_api = NULL;

extern void thread5_game_loop(void *arg);

#include "audio/audio_api.h"
#include "ie_audio_sample_table.h"
extern void game_audio_pump_voices(void);
extern void ie_audio_voice_note_reset_drained(void);

static struct AudioAPI *audio_api = NULL;

void platform_install_apis(struct GfxWindowManagerAPI* wm,
                           struct GfxRenderingAPI* rendering,
                           struct AudioAPI* audio) {
    wm_api = wm;
    rendering_api = rendering;
    audio_api = audio;
}

static int frameno = 0;
static int even_frame;

void game_loop_one_iteration(void) {
    even_frame = !((frameno++) & 1);

    gfx_start_frame();

    func_800CB2C4();

    // Update the gamestate if it has changed (racing, menus, credits, etc.).
    if (gGamestateNext != gGamestate) {
        gGamestate = gGamestateNext;
        update_gamestate();
    }

    config_gfx_pool();

    read_controllers();

    game_state_handler();

    end_master_display_list();

    display_and_vsync();

    gfx_end_frame();
}

static void send_display_list(struct SPTask *spTask) {
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

uint16_t __attribute__((aligned(32))) fb[3][4];

// mio0encode
s32 func_80040174(UNUSED void*, UNUSED s32, UNUSED s32) {
	return -1;
}

s32 mio0encode(UNUSED s32, UNUSED s32, UNUSED s32) {
	return -1;
}

u8 *_audio_banksSegmentRomStart;
u8 *_audio_tablesSegmentRomStart;
u8 *_audio_tablesPcm16SegmentRomStart;
u8 *_instrument_setsSegmentRomStart;
u8 *_sequencesSegmentRomStart;

//extern void init_all_sounds(void);
void _AudioInit(void);

#define AUDIOBANKS_SIZE 79936
#define AUDIOTABLES_SIZE 2409664
#define INSTRUMENT_SETS_SIZE 256
#define SEQUENCES_SIZE 143728

static u8* load_audio_segment(const char* name, size_t size) {
    u8* buf = malloc(size);

    if (buf == NULL) {
        platform_fatal("out of memory loading %s (%zu bytes)", name, size);
    }
    if (!platform_asset_read(name, buf, size, NULL)) {
        platform_fatal("failed to read audio asset %s", name);
    }
    return buf;
}

void game_audio_pump(void);

void setup_audio_data(void) {
    _audio_banksSegmentRomStart = load_audio_segment("audiobanks.bin", AUDIOBANKS_SIZE);
    _audio_tablesSegmentRomStart = load_audio_segment("audiotables.bin", AUDIOTABLES_SIZE);
    _audio_tablesPcm16SegmentRomStart = load_audio_segment("audiotables.pcm16.bin", IE_AUDIO_PCM16_SIZE);
    _instrument_setsSegmentRomStart = load_audio_segment("instrument_sets.bin", INSTRUMENT_SETS_SIZE);
    _sequencesSegmentRomStart = load_audio_segment("sequences.bin", SEQUENCES_SIZE);

    _AudioInit();

#ifdef IE_AUDIO_SVC
    /*
     * Audio service: worker B owns audio_init and the boot reset drain
     * (it publishes ready only after both). The main CPU keeps only the
     * producer half, so sound_init still runs here. On any service
     * failure fall through to the local path unchanged.
     */
    {
        extern int ie_audio_svc_init(void);

        if (ie_audio_svc_init()) {
            sound_init();
            return;
        }
    }
#endif

    audio_init();
    sound_init();

    /*
     * audio_init arms the audio reset state machine
     * (gAudioResetStatus); on the console the audio thread ran the
     * reset to completion before the game loaded any sequence. Here
     * the pump is driven from the game loop, so drain the reset now -
     * otherwise the first pumps run the shutdown steps after the SFX
     * sequence is enabled and silence it permanently.
     */
    {
        extern volatile u8 gAudioResetStatus;

        while (gAudioResetStatus != 0) {
            game_audio_pump();
        }
        ie_audio_voice_note_reset_drained();
    }
}

s32 osAppNmiBuffer[16];
void isPrintfInit(void);
extern int must_inval_bg;
extern int stupid_fucking_faces_hack;

int main(UNUSED int argc, UNUSED char **argv) {
    must_inval_bg = 0;
    stupid_fucking_faces_hack = 0;

    wasSoftReset = (s16)0;

    gPhysicalFramebuffers[0] = fb[0];
    gPhysicalFramebuffers[1] = fb[1];
    gPhysicalFramebuffers[2] = fb[2];

    setup_audio_data();

    thread5_game_loop(NULL);

    return 0;
}

void setup_mesg_queues(void) {
    return;
}

void start_sptask(UNUSED s32 taskType) {
    return;
}

/**
 * Initializes the Fast3D OSTask structure.
 * Loads F3DEX or F3DLX based on the number of players
 **/
void create_gfx_task_structure(void) {
    gGfxSPTask->msgqueue = NULL;
    // = &gGfxVblankQueue;
    gGfxSPTask->msg = (OSMesg) 2;
    gGfxSPTask->task.t.type = M_GFXTASK;
    gGfxSPTask->task.t.flags = OS_TASK_DP_WAIT;

    gGfxSPTask->task.t.ucode_boot = NULL;
    // = rspF3DBootStart;

    gGfxSPTask->task.t.ucode_boot_size = 0;
    // = ((u8*) rspF3DBootEnd - (u8*) rspF3DBootStart);

    // The split-screen multiplayer racing state uses F3DLX which has a simple subpixel calculation.
    // Singleplayer race mode and all other game states use F3DEX.
    // http://n64devkit.square7.ch/n64man/ucode/gspF3DEX.htm
    if (gGamestate != RACING || gPlayerCountSelection1 == 1) {
        gGfxSPTask->task.t.ucode = NULL;
        // = gspF3DEXTextStart;
        gGfxSPTask->task.t.ucode_data = NULL;
        // = gspF3DEXDataStart;
    } else {
        gGfxSPTask->task.t.ucode = NULL;
        // = gspF3DLXTextStart;
        gGfxSPTask->task.t.ucode_data = NULL;
        // = gspF3DLXDataStart;
    }
    gGfxSPTask->task.t.flags = 0;
    gGfxSPTask->task.t.flags = OS_TASK_DP_WAIT;
    gGfxSPTask->task.t.ucode_size = SP_UCODE_SIZE;
    gGfxSPTask->task.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    gGfxSPTask->task.t.dram_stack = NULL;
    // = (u64*) &gGfxSPTaskStack;
    gGfxSPTask->task.t.dram_stack_size = SP_DRAM_STACK_SIZE8;
    gGfxSPTask->task.t.output_buff = NULL;
    // = (u64*) &gGfxSPTaskOutputBuffer;
    gGfxSPTask->task.t.output_buff_size = NULL;
    // = (u64*) ((u8*) gGfxSPTaskOutputBuffer + sizeof(gGfxSPTaskOutputBuffer));
    gGfxSPTask->task.t.data_ptr = (u64*) gGfxPool->gfxPool;
    gGfxSPTask->task.t.data_size = (gDisplayListHead - gGfxPool->gfxPool) * sizeof(Gfx);
    gGfxSPTask->task.t.yield_data_ptr = NULL;
    // = (u64*) &gGfxSPTaskYieldBuffer;
    gGfxSPTask->task.t.yield_data_size = OS_YIELD_DATA_SIZE;
}

int held;
int sd_x,sd_y;

void init_controllers(void) {
    struct PlatformControllerState state;

    gControllerBits = 0;

    for (int i = 0; i < 4; i++) {
        if (platform_poll_controller(i, &state) && state.connected) {
            gControllerBits |= (1 << i);
        }
    }
    if ((gControllerBits & 1) == 0) {
        sIsController1Unplugged = 1;
    } else {
        sIsController1Unplugged = 0;
    }

    // jnmartin84 - my new vars
    sd_x = sd_y = held = 0;
}

extern void __osPfsCloseAllFiles(void);
u16 ucheld;
u16 stick;
void update_controller(s32 index) {
    struct Controller* controller = &gControllers[index];
    struct PlatformControllerState state;
    ucheld = 0;
    stick = 0;
    if (index > 3)
        return;
    if (!platform_poll_controller(index, &state) || !state.connected)
        return;

    /* The backend reports N64-layout buttons and N64-range stick values. */
    controller->rawStickX = state.stick_x;
    controller->rawStickY = state.stick_y;
    ucheld = state.buttons;

    controller->buttonPressed = ucheld & (ucheld ^ controller->button);
    controller->buttonDepressed = controller->button & (ucheld ^ controller->button);
    controller->button = ucheld;

    stick = 0;
    if (controller->rawStickX < -50)
        stick |= L_JPAD;
    if (controller->rawStickX > 50)
        stick |= R_JPAD;
    if (controller->rawStickY < -50)
        stick |= D_JPAD;
    if (controller->rawStickY > 50)
        stick |= U_JPAD;

    controller->stickPressed = stick & (stick ^ controller->stickDirection);
    controller->stickDepressed = controller->stickDirection & (stick ^ controller->stickDirection);
    controller->stickDirection = stick;
}

void read_controllers(void) {
    OSMesg msg;

    osRecvMesg(&gSIEventMesgQueue, &msg, OS_MESG_BLOCK);

    update_controller(0);
    update_controller(1);
    update_controller(2);
    update_controller(3);
    gControllerFive->button = (s16) (((gControllerOne->button | gControllerTwo->button) | gControllerThree->button) |
                                     gControllerFour->button);
    gControllerFive->buttonPressed =
        (s16) (((gControllerOne->buttonPressed | gControllerTwo->buttonPressed) | gControllerThree->buttonPressed) |
               gControllerFour->buttonPressed);
    gControllerFive->buttonDepressed = (s16) (((gControllerOne->buttonDepressed | gControllerTwo->buttonDepressed) |
                                               gControllerThree->buttonDepressed) |
                                              gControllerFour->buttonDepressed);
    gControllerFive->stickDirection =
        (s16) (((gControllerOne->stickDirection | gControllerTwo->stickDirection) | gControllerThree->stickDirection) |
               gControllerFour->stickDirection);
    gControllerFive->stickPressed =
        (s16) (((gControllerOne->stickPressed | gControllerTwo->stickPressed) | gControllerThree->stickPressed) |
               gControllerFour->stickPressed);
    gControllerFive->stickDepressed =
        (s16) (((gControllerOne->stickDepressed | gControllerTwo->stickDepressed) | gControllerThree->stickDepressed) |
               gControllerFour->stickDepressed);
}

void func_80000BEC(void) {
    gPhysicalZBuffer = VIRTUAL_TO_PHYSICAL(&gZBuffer);
}

void dispatch_audio_sptask(UNUSED struct SPTask* spTask) {
}

static void exec_display_list(UNUSED struct SPTask* spTask) {
	send_display_list(&gGfxPool->spTask);
}

/**
 * Set default RCP (Reality Co-Processor) settings.
 */
void init_rcp(void) {
    move_segment_table_to_dmem();
    init_rdp();
    set_viewport();
    select_framebuffer();
    init_z_buffer();
}

/**
 * End the master display list and initialize the graphics task structure for the next frame to be rendered.
 */
void end_master_display_list(void) {
    gDPFullSync(gDisplayListHead++);
    gSPEndDisplayList(gDisplayListHead++);
    create_gfx_task_structure();
}

// clear_frame_buffer from SM64, with a few edits
void clear_framebuffer(s32 color) {
    gDPPipeSync(gDisplayListHead++);

    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gDisplayListHead++, G_CYC_FILL);

    gDPSetFillColor(gDisplayListHead++, color);
    //gDPFillRectangle(gDisplayListHead++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

    gDPPipeSync(gDisplayListHead++);

    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
}

void rendering_init(void) {
    gGfxPool = &gGfxPools[0];
    set_segment_base_addr(1, gGfxPool);
    gGfxSPTask = &gGfxPool->spTask;
    gDisplayListHead = gGfxPool->gfxPool;
    init_rcp();
    clear_framebuffer(0);
    end_master_display_list();
    // don't do this yet
    // exec_display_list(&gGfxPool->spTask);
    sRenderingFramebuffer++;
    gGlobalTimer++;
}

void config_gfx_pool(void) {
    gGfxPool = &gGfxPools[gGlobalTimer & 1];
    set_segment_base_addr(1, gGfxPool);
    gDisplayListHead = gGfxPool->gfxPool;
    gGfxSPTask = &gGfxPool->spTask;
}

/**
 * Send current master display list for rendering.
 * Tell the VI which colour framebuffer to display.
 * Yields to the VI framerate twice, locking the game at 30 FPS.
 * Selects the next framebuffer to be rendered and displayed.
 */
void display_and_vsync(void) {
    exec_display_list(&gGfxPool->spTask);

    if (++sRenderedFramebuffer == 3) {
        sRenderedFramebuffer = 0;
    }

    if (++sRenderingFramebuffer == 3) {
        sRenderingFramebuffer = 0;
    }

    gGlobalTimer++;
}

//void dma_copy(u8* dest, u8* romAddr, size_t size) {
//    n64_memcpy(segmented_to_virtual(dest), segmented_to_virtual(romAddr), size);
//}

#include "buffer_sizes.h"

extern u8 __attribute__((aligned(32))) COMMON_BUF[COMMON_BUF_SIZE];
extern u16 common_texture_minimap_kart_toad[];
extern u16 common_texture_minimap_kart_luigi[];
extern u16 common_texture_minimap_kart_peach[];
extern u16 common_texture_minimap_kart_donkey_kong[];
extern u16 common_texture_minimap_finish_line[];
extern u16 common_texture_minimap_kart_mario[];
extern u16 common_texture_minimap_kart_wario[];
extern u16 common_texture_minimap_kart_yoshi[];
extern u16 common_texture_minimap_kart_bowser[];
extern u16 common_texture_minimap_progress_dot[];
extern u16 common_tlut_bomb[];
extern u16 common_tlut_player_emblem[];
extern u16 common_tlut_finish_line_banner[];
extern u16 common_tlut_trees_import[];
extern u16 common_texture_hud_total_time[];
extern u16 common_texture_hud_time[];
extern u16 common_texture_hud_normal_digit[];
extern u16 common_texture_banana[];
extern u16 common_texture_flat_banana[];
extern u16 common_tlut_green_shell[];
extern u16 common_tlut_blue_shell[];
extern u16 common_texture_hud_123[];
extern u16 common_texture_hud_lap[];
extern u16 common_texture_hud_123[];
extern u16 common_texture_hud_lap_time[];
extern u16 common_texture_hud_lap_1_on_3[];
extern u16 common_texture_hud_lap_2_on_3[];
extern u16 common_texture_hud_lap_3_on_3[];
extern u16 common_tlut_portrait_mario[];
extern u16 common_tlut_portrait_luigi[];
extern u16 common_tlut_portrait_wario[];
extern u16 common_tlut_portrait_peach[];
extern u16 common_tlut_portrait_toad[];
extern u16 common_tlut_portrait_yoshi[];
extern u16 common_tlut_portrait_bowser[];
extern u16 common_tlut_portrait_donkey_kong[];
extern u16 common_tlut_portrait_bomb_kart_and_question_mark[];
extern u8 D_0D02AA58[];
extern u8 common_texture_portrait_mario[];
extern u8 common_texture_portrait_luigi[];
extern u8 common_texture_portrait_wario[];
extern u8 common_texture_portrait_peach[];
extern u8 common_texture_portrait_toad[];
extern u8 common_texture_portrait_yoshi[];
extern u8 common_texture_portrait_bowser[];
extern u8 common_texture_portrait_donkey_kong[];
extern u8 common_texture_item_box_question_mark[];
extern u16 common_texture_particle_leaf[];
extern u16 common_tlut_item_window_none[];

extern u16 common_tlut_item_window_banana[];

extern u16 common_tlut_item_window_banana_bunch[];

extern u16 common_tlut_item_window_mushroom[];

extern u16 common_tlut_item_window_double_mushroom[];

extern u16 common_tlut_item_window_triple_mushroom[];

extern u16 common_tlut_item_window_super_mushroom[];

extern u16 common_tlut_item_window_blue_shell[];

extern u16 common_tlut_item_window_boo[];

extern u16 common_tlut_item_window_green_shell[];
extern u16 common_tlut_item_window_triple_green_shell[];
extern u16 common_tlut_item_window_red_shell[];

extern u16 common_tlut_item_window_triple_red_shell[];

extern u16 common_tlut_item_window_star[];
extern u16 common_tlut_item_window_thunder_bolt[];

extern u16 common_tlut_item_window_fake_item_box[];
extern u16 common_tlut_lakitu_countdown[][256];
extern u16 common_tlut_lakitu_checkered_flag[];
extern u16 common_tlut_lakitu_second_lap[];
extern u16 common_tlut_lakitu_reverse[];
extern u16 common_tlut_lakitu_final_lap[];
extern u16 common_tlut_lakitu_fishing[];
extern u16 l_common_texture_minimap_kart_mario[][64];
extern void load_ceremony_data(void);

/**
 * Setup main segments and framebuffers.
 */
void setup_game_memory(void) {
    /* Segment 0 is an identity mapping: low segmented addresses are
     * already real (the old port pointed this at its RAM base). */
    set_segment_base_addr(0, NULL);
    func_80000BEC();

    /* Per-course values for load_course, from the neutral asset layout. */
    course_metadata_load();

    memset(COMMON_BUF, 0, sizeof(COMMON_BUF));
    set_segment_base_addr(2, SEG_DATA_START);

    if (!platform_asset_read("common_data.bin", COMMON_BUF, sizeof(COMMON_BUF), NULL)) {
        platform_fatal("failed to read asset common_data.bin");
    }

    set_segment_base_addr(0xD, (void*) COMMON_BUF);
    /*
     * The previous port byte-swapped a long list of common-data TLUTs and
     * texels here for its little-endian backend. The neutral translator
     * reads palettes and texels in their stored (big-endian) form, so the
     * data is used as-is now.
     */
    load_ceremony_data();
}

/**
 * @brief
 *
 */
void game_init_clear_framebuffer(void) {
    gGamestateNext = 0; // = START_MENU_FROM_QUIT?
    clear_framebuffer(0);
}

void race_logic_loop(void) {
    s16 i;
    u16 rotY;

    gMatrixObjectCount = 0;
    gMatrixEffectCount = 0;
    if (gIsGamePaused != 0) {
        func_80290B14();
    }
    if (gIsInQuitToMenuTransition != 0) {
        func_802A38B4();
        return;
    }

    if (sNumVBlanks >= 6) {
        sNumVBlanks = 5;
    }
    if (sNumVBlanks < 0) {
        sNumVBlanks = 1;
    }
    func_802A4EF4();
    
    gTickSpeed = 2;

    switch (gActiveScreenMode) {
        case SCREEN_MODE_1P:
            //gTickSpeed = 2;
            staff_ghosts_loop();
            if (gIsGamePaused == 0) {
                for (i = 0; i < gTickSpeed; i++) {
                    if (D_8015011E) {
                        gCourseTimer += COURSE_TIMER_ITER;
                    }
                    func_802909F0();
                    evaluate_collision_for_players_and_actors();
                    func_800382DC();
                    func_8001EE98(gPlayerOneCopy, camera1, 0);
                    func_80028F70();
                    func_8028F474();
                    func_80059AC8();
                    update_course_actors();
                    course_update_water();
                    func_8028FCBC();
                }
                func_80022744();
            }
            func_8005A070();
            sNumVBlanks = 0;
            ////profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
            D_8015F788 = 0;
            render_player_one_1p_screen();
            if (!gEnableDebugMode) {
                D_800DC514 = false;
            } else {
                if (D_800DC514) {

                    if ((gControllerOne->buttonPressed & R_TRIG) && (gControllerOne->button & A_BUTTON) &&
                        (gControllerOne->button & B_BUTTON)) {
                        D_800DC514 = false;
                    }

                    rotY = camera1->rot[1];
                    gDebugPathCount = D_800DC5EC->pathCounter;
                    if (rotY < 0x2000) {
                        func_80057A50(40, 100, "SOUTH  ", gDebugPathCount);
                    } else if (rotY < 0x6000) {
                        func_80057A50(40, 100, "EAST   ", gDebugPathCount);
                    } else if (rotY < 0xA000) {
                        func_80057A50(40, 100, "NORTH  ", gDebugPathCount);
                    } else if (rotY < 0xE000) {
                        func_80057A50(40, 100, "WEST   ", gDebugPathCount);
                    } else {
                        func_80057A50(40, 100, "SOUTH  ", gDebugPathCount);
                    }

                } else {
                    if ((gControllerOne->buttonPressed & L_TRIG) && (gControllerOne->button & A_BUTTON) &&
                        (gControllerOne->button & B_BUTTON)) {
                        D_800DC514 = true;
                    }
                }
            }
            break;

        case SCREEN_MODE_2P_SPLITSCREEN_VERTICAL:
            /* if (gCurrentCourseId == COURSE_DK_JUNGLE) {
                gTickSpeed = 3;
            } else {
                gTickSpeed = 2;
            } */
            if (gIsGamePaused == 0) {
                for (i = 0; i < gTickSpeed; i++) {
                    if (D_8015011E != 0) {
                        gCourseTimer += COURSE_TIMER_ITER;
                    }
                    func_802909F0();
                    evaluate_collision_for_players_and_actors();
                    func_800382DC();
                    func_8001EE98(gPlayerOneCopy, camera1, 0);
                    func_80029060();
                    func_8001EE98(gPlayerTwoCopy, camera2, 1);
                    func_80029150();
                    func_8028F474();
                    func_80059AC8();
                    update_course_actors();
                    course_update_water();
                    func_8028FCBC();
                }
                func_80022744();
            }
            func_8005A070();
            ////profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
            sNumVBlanks = 0;
            move_segment_table_to_dmem();
            init_rdp();
            if (D_800DC5B0 != 0) {
                select_framebuffer();
            }
            D_8015F788 = 0;
            if (gPlayerWinningIndex == 0) {
                render_player_two_2p_screen_vertical();
                render_player_one_2p_screen_vertical();
            } else {
                render_player_one_2p_screen_vertical();
                render_player_two_2p_screen_vertical();
            }
            break;

        case SCREEN_MODE_2P_SPLITSCREEN_HORIZONTAL:

             if (gCurrentCourseId == COURSE_DK_JUNGLE ||
                gCurrentCourseId == COURSE_TOADS_TURNPIKE) {
                gTickSpeed = 3;
            } else {
                gTickSpeed = 2;
            }
//            gTickSpeed = 3;

            if (gIsGamePaused == 0) {
                for (i = 0; i < gTickSpeed; i++) {
                    if (D_8015011E != 0) {
                        gCourseTimer += COURSE_TIMER_ITER;
                    }
                    func_802909F0();
                    evaluate_collision_for_players_and_actors();
                    func_800382DC();
                    func_8001EE98(gPlayerOneCopy, camera1, 0);
                    func_80029060();
                    func_8001EE98(gPlayerTwoCopy, camera2, 1);
                    func_80029150();
                    func_8028F474();
                    func_80059AC8();
                    update_course_actors();
                    course_update_water();
                    func_8028FCBC();
                }
                func_80022744();
            }
            ////profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
            sNumVBlanks = (u16) 0;
            func_8005A070();
            move_segment_table_to_dmem();
            init_rdp();
            if (D_800DC5B0 != 0) {
                select_framebuffer();
            }
            D_8015F788 = 0;
            if (gPlayerWinningIndex == 0) {
                render_player_two_2p_screen_horizontal();
                render_player_one_2p_screen_horizontal();
            } else {
                render_player_one_2p_screen_horizontal();
                render_player_two_2p_screen_horizontal();
            }

            break;

        case SCREEN_MODE_3P_4P_SPLITSCREEN:
            /* if (gPlayerCountSelection1 == 3) {
                switch (gCurrentCourseId) {
                    case COURSE_BOWSER_CASTLE:
                    case COURSE_MOO_MOO_FARM:
                    case COURSE_SKYSCRAPER:
                    case COURSE_DK_JUNGLE:
                        gTickSpeed = 3;
                        break;
                    default:
                        gTickSpeed = 2;
                        break;
                }
            } else {
                // Four players
                switch (gCurrentCourseId) {
                    case COURSE_BLOCK_FORT:
                    case COURSE_DOUBLE_DECK:
                    case COURSE_BIG_DONUT:
                        gTickSpeed = 2;
                        break;
                    case COURSE_DK_JUNGLE:
                        gTickSpeed = 4;
                        break;
                    default:
                        gTickSpeed = 3;
                        break;
                }
            } */
            if (gIsGamePaused == 0) {
                for (i = 0; i < gTickSpeed; i++) {
                    if (D_8015011E != 0) {
                        gCourseTimer += COURSE_TIMER_ITER;
                    }
                    func_802909F0();
                    evaluate_collision_for_players_and_actors();
                    func_800382DC();
                    func_8001EE98(gPlayerOneCopy, camera1, 0);
                    func_80029158();
                    func_8001EE98(gPlayerTwo, camera2, 1);
                    func_800291E8();
                    func_8001EE98(gPlayerThree, camera3, 2);
                    func_800291F0();
                    func_8001EE98(gPlayerFour, camera4, 3);
                    func_800291F8();
                    func_8028F474();
                    func_80059AC8();
                    update_course_actors();
                    course_update_water();
                    func_8028FCBC();
                }
                func_80022744();
            }
            func_8005A070();
            sNumVBlanks = 0;
            ////profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
            move_segment_table_to_dmem();
            init_rdp();
            if (D_800DC5B0 != 0) {
                select_framebuffer();
            }
            D_8015F788 = 0;
            if (gPlayerWinningIndex == 0) {
                render_player_two_3p_4p_screen();
                render_player_three_3p_4p_screen();
                render_player_four_3p_4p_screen();
                render_player_one_3p_4p_screen();
            } else if (gPlayerWinningIndex == 1) {
                render_player_one_3p_4p_screen();
                render_player_three_3p_4p_screen();
                render_player_four_3p_4p_screen();
                render_player_two_3p_4p_screen();
            } else if (gPlayerWinningIndex == 2) {
                render_player_one_3p_4p_screen();
                render_player_two_3p_4p_screen();
                render_player_four_3p_4p_screen();
                render_player_three_3p_4p_screen();
            } else {
                render_player_one_3p_4p_screen();
                render_player_two_3p_4p_screen();
                render_player_three_3p_4p_screen();
                render_player_four_3p_4p_screen();
            }
            break;
    }

    if (!gEnableDebugMode) {
        gEnableResourceMeters = 0;
    } else {
        if (gEnableResourceMeters) {
            resource_display();
            if ((!(gControllerOne->button & L_TRIG)) && (gControllerOne->button & R_TRIG) &&
                (gControllerOne->buttonPressed & B_BUTTON)) {
                gEnableResourceMeters = 0;
            }
        } else {
            if ((!(gControllerOne->button & L_TRIG)) && (gControllerOne->button & R_TRIG) &&
                (gControllerOne->buttonPressed & B_BUTTON)) {
                gEnableResourceMeters = 1;
            }
        }
    }
    draw_splitscreen_separators();
    func_800591B4();
    func_80093E20();
#if DVDL
    display_dvdl();
#endif
    gDPFullSync(gDisplayListHead++);
    gSPEndDisplayList(gDisplayListHead++);
}

/**
 * mk64's game loop depends on a series of states.
 * It runs a wide branching series of code based on these states.
 * State 1) Clear framebuffer
 * State 2) Run menus
 * State 3) Process race related logic
 * State 4) Ending sequence
 * State 5) Credits
 *
 * Note that the state doesn't flip-flop at random but is permanent
 * until the state changes (ie. Exit menus and start a race).
 */

void game_state_handler(void) {
#if DVDL
    if ((gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) && (gControllerOne->button & Z_TRIG) &&
        (gControllerOne->button & A_BUTTON)) {
        gGamestateNext = CREDITS_SEQUENCE;
    } else if ((gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) &&
               (gControllerOne->button & Z_TRIG) && (gControllerOne->button & B_BUTTON)) {
        gGamestateNext = ENDING;
    }
#endif

    switch (gGamestate) {
        case 7:
            game_init_clear_framebuffer();
            break;
        case START_MENU_FROM_QUIT:
        case MAIN_MENU_FROM_QUIT:
        case PLAYER_SELECT_MENU_FROM_QUIT:
        case COURSE_SELECT_MENU_FROM_QUIT:
            // Display black
            // 
            update_menus();
            init_rcp();
            func_80094A64(gGfxPool);
#if DVDL
            display_dvdl();
#endif
            break;
        case RACING:
            race_logic_loop();
            break;
        case ENDING:
            podium_ceremony_loop();
            break;
        case CREDITS_SEQUENCE:
            credits_loop();
            break;
    }
}

void interrupt_gfx_sptask(void) {
    if (gActiveSPTask->task.t.type == M_GFXTASK) {
        gActiveSPTask->state = SPTASK_STATE_INTERRUPTED;
        osSpTaskYield();
    }
}

void receive_new_tasks(void) {
    UNUSED s32 pad;
    struct SPTask* spTask;

    while (osRecvMesg(&gSPTaskMesgQueue, (OSMesg*) &spTask, OS_MESG_NOBLOCK) != -1) {
        spTask->state = SPTASK_STATE_NOT_STARTED;
        switch (spTask->task.t.type) {
            case 2:
                sNextAudioSPTask = spTask;
                break;
            case 1:
                sNextDisplaySPTask = spTask;
                break;
        }
    }

    if (sCurrentAudioSPTask == NULL && sNextAudioSPTask != NULL) {
        sCurrentAudioSPTask = sNextAudioSPTask;
        sNextAudioSPTask = NULL;
    }
    if (sCurrentDisplaySPTask == NULL && sNextDisplaySPTask != NULL) {
        sCurrentDisplaySPTask = sNextDisplaySPTask;
        sNextDisplaySPTask = NULL;
    }
}

void set_vblank_handler(s32 index, struct VblankHandler* handler, OSMesgQueue* queue, OSMesg* msg) {
    handler->queue = queue;
    handler->msg = msg;
    switch (index) {
        case 1:
            gVblankHandler1 = handler;
            break;
        case 2:
            gVblankHandler2 = handler;
            break;
    }
}

void start_gfx_sptask(void) {
    if (gActiveSPTask == NULL && sCurrentDisplaySPTask != NULL &&
        sCurrentDisplaySPTask->state == SPTASK_STATE_NOT_STARTED) {
        ////profiler_log_gfx_time(TASKS_QUEUED);
        start_sptask(M_GFXTASK);
    }
}

void handle_vblank(void) {
    gVBlankTimer += V_BlANK_TIMER_ITER;
    sNumVBlanks++;

    receive_new_tasks();

    // First try to kick off an audio task. If the gfx task is currently
    // running, we need to asynchronously interrupt it -- handle_sp_complete
    // will pick up on what we're doing and start the audio task for us.
    // If there is already an audio task running, there is nothing to do.
    // If there is no audio task available, try a gfx task instead.
    if (sCurrentAudioSPTask != NULL) {
        if (gActiveSPTask != NULL) {
            interrupt_gfx_sptask();
        } else {
            ////profiler_log_vblank_time();
            start_sptask(M_AUDTASK);
        }
    } else {
        if (gActiveSPTask == NULL && sCurrentDisplaySPTask != NULL &&
            sCurrentDisplaySPTask->state != SPTASK_STATE_FINISHED) {
            ////profiler_log_gfx_time(TASKS_QUEUED);
            start_sptask(M_GFXTASK);
        }
    }

/* This is where I would put my rumble code... If I had any. */
#if ENABLE_RUMBLE
    rumble_thread_update_vi();
#endif

    if (gVblankHandler1 != NULL) {
        osSendMesg(gVblankHandler1->queue, gVblankHandler1->msg, OS_MESG_NOBLOCK);
    }
    if (gVblankHandler2 != NULL) {
        osSendMesg(gVblankHandler2->queue, gVblankHandler2->msg, OS_MESG_NOBLOCK);
    }
}

void handle_dp_complete(void) {
    // Gfx SP task is completely done.
    if (sCurrentDisplaySPTask->msgqueue != NULL) {
        osSendMesg(sCurrentDisplaySPTask->msgqueue, sCurrentDisplaySPTask->msg, OS_MESG_NOBLOCK);
    }
    ////profiler_log_gfx_time(RDP_COMPLETE);
    sCurrentDisplaySPTask->state = SPTASK_STATE_FINISHED_DP;
    sCurrentDisplaySPTask = NULL;
}

void handle_sp_complete(void) {
    struct SPTask* curSPTask = gActiveSPTask;

    gActiveSPTask = NULL;

    if (curSPTask->state == SPTASK_STATE_INTERRUPTED) {
        // handle_vblank tried to start an audio task while there was already a
        // gfx task running, so it had to interrupt the gfx task. That interruption
        // just finished.
        if (osSpTaskYielded((OSTask*) curSPTask) == 0) {
            // The gfx task completed before we had time to interrupt it.
            // Mark it finished, just like below.
            curSPTask->state = SPTASK_STATE_FINISHED;
            ////profiler_log_gfx_time(RSP_COMPLETE);
        }
        // Start the audio task, as expected by handle_vblank.
        ////profiler_log_vblank_time();
        start_sptask(M_AUDTASK);
    } else {
        curSPTask->state = SPTASK_STATE_FINISHED;
        if (curSPTask->task.t.type == M_AUDTASK) {
            // After audio tasks come gfx tasks.
            ////profiler_log_vblank_time();
            if (sCurrentDisplaySPTask != NULL) {
                if (sCurrentDisplaySPTask->state != SPTASK_STATE_FINISHED) {
                    if (sCurrentDisplaySPTask->state != SPTASK_STATE_INTERRUPTED) {
                        ////profiler_log_gfx_time(TASKS_QUEUED);
                    }
                    start_sptask(M_GFXTASK);
                }
            }
            sCurrentAudioSPTask = NULL;
            if (curSPTask->msgqueue != NULL) {
                osSendMesg(curSPTask->msgqueue, curSPTask->msg, OS_MESG_NOBLOCK);
            }
        } else {
            // The SP process is done, but there is still a Display Processor notification
            // that needs to arrive before we can consider the task completely finished and
            // null out sCurrentDisplaySPTask. That happens in handle_dp_complete.
            ////profiler_log_gfx_time(RSP_COMPLETE);
        }
    };
}

void thread3_video(UNUSED void* arg0) {
#if 0
    s32 i;
    u64* framebuffer1;
    OSMesg msg;
    UNUSED s32 pad[4];

    gPhysicalFramebuffers[0] = (u16*) &gFramebuffer0;
    gPhysicalFramebuffers[1] = (u16*) &gFramebuffer1;
    gPhysicalFramebuffers[2] = (u16*) &gFramebuffer2;

    // Clear framebuffer.
    framebuffer1 = (u64*) &gFramebuffer1;
    for (i = 0; i < 19200; i++) {
        framebuffer1[i] = 0;
    }
    setup_mesg_queues();
    setup_game_memory();

    create_thread(&gAudioThread, 4, &thread4_audio, 0, gAudioThreadStack + ARRAY_COUNT(gAudioThreadStack), 20);
    osStartThread(&gAudioThread);

    create_thread(&gGameLoopThread, 5, &thread5_game_loop, 0, gGameLoopThreadStack + ARRAY_COUNT(gGameLoopThreadStack),
                  10);
    osStartThread(&gGameLoopThread);

    while (true) {
        osRecvMesg(&gIntrMesgQueue, &msg, OS_MESG_BLOCK);
        switch ((u32) msg) {
            case MESG_VI_VBLANK:
                handle_vblank();
                break;
            case MESG_SP_COMPLETE:
                handle_sp_complete();
                break;
            case MESG_DP_COMPLETE:
                handle_dp_complete();
                break;
            case MESG_START_GFX_SPTASK:
                start_gfx_sptask();
                break;
        }
    }
#endif
}

void func_800025D4(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    set_perspective_and_aspect_ratio();
}

void func_80002600(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    set_perspective_and_aspect_ratio();
}

void func_8000262C(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    set_perspective_and_aspect_ratio();
}

void func_80002658(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    set_perspective_and_aspect_ratio();
}

int credits_started = 0;

void update_gamestate(void) {
    switch (gGamestate) {
        case START_MENU_FROM_QUIT:
            func_80002658();
            gCurrentlyLoadedCourseId = COURSE_NULL;
            break;
        case MAIN_MENU_FROM_QUIT:
            func_800025D4();
            gCurrentlyLoadedCourseId = COURSE_NULL;
            break;
        case PLAYER_SELECT_MENU_FROM_QUIT:
            func_80002600();
            gCurrentlyLoadedCourseId = COURSE_NULL;
            break;
        case COURSE_SELECT_MENU_FROM_QUIT:
            func_8000262C();
            gCurrentlyLoadedCourseId = COURSE_NULL;
            break;
        case RACING:
            setup_race();
            break;
        case ENDING:
            gCurrentlyLoadedCourseId = COURSE_NULL;
            load_ceremony_cutscene();
            break;
        case CREDITS_SEQUENCE:
            credits_started = 1;
            gCurrentlyLoadedCourseId = COURSE_NULL;
            load_credits();
            break;
    }
}

void thread5_game_loop(UNUSED void* arg) {
    setup_mesg_queues();
    setup_game_memory();

    gfx_init(wm_api, rendering_api, "Mario Kart 64", false);

    osCreateMesgQueue(&gGfxVblankQueue, gGfxMesgBuf, 1);
    osCreateMesgQueue(&gGameVblankQueue, &gGameMesgBuf, 1);

    init_controllers();
    if (!wasSoftReset) {
        clear_nmi_buffer();
    }

    // These variables track stats such as player wins.
    // In the event of a console reset, it remembers them.
    gNmiUnknown1 = &pAppNmiBuffer[0]; // 2  u8's, tracks number of times player 1/2 won a VS race
    gNmiUnknown2 =
        &pAppNmiBuffer[2]; // 9  u8's, 3x3, tracks number of times player 1/2/3   has placed in 1st/2nd/3rd in a VS race
    gNmiUnknown3 = &pAppNmiBuffer[11]; // 12 u8's, 4x3, tracks number of times player 1/2/3/4 has placed in 1st/2nd/3rd
                                       // in a VS race
    gNmiUnknown4 = &pAppNmiBuffer[23]; // 2  u8's, tracking number of Battle mode wins by player 1/2
    gNmiUnknown5 = &pAppNmiBuffer[25]; // 3  u8's, tracking number of Battle mode wins by player 1/2/3
    gNmiUnknown6 = &pAppNmiBuffer[28]; // 4  u8's, tracking number of Battle mode wins by player 1/2/3/4
    rendering_init();
    read_controllers();
    func_800C5CB8();
    inited = 1;

    while (true) {
        game_loop_one_iteration();
    }
}

void _AudioInit(void) {
    /*
     * No audio backend exists in this branch. A real backend registers
     * itself here in a later stage; until then this must fail loudly
     * rather than pretend audio is available.
     */
    if (audio_api == NULL) {
        platform_fatal("no audio backend registered");
    }
    audio_api->init();
}

/*
 * Audio pump: the platform backend must call this once per vblank (see the
 * scheduling contract in platform/platform.h). The previous console port
 * ran this on a helper thread woken from the vblank interrupt.
 */
void game_audio_pump(void) {
#ifdef IE_AUDIO_SVC
    extern int ie_audio_svc_active(void);
    extern void ie_audio_svc_pump(void);

    if (ie_audio_svc_active()) {
        ie_audio_svc_pump();
        return;
    }
#endif
    game_audio_pump_voices();
}
