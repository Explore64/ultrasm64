#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "area.h"
#include "camera.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game/game_init.h"
#include "geo_misc.h"
#include "gfx_dimensions.h"
#include "memory.h"
#include "screen_transition.h"
#include "segment2.h"
#include "print.h"
#include "sm64.h"
#include "ingame_menu.h"
#include "rendering_graph_node.h"

u8 sTransitionColorFadeCount[4] = { 0 };
u16 sTransitionTextureFadeCount[2] = { 0 };
f32 sTransitionColorFadeCountLerp[4] = { 0 };
f32 sTransitionTextureFadeCountLerp[2] = { 0 };

s32 set_and_reset_transition_fade_timer(s8 fadeTimer, u8 transTime) {
    s32 reset = FALSE;

    if (sTransitionColorFadeCount[fadeTimer] >= transTime) {
        sTransitionColorFadeCount[fadeTimer] = 0;
        sTransitionColorFadeCountLerp[fadeTimer] = 0;
        sTransitionTextureFadeCount[fadeTimer] = 0;
        sTransitionTextureFadeCountLerp[fadeTimer] = 0;
        reset = TRUE;
    }
    return reset;
}

u32 set_transition_color_fade_alpha(s8 fadeType, s8 fadeTimer, u8 transTime) {
    u8 time = 0;

    sTransitionColorFadeCountLerp[fadeTimer] = approach_f32_asymptotic(sTransitionColorFadeCountLerp[fadeTimer], sTransitionColorFadeCount[fadeTimer], gLerpSpeed);
    switch (fadeType) {
        case 0:
            time = sTransitionColorFadeCountLerp[fadeTimer] * 255.0f / (f32)(transTime - 1) + 0.5f; // fade in
            break;
        case 1:
            time = (1.0f - sTransitionColorFadeCountLerp[fadeTimer] / (f32)(transTime - 1)) * 255.0f + 0.5f; // fade out
            break;
    }
    return time;
}

Vtx *vertex_transition_color(struct WarpTransitionData *transData, u8 alpha) {
    Vtx *verts = alloc_display_list(4 * sizeof(*verts));
    u8 r = transData->red;
    u8 g = transData->green;
    u8 b = transData->blue;

    if (verts != NULL) {
        make_vertex(verts, 0, GFX_DIMENSIONS_FROM_LEFT_EDGE(0), 0, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 1, gScreenWidth, 0, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 2, gScreenWidth, SCREEN_HEIGHT, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 3, GFX_DIMENSIONS_FROM_LEFT_EDGE(0), SCREEN_HEIGHT, -1, 0, 0, r, g, b, alpha);
    }
    return verts;
}

void dl_transition_color(struct WarpTransitionData *transData, u8 alpha) {
    Vtx *verts = vertex_transition_color(transData, alpha);

    if (verts != NULL) {
        gSPDisplayList(gDisplayListHead++, dl_proj_mtx_fullscreen);
        gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        gSPVertex(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(verts), 4, 0);
        gSPDisplayList(gDisplayListHead++, dl_draw_quad_verts_0123);
        gSPDisplayList(gDisplayListHead++, dl_screen_transition_end);
    }
}

void render_fade_transition_from_color(s8 fadeTimer, u8 transTime, struct WarpTransitionData *transData)
{
    u8 alpha = set_transition_color_fade_alpha(1, fadeTimer, transTime);
    dl_transition_color(transData, alpha);
}

void render_fade_transition_into_color(s8 fadeTimer, u8 transTime, struct WarpTransitionData *transData)
{
    u8 alpha = set_transition_color_fade_alpha(0, fadeTimer, transTime);
    dl_transition_color(transData, alpha);
}

s16 calc_tex_transition_radius(s8 fadeTimer, s8 transTime, struct WarpTransitionData *transData) {
    f32 texRadius = transData->endTexRadius - transData->startTexRadius;
    f32 radiusTime = sTransitionColorFadeCountLerp[fadeTimer] * texRadius / (f32)(transTime - 1);
    f32 result = transData->startTexRadius + radiusTime;

    return (s16)(result + 0.5f);
}

f32 calc_tex_transition_time(s8 fadeTimer, s8 transTime, struct WarpTransitionData *transData) {
    f32 startX = transData->startTexX;
    f32 startY = transData->startTexY;
    f32 endX = transData->endTexX;
    f32 endY = transData->endTexY;
    f32 sqrtfXY = sqrtf((startX - endX) * (startX - endX) + (startY - endY) * (startY - endY));
    f32 result = (f32) sTransitionColorFadeCountLerp[fadeTimer] * sqrtfXY / (f32)(transTime - 1);

    return result;
}

u16 convert_tex_transition_angle_to_pos(struct WarpTransitionData *transData) {
    f32 x = transData->endTexX - transData->startTexX;
    f32 y = transData->endTexY - transData->startTexY;

    return atan2s(x, y);
}

s16 center_tex_transition_x(struct WarpTransitionData *transData, f32 texTransTime, u16 texTransPos) {
    f32 x = transData->startTexX + coss(texTransPos) * texTransTime;

    return (s16)((x + 0.5f));
}

s16 center_tex_transition_y(struct WarpTransitionData *transData, f32 texTransTime, u16 texTransPos) {
    f32 y = transData->startTexY + sins(texTransPos) * texTransTime;

    return (s16)((y + 0.5f));
}

void make_tex_transition_vertex(Vtx *verts, s32 n, s8 fadeTimer, struct WarpTransitionData *transData, s16 centerTransX, s16 centerTransY, s16 texRadius1, s16 texRadius2, s16 tx, s16 ty) {
    u8 r = transData->red;
    u8 g = transData->green;
    u8 b = transData->blue;
    u16 zeroTimer = sTransitionTextureFadeCountLerp[fadeTimer];
    f32 centerX = texRadius1 * coss(zeroTimer) - texRadius2 * sins(zeroTimer) + centerTransX;
    f32 centerY = texRadius1 * sins(zeroTimer) + texRadius2 * coss(zeroTimer) + centerTransY;
    s16 x = round_float(centerX);
    s16 y = round_float(centerY);

    make_vertex(verts, n, x, y, -1, tx * 32, ty * 32, r, g, b, 255);
}

#define BLANK 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE

void prepare_blank_box(void) {
    gDPSetCombineMode(gDisplayListHead++, BLANK, BLANK);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
}

void finish_blank_box(void) {
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

void load_tex_transition_vertex(Vtx *verts, s8 fadeTimer, struct WarpTransitionData *transData, s16 centerTransX, s16 centerTransY, s16 texTransRadius, s8 transTexType) {
    switch (transTexType) {
        case TRANS_TYPE_MIRROR:
            make_tex_transition_vertex(verts, 0, fadeTimer, transData, centerTransX, centerTransY, -texTransRadius, -texTransRadius, -31, 63);
            make_tex_transition_vertex(verts, 1, fadeTimer, transData, centerTransX, centerTransY, texTransRadius, -texTransRadius, 31, 63);
            make_tex_transition_vertex(verts, 2, fadeTimer, transData, centerTransX, centerTransY, texTransRadius, texTransRadius, 31, 0);
            make_tex_transition_vertex(verts, 3, fadeTimer, transData, centerTransX, centerTransY, -texTransRadius, texTransRadius, -31, 0);
            break;
        case TRANS_TYPE_CLAMP:
            make_tex_transition_vertex(verts, 0, fadeTimer, transData, centerTransX, centerTransY, -texTransRadius, -texTransRadius, 0, 63);
            make_tex_transition_vertex(verts, 1, fadeTimer, transData, centerTransX, centerTransY, texTransRadius, -texTransRadius, 63, 63);
            make_tex_transition_vertex(verts, 2, fadeTimer, transData, centerTransX, centerTransY, texTransRadius, texTransRadius, 63, 0);
            make_tex_transition_vertex(verts, 3, fadeTimer, transData, centerTransX, centerTransY, -texTransRadius, texTransRadius, 0, 0);
            break;
    }
    f32 centerX = (-texTransRadius) * coss(sTransitionTextureFadeCountLerp[fadeTimer]) - (-texTransRadius) * sins(sTransitionTextureFadeCountLerp[fadeTimer]) + centerTransX;
    if (centerX > 0) {
        f32 centerY = (texTransRadius) * sins(sTransitionTextureFadeCountLerp[fadeTimer]) - (texTransRadius) * coss(sTransitionTextureFadeCountLerp[fadeTimer]) + centerTransY;
        prepare_blank_box();
        gDPSetPrimColor(gDisplayListHead++, 0, 0, transData->red, transData->blue, transData->green, 255);
        gDPFillRectangle(gDisplayListHead++, 0, 0, centerX, SCREEN_HEIGHT);
        gDPFillRectangle(gDisplayListHead++, gScreenWidth - centerX, 0, gScreenWidth, SCREEN_HEIGHT);
        if (centerY > 0) {
            gDPFillRectangle(gDisplayListHead++, centerX, 0, gScreenWidth - centerX, centerY);
            gDPFillRectangle(gDisplayListHead++, centerX, SCREEN_HEIGHT - centerY, gScreenWidth - centerX, SCREEN_HEIGHT);
        }
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
        finish_blank_box();
    }
}

void *sTextureTransitionID[] = {
    texture_transition_star_half,
    texture_transition_circle_half,
    texture_transition_mario,
    texture_transition_bowser_half,
};

void render_textured_transition(s8 fadeTimer, s8 transTime, struct WarpTransitionData *transData, s8 texID, s8 transTexType) {
    f32 texTransTime = calc_tex_transition_time(fadeTimer, transTime, transData);
    u16 texTransPos = convert_tex_transition_angle_to_pos(transData);
    s16 centerTransX = center_tex_transition_x(transData, texTransTime, texTransPos);
    s16 centerTransY = center_tex_transition_y(transData, texTransTime, texTransPos);
    s16 texTransRadius = calc_tex_transition_radius(fadeTimer, transTime, transData) * 1.5f;
    Vtx *verts = alloc_display_list(8 * sizeof(*verts));

    if (verts != NULL) {
        load_tex_transition_vertex(verts, fadeTimer, transData, centerTransX, centerTransY, texTransRadius, transTexType);
        gSPDisplayList(gDisplayListHead++, dl_proj_mtx_fullscreen)
        gDPPipeSync(gDisplayListHead++);
        gDPSetCombineMode(gDisplayListHead++, G_CC_MODULATEIDECALA, G_CC_MODULATEIDECALA);
        gDPSetRenderMode(gDisplayListHead++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        gDPSetTextureFilter(gDisplayListHead++, G_TF_BILERP);
        switch (transTexType) {
        case TRANS_TYPE_MIRROR:
            gDPLoadTextureBlock(gDisplayListHead++, sTextureTransitionID[texID], G_IM_FMT_IA, G_IM_SIZ_8b, 32, 64, 0,
                G_TX_WRAP | G_TX_MIRROR, G_TX_WRAP | G_TX_MIRROR, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case TRANS_TYPE_CLAMP:
            gDPLoadTextureBlock(gDisplayListHead++, sTextureTransitionID[texID], G_IM_FMT_IA, G_IM_SIZ_8b, 64, 64, 0,
                G_TX_CLAMP, G_TX_CLAMP, 6, 6, G_TX_NOLOD, G_TX_NOLOD);
            break;
        }
        gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gSPVertex(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(verts), 4, 0);
        gSPDisplayList(gDisplayListHead++, dl_draw_quad_verts_0123);
        gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
        gSPDisplayList(gDisplayListHead++, dl_screen_transition_end);
        sTransitionTextureFadeCountLerp[fadeTimer] = approach_f32_asymptotic(sTransitionTextureFadeCountLerp[fadeTimer], sTransitionTextureFadeCount[fadeTimer], gLerpSpeed);
        sTransitionColorFadeCountLerp[fadeTimer] = approach_f32_asymptotic(sTransitionColorFadeCountLerp[fadeTimer], sTransitionColorFadeCount[fadeTimer], gLerpSpeed);
    }
}

void render_screen_transition(s8 fadeTimer, s8 transType, u8 transTime, struct WarpTransitionData *transData) {
    switch (transType) {
        case WARP_TRANSITION_FADE_FROM_COLOR:
            render_fade_transition_from_color(fadeTimer, transTime, transData);
            break;
        case WARP_TRANSITION_FADE_INTO_COLOR:
            render_fade_transition_into_color(fadeTimer, transTime, transData);
            break;
        case WARP_TRANSITION_FADE_FROM_STAR:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_STAR, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_INTO_STAR:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_STAR, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_FROM_CIRCLE:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_CIRCLE, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_INTO_CIRCLE:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_CIRCLE, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_FROM_MARIO:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_MARIO, TRANS_TYPE_CLAMP);
            break;
        case WARP_TRANSITION_FADE_INTO_MARIO:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_MARIO, TRANS_TYPE_CLAMP);
            break;
        case WARP_TRANSITION_FADE_FROM_BOWSER:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_BOWSER, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_INTO_BOWSER:
            render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_BOWSER, TRANS_TYPE_MIRROR);
            break;
    }
}

Gfx *render_cannon_circle_base(void) {
    Vtx *verts = alloc_display_list(4 * sizeof(*verts));
    Gfx *dlist = alloc_display_list(24 * sizeof(*dlist));
    Gfx *g = dlist;

    if (verts != NULL && dlist != NULL) {
        f32 scaleVal = (f32) SCREEN_WIDTH / (f32)gScreenWidth;
        make_vertex(verts, 0, ((gScreenWidth / 2) - (SCREEN_WIDTH / 2)) * scaleVal, 0, -1, -1152, 1824, 0, 0, 0, 255);
        make_vertex(verts, 1, ((gScreenWidth / 2) + (SCREEN_WIDTH / 2)) * scaleVal, 0, -1, 1152, 1824, 0, 0, 0, 255);
        make_vertex(verts, 2, ((gScreenWidth / 2) + (SCREEN_WIDTH / 2)) * scaleVal, SCREEN_HEIGHT, -1, 1152, 192, 0, 0, 0, 255);
        make_vertex(verts, 3, ((gScreenWidth / 2) - (SCREEN_WIDTH / 2)) * scaleVal, SCREEN_HEIGHT, -1, -1152, 192, 0, 0, 0, 255);

        gSPDisplayList(g++, dl_proj_mtx_fullscreen);
        gDPSetCombineMode(g++, G_CC_MODULATEIDECALA, G_CC_PASS2);
        gDPSetTextureFilter(g++, G_TF_BILERP);
        gDPLoadTextureBlock(g++, sTextureTransitionID[TEX_TRANS_CIRCLE], G_IM_FMT_IA, G_IM_SIZ_8b, 32, 64, 0,
            G_TX_WRAP | G_TX_MIRROR, G_TX_WRAP | G_TX_MIRROR, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
        gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gSPVertex(g++, VIRTUAL_TO_PHYSICAL(verts), 8, 0);
        gSPDisplayList(g++, dl_draw_quad_verts_0123);
        gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
        if (gScreenMode) {
            prepare_blank_box();
            gDPSetPrimColor(g++, 0, 0, 0, 0, 0, 255);
            gDPFillRectangle(g++, 0, 0, ((gScreenWidth / 2) - (SCREEN_WIDTH / 2)) + 4, SCREEN_HEIGHT);
            gDPFillRectangle(g++, gScreenWidth - ((gScreenWidth / 2) - (SCREEN_WIDTH / 2)) - 4, 0, gScreenWidth, SCREEN_HEIGHT);
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
        finish_blank_box();
        }
        gSPDisplayList(g++, dl_screen_transition_end);
        gSPEndDisplayList(g);
    } else {
        return NULL;
    }
    return dlist;
}

Gfx *geo_cannon_circle_base(s32 callContext, struct GraphNode *node, UNUSED Mat4 mtx) {
    struct GraphNodeGenerated *graphNode = (struct GraphNodeGenerated *) node;
    Gfx *dlist = NULL;

    if (callContext == GEO_CONTEXT_RENDER && gCurrentArea != NULL
        && gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON) {
        graphNode->fnNode.node.flags = (graphNode->fnNode.node.flags & 0xFF) | 0x500;
#ifndef L3DEX2_ALONE
        dlist = render_cannon_circle_base();
#endif
    }
    return dlist;
}

s32 screen_transition_logic(s8 fadeTimer, s8 transType, u8 transTime, struct WarpTransitionData *transData) {
    if (transType == WARP_TRANSITION_FADE_FROM_COLOR || transType == WARP_TRANSITION_FADE_INTO_COLOR) {
        sTransitionColorFadeCount[fadeTimer]++;
        return set_and_reset_transition_fade_timer(fadeTimer, transTime);
    } else {
        sTransitionColorFadeCount[fadeTimer]++;
        sTransitionTextureFadeCount[fadeTimer] += transData->texTimer;
        return set_and_reset_transition_fade_timer(fadeTimer, transTime);
    }
}