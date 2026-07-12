#ifndef GFX_PC_H
#define GFX_PC_H

#include <stdint.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct GfxDimensions {
    uint32_t width, height;
    float aspect_ratio;
};

extern struct GfxDimensions gfx_current_dimensions;

#ifdef __cplusplus
extern "C" {
#endif

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *game_name, uint8_t start_in_fullscreen);
struct GfxRenderingAPI *gfx_get_current_rendering_api(void);
void gfx_start_frame(void);
void gfx_run(Gfx *commands);
void gfx_end_frame(void);

/* Mark every cached texture decoded from addr for re-upload. */
void gfx_texture_cache_invalidate(void *addr);
/* Drop all cached textures and force full state resubmission. */
void nuke_everything(void);

#ifdef __cplusplus
}
#endif

#endif
