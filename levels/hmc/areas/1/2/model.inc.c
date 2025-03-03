// 0x070079E8 - 0x07007AE8
static const Vtx hmc_seg7_vertex_070079E8[] = {
    {{{ -6860,   1760,   3753}, 0, {  5998,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1658,   3753}, 0, {    70,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1658,   3753}, 0, {  5998,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1536,   2545}, 0, {  5998,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1536,   2545}, 0, {    70,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1434,   2545}, 0, {    70,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1434,   2545}, 0, {  5998,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1331,   3548}, 0, {  5998,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1331,   3548}, 0, {    70,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1229,   3548}, 0, {    70,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1229,   3548}, 0, {  5998,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,   1024,   3343}, 0, {  5998,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,    922,   3343}, 0, {    70,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6860,    922,   3343}, 0, {  5998,    990}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1024,   3343}, 0, {    70,      0}, {0xff, 0xff, 0xff, 0xff}}},
    {{{ -6245,   1760,   3753}, 0, {    70,      0}, {0xff, 0xff, 0xff, 0xff}}},
};

// 0x07007AE8 - 0x07007B50
static const Gfx hmc_seg7_dl_07007AE8[] = {
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, cave_09003000),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
    gsSPVertex(hmc_seg7_vertex_070079E8, 16, 0),
    gsSP2Triangles( 0,  1,  2, 0x0,  3,  4,  5, 0x0),
    gsSP2Triangles( 3,  5,  6, 0x0,  7,  8,  9, 0x0),
    gsSP2Triangles( 7,  9, 10, 0x0, 11, 12, 13, 0x0),
    gsSP2Triangles(11, 14, 12, 0x0,  0, 15,  1, 0x0),
    gsSPEndDisplayList(),
};

// 0x07007B50 - 0x07007C08
const Gfx hmc_seg7_dl_07007B50[] = {
    gsDPPipeSync(),
    gsDPSetDepthSource(G_ZS_PIXEL),
    gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_PASS2),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD),
    gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPDisplayList(hmc_seg7_dl_07007AE8),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_PASS2),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPEndDisplayList(),
};
