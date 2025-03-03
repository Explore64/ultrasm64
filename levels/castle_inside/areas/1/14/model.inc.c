// 0x07033068 - 0x07033080

// 0x07033080 - 0x07033100
static const Vtx inside_castle_seg7_vertex_07033080[] = {
    {{{ -3184,    410,   -127}, 0, {     0,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    256,   -127}, 0, {     0,    990}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    256,     26}, 0, {   990,    990}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    410,     26}, 0, {   990,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    410,   -485}, 0, {     0,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    256,   -332}, 0, {   990,    990}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    410,   -332}, 0, {   990,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{ -3184,    256,   -485}, 0, {     0,    990}, {0x81, 0x00, 0x00, 0xff}}},
};

// 0x07033100 - 0x07033158
static const Gfx inside_castle_seg7_dl_07033100[] = {
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, inside_castle_seg7_texture_07003000),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
    gsSPLightColor(LIGHT_1, 0xffffffff),
    gsSPLightColor(LIGHT_2, 0x5f5f5fff),
    gsSPVertex(inside_castle_seg7_vertex_07033080, 8, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  0,  2,  3, 0x0),
    gsSP2Triangles( 4,  5,  6, 0x0,  4,  7,  5, 0x0),
    gsSPEndDisplayList(),
};

// 0x07033158 - 0x070331C8
const Gfx inside_castle_seg7_dl_07033158[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_PASS2),
    gsSPClearGeometryMode(G_SHADING_SMOOTH),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, G_TX_CLAMP, 5, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPDisplayList(inside_castle_seg7_dl_07033100),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_PASS2),
    gsSPSetGeometryMode(G_SHADING_SMOOTH),
    gsSPEndDisplayList(),
};
