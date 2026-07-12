#include <ultra64.h>
#include <macros.h>
#include <defines.h>
#include <segments.h>
#include <mk64.h>
#include <course.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_80281780.h"
#include "memory.h"
#include "camera.h"
#include "camera_junk.h"
#include "spawn_players.h"
#include "skybox_and_splitscreen.h"
#include "code_8006E9C0.h"
#include "podium_ceremony_actors.h"
#include "cpu_vehicles_camera_path.h"
#include "collision.h"
#include "code_80281C40.h"
#include "code_800029B0.h"
#include "menu_items.h"
#include "main.h"
#include "menus.h"
#include "render_courses.h"
#include "platform/platform.h"

u8 defaultCharacterIds[] = { 1, 2, 3, 4, 5, 6, 7, 0 };

void debug_switch_character_ceremony_cutscene(void) {
    if (gEnableDebugMode) {
        if (gControllerOne->button & HOLD_ALL_DPAD_AND_C_BUTTONS) {
            // Allows to switch character in debug mode?
            if (gControllerOne->button & U_CBUTTONS) {
                gCharacterSelections[0] = LUIGI;
            } else if (gControllerOne->button & L_CBUTTONS) {
                gCharacterSelections[0] = YOSHI;
            } else if (gControllerOne->button & R_CBUTTONS) {
                gCharacterSelections[0] = TOAD;
            } else if (gControllerOne->button & D_CBUTTONS) {
                gCharacterSelections[0] = DK;
            } else if (gControllerOne->button & U_JPAD) {
                gCharacterSelections[0] = WARIO;
            } else if (gControllerOne->button & L_JPAD) {
                gCharacterSelections[0] = PEACH;
            } else if (gControllerOne->button & R_JPAD) {
                gCharacterSelections[0] = BOWSER;
            } else {
                gCharacterSelections[0] = MARIO;
            }
            //! @todo confirm this.
            // Resets gCharacterIdByGPOverallRank to default?
            bcopy(&defaultCharacterIds, &gCharacterIdByGPOverallRank, 8);
        }
    }
}

s32 func_80281880(s32 arg0) {
    s32 i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (gCharacterIdByGPOverallRank[i] == gCharacterSelections[arg0]) {
            break;
        }
    }
    return i;
}

void func_802818BC(void) {
    s32 temp_v0;
    UNUSED s32 pad;
    s32 sp1C;
    s32 temp_v0_2;

    if (gPlayerCount != TWO_PLAYERS_SELECTED) {
        D_802874D8.unk1D = func_80281880(0);
        D_802874D8.unk1E = gCharacterSelections[0];
        return;
    }
    // weird pattern but if it matches it matches
    temp_v0 = sp1C = func_80281880(0);
    temp_v0_2 = func_80281880(1);
    if (sp1C < temp_v0_2) {
        D_802874D8.unk1E = gCharacterSelections[0];
        D_802874D8.unk1D = temp_v0;
    } else {
        D_802874D8.unk1E = gCharacterSelections[1];
        D_802874D8.unk1D = temp_v0_2;
    }
}

#include "buffer_sizes.h"

/*
 * Royal Raceway displaylists used for the ceremony collision mesh.
 * These live in the unpacked segment-7 stream produced by
 * displaylist_unpack at load_course time; the constants are the
 * original segment-7 byte offsets (8-byte Gfx units), converted to
 * native Gfx indexing so they stay correct when Gfx is wider.
 */
extern Gfx __attribute__((aligned(32))) UNPACKED_DL_BUF[UNPACKED_DL_BUF_SIZE / 8];
#define ROYAL_RACEWAY_UNPACKED_DL(offset) (&UNPACKED_DL_BUF[(offset) / 8])
extern uint8_t __attribute__((aligned(32))) CEREMONY_BUF[CEREMONY_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) COURSE_BUF[COURSE_BUF_SIZE];
extern u16 reflection_map_silver[1024];
extern u16 reflection_map_gold[1024];
extern u16 reflection_map_brass[1024];
extern CollisionTriangle __attribute__((aligned(32))) allColTris[allColTris_SIZE];

extern u16 gTexturePodium1[];
extern u16 gTexturePodium2[];
extern u16 gTexturePodium3[];

void load_ceremony_data(void) {
    if (!platform_asset_read("ceremony_data.bin", CEREMONY_BUF, sizeof(CEREMONY_BUF), NULL)) {
        platform_fatal("failed to read asset ceremony_data.bin");
    }
    set_segment_base_addr(0xB, (void*) CEREMONY_BUF);
    /* Podium and reflection-map texels stay in stored (big-endian)
     * form; the translator decodes them at import time. */
}

void load_ceremony_cutscene(void) {
    Camera* camera = &cameras[0];
    memset(&D_802874D8, 0, sizeof(D_802874D8));
    memset(sPodiumActorList, 0, (sizeof(CeremonyActor) * 200));
    sPodiumActorList = NULL;

    gCurrentCourseId = COURSE_ROYAL_RACEWAY;
    D_800DC5B4 = (u16) 1;
    gIsMirrorMode = 0;
    gGotoMenu = 0xFFFF;
    D_80287554 = 0;
    set_perspective_and_aspect_ratio();
    func_802A74BC();
    camera->unk_B4 = 60.0f;
    gCameraZoom[0] = 60.0f;
    D_800DC5EC->screenWidth = SCREEN_WIDTH;
    D_800DC5EC->screenHeight = SCREEN_HEIGHT;
    D_800DC5EC->screenStartX = 160;
    D_800DC5EC->screenStartY = 120;
    gScreenModeSelection = SCREEN_MODE_1P;
    gActiveScreenMode = SCREEN_MODE_1P;
    gModeSelection = GRAND_PRIX;
    load_course(gCurrentCourseId);
    load_ceremony_data();

    if (!platform_asset_read("banshee_boardwalk_data.bin", COURSE_BUF, sizeof(COURSE_BUF), NULL)) {
        platform_fatal("failed to read asset banshee_boardwalk_data.bin");
    }

    set_segment_base_addr(6, (void*) COURSE_BUF);

    D_8015F8E4 = -2000.0f;

    gCourseMinX = -0x15A1;
    gCourseMinY = -0x15A1;
    gCourseMinZ = -0x15A1;

    gCourseMaxX = 0x15A1;
    gCourseMaxY = 0x15A1;
    gCourseMaxZ = 0x15A1;

    D_8015F59C = 0;
    D_8015F5A0 = 0;
    D_8015F58C = 0;
    gCollisionMeshCount = (u16) 0;
    D_800DC5BC = (u16) 0;
    D_800DC5C8 = (u16) 0;
    gCollisionMesh = (CollisionTriangle*) allColTris;
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0x67E8), -1);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xAEF8), -1);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xA970), 8);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xAC30), 8);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xCE0), 0x10);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xE88), 0x10);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xA618), -1);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0xA618), -1);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0x23F8), 1);
    generate_collision_mesh_with_default_section_id(ROYAL_RACEWAY_UNPACKED_DL(0x2478), 1);
    func_80295C6C();
    debug_switch_character_ceremony_cutscene();
    func_802818BC();
    func_8003D080();
    init_hud();
    func_8001C05C();
    balloons_and_fireworks_init();
    init_camera_podium_ceremony();
    func_80093E60();
}
