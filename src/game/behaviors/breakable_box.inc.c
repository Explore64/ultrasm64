// breakable_box.inc.c

struct ObjectHitbox sBreakableBoxHitbox = {
    /* interactType:      */ INTERACT_BREAKABLE,
    /* downOffset:        */ 20,
    /* damageOrCoinValue: */ 0,
    /* health:            */ 1,
    /* numLootCoins:      */ 0,
    /* radius:            */ 150,
    /* height:            */ 200,
    /* hurtboxRadius:     */ 150,
    /* hurtboxHeight:     */ 200,
};

void breakable_box_init(void) {
    o->oHiddenObjectUnkF4 = NULL;
    o->oAnimState = 1;

    switch (o->oBehParams2ndByte) {
        case 0:
            o->oNumLootCoins = 0;
            break;
        case 1:
            o->oNumLootCoins = 3;
            break;
        case 2:
            o->oNumLootCoins = 5;
            break;
        case 3:
            cur_obj_scale(1.5f);
            break;
    }
}

void bhv_breakable_box_loop(void) {
    obj_set_hitbox(o, &sBreakableBoxHitbox);
    cur_obj_set_model(MODEL_BREAKABLE_BOX_SMALL);

    if (o->oTimer == 0) {
        breakable_box_init();
    }

    if (cur_obj_was_attacked_or_ground_pounded()) {
        obj_explode_and_spawn_coins(46.0f, 1);
        create_sound_spawner(SOUND_GENERAL_BREAK_BOX);
    }
}
