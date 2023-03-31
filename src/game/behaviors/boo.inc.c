// boo.inc.c

static struct ObjectHitbox sBooGivingStarHitbox = {
    /* interactType:      */ 0,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 3,
    /* health:            */ 3,
    /* numLootCoins:      */ 0,
    /* radius:            */ 140,
    /* height:            */ 80,
    /* hurtboxRadius:     */ 40,
    /* hurtboxHeight:     */ 60,
};

// Boo Roll
static s16 sBooHitRotations[] = {
    6047, 5664, 5292, 4934, 4587, 4254, 3933, 3624, 3329, 3046, 2775,
    2517, 2271, 2039, 1818, 1611, 1416, 1233, 1063, 906,  761,  629,
    509,  402,  308,  226,  157,  100,  56,   25,   4,    0,
};

#include "boo_common.inc.c"

void bhv_boo_init(void) {
    o->oBooInitialMoveYaw = o->oMoveAngleYaw;
}

// called iff big boo nonlethally hit
static s32 big_boo_update_during_nonlethal_hit(f32 a0) {
    boo_stop();

    if (o->oTimer == 0) {
        boo_set_move_yaw_for_during_hit(TRUE);
    }

    if (o->oTimer < 32) {
        boo_move_during_hit(TRUE, sBooHitRotations[o->oTimer] / 5000.0f * a0);
    } else if (o->oTimer < 48) {
        big_boo_shake_after_hit();
    } else {
        cur_obj_become_tangible();
        boo_reset_after_hit();
        o->oAction = 1;

        return TRUE;
    }

    return FALSE;
}

static void boo_act_0(void) {
    o->activeFlags |= ACTIVE_FLAG_MOVE_THROUGH_GRATE;

    if (o->oBehParams2ndByte == 2) {
        o->oRoom = 10;
    }

    cur_obj_set_pos_to_home();
    o->oMoveAngleYaw = o->oBooInitialMoveYaw;
    boo_stop();

    o->oBooParentBigBoo = cur_obj_nearest_object_with_behavior(bhvGhostHuntBigBoo);
    o->oBooBaseScale = 1.0f;
    o->oBooTargetOpacity = 255;

    if (boo_should_be_active()) {
        // Condition is met if the object is bhvBalconyBigBoo or bhvMerryGoRoundBoo
        if (o->oBehParams2ndByte == 2) {
            o->oBooParentBigBoo = NULL;
            o->oAction = 5;
        } else {
            o->oAction = 1;
        }
    }
}

static void boo_act_5(void) {
    if (o->oTimer < 30) {
        o->oVelY = 0.0f;
        o->oForwardVel = 13.0f;
        boo_oscillate(FALSE);
        o->oWallHitboxRadius = 0.0f;
    } else {
        o->oAction = 1;
        o->oWallHitboxRadius = 30.0f;
    }
}

static void boo_act_1(void) {
    s32 attackStatus;

    if (o->oTimer == 0) {
        o->oBooNegatedAggressiveness = -random_float() * 5.0f;
        o->oBooTurningSpeed = (s32)(random_float() * 128.0f);
    }

    boo_chase_mario(-100.0f, o->oBooTurningSpeed + 0x180, 0.5f);

    attackStatus = boo_get_attack_status();

    if (boo_should_be_stopped()) {
        o->oAction = 0;
    }

    if (attackStatus == BOO_BOUNCED_ON) {
        o->oAction = 2;
    }

    if (attackStatus == BOO_ATTACKED) {
        o->oAction = 3;
    }

    if (attackStatus == BOO_ATTACKED) {
        create_sound_spawner(SOUND_OBJ_DYING_ENEMY1);
    }
}

static void boo_act_2(void) {
    if (boo_update_after_bounced_on(20.0f)) {
        o->oAction = 1;
    }
}

static void boo_act_3(void) {
    if (boo_update_during_death()) {
        if (o->oBehParams2ndByte != 0) {
            obj_mark_for_deletion(o);
        } else {
            o->oAction = 4;
            cur_obj_disable();
        }
    }
}

// Called when a Go on a Ghost Hunt boo dies
static void boo_act_4(void) {
    s32 dialogID;

    // If there are no remaining "minion" boos, show the dialog of the Big Boo
    if (cur_obj_nearest_object_with_behavior(bhvGhostHuntBoo) == NULL) {
        dialogID = DIALOG_108;
    } else {
        dialogID = DIALOG_107;
    }

    if (cur_obj_update_dialog(MARIO_DIALOG_LOOK_UP, DIALOG_FLAG_TEXT_DEFAULT, dialogID, 0)) {
        create_sound_spawner(SOUND_OBJ_DYING_ENEMY1);
        obj_mark_for_deletion(o);

        if (dialogID == DIALOG_108) { // If the Big Boo should spawn, play the jingle
            play_puzzle_jingle();
        }
    }
}

static void (*sBooActions[])(void) = {
    boo_act_0,
    boo_act_1,
    boo_act_2,
    boo_act_3,
    boo_act_4,
    boo_act_5,
};

void bhv_boo_loop(void) {
    //PARTIAL_UPDATE

    cur_obj_update_floor_and_walls();
    cur_obj_call_action_function(sBooActions);
    cur_obj_move_standard(78);
    boo_approach_target_opacity_and_update_scale();

    if (obj_has_behavior(o->parentObj, bhvMerryGoRoundBooManager)
        && o->activeFlags == ACTIVE_FLAG_DEACTIVATED) {
        o->parentObj->oMerryGoRoundBooManagerNumBoosKilled++;
    }

    o->oInteractStatus = 0;
}

static void big_boo_act_0(void) {
    if (cur_obj_has_behavior(bhvBalconyBigBoo)) {
        obj_set_secondary_camera_focus();
        // number of killed boos set > 5 so that boo always loads
        // redundant? this is also done in behavior_data.s
        o->oBigBooNumMinionBoosKilled = 10;
    }

    o->oBooParentBigBoo = NULL;

    if (boo_should_be_active() && o->oBigBooNumMinionBoosKilled >= 5) {
        o->oAction = 1;

        cur_obj_set_pos_to_home();
        o->oMoveAngleYaw = o->oBooInitialMoveYaw;

        cur_obj_unhide();

        o->oBooTargetOpacity = 255;
        o->oBooBaseScale = 3.0f;
        o->oHealth = 3;

        cur_obj_scale(3.0f);
        cur_obj_become_tangible();
    } else {
        cur_obj_hide();
        cur_obj_become_intangible();
        boo_stop();
    }
}

static void big_boo_act_1(void) {
    s32 attackStatus;
    s16 sp22;
    f32 sp1C;

    if (o->oHealth == 3) {
        sp22 = 0x180; sp1C = 0.5f;
    } else if (o->oHealth == 2) {
        sp22 = 0x240; sp1C = 0.6f;
    } else {
        sp22 = 0x300; sp1C = 0.8f;
    }

    boo_chase_mario(-100.0f, sp22, sp1C);

    attackStatus = boo_get_attack_status();

    // redundant; this check is in boo_should_be_stopped
    if (cur_obj_has_behavior(bhvMerryGoRoundBigBoo)) {
        if (!gMarioOnMerryGoRound) {
            o->oAction = 0;
        }
    } else if (boo_should_be_stopped()) {
        o->oAction = 0;
    }

    if (attackStatus == BOO_BOUNCED_ON) {
        o->oAction = 2;
    }

    if (attackStatus == BOO_ATTACKED) {
        o->oAction = 3;
    }

    if (attackStatus == BOO_ATTACKED) {
        create_sound_spawner(SOUND_OBJ_THWOMP);
    }
}

static void big_boo_act_2(void) {
    if (boo_update_after_bounced_on(20.0f)) {
        o->oAction = 1;
    }
}

static void big_boo_spawn_ghost_hunt_star(void) {
    spawn_default_star(980.0f, 1100.0f, 250.0f);
}

static void big_boo_spawn_balcony_star(void) {
    spawn_default_star(700.0f, 3200.0f, 1900.0f);
}

static void big_boo_spawn_merry_go_round_star(void) {
    struct Object *merryGoRound;

    spawn_default_star(-1600.0f, -2100.0f, 205.0f);

    merryGoRound = cur_obj_nearest_object_with_behavior(bhvMerryGoRound);

    if (merryGoRound != NULL) {
        merryGoRound->oMerryGoRoundStopped = TRUE;
    }
}

static void big_boo_act_3(void) {
    if (o->oTimer == 0) {
        o->oHealth--;
    }

    if (o->oHealth == 0) {
        if (boo_update_during_death()) {
            cur_obj_disable();

            o->oAction = 4;

            obj_set_angle(o, 0, 0, 0);

            if (o->oBehParams2ndByte == 0) {
                big_boo_spawn_ghost_hunt_star();
            } else if (o->oBehParams2ndByte == 1) {
                big_boo_spawn_merry_go_round_star();
            } else {
                big_boo_spawn_balcony_star();
            }
        }
    } else {
        if (o->oTimer == 0) {
            spawn_mist_particles();
            o->oBooBaseScale -= 0.5f;
        }

        if (big_boo_update_during_nonlethal_hit(40.0f)) {
            o->oAction = 1;
        }
    }
}

static void big_boo_act_4(void) {
#ifndef VERSION_JP
    boo_stop();
#endif

    if (o->oBehParams2ndByte == 0) {
        obj_set_pos(o, 973, 0, 626);

        if (o->oTimer > 60 && o->oDistanceToMario < 600.0f) {
            obj_set_pos(o,  973, 0, 717);

            spawn_object_relative(0, 0, 0,    0, o, MODEL_BBH_STAIRCASE_STEP, bhvBooStaircase);
            spawn_object_relative(1, 0, 0, -200, o, MODEL_BBH_STAIRCASE_STEP, bhvBooStaircase);
            spawn_object_relative(2, 0, 0,  200, o, MODEL_BBH_STAIRCASE_STEP, bhvBooStaircase);

            obj_mark_for_deletion(o);
        }
    } else {
        obj_mark_for_deletion(o);
    }
}

static void (*sBooGivingStarActions[])(void) = {
    big_boo_act_0,
    big_boo_act_1,
    big_boo_act_2,
    big_boo_act_3,
    big_boo_act_4,
};

void bhv_big_boo_loop(void) {
    //PARTIAL_UPDATE

    obj_set_hitbox(o, &sBooGivingStarHitbox);

    o->oGraphYOffset = o->oBooBaseScale * 60.0f;

    cur_obj_update_floor_and_walls();
    cur_obj_call_action_function(sBooGivingStarActions);
    cur_obj_move_standard(78);

    boo_approach_target_opacity_and_update_scale();

    o->oInteractStatus = 0;
}

void bhv_merry_go_round_boo_manager_loop(void) {
    switch (o->oAction) {
        case 0:
            if (o->oDistanceToMario < 1000.0f) {
                if (o->oMerryGoRoundBooManagerNumBoosKilled < 5) {
                    if (o->oMerryGoRoundBooManagerNumBoosSpawned != 5) {
                        if (o->oMerryGoRoundBooManagerNumBoosSpawned
                                - o->oMerryGoRoundBooManagerNumBoosKilled < 2) {
                            spawn_object(o, MODEL_BOO, bhvMerryGoRoundBoo);
                            o->oMerryGoRoundBooManagerNumBoosSpawned++;
                        }
                    }

                    o->oAction++;
                }

                if (o->oMerryGoRoundBooManagerNumBoosKilled >= 5) {
                    struct Object *boo = spawn_object(o, MODEL_BOO, bhvMerryGoRoundBigBoo);
                    obj_copy_behavior_params(boo, o);

                    o->oAction = 2;

#ifndef VERSION_JP
                    play_puzzle_jingle();
#else
                    play_sound(SOUND_GENERAL2_RIGHT_ANSWER, gGlobalSoundSource);
#endif
                }
            }

            break;

        case 1:
            if (o->oTimer > 60) {
                o->oAction = 0;
            }

            break;

        case 2:
            break;
    }
}

void obj_set_secondary_camera_focus(void) {
    gSecondCameraFocus = o;
}

void bhv_animated_texture_loop(void) {
    cur_obj_set_pos_to_home_with_debug();
}

void bhv_boo_staircase(void) {
    f32 targetY = 0.0f;

    switch (o->oBehParams2ndByte) {
        case 1:
            targetY = 0.0f;
            break;
        case 0:
            targetY = -206.0f;
            break;
        case 2:
            targetY = -413.0f;
            break;
    }

    switch (o->oAction) {
        case 0:
            o->oPosY = o->oHomeY - 620.0f;
            o->oAction++;
            // fallthrough
        case 1:
            o->oPosY += 8.0f;
            cur_obj_play_sound_1(SOUND_ENV_ELEVATOR2);

            if (o->oPosY > targetY) {
                o->oPosY = targetY;
                o->oAction++;
            }

            break;

        case 2:
            if (o->oTimer == 0) {
                cur_obj_play_sound_2(SOUND_GENERAL_UNKNOWN4_LOWPRIO);
            }

            if (jiggle_bbh_stair(o->oTimer)) {
                o->oAction++;
            }

            break;

        case 3:
            if (o->oTimer == 0 && o->oBehParams2ndByte == 1) {
                play_puzzle_jingle();
            }

            break;
    }
}
