#include <ultra64.h>
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/synthesis.h"

struct SynthesisReverb gSynthesisReverbs[4] = {0};

void func_800B6FB4(s32 updateIndexStart, s32 noteIndex) {
    s32 i = 0;

    for (i = updateIndexStart + 1; i < gAudioBufferParameters.updatesPerFrame; i++) {
        if (!gNoteSubsEu[gMaxSimultaneousNotes * i + noteIndex].needsInit) {
            gNoteSubsEu[gMaxSimultaneousNotes * i + noteIndex].enabled = 0;
        } else {
            break;
        }
    }
}

void synthesis_load_note_subs_eu(s32 updateIndex) {
    struct NoteSubEu* src = NULL;
    struct NoteSubEu* dest = NULL;
    s32 i = 0;

    for (i = 0; i < gMaxSimultaneousNotes; i++) {
        src = &gNotes[i].noteSubEu;
        dest = &gNoteSubsEu[gMaxSimultaneousNotes * updateIndex + i];
        if (src->enabled) {
            *dest = *src;
            src->needsInit = 0;
        } else {
            dest->enabled = 0;
        }
    }
}
