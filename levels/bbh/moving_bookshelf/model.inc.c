// 0x0701FB20 - 0x0701FB38

// 0x0701FB38 - 0x0701FB78
static const Vtx bbh_seg7_vertex_0701FB38[] = {
    {{{  -255,    819,   -204}, 0, {   990,      0}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -153,    819,   -204}, 0, {   734,      0}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -153,      0,   -204}, 0, {   734,   2012}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -255,      0,   -204}, 0, {   990,   2012}, {0x00, 0x00, 0x81, 0xff}}},
};

// 0x0701FB78 - 0x0701FC78
static const Vtx bbh_seg7_vertex_0701FB78[] = {
    {{{  -255,      0,    205}, 0, {  2012,   2012}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,    512,    205}, 0, {  2012,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,    512,   -204}, 0, {     0,      0}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -153,    512,   -204}, 0, {  2012,      0}, {0x00, 0x00, 0x81, 0xff}}},
    {{{   256,      0,   -204}, 0, {     0,   2012}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -153,      0,   -204}, 0, {  2012,   2012}, {0x00, 0x00, 0x81, 0xff}}},
    {{{   256,    512,   -204}, 0, {     0,      0}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -153,    819,   -204}, 0, {  2012,    580}, {0x00, 0x00, 0x81, 0xff}}},
    {{{   256,    512,   -204}, 0, {     0,   1808}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -153,    512,   -204}, 0, {  2012,   1808}, {0x00, 0x00, 0x81, 0xff}}},
    {{{   256,    819,   -204}, 0, {     0,    580}, {0x00, 0x00, 0x81, 0xff}}},
    {{{  -255,    512,    205}, 0, {  2012,   1808}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,    819,   -204}, 0, {     0,    580}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,    512,   -204}, 0, {     0,   1808}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,    819,    205}, 0, {  2012,    580}, {0x81, 0x00, 0x00, 0xff}}},
    {{{  -255,      0,   -204}, 0, {     0,   2012}, {0x81, 0x00, 0x00, 0xff}}},
};

// 0x0701FC78 - 0x0701FCC0
static const Gfx bbh_seg7_dl_0701FC78[] = {
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, spooky_09003800),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 64 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
    gsSPLightColor(LIGHT_1, 0xffffffff),
    gsSPLightColor(LIGHT_2, 0x666666ff),
    gsSPVertex(bbh_seg7_vertex_0701FB38, 4, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  0,  2,  3, 0x0),
    gsSPEndDisplayList(),
};

// 0x0701FCC0 - 0x0701FD28
static const Gfx bbh_seg7_dl_0701FCC0[] = {
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, spooky_09002800),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 64 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
    gsSPVertex(bbh_seg7_vertex_0701FB78, 16, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  3,  4,  5, 0x0),
    gsSP2Triangles( 3,  6,  4, 0x0,  7,  8,  9, 0x0),
    gsSP2Triangles( 7, 10,  8, 0x0, 11, 12, 13, 0x0),
    gsSP2Triangles(11, 14, 12, 0x0,  0,  2, 15, 0x0),
    gsSPEndDisplayList(),
};

// 0x0701FD28 - 0x0701FDB8
const Gfx bbh_seg7_dl_0701FD28[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_PASS2),
    gsSPClearGeometryMode(G_SHADING_SMOOTH),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 6, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (64 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPDisplayList(bbh_seg7_dl_0701FC78),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, 6, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (64 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPDisplayList(bbh_seg7_dl_0701FCC0),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_PASS2),
    gsSPSetGeometryMode(G_SHADING_SMOOTH),
    gsSPEndDisplayList(),
};
