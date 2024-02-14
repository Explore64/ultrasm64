#include <PR/ultratypes.h>

#include "area.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "gfx_dimensions.h"
#include "main.h"
#include "memory.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "shadow.h"
#include "sm64.h"
#include "lerp.h"
#include "level_update.h"
#include "memory.h"
#include "string.h"
#include "game_init.h"
#include "object_list_processor.h"
#include "behavior_data.h"
#include "segment2.h"

/**
 * This file contains the code that processes the scene graph for rendering.
 * The scene graph is responsible for drawing everything except the HUD / text boxes.
 * First the root of the scene graph is processed when geo_process_root
 * is called from level_script.c. The rest of the tree is traversed recursively
 * using the function geo_process_node_and_siblings, which switches over all
 * geo node types and calls a specialized function accordingly.
 * The types are defined in engine/graph_node.h
 *
 * The scene graph typically looks like:
 * - Root (viewport)
 *  - Master list
 *   - Ortho projection
 *    - Background (skybox)
 *  - Master list
 *   - Perspective
 *    - Camera
 *     - <area-specific display lists>
 *     - Object parent
 *      - <group with 240 object nodes>
 *  - Master list
 *   - Script node (Cannon overlay)
 *
 */

s16 gMatStackIndex = 0;
Mat4 gMatStack[32];
Mtx *gMatStackFixed[32];
Mat4 gThrowMatStack[2][THROWMATSTACK];
u16 gThrowMatIndex = 0;
u8 gThrowMatSwap = 0;

/**
 * Animation nodes have state in global variables, so this struct captures
 * the animation state so a 'context switch' can be made when rendering the
 * held object.
 */
struct GeoAnimState {
    /*0x00*/ u8 type;
    /*0x01*/ u8 enabled;
    /*0x02*/ s16 frame;
    /*0x04*/ f32 translationMultiplier;
    /*0x08*/ u16 *attribute;
    /*0x0C*/ s16 *data;
};

// For some reason, this is a GeoAnimState struct, but the current state consists
// of separate global variables. It won't match EU otherwise.
struct GeoAnimState gGeoTempState;

u8 gCurrAnimType;
u8 gCurrAnimEnabled;
s16 gCurrAnimFrame;
f32 gCurrAnimTranslationMultiplier;
u16 *gCurrAnimAttribute;
s16 *gCurrAnimData;
Mat4 gCameraTransform;
f32 gHalfFovVert;
f32 gHalfFovHor;
u32 gCurrAnimPos;
u8 gCameraSnap = FALSE;
void *gMarioAnimHeap;
struct AnimInfo gMarioGfxAnim;
struct DmaHandlerList gMarioGfxAnimBuf;
struct DmaHandlerList *gMarioGfxAnimList = &gMarioGfxAnimBuf;
RenderNode *gRenderNodeHead[7];
RenderNode *gRenderNodeTail[7];
RenderList *gMateriallistHead[7];
RenderList *gMateriallistTail[7];
u8 sShowAll = FALSE;

struct RenderModeContainer {
    u32 modes[7];
};

s8 gAntiAliasing = 0;
u8 gScreenMode = 0;
u8 gFrameCap = 0;
u8 gDedither = 0;

/* Rendermode settings for cycle 2 for all 8 layers. */
static struct RenderModeContainer renderModeTable_2Cycle[3] = { { {
    // AA off.
    G_RM_OPA_SURF2,
    G_RM_ZB_OPA_SURF2,
    G_RM_ZB_OPA_DECAL2,
    G_RM_RA_ZB_TEX_EDGE2,
    G_RM_ZB_XLU_SURF2,
    G_RM_ZB_XLU_SURF2 | Z_UPD,
    G_RM_ZB_XLU_DECAL2,
    } },
    { {
    // AA fast.
    G_RM_OPA_SURF2,
    G_RM_RA_ZB_OPA_SURF2,
    G_RM_RA_ZB_OPA_DECAL2,
    G_RM_RA_ZB_TEX_EDGE2,
    G_RM_AA_ZB_XLU_SURF2,
    G_RM_AA_ZB_XLU_SURF2 | Z_UPD,
    G_RM_AA_ZB_XLU_DECAL2,
    } },
    { {
    // AA fancy. Applied to objects
    G_RM_OPA_SURF2,
    G_RM_AA_ZB_OPA_SURF2,
    G_RM_AA_ZB_OPA_DECAL2,
    G_RM_AA_ZB_TEX_EDGE2,
    G_RM_AA_ZB_XLU_SURF2,
    G_RM_AA_ZB_XLU_SURF2 | Z_UPD,
    G_RM_AA_ZB_XLU_DECAL2,
    } } };

struct GraphNodeRoot *gCurGraphNodeRoot = NULL;
struct GraphNodeMasterList *gCurGraphNodeMasterList = NULL;
struct GraphNodePerspective *gCurGraphNodeCamFrustum = NULL;
struct GraphNodeCamera *gCurGraphNodeCamera = NULL;
struct GraphNodeObject *gCurGraphNodeObject = NULL;
struct GraphNodeHeldObject *gCurGraphNodeHeldObject = NULL;
u16 gAreaUpdateCounter = 0;
LookAt* gCurLookAt;

struct LevelFog {
    s16 near;
    s16 far;
    u8 c[4];
    u8 areaFlags;
};

struct LevelFog gLevelFog[] = {
    {0, 0, {0, 0, 0, 0}, 0},
    {0, 0, {0, 0, 0, 0}, 0},
    {0, 0, {0, 0, 0, 0}, 0},
    {0, 0, {0, 0, 0, 0}, 0},
    {0, 0, {0, 0, 0, 0}, 0}, // BBH
    {0, 0, {0, 0, 0, 0}, 0}, // CCM
    {0, 0, {0, 0, 0, 0}, 0}, // CASTLE
    {960, 1000, {0, 0, 0, 255}, 1 | 2}, // HMC
    {980, 1000, {0, 0, 0, 255}, 2}, // SSL
    {980, 1000, {160, 160, 160, 255}, 1}, // BOB
    {0, 0, {0, 0, 0, 0}, 0}, // SL
    {0, 0, {0, 0, 0, 0}, 0}, // WDW
    {900, 1000, {5, 80, 75, 255}, 1 | 2}, // JRB
    {0, 0, {0, 0, 0, 0}, 0}, // THI
    {900, 1000, {255, 255, 255, 255}, 1}, // TTC
    {0, 0, {0, 0, 0, 0}, 0}, // RR
    {0, 0, {0, 0, 0, 0}, 0}, // CASTLE_GROUNDS
    {0, 0, {0, 0, 0, 0}, 0}, // BITDW
    {0, 0, {0, 0, 0, 0}, 0}, // VCUTM
    {0, 0, {0, 0, 0, 0}, 0}, // BITFS
    {0, 0, {0, 0, 0, 0}, 0}, // SA
    {0, 0, {0, 0, 0, 0}, 0}, // BITS
    {980, 1000, {0, 0, 0, 255}, 2}, // LLL
    {0, 0, {0, 0, 0, 0}, 0}, // DDD
    {0, 0, {0, 0, 0, 0}, 0}, // WF
    {0, 0, {0, 0, 0, 0}, 0}, // ENDING
    {0, 0, {0, 0, 0, 0}, 0}, // CASTLE_COURTYARD
    {980, 1000, {0, 0, 0, 255}, 1}, // PSS
    {980, 1000, {0, 0, 0, 255}, 1}, // COTMC
    {0, 0, {0, 0, 0, 0}, 0}, // TOTWC
    {960, 1000, {10, 30, 20, 255}, 1}, // BOWSER_1
    {0, 0, {0, 0, 0, 0}, 0}, // WMOTR
    {0, 0, {0, 0, 0, 0}, 0}, // UNKNOWN_32
    {960, 1000, {200, 50, 0, 255}, 1}, // BOWSER_2
    {1000, 2000, {0, 0, 0, 255}, 1}, // BOWSER_3
    {0, 0, {0, 0, 0, 0}, 0}, // UNKNOWN_35
    {960, 1000, {0, 0, 0, 255}, 2 | 4}, // TTM
    {0, 0, {0, 0, 0, 0}, 0}, 
    {0, 0, {0, 0, 0, 0}, 0},
};

u32 gFirstCycleRM = G_RM_PASS;

void update_level_fog(Gfx **gfx) {
    if (gLevelFog[gCurrLevelNum].areaFlags & (1 << (gCurrAreaIndex - 1)) && gFileSelect == FALSE) {
        gDPSetFogColor((*gfx)++, gLevelFog[gCurrLevelNum].c[0], gLevelFog[gCurrLevelNum].c[1], gLevelFog[gCurrLevelNum].c[2], gLevelFog[gCurrLevelNum].c[3]);
        gSPFogPosition((*gfx)++, gLevelFog[gCurrLevelNum].near, gLevelFog[gCurrLevelNum].far);
        gSPSetGeometryMode((*gfx)++, G_FOG);
        gFirstCycleRM = G_RM_FOG_SHADE_A;
    } else {
        gSPClearGeometryMode((*gfx)++, G_FOG);
        gFirstCycleRM = G_RM_PASS;
    }
}

s32 sMaterialSwaps = 0;

/**
 * Process a master list node.
 */
void geo_process_master_list_sub(void) {
    struct RenderModeContainer *mode2List;
    RenderNode *list;
    Gfx *lastMaterial = NULL;
    Gfx *gfx = gDisplayListHead;
    s32 switchAA = FALSE;
    s32 lastAA = 0;

    gSPSetGeometryMode(gfx++, G_ZBUFFER);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    update_level_fog(&gfx);
    for (u32 i = 0; i < GFX_NUM_MASTER_LISTS; i++, switchAA = TRUE) {
        list = gRenderNodeHead[i];
        while (list) {
            if (switchAA == TRUE || list->aaMode != lastAA) {
                mode2List = &renderModeTable_2Cycle[gAntiAliasing + 1 + list->aaMode];
                gDPSetRenderMode(gfx++, gFirstCycleRM, mode2List->modes[i]);
                switchAA = FALSE;
            }
            if (list->mtx) {
                gSPMatrix(gfx++, OS_K0_TO_PHYSICAL(list->mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
            }
            if (list->material && list->material != lastMaterial) {
                gSPDisplayList(gfx++, list->material);
                sMaterialSwaps++;
            }
            if (list->material == NULL) {
                sMaterialSwaps++;
            }
            lastMaterial = list->material;
            gSPDisplayList(gfx++, list->dl);
            list = list->next;
        }
        gRenderNodeHead[i] = NULL;
        gRenderNodeTail[i] = NULL;
        gMateriallistHead[i] = NULL;
        gMateriallistTail[i] = NULL;
    }
    gSPClearGeometryMode(gfx++, G_ZBUFFER | G_FOG);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDisplayListHead = gfx;
}

#define ALIGN8(val) (((val) + 0x7) & ~0x7)

static void *alloc_display_listGRAPH(u32 size) {
    gGfxPoolEnd -= size;
    return gGfxPoolEnd;
}

void find_material_list(RenderNode *node, s32 layer) {
    RenderList *matList;
    if (gRenderNodeHead[layer] == NULL) {
        gRenderNodeHead[layer] = node;
        matList = alloc_display_listGRAPH(sizeof(RenderList));
        gMateriallistHead[layer] = matList;
    } else {
        if (node->material) {
            RenderList *list = gMateriallistHead[layer];
            while (list) {
                if (list->entryHead->material == node->material) {
                    if (list->entryHead == gRenderNodeHead[layer]) {
                        gRenderNodeHead[layer] = node;
                    } else {
                        list->entryHead->prev->next = node;
                    }
                    node->next = list->entryHead;
                    node->prev = list->entryHead->prev;
                    list->entryHead = node;
                    return;
                }
                list = list->next;
            }
        }
        matList = alloc_display_listGRAPH(sizeof(RenderList));
        gMateriallistTail[layer]->next = matList;
        gRenderNodeTail[layer]->next = node;
    }
    node->next = NULL;
    matList->entryHead = node;
    matList->next = NULL;
    gMateriallistTail[layer] = matList;
    node->prev = gRenderNodeTail[layer];
    gRenderNodeTail[layer] = node;
}

/**
 * Appends the display list to one of the master lists based on the layer
 * parameter. Look at the RenderModeContainer struct to see the corresponding
 * render modes of layers.
 */
void geo_append_display_list(void *displayList, s16 layer, void *material) {

    if (gCurGraphNodeMasterList != 0) {
        RenderNode *entry = alloc_display_listGRAPH(sizeof(RenderNode));
        entry->mtx = gMatStackFixed[gMatStackIndex];
        if (gCurGraphNodeObject != NULL && gAntiAliasing == 0 && ((struct Object *) gCurGraphNodeObject)->behavior != segmented_to_virtual(bhvStaticObject)) {
            entry->aaMode = TRUE;
        } else {
            entry->aaMode = FALSE;
        }
        entry->material = material;
        entry->dl = displayList;
        find_material_list(entry, layer);
    }
}

void inc_mat_stack(void) {
    Mtx *mtx = alloc_display_listGRAPH(sizeof(*mtx));
    gMatStackIndex++;
    mtxf_to_mtx((s16 *) mtx, (f32 *) gMatStack[gMatStackIndex]);
    gMatStackFixed[gMatStackIndex] = mtx;
}

void append_dl_and_return(const struct GraphNodeDisplayList *node, void *material) {
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8, material);
    }
    if (((struct GraphNodeRoot *) node)->node.children != NULL) {
        geo_process_node_and_siblings(((struct GraphNodeRoot *) node)->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process the master list node.
 */
void geo_process_master_list(struct GraphNodeMasterList *node) {
    if (gCurGraphNodeMasterList == NULL && node->node.children != NULL) {
        gCurGraphNodeMasterList = node;
        geo_process_node_and_siblings(node->node.children);
        geo_process_master_list_sub();
        gCurGraphNodeMasterList = NULL;
    }
}

/**
 * Process an orthographic projection node.
 */
void geo_process_ortho_projection(struct GraphNodeOrthoProjection *node) {
    Mtx *mtx = alloc_display_listGRAPH(sizeof(*mtx));
    f32 left = (gCurGraphNodeRoot->x - gCurGraphNodeRoot->width) / 2.0f * node->scale;
    f32 right = (gCurGraphNodeRoot->x + gCurGraphNodeRoot->width) / 2.0f * node->scale;
    f32 top = (gCurGraphNodeRoot->y - gCurGraphNodeRoot->height) / 2.0f * node->scale;
    f32 bottom = (gCurGraphNodeRoot->y + gCurGraphNodeRoot->height) / 2.0f * node->scale;

    guOrtho(mtx, left, right, bottom, top, -2.0f, 2.0f, 1.0f);
    gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    geo_process_node_and_siblings(node->node.children);
}

/**
 * Process a perspective projection node.
 */
void geo_process_perspective(struct GraphNodePerspective *node) {
    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    if (node->fnNode.node.children != NULL) {
        u16 perspNorm;
        Mtx *mtx = alloc_display_listGRAPH(sizeof(*mtx));

        f32 aspect = (f32) gScreenWidth / (f32) gScreenHeight;

        guPerspective(mtx, &perspNorm, node->fov, aspect, node->near, node->far, 1.0f);
        gSPPerspNormalize(gDisplayListHead++, perspNorm);

        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

        gCurGraphNodeCamFrustum = node;
        gHalfFovVert = (gCurGraphNodeCamFrustum->fov + 2.0f) * 91.0222222222f + 0.5f;
        if (!gIsConsole) {
            aspect *= 5.0f;
        }
        gHalfFovHor = aspect * gHalfFovVert;
        gHalfFovVert = sins(gHalfFovVert) / coss(gHalfFovVert);
        gHalfFovHor = sins(gHalfFovHor) / coss(gHalfFovHor);
        geo_process_node_and_siblings(node->fnNode.node.children);
        gCurGraphNodeCamFrustum = NULL;
    }
}

f32 get_dist_from_camera(Vec3f pos) {
    return -((gCameraTransform[0][2] * pos[0]) + (gCameraTransform[1][2] * pos[1])
           + (gCameraTransform[2][2] * pos[2]) +  gCameraTransform[3][2]);
}

/**
 * Process a level of detail node. From the current transformation matrix,
 * the perpendicular distance to the camera is extracted and the children
 * of this node are only processed if that distance is within the render
 * range of this node.
 */
void geo_process_level_of_detail(struct GraphNodeLevelOfDetail *node) {
    // The fixed point Mtx type is defined as 16 longs, but it's actually 16
    // shorts for the integer parts followed by 16 shorts for the fraction parts
    f32 distanceFromCam = get_dist_from_camera(gMatStack[gMatStackIndex][3]);

    if (node->minDistance <= distanceFromCam && distanceFromCam < node->maxDistance) {
        if (node->node.children != 0) {
            geo_process_node_and_siblings(node->node.children);
        }
    }
}

/**
 * Process a switch case node. The node's selection function is called
 * if it is 0, and among the node's children, only the selected child is
 * processed next.
 */
void geo_process_switch(struct GraphNodeSwitchCase *node) {
    struct GraphNode *selectedChild = node->fnNode.node.children;

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    for (s32 i = 0; selectedChild != NULL && node->selectedCase > i; i++) {
        selectedChild = selectedChild->next;
    }
    if (selectedChild != NULL) {
        geo_process_node_and_siblings(selectedChild);
    }
}

void interpolate_node(struct Object *node) {
    for (u32 i = 0; i < 3; i++) {
        if (gMoveSpeed == 1) {
            node->header.gfx.posLerp[i] = approach_pos_lerp(node->header.gfx.posLerp[i], node->header.gfx.pos[i]);
        } else {
            node->header.gfx.posLerp[i] = node->header.gfx.pos[i] + ((f32 *) &node->oVelX)[i];
        }
        node->header.gfx.scaleLerp[i] = approach_pos_lerp(node->header.gfx.scaleLerp[i], node->header.gfx.scale[i]);
        node->header.gfx.angleLerp[i] = approach_angle_lerp(node->header.gfx.angleLerp[i], node->header.gfx.angle[i]);
    }
}

f32 billboardMatrix[3][4] = {
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 1.0f, 0 },
};

Lights1 defaultLight = gdSPDefLights1(0x3F, 0x3F, 0x3F, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00);

Vec3f globalLightDirection = { 0x4A, 0x4A, 0x4A };

void linear_mtxf_transpose_mul_vec3f_render(Mat4 m, Vec3f dst, Vec3f v) {
    s32 i;
    for (i = 0; i < 3; i++) {
        dst[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    }
}

void setup_global_light(void) {
    Lights1* curLight = (Lights1*)alloc_display_list(sizeof(Lights1));
    bcopy(&defaultLight, curLight, sizeof(Lights1));

    Vec3f transformedLightDirection;
    linear_mtxf_transpose_mul_vec3f_render(gCameraTransform, transformedLightDirection, globalLightDirection);
    curLight->l->l.dir[0] = (s8)(transformedLightDirection[0]);
    curLight->l->l.dir[1] = (s8)(transformedLightDirection[1]);
    curLight->l->l.dir[2] = (s8)(transformedLightDirection[2]);

    gSPSetLights1(gDisplayListHead++, (*curLight));
}

extern struct Object gObjectPool[OBJECT_POOL_CAPACITY];
extern s16 sCurrPlayMode;

/**
 * Copy Lakitu's pos and foc into `gc`
 */
void update_graph_node_camera(struct GraphNodeCamera *gc) {

    gc->rollScreen = gLakituState.roll;
    vec3f_copy(gc->pos, gLakituState.pos);
    vec3f_copy(gc->focus, gLakituState.focus);
    zoom_out_if_paused_and_outside(gc);

    if (!gMoveSpeed || sCurrPlayMode == 2 || gCameraSnap) {
        gc->posLerp[0] = gc->pos[0];
        gc->posLerp[1] = gc->pos[1];
        gc->posLerp[2] = gc->pos[2];
        gc->focusLerp[0] = gc->focus[0];
        gc->focusLerp[1] = gc->focus[1];
        gc->focusLerp[2] = gc->focus[2];
        gCameraSnap = FALSE;
    } else {
        gc->posLerp[0] = approach_pos_lerp(gc->posLerp[0], gc->pos[0]);
        gc->posLerp[1] = approach_pos_lerp(gc->posLerp[1], gc->pos[1]);
        gc->posLerp[2] = approach_pos_lerp(gc->posLerp[2], gc->pos[2]);
        gc->focusLerp[0] = approach_pos_lerp(gc->focusLerp[0], gc->focus[0]);
        gc->focusLerp[1] = approach_pos_lerp(gc->focusLerp[1], gc->focus[1]);
        gc->focusLerp[2] = approach_pos_lerp(gc->focusLerp[2], gc->focus[2]);
    }

    for (u32 i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        if (gObjectPool[i].header.gfx.node.flags & GRAPH_RENDER_PRIORITY) {
            if (gMoveSpeed && gObjectPool[i].header.gfx.bothMats >= 2) {
                interpolate_node(&gObjectPool[i]);
            } else {
                warp_node(&gObjectPool[i]);
            }
        }
    }
}

/**
 * Process a camera node.
 */
void geo_process_camera(struct GraphNodeCamera *node) {
    Mtx *rollMtx = alloc_display_list(sizeof(*rollMtx));
    Mtx *mtx = alloc_display_list(sizeof(*mtx));
    Mtx *viewMtx = alloc_display_list(sizeof(Mtx));
    Gfx *gfx = gDisplayListHead;

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }

    gSPLookAt(gfx++, &gCurLookAt);
    mtxf_rotate_xy(rollMtx, node->rollScreen);

    gSPMatrix(gfx++, VIRTUAL_TO_PHYSICAL(rollMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

    mtxf_lookat(gCameraTransform, node->posLerp, node->focusLerp, node->roll);
    Mat4* cameraMatrix = &gCameraTransform;
    gCurLookAt->l[0].l.dir[0] = (s8)(127.0f * (*cameraMatrix)[0][0]);
    gCurLookAt->l[0].l.dir[1] = (s8)(127.0f * (*cameraMatrix)[1][0]);
    gCurLookAt->l[0].l.dir[2] = (s8)(127.0f * (*cameraMatrix)[2][0]);
    gCurLookAt->l[1].l.dir[0] = (s8)(127.0f * (*cameraMatrix)[0][1]);
    gCurLookAt->l[1].l.dir[1] = (s8)(127.0f * (*cameraMatrix)[1][1]);
    gCurLookAt->l[1].l.dir[2] = (s8)(127.0f * (*cameraMatrix)[2][1]);
    // Convert the scaled matrix to fixed-point and integrate it into the projection matrix stack
    guMtxF2L(gCameraTransform, viewMtx);
    gSPMatrix(gfx++, VIRTUAL_TO_PHYSICAL(viewMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    setup_global_light();
    gDisplayListHead = gfx;
    if (node->fnNode.node.children != NULL) {
        gCurGraphNodeCamera = node;
        billboardMatrix[0][0] = coss(0);
        billboardMatrix[0][1] = sins(0);
        billboardMatrix[1][0] = -billboardMatrix[0][1];
        billboardMatrix[1][1] = billboardMatrix[0][0];
        node->matrixPtr = &gCameraTransform;
        geo_process_node_and_siblings(node->fnNode.node.children);
        gCurGraphNodeCamera = NULL;
    }
}

/**
 * Process a translation / rotation node. A transformation matrix based
 * on the node's translation and rotation is created and pushed on both
 * the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_translation_rotation(struct GraphNodeTranslationRotation *node) {
    Mat4 mtxf;
    Vec3f translation;
    // vec3s_to_vec3f(translation, node->translation);
    translation[0] = node->translation[0];
    translation[1] = node->translation[1];
    translation[2] = node->translation[2];
    mtxf_rotate_zxy_and_translate(mtxf, translation, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node, NULL);
}

/**
 * Process a translation node. A transformation matrix based on the node's
 * translation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_translation(const struct GraphNodeTranslation *node) {
    f32 entires[3];
    for (s32 i = 0; i < 3; i++) {
        for (s32 j = 0; j < 3; j++) {
            gMatStack[gMatStackIndex + 1][j][i] = gMatStack[gMatStackIndex][j][i];
        }
        entires[i] = node->translation[i];
    }
    for (s32 i = 0; i < 3; i++) {
        gMatStack[gMatStackIndex + 1][3][i] = entires[0] * gMatStack[gMatStackIndex][0][i] + 
                                              entires[1] * gMatStack[gMatStackIndex][1][i] + 
                                              entires[2] * gMatStack[gMatStackIndex][2][i] + gMatStack[gMatStackIndex][3][i];
    }

    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node, NULL);
}

/**
 * Process a rotation node. A transformation matrix based on the node's
 * rotation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_rotation(struct GraphNodeRotation *node) {
    Mat4 mtxf;
    mtxf_rotate_zxy_and_translate(mtxf, gVec3fZero, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);

    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node, NULL);
}

void mtxf_scale_vec3f(Mat4 dest, Mat4 mtx, Vec3f s) {
    f32 *temp = (f32 *) dest;
    f32 *temp2 = (f32 *) mtx;
    while (temp < ((f32 *) dest) + 4) {
        for (s32 i = 0; i < 3; i++) {
            temp[i * 4] = temp2[i * 4] * s[i];
        }
        temp[12] = temp2[12];
        temp++;
        temp2++;
    }
}

/**
 * Process a scaling node. A transformation matrix based on the node's
 * scale is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_scale(const struct GraphNodeScale *node) {
    Vec3f scaleVec;

    vec3f_set(scaleVec, node->scale, node->scale, node->scale);
    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], scaleVec);
    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node, NULL);
}

/**
 * Process a billboard node. A transformation matrix is created that makes its
 * children face the camera, and it is pushed on the floating point and fixed
 * point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_billboard(struct GraphNodeBillboard *node) {
    Vec3f translation;
    Vec3f scale = { 1.0f, 1.0f, 1.0f };

    vec3s_to_vec3f(translation, node->translation);
    if (gCurGraphNodeHeldObject != NULL) {
        vec3f_copy(scale, gCurGraphNodeHeldObject->objNode->header.gfx.scaleLerp);
    } else if (gCurGraphNodeObject != NULL) {
        vec3f_copy(scale, gCurGraphNodeObject->scaleLerp);
    }
    mtxf_billboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], translation, scale, gCurGraphNodeCamera->roll);
    inc_mat_stack();
    append_dl_and_return(((struct GraphNodeDisplayList *) node), NULL);
}

/**
 * Process a display list node. It draws a display list without first pushing
 * a transformation on the stack, so all transformations are inherited from the
 * parent node. It processes its children if it has them.
 */
void geo_process_display_list(const struct GraphNodeDisplayList *node) {

    append_dl_and_return((struct GraphNodeDisplayList *) node, node->material);
    gMatStackIndex++;
}

/**
 * Process a generated list. Instead of storing a pointer to a display list,
 * the list is generated on the fly by a function.
 */
void geo_process_generated_list(struct GraphNodeGenerated *node) {
    if (node->fnNode.func != NULL) {
        Gfx *list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, (struct AllocOnlyPool *) gMatStack[gMatStackIndex]);

        if (list != NULL) {
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8, NULL);
        }
    }
    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

/**
 * Process a background node. Tries to retrieve a background display list from
 * the function of the node. If that function is null or returns null, a black
 * rectangle is drawn instead.
 */
void geo_process_background(struct GraphNodeBackground *node) {
    Gfx *list = NULL;

    if (node->fnNode.func != NULL) {
        list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, (struct AllocOnlyPool *) gMatStack[gMatStackIndex]);
    }
    if (list != NULL) {
        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8, NULL);
    } else if (gCurGraphNodeMasterList != NULL) {
        Gfx *gfxStart = alloc_display_listGRAPH(sizeof(Gfx) * 8);
        Gfx *gfx = gfxStart;

        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_FILL);
        gDPSetFillColor(gfx++, node->background);
        gDPFillRectangle(gfx++, 0, 0, gScreenWidth - 1, gScreenHeight - 1);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gSPEndDisplayList(gfx++);

        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(gfxStart), 0, NULL);
    }
    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

void linear_mtxf_mul_vec3f2(Mat4 m, Vec3f dst, Vec3f v) {
    for (u8 i = 0; i < 3; i++) {
        dst[i] = ((m[0][i] * v[0])
                + (m[1][i] * v[1])
                + (m[2][i] * v[2]));
    }
}

void mtxf_rot_trans_mul(Vec3s rot, Vec3f trans, Mat4 dest, Mat4 src) {
    f32 entry[3];
    f32 cx = coss(rot[0]);
    f32 cy = coss(rot[1]);
    f32 cz = coss(rot[2]);
    f32 sx = sins(rot[0]);
    f32 sy = sins(rot[1]);
    f32 sz = sins(rot[2]);
    entry[0] = cy * cz;
    entry[1] = cy * sz;
    entry[2] = -sy;
    dest[0][0] = entry[0] * src[0][0] + entry[1] * src[1][0] + entry[2] * src[2][0];
    dest[0][1] = entry[0] * src[0][1] + entry[1] * src[1][1] + entry[2] * src[2][1];
    dest[0][2] = entry[0] * src[0][2] + entry[1] * src[1][2] + entry[2] * src[2][2];

    entry[0] = sx * sy * cz - cx * sz;
    entry[1] = sx * sy * sz + cx * cz;
    entry[2] = sx * cy;
    dest[1][0] = entry[0] * src[0][0] + entry[1] * src[1][0] + entry[2] * src[2][0];
    dest[1][1] = entry[0] * src[0][1] + entry[1] * src[1][1] + entry[2] * src[2][1];
    dest[1][2] = entry[0] * src[0][2] + entry[1] * src[1][2] + entry[2] * src[2][2];

    entry[0] = cx * sy * cz + sx * sz;
    entry[1] = cx * sy * sz - sx * cz;
    entry[2] = cx * cy;
    dest[2][0] = entry[0] * src[0][0] + entry[1] * src[1][0] + entry[2] * src[2][0];
    dest[2][1] = entry[0] * src[0][1] + entry[1] * src[1][1] + entry[2] * src[2][1];
    dest[2][2] = entry[0] * src[0][2] + entry[1] * src[1][2] + entry[2] * src[2][2];

    dest[3][0] = trans[0] * src[0][0] + trans[1] * src[1][0] + trans[2] * src[2][0] + src[3][0];
    dest[3][1] = trans[0] * src[0][1] + trans[1] * src[1][1] + trans[2] * src[2][1] + src[3][1];
    dest[3][2] = trans[0] * src[0][2] + trans[1] * src[1][2] + trans[2] * src[2][2] + src[3][2];

    dest[0][3] = dest[1][3] = dest[2][3] = 0;
    ((u32 *) dest)[15] = 0x3F800000;
}

u32 gSkipLerp = FALSE;

/**
 * Render an animated part. The current animation state is not part of the node
 * but set in global variables. If an animated part is skipped, everything afterwards desyncs.
 */
void geo_process_animated_part(const struct GraphNodeAnimatedPart *node) {
    Vec3f translation;
    Vec3s rotation;

    vec3f_set(translation, node->translation[0], node->translation[1], node->translation[2]);
    if (gCurrAnimType) {
        if (gCurrAnimType != ANIM_TYPE_ROTATION) {
            if (gCurrAnimType == ANIM_TYPE_TRANSLATION) {
                translation[0] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
                translation[1] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
                translation[2] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
            } else {
                if (gCurrAnimType == ANIM_TYPE_LATERAL_TRANSLATION) {
                    translation[0] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
                    gCurrAnimAttribute += 2;
                    translation[2] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
                } else {
                    if (gCurrAnimType == ANIM_TYPE_VERTICAL_TRANSLATION) {
                        gCurrAnimAttribute += 2;
                        translation[1] += gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * gCurrAnimTranslationMultiplier;
                        gCurrAnimAttribute += 2;
                    } else if (gCurrAnimType == ANIM_TYPE_NO_TRANSLATION) {
                        gCurrAnimAttribute += 6;
                    }
                }
            }

            if (gMoveSpeed == 0 || gSkipLerp) {
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0] = translation[0];
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1] = translation[1];
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2] = translation[2];
            } else {
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0], translation[0]);
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1], translation[1]);
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2], translation[2]);
                vec3f_set(translation, gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0], gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1], gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2]);
            }
            gCurrAnimPos++;
        }
        gCurrAnimType = ANIM_TYPE_ROTATION;
        rotation[0] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        rotation[1] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        rotation[2] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        if (gMoveSpeed == 0 || gSkipLerp) {
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0] = rotation[0];
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1] = rotation[1];
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2] = rotation[2];
        } else {
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0], rotation[0]);
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1], rotation[1]);
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2], rotation[2]);
            vec3s_copy(rotation, gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos]);
        }
    }
    mtxf_rot_trans_mul(rotation, translation, gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex]);
    gCurrAnimPos++;
    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node, node->material);
}

s32 load_patchable_table_render(struct DmaHandlerList *list, s32 index) {
    s32 ret = FALSE;
    struct DmaTable *table = list->dmaTable;

    if ((u32)index < table->count) {
        u8 *addr = table->srcAddr + table->anim[index].offset;
        s32 size = table->anim[index].size;

        if (list->currentAddr != addr) {
            dma_read(list->bufTarget, addr, addr + size);
            list->currentAddr = addr;
            ret = TRUE;
        }
    }
    return ret;
}

void load_mario_anim_gfx(void) {
    s32 targetAnimID = gMarioState->marioObj->header.gfx.animInfo.animID;
    struct Animation *targetAnim = gMarioGfxAnimList->bufTarget;

    if (load_patchable_table_render(gMarioGfxAnimList, targetAnimID)) {
        targetAnim->values = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->values);
        targetAnim->index = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->index);
    }

    if (gMarioGfxAnim.animID != targetAnimID) {
        gMarioGfxAnim.animID = targetAnimID;
        gMarioGfxAnim.curAnim = targetAnim;
        gMarioGfxAnim.animAccel = 0;
        gMarioGfxAnim.animYTrans = gMarioState->unkB0;
    }
        
        if (gMarioState->marioObj->header.gfx.animInfo.animAccel == 0) {
            gMarioGfxAnim.animFrame = gMarioState->marioObj->header.gfx.animInfo.animFrame;
        } else {
            gMarioGfxAnim.animFrameAccelAssist = gMarioState->marioObj->header.gfx.animInfo.animFrameAccelAssist;
            gMarioGfxAnim.animFrame = gMarioState->marioObj->header.gfx.animInfo.animFrame;
        }
    if (gMarioState->marioObj->header.gfx.animInfo.animAccel != 0) {
        gMarioGfxAnim.animAccel = gMarioState->marioObj->header.gfx.animInfo.animAccel;
    }
}

/**
 * Initialize the animation-related global variables for the currently drawn
 * object's animation.
 */
void geo_set_animation_globals(struct AnimInfo *node, struct Object *obj) {
    struct AnimInfo *tempNode;
    struct Animation *anim;

    if (obj == gMarioState->marioObj) {
        load_mario_anim_gfx();
        tempNode = &gMarioGfxAnim;
        gSkipLerp = FALSE;
    } else {
        tempNode = node;
        if (obj->oDistanceToMario > 1000.0f) {
            gSkipLerp = TRUE;
        } else {
            gSkipLerp = FALSE;
        }
    }
    anim = tempNode->curAnim;

    tempNode->animTimer = gAreaUpdateCounter;
    if (anim->flags & ANIM_FLAG_HOR_TRANS) {
        gCurrAnimType = ANIM_TYPE_VERTICAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_VERT_TRANS) {
        gCurrAnimType = ANIM_TYPE_LATERAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_6) {
        gCurrAnimType = ANIM_TYPE_NO_TRANSLATION;
    } else {
        gCurrAnimType = ANIM_TYPE_TRANSLATION;
    }

    gCurrAnimFrame = tempNode->animFrame;
    gCurrAnimEnabled = (anim->flags & ANIM_FLAG_5) == 0;
    gCurrAnimAttribute = segmented_to_virtual((void *) anim->index);
    gCurrAnimData = segmented_to_virtual((void *) anim->values);
    gCurrAnimPos = 0;

    if (anim->animYTransDivisor == 0) {
        gCurrAnimTranslationMultiplier = 1.0f;
    } else {
        gCurrAnimTranslationMultiplier = (f32) tempNode->animYTrans / (f32) anim->animYTransDivisor;
    }
}

void vec3f_prod(Vec3f dest, const Vec3f a, const Vec3f b) {
    f32 x1 = ((f32 *) a)[0];
    f32 y1 = ((f32 *) a)[1];
    f32 z1 = ((f32 *) a)[2];
    f32 x2 = ((f32 *) b)[0];
    f32 y2 = ((f32 *) b)[1];
    f32 z2 = ((f32 *) b)[2];
    ((f32 *) dest)[0] = (x1 * x2);
    ((f32 *) dest)[1] = (y1 * y2);
    ((f32 *) dest)[2] = (z1 * z2);
}

void mtxf_shadow(Mat4 dest, Vec3f upDir, Vec3f pos, Vec3f scale, s32 yaw) {
    Vec3f lateralDir;
    Vec3f leftDir;
    Vec3f forwardDir;
    vec3f_set(lateralDir, sins(yaw), 0.0f, coss(yaw));
    vec3f_normalize(upDir);
    vec3f_cross(leftDir, upDir, lateralDir);
    vec3f_normalize(leftDir);
    vec3f_cross(forwardDir, leftDir, upDir);
    vec3f_normalize(forwardDir);
    vec3f_prod(dest[0], leftDir, scale);
    vec3f_prod(dest[1], upDir, scale);
    vec3f_prod(dest[2], forwardDir, scale);
    vec3f_copy(dest[3], pos);
    (dest)[0][3] = (dest)[1][3] = (dest)[2][3] = 0;
    ((u32 *)(dest))[15] = 0x3F800000;
}

/**
 * Process a shadow node. Renders a shadow under an object offset by the
 * translation of the first animated component and rotated according to
 * the floor below it.
 */
void geo_process_shadow(struct GraphNodeShadow *node) {
    if (gCurGraphNodeCamera != NULL && gCurGraphNodeObject != NULL) {
        Vec3f shadowPos;
        f32 shadowScale;

        if (gCurGraphNodeHeldObject != NULL) {
            vec3f_copy(shadowPos, gMatStack[gMatStackIndex][3]);
            shadowScale = node->shadowScale * gCurGraphNodeHeldObject->objNode->header.gfx.scaleLerp[0];
        } else {
            if (gCurGraphNodeObjectNode->oFloor != NULL) {
                // I'm reverting this for now simply because I need the floor pitch to make this seamless.
                /*if (gCurGraphNodeObjectNode->oFloorHeight > gCamera->pos[1]) {
                    goto skipShadow;
                }*/
            }
            vec3f_copy(shadowPos, gCurGraphNodeObject->posLerp);
            shadowScale = node->shadowScale * gCurGraphNodeObject->scaleLerp[0];
        }

        u8 shifted = (gCurrAnimEnabled && (gCurrAnimType == ANIM_TYPE_TRANSLATION || gCurrAnimType == ANIM_TYPE_LATERAL_TRANSLATION));

        if (shifted) {
            struct GraphNode *geo = node->node.children;
            f32 objScale = 1.0f;
            if (geo != NULL && geo->type == GRAPH_NODE_TYPE_SCALE) {
                objScale = ((struct GraphNodeScale *) geo)->scale;
            }

            f32 animScale = gCurrAnimTranslationMultiplier * objScale;
            Vec3f animOffset;
            animOffset[0] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * animScale;
            animOffset[1] = 0.0f;
            gCurrAnimAttribute += 2;
            animOffset[2] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)] * animScale;
            gCurrAnimAttribute -= 6;

            // simple matrix rotation so the shadow offset rotates along with the object
            f32 sinAng = sins(gCurGraphNodeObject->angleLerp[1]);
            f32 cosAng = coss(gCurGraphNodeObject->angleLerp[1]);

            shadowPos[0] += animOffset[0] * cosAng + animOffset[2] * sinAng;
            shadowPos[2] += -animOffset[0] * sinAng + animOffset[2] * cosAng;
        }

        Gfx *shadowList = create_shadow_below_xyz(shadowPos, shadowScale * 0.5f, node->shadowSolidity, node->shadowType, shifted);

        if (shadowList != NULL) {
            mtxf_shadow(gMatStack[gMatStackIndex + 1], gCurrShadow.floorNormal, shadowPos, gCurrShadow.scale, gCurGraphNodeObject->angleLerp[1]);
            Gfx *material;
            if (node->shadowType == SHADOW_CIRCLE) {
                material = dl_shadow_circle;
            } else {
                material = dl_shadow_square;
            }
            inc_mat_stack();
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), gCurrShadow.isDecal ? LAYER_TRANSPARENT_DECAL : LAYER_TRANSPARENT, NULL);

            gMatStackIndex--;
        }
    }
    skipShadow:
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Check whether an object is in view to determine whether it should be drawn.
 * This is known as frustum culling.
 * It checks whether the object is far away, very close / behind the camera,
 * or horizontally out of view. It does not check whether it is vertically
 * out of view. It assumes a sphere of 300 units around the object's position
 * unless the object has a culling radius node that specifies otherwise.
 *
 * The matrix parameter should be the top of the matrix stack, which is the
 * object's transformation matrix times the camera 'look-at' matrix. The math
 * is counter-intuitive, but it checks column 3 (translation vector) of this
 * matrix to determine where the origin (0,0,0) in object space will be once
 * transformed to camera space (x+ = right, y+ = up, z = 'coming out the screen').
 * In 3D graphics, you typically model the world as being moved in front of a
 * camera instead of a moving camera through a world, which in
 * this case simplifies calculations. Note that the perspective matrix is not
 * on the matrix stack, so there are still calculations with the fov to compute
 * the slope of the lines of the frustum.
 *
 *        z-
 *
 *  \     |     /
 *   \    |    /
 *    \   |   /
 *     \  |  /
 *      \ | /
 *       \|/
 *        C       x+
 *
 * Since (0,0,0) is unaffected by rotation, columns 0, 1 and 2 are ignored.
 */
s32 obj_is_in_view(struct GraphNodeObject *node) {
    s32 cullingRadius;
    struct GraphNode *geo;

    if (node->node.flags & GRAPH_RENDER_INVISIBLE)
        return FALSE;

    geo = node->sharedChild;

    // ! @bug The aspect ratio is not accounted for. When the fov value is 45,
    // the horizontal effective fov is actually 60 degrees, so you can see objects
    // visibly pop in or out at the edge of the screen.
    s32 halfFov = (gCurGraphNodeCamFrustum->fov / 2.0f + 1.0f) * 32768.0f / 180.0f + 0.5f;

    f32 hScreenEdge = -node->cameraToObject[2] * sins(halfFov) / coss(halfFov);
    // -matrix[3][2] is the depth, which gets multiplied by tan(halfFov) to get
    // the amount of units between the center of the screen and the horizontal edge
    // given the distance from the object to the camera.

    if (geo != NULL && geo->type == GRAPH_NODE_TYPE_CULLING_RADIUS)
        cullingRadius = (s16)((struct GraphNodeCullingRadius *) geo)->cullingRadius;
    else
        cullingRadius = 300;

    // Don't render if the object is close to or behind the camera
    if (node->cameraToObject[2] > -100.0f + cullingRadius)
        return FALSE;

    //! This makes the HOLP not update when the camera is far away, and it
    //  makes PU travel safe when the camera is locked on the main map.
    //  If Mario were rendered with a depth over 65536 it would cause overflow
    //  when converting the transformation matrix to a fixed point matrix.
    if (node->cameraToObject[2] < -20000.0f - cullingRadius)
        return FALSE;
    // Check whether the object is horizontally in view
    if (node->cameraToObject[0] > hScreenEdge + cullingRadius)
        return FALSE;
    if (node->cameraToObject[0] < -hScreenEdge - cullingRadius)
        return FALSE;
    return TRUE;
}

void linear_mtxf_mul_vec3f_and_translate(Mat4 m, Vec3f dst, Vec3f v) {
    for (u32 i = 0; i < 3; i++) {
        dst[i] = ((m[0][i] * v[0]) + (m[1][i] * v[1]) + (m[2][i] * v[2]) +  m[3][i]);
    }
}

/**
 * Process an object node.
 */
void geo_process_object(struct Object *node) {
    // !@Bug: Breaks JRB ship
    if ((node->header.gfx.sharedChild == NULL || !(node->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) || 
    node->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) && node->behavior != segmented_to_virtual(bhvInSunkenShip3)) {
        // Still want to know where the object is in worldspace, for audio panning to correctly work.
        mtxf_translate(gMatStack[gMatStackIndex + 1], node->header.gfx.pos);
        linear_mtxf_mul_vec3f_and_translate(gCameraTransform, node->header.gfx.cameraToObject, gMatStack[gMatStackIndex + 1][3]);
        node->header.gfx.bothMats = 0;
        return;
    }

    if ((node->header.gfx.node.flags & GRAPH_RENDER_PRIORITY) == 0 || gAreaUpdateCounter <= 8) {
        if (gMoveSpeed && node->header.gfx.bothMats >= 2)
            interpolate_node(node);
        else
            warp_node(node);
    }

    if (node->header.gfx.matrixID[gThrowMatSwap ^ 1] != MATRIX_NULL) {
        node->header.gfx.throwMatrix = &gThrowMatStack[gThrowMatSwap ^ 1][node->header.gfx.matrixID[gThrowMatSwap ^ 1]];
    } else {
        node->header.gfx.throwMatrix = NULL;
    }

    if (node->header.gfx.bothMats < 5) {
        node->header.gfx.bothMats++;
    }

    if (node->header.gfx.areaIndex == gCurGraphNodeRoot->areaIndex) {
        if (node->header.gfx.throwMatrix != NULL) {
            mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], *node->header.gfx.throwMatrix, node->header.gfx.scaleLerp);
        } else if (node->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD) {
            mtxf_billboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], node->header.gfx.posLerp, node->header.gfx.scaleLerp, gCurGraphNodeCamera->roll);
        } else {
            mtxf_rotate_zxy_and_translate(gMatStack[gMatStackIndex + 1], node->header.gfx.posLerp, node->header.gfx.angleLerp);
            mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1], node->header.gfx.scaleLerp);
        }

        node->header.gfx.throwMatrix = &gMatStack[++gMatStackIndex];
        linear_mtxf_mul_vec3f_and_translate(gCameraTransform, node->header.gfx.cameraToObject, (*node->header.gfx.throwMatrix)[3]);
    }

    if (node->header.gfx.areaIndex == gCurGraphNodeRoot->areaIndex){
        if (node->header.gfx.animInfo.curAnim != NULL) {
            geo_set_animation_globals(&node->header.gfx.animInfo, node);
        }
        if (obj_is_in_view(&node->header.gfx)) {
            gMatStackIndex--;
            inc_mat_stack();

            if (node->header.gfx.sharedChild != NULL) {
                gCurGraphNodeObject = (struct GraphNodeObject *) node;
                node->header.gfx.sharedChild->parent = &node->header.gfx.node;
                geo_process_node_and_siblings(node->header.gfx.sharedChild);
                node->header.gfx.sharedChild->parent = NULL;
                gCurGraphNodeObject = NULL;
            }
            if (node->header.gfx.node.children != NULL)
                geo_process_node_and_siblings(node->header.gfx.node.children);
        }

        gMatStackIndex--;
        gCurrAnimType = ANIM_TYPE_NONE;
        node->header.gfx.throwMatrix = NULL;
    }
}

/**
 * Process an object parent node. Temporarily assigns itself as the parent of
 * the subtree rooted at 'sharedChild' and processes the subtree, after which the
 * actual children are be processed. (in practice they are null though)
 */
void geo_process_object_parent(struct GraphNodeObjectParent *node) {
    if (node->sharedChild != NULL) {
        node->sharedChild->parent = (struct GraphNode *) node;
        geo_process_node_and_siblings(node->sharedChild);
        node->sharedChild->parent = NULL;
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Process a held object node.
 */
void geo_process_held_object(struct GraphNodeHeldObject *node) {
    Mat4 mat;
    Vec3f translation;
    Mat4 tempMtx;

    gSPLookAt(gDisplayListHead++, gCurLookAt);

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    if (node->objNode != NULL && node->objNode->header.gfx.sharedChild != NULL) {

        translation[0] = node->translation[0] / 4;
        translation[1] = node->translation[1] / 4;
        translation[2] = node->translation[2] / 4;

        mtxf_translate(mat, translation);
        mtxf_copy(gMatStack[gMatStackIndex + 1], *gCurGraphNodeObject->throwMatrix);
        vec3f_copy(gMatStack[gMatStackIndex + 1][3], gMatStack[gMatStackIndex][3]);
        mtxf_copy(tempMtx, gMatStack[gMatStackIndex + 1]);
        mtxf_mul(gMatStack[gMatStackIndex + 1], mat, tempMtx);
        mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1], node->objNode->header.gfx.scaleLerp);
        if (node->fnNode.func != NULL) {
            node->fnNode.func(GEO_CONTEXT_HELD_OBJ, &node->fnNode.node, (struct AllocOnlyPool *) gMatStack[gMatStackIndex + 1]);
        }
        inc_mat_stack();
        gGeoTempState.type = gCurrAnimType;
        gGeoTempState.enabled = gCurrAnimEnabled;
        gGeoTempState.frame = gCurrAnimFrame;
        gGeoTempState.translationMultiplier = gCurrAnimTranslationMultiplier;
        gGeoTempState.attribute = gCurrAnimAttribute;
        gGeoTempState.data = gCurrAnimData;
        gCurrAnimType = 0;
        gCurGraphNodeHeldObject = (void *) node;
        if (node->objNode->header.gfx.animInfo.curAnim != NULL) {
            geo_set_animation_globals(&node->objNode->header.gfx.animInfo, node->objNode);
        }

        geo_process_node_and_siblings(node->objNode->header.gfx.sharedChild);
        gCurGraphNodeHeldObject = NULL;
        gCurrAnimType = gGeoTempState.type;
        gCurrAnimEnabled = gGeoTempState.enabled;
        gCurrAnimFrame = gGeoTempState.frame;
        gCurrAnimTranslationMultiplier = gGeoTempState.translationMultiplier;
        gCurrAnimAttribute = gGeoTempState.attribute;
        gCurrAnimData = gGeoTempState.data;
        gMatStackIndex--;
    }

    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

/**
 * Processes the children of the given GraphNode if it has any
 */
void geo_try_process_children(struct GraphNode *node) {
    if (node->children != NULL) {
        geo_process_node_and_siblings(node->children);
    }
}

void (*geoFunctionTable[])() = {
    geo_try_process_children, // GRAPH_NODE_TYPE_ROOT,
    geo_process_ortho_projection, // GRAPH_NODE_TYPE_ORTHO_PROJECTION,
    geo_process_perspective, // GRAPH_NODE_TYPE_PERSPECTIVE,
    geo_process_master_list, // GRAPH_NODE_TYPE_MASTER_LIST,
    geo_try_process_children, // GRAPH_NODE_TYPE_START,
    geo_process_level_of_detail, // GRAPH_NODE_TYPE_LEVEL_OF_DETAIL,
    geo_process_switch, // GRAPH_NODE_TYPE_SWITCH_CASE,
    geo_process_camera, // GRAPH_NODE_TYPE_CAMERA,
    geo_process_translation_rotation, // GRAPH_NODE_TYPE_TRANSLATION_ROTATION,
    geo_process_translation, // GRAPH_NODE_TYPE_TRANSLATION,
    geo_process_rotation, // GRAPH_NODE_TYPE_ROTATION,
    geo_process_object, // GRAPH_NODE_TYPE_OBJECT,
    geo_process_animated_part, // GRAPH_NODE_TYPE_ANIMATED_PART,
    geo_process_billboard, // GRAPH_NODE_TYPE_BILLBOARD,
    geo_process_display_list, // GRAPH_NODE_TYPE_DISPLAY_LIST,
    geo_process_scale, // GRAPH_NODE_TYPE_SCALE,
    geo_process_shadow, // GRAPH_NODE_TYPE_SHADOW,
    geo_process_object_parent, // GRAPH_NODE_TYPE_OBJECT_PARENT,
    geo_process_generated_list, // GRAPH_NODE_TYPE_GENERATED_LIST,
    geo_process_background, // GRAPH_NODE_TYPE_BACKGROUND,
    geo_process_held_object, // GRAPH_NODE_TYPE_HELD_OBJ,
    geo_try_process_children, // GRAPH_NODE_TYPE_CULLING_RADIUS,
};

/**
 * Process a generic geo node and its siblings.
 * The first argument is the start node, and all its siblings will
 * be iterated over.
 */
void geo_process_node_and_siblings(struct GraphNode *firstNode) {
    s16 iterateChildren = TRUE;
    struct GraphNode *curGraphNode = firstNode;
    struct GraphNode *parent = curGraphNode->parent;

    // In the case of a switch node, exactly one of the children of the node is
    // processed instead of all children like usual
    if (parent != NULL) {
        iterateChildren = (parent->type != GRAPH_NODE_TYPE_SWITCH_CASE);
    }

    do {
        if (curGraphNode->flags & GRAPH_RENDER_ACTIVE) {
            if (curGraphNode->flags & GRAPH_RENDER_CHILDREN_FIRST) {
                geo_try_process_children(curGraphNode);
            } else {
                switch (curGraphNode->type) {
                    case GRAPH_NODE_TYPE_ORTHO_PROJECTION:
                        geo_process_ortho_projection((struct GraphNodeOrthoProjection *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_PERSPECTIVE:
                        geo_process_perspective((struct GraphNodePerspective *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_MASTER_LIST:
                        geo_process_master_list((struct GraphNodeMasterList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL:
                        geo_process_level_of_detail((struct GraphNodeLevelOfDetail *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SWITCH_CASE:
                        geo_process_switch((struct GraphNodeSwitchCase *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_CAMERA:
                        geo_process_camera((struct GraphNodeCamera *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION_ROTATION:
                        geo_process_translation_rotation(
                            (struct GraphNodeTranslationRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION:
                        geo_process_translation((struct GraphNodeTranslation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ROTATION:
                        geo_process_rotation((struct GraphNodeRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT:
                        geo_process_object((struct Object *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ANIMATED_PART:
                        geo_process_animated_part((struct GraphNodeAnimatedPart *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BILLBOARD:
                        geo_process_billboard((struct GraphNodeBillboard *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_DISPLAY_LIST:
                        geo_process_display_list((struct GraphNodeDisplayList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SCALE:
                        geo_process_scale((struct GraphNodeScale *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SHADOW:
                        geo_process_shadow((struct GraphNodeShadow *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT_PARENT:
                        geo_process_object_parent((struct GraphNodeObjectParent *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_GENERATED_LIST:
                        geo_process_generated_list((struct GraphNodeGenerated *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BACKGROUND:
                        geo_process_background((struct GraphNodeBackground *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_HELD_OBJ:
                        geo_process_held_object((struct GraphNodeHeldObject *) curGraphNode);
                        break;
                    default:
                        geo_try_process_children((struct GraphNode *) curGraphNode);
                        break;
                }
            }
        } else {
            if (curGraphNode->type == GRAPH_NODE_TYPE_OBJECT) {
                ((struct GraphNodeObject *) curGraphNode)->throwMatrix = NULL;
            }
        }
    } while (iterateChildren && (curGraphNode = curGraphNode->next) != firstNode);
}

/**
 * Process a root node. This is the entry point for processing the scene graph.
 * The root node itself sets up the viewport, then all its children are processed
 * to set up the projection and draw display lists.
 */
void geo_process_root(struct GraphNodeRoot *node, Vp *b, Vp *c, s32 clearColor) {

    if (node->node.flags & GRAPH_RENDER_ACTIVE) {
        Mtx *initialMatrix;
        Vp *viewport = alloc_display_listGRAPH(sizeof(*viewport));

        //print_text_fmt_int(32,64, "%d", sMaterialSwaps);
        sMaterialSwaps = 0;
        initialMatrix = alloc_display_listGRAPH(sizeof(*initialMatrix));
        gCurLookAt = (LookAt*) alloc_display_list(sizeof(LookAt));
        bzero(gCurLookAt, sizeof(LookAt));
        gMatStackIndex = 0;
        gCurrAnimType = 0;
        vec3s_set(viewport->vp.vtrans, gScreenWidth * 2, gScreenHeight * 2, 511);
        vec3s_set(viewport->vp.vscale, gScreenWidth * 2, gScreenHeight * 2, 511);
        if (b != NULL) {
            clear_framebuffer(clearColor);
            make_viewport_clip_rect(b);
            *viewport = *b;
        } else if (c != NULL) {
            clear_framebuffer(clearColor);
            make_viewport_clip_rect(c);
        }

        mtxf_identity(gMatStack[gMatStackIndex]);
        mtxf_to_mtx((s16 *) initialMatrix, (f32 *) gMatStack[gMatStackIndex]);
        gMatStackFixed[gMatStackIndex] = initialMatrix;
        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(viewport));
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(gMatStackFixed[gMatStackIndex]),
                  G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gCurGraphNodeRoot = node;
        if (node->node.children != NULL) {
            geo_process_node_and_siblings(node->node.children);
        }
        gCurGraphNodeRoot = NULL;
    }
}
