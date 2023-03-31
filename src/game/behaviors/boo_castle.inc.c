
#ifdef UNLOCK_ALL
#define SPAWN_CASTLE_BOO_STAR_REQUIREMENT 0
#else
#define SPAWN_CASTLE_BOO_STAR_REQUIREMENT 12
#endif
static s16 sBooHitRotations[] = {
    6047, 5664, 5292, 4934, 4587, 4254, 3933, 3624, 3329, 3046, 2775,
    2517, 2271, 2039, 1818, 1611, 1416, 1233, 1063, 906,  761,  629,
    509,  402,  308,  226,  157,  100,  56,   25,   4,    0,
};

// Relative positions
static s16 sCourtyardBooTripletPositions[][3] = {
    { 0, 50, 0 },
    { 210, 110, 210 },
    { -210, 70, -210 },
};

#include "boo_common.inc.c"

void bhv_courtyard_boo_triplet_init(void) {
    s32 i;

    if (gHudDisplay.stars < SPAWN_CASTLE_BOO_STAR_REQUIREMENT) {
        obj_mark_for_deletion(o);
    } else {
        for (i = 0; i < 3; i++) {
            struct Object *boo = spawn_object_relative(
                1, sCourtyardBooTripletPositions[i][0], sCourtyardBooTripletPositions[i][1],
                sCourtyardBooTripletPositions[i][2], o, MODEL_BOO, bhvGhostHuntBoo);

            boo->oMoveAngleYaw = random_u16();
        }
    }
}

void bhv_boo_in_castle_loop(void) {
    s16 targetAngle;

    o->oBooBaseScale = 2.0f;

    if (o->oAction == 0) {
        cur_obj_hide();

        if (gHudDisplay.stars < SPAWN_CASTLE_BOO_STAR_REQUIREMENT) {
            obj_mark_for_deletion(o);
        }

        if (gMarioCurrentRoom == 1) {
            o->oAction++;
        }
    } else if (o->oAction == 1) {
        cur_obj_unhide();

        o->oOpacity = 180;

        if (o->oTimer == 0) {
            cur_obj_scale(o->oBooBaseScale);
        }

        if (o->oDistanceToMario < 1000.0f) {
            o->oAction++;
            cur_obj_play_sound_2(SOUND_OBJ_BOO_LAUGH_LONG);
        }

        o->oForwardVel = 0.0f;
        targetAngle = o->oAngleToMario;
    } else {
        cur_obj_forward_vel_approach_upward(32.0f, 1.0f);

        o->oHomeX = -1000.0f;
        o->oHomeZ = -9000.0f;

        targetAngle = cur_obj_angle_to_home();

        if (o->oPosZ < -5000.0f) {
            if (o->oOpacity > 0) {
                o->oOpacity -= 20;
            } else {
                o->oOpacity = 0;
            }
        }

        if (o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM) {
            o->oAction = 1;
        }
    }

    o->oVelY = 0.0f;

    targetAngle = cur_obj_angle_to_home();

    cur_obj_rotate_yaw_toward(targetAngle, 0x5A8);
    boo_oscillate(TRUE);
    cur_obj_move_using_fvel_and_gravity();
}

static void boo_with_cage_act_0(void) {
    o->oBooParentBigBoo = NULL;
    o->oBooTargetOpacity = 255;
    o->oBooBaseScale = 2.0f;

    cur_obj_scale(2.0f);
    cur_obj_become_tangible();

    if (boo_should_be_active()) {
        o->oAction = 1;
    }
}

static void boo_with_cage_act_1(void) {
    s32 attackStatus;

    boo_chase_mario(100.0f, 512, 0.5f);

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
}

static void boo_with_cage_act_2(void) {
    if (boo_update_after_bounced_on(20.0f)) {
        o->oAction = 1;
    }
}

static void boo_with_cage_act_3(void) {
    if (boo_update_during_death()) {
        obj_mark_for_deletion(o);
    }
}

void bhv_boo_with_cage_init(void) {
    if (gHudDisplay.stars < SPAWN_CASTLE_BOO_STAR_REQUIREMENT) {
        obj_mark_for_deletion(o);
    } else {
        struct Object *cage = spawn_object(o, MODEL_HAUNTED_CAGE, bhvBooCage);
        cage->oBehParams = o->oBehParams;
    }
}

static void (*sBooWithCageActions[])(void) = {
    boo_with_cage_act_0,
    boo_with_cage_act_1,
    boo_with_cage_act_2,
    boo_with_cage_act_3,
};

void bhv_boo_with_cage_loop(void) {
    //PARTIAL_UPDATE

    cur_obj_update_floor_and_walls();
    cur_obj_call_action_function(sBooWithCageActions);
    cur_obj_move_standard(78);

    boo_approach_target_opacity_and_update_scale();

    o->oInteractStatus = 0;
}