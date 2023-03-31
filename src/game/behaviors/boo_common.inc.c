#include "game/level_update.h"
#include "game/interaction.h"

static s32 boo_vanish_or_appear(void) {
    s16 relativeAngleToMario = abs_angle_diff(o->oAngleToMario, o->oMoveAngleYaw);
    s16 relativeMarioFaceAngle = abs_angle_diff(o->oMoveAngleYaw, gMarioObject->oFaceAngleYaw);
    // magic?
    s16 relativeAngleToMarioThreshhold = 0x1568;
    s16 relativeMarioFaceAngleThreshhold = 0x6B58;
    s32 doneAppearing = FALSE;

    o->oVelY = 0.0f;

    if (relativeAngleToMario > relativeAngleToMarioThreshhold
        || relativeMarioFaceAngle < relativeMarioFaceAngleThreshhold) {
        if (o->oOpacity == 40) {
            o->oBooTargetOpacity = 255;
            cur_obj_play_sound_2(SOUND_OBJ_BOO_LAUGH_LONG);
        }

        if (o->oOpacity > 180) {
            doneAppearing = TRUE;
        }
    } else if (o->oOpacity == 255) {
        o->oBooTargetOpacity = 40;
    }

    return doneAppearing;
}

static void boo_oscillate(s32 ignoreOpacity) {
    o->oFaceAnglePitch = sins(o->oBooOscillationTimer) * 0x400;

    if (o->oOpacity == 255 || ignoreOpacity == TRUE) {
        o->header.gfx.scale[0] = sins(o->oBooOscillationTimer) * 0.08 + o->oBooBaseScale;
        o->header.gfx.scale[1] = -sins(o->oBooOscillationTimer) * 0.08 + o->oBooBaseScale;
        o->header.gfx.scale[2] = o->header.gfx.scale[0];
        o->oGravity = sins(o->oBooOscillationTimer) * o->oBooBaseScale;
        o->oBooOscillationTimer += 0x400;
    }
}

// boo idle/chasing movement?
static void boo_chase_mario(f32 a0, s16 a1, f32 a2) {
    f32 sp1C;
    s16 sp1A;

    if (boo_vanish_or_appear()) {
        o->oInteractType = INTERACT_BOUNCE_TOP;

        if (cur_obj_lateral_dist_from_mario_to_home() > 1500.0f) {
            sp1A = cur_obj_angle_to_home();
        } else {
            sp1A = o->oAngleToMario;
        }

        cur_obj_rotate_yaw_toward(sp1A, a1);
        o->oVelY = 0.0f;

        if (!mario_is_in_air_action()) {
            sp1C = o->oPosY - gMarioObject->oPosY;
            if (a0 < sp1C && sp1C < 500.0f) {
                o->oVelY = increment_velocity_toward_range
                               (o->oPosY, gMarioObject->oPosY + 50.0f, 10.0f, 2.0f);
            }
        }

        cur_obj_set_vel_from_mario_vel(10.0f - o->oBooNegatedAggressiveness, a2);

        if (o->oForwardVel != 0.0f) {
            boo_oscillate(FALSE);
        }
    } else {
        o->oInteractType = 0;
        // why is boo_stop not used here
        o->oForwardVel = 0.0f;
        o->oVelY = 0.0f;
        o->oGravity = 0.0f;
    }
}

static void boo_approach_target_opacity_and_update_scale(void) {
    f32 scale;

    if (o->oBooTargetOpacity != o->oOpacity) {
        if (o->oBooTargetOpacity > o->oOpacity) {
            o->oOpacity += 20;

            if (o->oBooTargetOpacity < o->oOpacity) {
                o->oOpacity = o->oBooTargetOpacity;
            }
        } else {
            o->oOpacity -= 20;

            if (o->oBooTargetOpacity > o->oOpacity) {
                o->oOpacity = o->oBooTargetOpacity;
            }
        }
    }

    scale = (o->oOpacity / 255.0f * 0.4 + 0.6) * o->oBooBaseScale;
    obj_scale(o, scale); // why no cur_obj_scale? was cur_obj_scale written later?
}

static s32 obj_has_attack_type(u32 attackType) {
    if ((o->oInteractStatus & INT_STATUS_ATTACK_MASK) == attackType) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static s32 boo_get_attack_status(void) {
    s32 attackStatus = BOO_NOT_ATTACKED;

    if (o->oInteractStatus & INT_STATUS_INTERACTED) {
        if ((o->oInteractStatus & INT_STATUS_WAS_ATTACKED)
            && !obj_has_attack_type(ATTACK_FROM_ABOVE)) {
            cur_obj_become_intangible();

            o->oInteractStatus = 0;

            cur_obj_play_sound_2(SOUND_OBJ_BOO_LAUGH_SHORT);

            attackStatus = BOO_ATTACKED;
        } else {
            cur_obj_play_sound_2(SOUND_OBJ_BOO_BOUNCE_TOP);

            o->oInteractStatus = 0;

            attackStatus = BOO_BOUNCED_ON;
        }
    }

    return attackStatus;
}

static void big_boo_shake_after_hit(void) {
    // Oscillate yaw
    s32 oscillationVel = o->oTimer * 0x2000 - 0x3E000;
    o->oFaceAngleYaw += coss(oscillationVel) * 0x400;
}

static void boo_reset_after_hit(void) {
    o->oMoveAngleYaw = o->oBooMoveYawBeforeHit;
    o->oFlags |= OBJ_FLAG_SET_FACE_YAW_TO_MOVE_YAW;
    o->oInteractStatus = 0;
}

static void boo_set_move_yaw_for_during_hit(s32 hurt) {
    cur_obj_become_intangible();

    o->oFlags &= ~OBJ_FLAG_SET_FACE_YAW_TO_MOVE_YAW;
    o->oBooMoveYawBeforeHit = (f32) o->oMoveAngleYaw;

    if (hurt) {
        o->oBooMoveYawDuringHit = gMarioObject->oMoveAngleYaw;
    } else if (coss((s16) o->oMoveAngleYaw - (s16) o->oAngleToMario) < 0.0f) {
        o->oBooMoveYawDuringHit = o->oMoveAngleYaw;
    } else {
        o->oBooMoveYawDuringHit = (s16)(o->oMoveAngleYaw + 0x8000);
    }
}

static void boo_stop(void) {
    o->oForwardVel = 0.0f;
    o->oVelY = 0.0f;
    o->oGravity = 0.0f;
}

static void boo_move_during_hit(s32 roll, f32 fVel) {
    // Boos seem to have been supposed to oscillate up then down then back again
    // when hit. However it seems the programmers forgot to scale the cosine,
    // so the Y velocity goes from 1 to -1 and back to 1 over 32 frames.
    // This is such a small change that the Y position only changes by 5 units.
    // It's completely unnoticable in-game.
    s32 oscillationVel = o->oTimer * 0x800 + 0x800;

    o->oForwardVel = fVel;
    o->oVelY = coss(oscillationVel);
    o->oMoveAngleYaw = o->oBooMoveYawDuringHit;

    if (roll) {
        o->oFaceAngleYaw  += sBooHitRotations[o->oTimer];
        o->oFaceAngleRoll += sBooHitRotations[o->oTimer];
    }
}

static s32 boo_update_after_bounced_on(f32 a0) {
    boo_stop();

    if (o->oTimer == 0) {
        boo_set_move_yaw_for_during_hit(FALSE);
    }

    if (o->oTimer < 32) {
        boo_move_during_hit(FALSE, sBooHitRotations[o->oTimer] / 5000.0f * a0);
    } else {
        cur_obj_become_tangible();
        boo_reset_after_hit();
        o->oAction = 1;

        return TRUE;
    }

    return FALSE;
}

// called every frame once mario lethally hits the boo until the boo is deleted,
// returns whether death is complete
static s32 boo_update_during_death(void) {
    if (o->oTimer == 0) {
        o->oForwardVel = 40.0f;
        o->oMoveAngleYaw = gMarioObject->oMoveAngleYaw;
        o->oBooDeathStatus = BOO_DEATH_STATUS_DYING;
        o->oFlags &= ~OBJ_FLAG_SET_FACE_YAW_TO_MOVE_YAW;
    } else {
        if (o->oTimer == 5) {
            o->oBooTargetOpacity = 0;
        }

        if (o->oTimer > 30 || o->oMoveFlags & OBJ_MOVE_HIT_WALL) {
            spawn_mist_particles();
            o->oBooDeathStatus = BOO_DEATH_STATUS_DEAD;

            if (o->oBooParentBigBoo != NULL) {
                struct Object *parentBigBoo = o->oBooParentBigBoo;

#ifndef VERSION_JP
                if (!cur_obj_has_behavior(bhvBoo)) {
#endif
                    parentBigBoo->oBigBooNumMinionBoosKilled++;
#ifndef VERSION_JP
                }
#endif
            }

            return TRUE;
        }
    }

    o->oVelY = 5.0f;
    o->oFaceAngleRoll += 0x800;
    o->oFaceAngleYaw += 0x800;

    return FALSE;
}

static s32 boo_should_be_stopped(void) {
    if (cur_obj_has_behavior(bhvMerryGoRoundBigBoo) || cur_obj_has_behavior(bhvMerryGoRoundBoo)) {
        if (!gMarioOnMerryGoRound) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        if (o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM) {
            return TRUE;
        }

        if (o->oRoom == 10 && (gTimeStopState & TIME_STOP_MARIO_OPENED_DOOR)) {
            return TRUE;
        }
    }

    return FALSE;
}

static s32 boo_should_be_active(void) {
    f32 activationRadius;

    if (cur_obj_has_behavior(bhvBalconyBigBoo)) {
        activationRadius = 5000.0f;
    } else {
        activationRadius = 1500.0f;
    }

    if (cur_obj_has_behavior(bhvMerryGoRoundBigBoo) || cur_obj_has_behavior(bhvMerryGoRoundBoo)) {
        if (gMarioOnMerryGoRound == TRUE) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else if (o->oRoom == -1) {
        if (o->oDistanceToMario < activationRadius) {
            return TRUE;
        }
    } else if (!boo_should_be_stopped()) {
        if (o->oDistanceToMario < activationRadius
            && (o->oRoom == gMarioCurrentRoom || gMarioCurrentRoom == 0)) {
            return TRUE;
        }
    }

    return FALSE;
}