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

s16 gMatStackIndex;
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
ALIGNED16 u8 gMarioAnimHeap[0x4000];
struct AnimInfo gMarioGfxAnim;
struct DmaHandlerList gMarioGfxAnimBuf;
struct DmaHandlerList *gMarioGfxAnimList = &gMarioGfxAnimBuf;

struct AllocOnlyPool *gDisplayListHeap;

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
    {0, 0, {0, 0, 0, 0}, 1}, // BOWSER_3
    {0, 0, {0, 0, 0, 0}, 0}, // UNKNOWN_35
    {960, 1000, {0, 0, 0, 255}, 2 | 4}, // TTM
    {0, 0, {0, 0, 0, 0}, 0}, 
    {0, 0, {0, 0, 0, 0}, 0},
};

u32 gFirstCycleRM = G_RM_PASS;

void update_level_fog(Gfx **gfx) {
    if (gLevelFog[gCurrLevelNum].areaFlags & (1 << (gCurrAreaIndex - 1))) {
        gDPSetFogColor((*gfx)++, gLevelFog[gCurrLevelNum].c[0], gLevelFog[gCurrLevelNum].c[1], gLevelFog[gCurrLevelNum].c[2], gLevelFog[gCurrLevelNum].c[3]);
        gSPFogPosition((*gfx)++, gLevelFog[gCurrLevelNum].near, gLevelFog[gCurrLevelNum].far);
        gSPSetGeometryMode((*gfx)++, G_FOG);
        gFirstCycleRM = G_RM_FOG_SHADE_A;
    } else {
        gSPClearGeometryMode((*gfx)++, G_FOG);
        gFirstCycleRM = G_RM_PASS;
    }
}

/**
 * Process a master list node.
 */
void geo_process_master_list_sub(struct GraphNodeMasterList *node) {
    struct DisplayListNode *currList;
    struct RenderModeContainer *mode2List;
    Gfx *gfx = gDisplayListHead;
    s32 switchAA = FALSE;
    s32 lastAA = 0;

    gSPSetGeometryMode(gfx++, G_ZBUFFER);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    update_level_fog(&gfx);

    for (u32 i = 0; i < GFX_NUM_MASTER_LISTS; i++, switchAA = TRUE) {
        if ((currList = node->listHeads[i]) != NULL) {
            while (currList != NULL) {
                if (switchAA == TRUE || currList->fancyAA != lastAA) {
                    mode2List = &renderModeTable_2Cycle[gAntiAliasing + 1 + currList->fancyAA];
                    gDPSetRenderMode(gfx++, gFirstCycleRM, mode2List->modes[i]);
                    switchAA = FALSE;
                }
                gSPMatrix(gfx++, OS_K0_TO_PHYSICAL(currList->transform), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPDisplayList(gfx++, currList->displayList);
                currList = currList->next;
            }
        }
    }
    gSPClearGeometryMode(gfx++, G_ZBUFFER | G_FOG);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDisplayListHead = gfx;
}

#define ALIGN8(val) (((val) + 0x7) & ~0x7)

static void *alloc_only_pool_allocGRAPH(struct AllocOnlyPool *pool, s32 size) { // refreshes once per frame
    void *addr;
    addr = pool->freePtr;
    pool->freePtr += size;
    pool->usedSpace += size;
    return addr;
}

static void *alloc_display_listGRAPH(u32 size) {
    gGfxPoolEnd -= size;
    return gGfxPoolEnd;
}

/**
 * Appends the display list to one of the master lists based on the layer
 * parameter. Look at the RenderModeContainer struct to see the corresponding
 * render modes of layers.
 */
void geo_append_display_list(void *displayList, s16 layer) {

    if (gCurGraphNodeMasterList != 0) {
        struct DisplayListNode *listNode = alloc_only_pool_allocGRAPH(gDisplayListHeap, sizeof(struct DisplayListNode));

        listNode->transform = gMatStackFixed[gMatStackIndex];
        listNode->displayList = displayList;
        listNode->next = 0;
        if (gCurGraphNodeObject != NULL && gAntiAliasing == 0) {
            listNode->fancyAA = TRUE;
        } else {
            listNode->fancyAA = FALSE;
        }
        if (gCurGraphNodeMasterList->listHeads[layer] == 0) {
            gCurGraphNodeMasterList->listHeads[layer] = listNode;
        } else {
            gCurGraphNodeMasterList->listTails[layer]->next = listNode;
        }
        gCurGraphNodeMasterList->listTails[layer] = listNode;
    }
}

void inc_mat_stack() {
    Mtx *mtx = alloc_display_listGRAPH(sizeof(*mtx));
    gMatStackIndex++;
    mtxf_to_mtx((s16 *) mtx, (f32 *) gMatStack[gMatStackIndex]);
    gMatStackFixed[gMatStackIndex] = mtx;
}

void append_dl_and_return(const struct GraphNodeDisplayList *node) {
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
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
        for (u32 i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
            node->listHeads[i] = NULL;
        }
        geo_process_node_and_siblings(node->node.children);
        geo_process_master_list_sub(node);
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

        f32 aspect = (f32) gScreenWidth / (f32) SCREEN_HEIGHT;

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

Lights1 defaultLight = gdSPDefLights1(
    0x3F, 0x3F, 0x3F, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00
);

Vec3f globalLightDirection = { 0x28, 0x28, 0x28 };

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

/**
 * Process a camera node.
 */
void geo_process_camera(struct GraphNodeCamera *node) {
    Mtx *rollMtx = alloc_display_list(sizeof(*rollMtx));
    Mtx *mtx = alloc_display_list(sizeof(*mtx));
    Mtx *viewMtx = alloc_display_list(sizeof(Mtx));

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }

    gSPLookAt(gDisplayListHead++, &gCurLookAt);
    mtxf_rotate_xy(rollMtx, node->rollScreen);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(rollMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

    if (!gMoveSpeed) {
        node->posLerp[0] = node->pos[0];
        node->posLerp[1] = node->pos[1];
        node->posLerp[2] = node->pos[2];
        node->focusLerp[0] = node->focus[0];
        node->focusLerp[1] = node->focus[1];
        node->focusLerp[2] = node->focus[2];
    } else {
        node->posLerp[0] = approach_pos_lerp(node->posLerp[0], node->pos[0]);
        node->posLerp[1] = approach_pos_lerp(node->posLerp[1], node->pos[1]);
        node->posLerp[2] = approach_pos_lerp(node->posLerp[2], node->pos[2]);
        node->focusLerp[0] = approach_pos_lerp(node->focusLerp[0], node->focus[0]);
        node->focusLerp[1] = approach_pos_lerp(node->focusLerp[1], node->focus[1]);
        node->focusLerp[2] = approach_pos_lerp(node->focusLerp[2], node->focus[2]);
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
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(viewMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    setup_global_light();
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
    gMatStackIndex--;
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
    append_dl_and_return((struct GraphNodeDisplayList *) node);
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
    append_dl_and_return((struct GraphNodeDisplayList *) node);
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
    append_dl_and_return((struct GraphNodeDisplayList *) node);
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
    append_dl_and_return((struct GraphNodeDisplayList *) node);
}

/**
 * Process a billboard node. A transformation matrix is created that makes its
 * children face the camera, and it is pushed on the floating point and fixed
 * point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
void geo_process_billboard(struct GraphNodeBillboard *node) {
    Vec3f translation;

    vec3s_to_vec3f(translation, node->translation);
    mtxf_billboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], translation, gVec3fOne, gCurGraphNodeCamera->roll);
    if (gCurGraphNodeHeldObject != NULL) {
        mtxf_scale_vec3f(gMatStack[gMatStackIndex], gMatStack[gMatStackIndex], gCurGraphNodeHeldObject->objNode->header.gfx.scale);
    } else if (gCurGraphNodeObject != NULL) {
        mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1], gCurGraphNodeObject->scale);
    }
    inc_mat_stack();
    append_dl_and_return(((struct GraphNodeDisplayList *) node));
}

/**
 * Process a display list node. It draws a display list without first pushing
 * a transformation on the stack, so all transformations are inherited from the
 * parent node. It processes its children if it has them.
 */
void geo_process_display_list(const struct GraphNodeDisplayList *node) {

    append_dl_and_return((struct GraphNodeDisplayList *) node);
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
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
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
        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
    } else if (gCurGraphNodeMasterList != NULL) {
        Gfx *gfxStart = alloc_display_listGRAPH(sizeof(Gfx) * 8);
        Gfx *gfx = gfxStart;

        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_FILL);
        gDPSetFillColor(gfx++, node->background);
        gDPFillRectangle(gfx++, 0, 0, gScreenWidth - 1, SCREEN_HEIGHT - 1);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gSPEndDisplayList(gfx++);

        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(gfxStart), 0);
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

/**
 * Render an animated part. The current animation state is not part of the node
 * but set in global variables. If an animated part is skipped, everything afterwards desyncs.
 */
void geo_process_animated_part(const struct GraphNodeAnimatedPart *node) {
    Vec3f translation;
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

            if (gMoveSpeed == 0) {
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0] = translation[0];
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1] = translation[1];
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2] = translation[2];
            } else {
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 0], translation[0]);
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 1], translation[1]);
                gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2] = approach_pos_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0 + 2], translation[2]);
            }
            vec3f_set(translation, gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0], gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1], gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2]);
            gCurrAnimPos++;
        }
        gCurrAnimType = ANIM_TYPE_ROTATION;
        if (gMoveSpeed == 0) {
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2] = gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        } else {
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][0], gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]);
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][1], gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]);
            gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2] = approach_angle_lerp(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos][2], gCurrAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]);
        }
    }
    mtxf_rot_trans_mul(gCurGraphNodeObjectNode->header.gfx.animInfo.animRotStack[gCurrAnimPos], translation, gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex]);
    gCurrAnimPos++;
    inc_mat_stack();
    append_dl_and_return((struct GraphNodeDisplayList *) node);
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
    } else {
        tempNode = node;
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
void geo_process_shadow(struct GraphNodeShadow *node)
{
    if (gCurGraphNodeCamera != NULL && gCurGraphNodeObject != NULL) {
        Vec3f shadowPos;
        f32 shadowScale;

        if (gCurGraphNodeHeldObject != NULL) {
            vec3f_copy(shadowPos, gMatStack[gMatStackIndex][3]);
            shadowScale = node->shadowScale * gCurGraphNodeHeldObject->objNode->header.gfx.scaleLerp[0];
        } else {
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

            inc_mat_stack();
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), gCurrShadow.isDecal ? LAYER_TRANSPARENT_DECAL : LAYER_TRANSPARENT);

            gMatStackIndex--;
        }
    }
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
    /*if (node->header.gfx.sharedChild == NULL || !(node->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) || node->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) {
        // Still want to know where the object is in worldspace, for audio panning to correctly work.
        mtxf_translate(gMatStack[gMatStackIndex + 1], node->header.gfx.pos);
        linear_mtxf_mul_vec3f_and_translate(gCameraTransform, node->header.gfx.cameraToObject, gMatStack[gMatStackIndex + 1][3]);
        node->header.gfx.bothMats = 0;
        return;
    }*/

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
        gMatStack[gMatStackIndex + 1][3][0] = gMatStack[gMatStackIndex][3][0];
        gMatStack[gMatStackIndex + 1][3][1] = gMatStack[gMatStackIndex][3][1];
        gMatStack[gMatStackIndex + 1][3][2] = gMatStack[gMatStackIndex][3][2];
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
            geo_set_animation_globals(&node->objNode->header.gfx.animInfo, NULL);
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
                u32 curGraphNodeType = curGraphNode->type;
                if (curGraphNodeType < ARRAY_COUNT(geoFunctionTable)) {
                    geoFunctionTable[curGraphNodeType](curGraphNode);
                } else {
                    geo_try_process_children(curGraphNode);
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

        gDisplayListHeap = alloc_only_pool_init(main_pool_available() - sizeof(struct AllocOnlyPool), MEMORY_POOL_LEFT);
        initialMatrix = alloc_display_listGRAPH(sizeof(*initialMatrix));
        gCurLookAt = (LookAt*) alloc_display_list(sizeof(LookAt));
        bzero(gCurLookAt, sizeof(LookAt));
        gCurLookAt->l[1].l.col[1] = 0x80;
        gCurLookAt->l[1].l.colc[1] = 0x80;
        gMatStackIndex = 0;
        gCurrAnimType = 0;
        vec3s_set(viewport->vp.vtrans, gScreenWidth * 2, SCREEN_HEIGHT * 2, 511);
        vec3s_set(viewport->vp.vscale, gScreenWidth * 2, SCREEN_HEIGHT * 2, 511);
        if (b != NULL) {
            clear_framebuffer(clearColor);
            make_viewport_clip_rect(b);
            *viewport = *b;
        }

        else if (c != NULL) {
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
        if (gShowDebugText) {
            print_text_fmt_int(180, 36, "MEM %d", gDisplayListHeap->totalSpace - gDisplayListHeap->usedSpace);
        }
        main_pool_free(gDisplayListHeap);
    }
}
