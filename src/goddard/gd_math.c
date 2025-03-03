#include <PR/ultratypes.h>

#include "debug_utils.h"
#include "gd_macros.h"
#include "gd_main.h"
#include "gd_math.h"
#include "gd_types.h"
#include "macros.h"
#include "renderer.h"

/**
 * Set mtx to a look-at matrix for the camera. The resulting transformation
 * transforms the world as if there exists a camera at position 'from' pointed
 * at the position 'to'.
 * An effective goddard copy of mtxf_lookat.
 */
void gd_mat4f_lookat(Mat4f *mtx, f32 xFrom, f32 yFrom, f32 zFrom, f32 xTo, f32 yTo, f32 zTo,
                     f32 zColY, f32 yColY, f32 xColY) {
    f32 invLength;

    struct GdVec3f d;
    struct GdVec3f colX;
    struct GdVec3f norm;

    // No reason to do this? mtx is set lower.
    gd_set_identity_mat4(mtx);

    d.z = xTo - xFrom;
    d.y = yTo - yFrom;
    d.x = zTo - zFrom;

    invLength = ABS(d.z) + ABS(d.y) + ABS(d.x);

    // Scales 'd' if smaller than 10 or larger than 10,000 to be
    // of a magnitude of 10,000.
    if (invLength > 10000.0f || invLength < 10.0f) {
        norm.x = d.z;
        norm.y = d.y;
        norm.z = d.x;
        gd_normalize_vec3f(&norm);
        norm.x *= 10000.0f;
        norm.y *= 10000.0f;
        norm.z *= 10000.0f;

        d.z = norm.x;
        d.y = norm.y;
        d.x = norm.z;
    }

    invLength = -1.0f / sqrtf(SQ(d.z) + SQ(d.y) + SQ(d.x));
    d.z *= invLength;
    d.y *= invLength;
    d.x *= invLength;

    colX.z = yColY * d.x - xColY * d.y;
    colX.y = xColY * d.z - zColY * d.x;
    colX.x = zColY * d.y - yColY * d.z;

    invLength = 1.0f / sqrtf(SQ(colX.z) + SQ(colX.y) + SQ(colX.x));

    colX.z *= invLength;
    colX.y *= invLength;
    colX.x *= invLength;

    zColY = d.y * colX.x - d.x * colX.y;
    yColY = d.x * colX.z - d.z * colX.x;
    xColY = d.z * colX.y - d.y * colX.z;

    invLength = 1.0f / sqrtf(SQ(zColY) + SQ(yColY) + SQ(xColY));

    zColY *= invLength;
    yColY *= invLength;
    xColY *= invLength;

    (*mtx)[0][0] = colX.z;
    (*mtx)[1][0] = colX.y;
    (*mtx)[2][0] = colX.x;
    (*mtx)[3][0] = -(xFrom * colX.z + yFrom * colX.y + zFrom * colX.x);

    (*mtx)[0][1] = zColY;
    (*mtx)[1][1] = yColY;
    (*mtx)[2][1] = xColY;
    (*mtx)[3][1] = -(xFrom * zColY + yFrom * yColY + zFrom * xColY);

    (*mtx)[0][2] = d.z;
    (*mtx)[1][2] = d.y;
    (*mtx)[2][2] = d.x;
    (*mtx)[3][2] = -(xFrom * d.z + yFrom * d.y + zFrom * d.x);

    (*mtx)[0][3] = 0.0f;
    (*mtx)[1][3] = 0.0f;
    (*mtx)[2][3] = 0.0f;
    (*mtx)[3][3] = 1.0f;
}

/**
 * Scales a mat4f in each dimension by a vector.
 */
void gd_scale_mat4f_by_vec3f(Mat4f *mtx, struct GdVec3f *vec) {
    (*mtx)[0][0] *= vec->x;
    (*mtx)[0][1] *= vec->x;
    (*mtx)[0][2] *= vec->x;
    (*mtx)[1][0] *= vec->y;
    (*mtx)[1][1] *= vec->y;
    (*mtx)[1][2] *= vec->y;
    (*mtx)[2][0] *= vec->z;
    (*mtx)[2][1] *= vec->z;
    (*mtx)[2][2] *= vec->z;
}

/**
 * Rotates the matrix 'mtx' about the vector given.
 */
void gd_rot_mat_about_vec(Mat4f *mtx, struct GdVec3f *vec) { //600us
    if (vec->x != 0.0f) {
        gd_absrot_mat4(mtx, GD_X_AXIS, vec->x);
    }
    if (vec->y != 0.0f) {
        gd_absrot_mat4(mtx, GD_Y_AXIS, vec->y);
    }
    if (vec->z != 0.0f) {
        gd_absrot_mat4(mtx, GD_Z_AXIS, vec->z);
    }
}

/**
 * Adds each component of a vector to the
 * translation column of a mat4f matrix.
 */
void gd_add_vec3f_to_mat4f_offset(Mat4f *mtx, struct GdVec3f *vec) {
    f32 z, y, x;

    x = vec->x;
    y = vec->y;
    z = vec->z;

    (*mtx)[3][0] += x;
    (*mtx)[3][1] += y;
    (*mtx)[3][2] += z;
}

/**
 * Creates a lookat matrix, but specifically from the perspective of the origin.
 * Rolls is only ever 0 in practice, and this is really only ever used once.
 *
 * Matrix has form-  | -(cz+sxy)/h sh  (cx-syz)/h 0 |
 *                   |  (sz-cxy)/h ch -(sx+cyz)/h 0 |
 *                   |     -x      -y     -z      0 |
 *                   |      0       0      0      1 |
 */
void gd_create_origin_lookat(Mat4f *mtx, struct GdVec3f *vec, f32 roll) {
    f32 invertedHMag;
    f32 hMag;
    f32 c;
    f32 s;
    f32 radPerDeg = RAD_PER_DEG;
    struct GdVec3f unit;

    unit.x = vec->x;
    unit.y = vec->y;
    unit.z = vec->z;

    gd_normalize_vec3f(&unit);
    hMag = sqrtf(SQ(unit.x) + SQ(unit.z));

    roll *= radPerDeg; // convert roll from degrees to radians
    s = sinf(roll);
    c = cosf(roll);

    gd_set_identity_mat4(mtx);
    if (hMag != 0.0f) {
        invertedHMag = 1.0f / hMag;
        (*mtx)[0][0] = ((-unit.z * c) - (s * unit.y * unit.x)) * invertedHMag;
        (*mtx)[1][0] = ((unit.z * s) - (c * unit.y * unit.x)) * invertedHMag;
        (*mtx)[2][0] = -unit.x;
        (*mtx)[3][0] = 0.0f;

        (*mtx)[0][1] = s * hMag;
        (*mtx)[1][1] = c * hMag;
        (*mtx)[2][1] = -unit.y;
        (*mtx)[3][1] = 0.0f;

        (*mtx)[0][2] = ((c * unit.x) - (s * unit.y * unit.z)) * invertedHMag;
        (*mtx)[1][2] = ((-s * unit.x) - (c * unit.y * unit.z)) * invertedHMag;
        (*mtx)[2][2] = -unit.z;
        (*mtx)[3][2] = 0.0f;

        (*mtx)[0][3] = 0.0f;
        (*mtx)[1][3] = 0.0f;
        (*mtx)[2][3] = 0.0f;
        (*mtx)[3][3] = 1.0f;
    } else {
        (*mtx)[0][0] = 0.0f;
        (*mtx)[1][0] = 1.0f;
        (*mtx)[2][0] = 0.0f;
        (*mtx)[3][0] = 0.0f;

        (*mtx)[0][1] = 0.0f;
        (*mtx)[1][1] = 0.0f;
        (*mtx)[2][1] = 1.0f;
        (*mtx)[3][1] = 0.0f;

        (*mtx)[0][2] = 1.0f;
        (*mtx)[1][2] = 0.0f;
        (*mtx)[2][2] = 0.0f;
        (*mtx)[3][2] = 0.0f;

        (*mtx)[0][3] = 0.0f;
        (*mtx)[1][3] = 0.0f;
        (*mtx)[2][3] = 0.0f;
        (*mtx)[3][3] = 1.0f;
    }
}

/**
 * Clamps a float within a set range about zero.
 */
f32 gd_clamp_f32(f32 a, f32 b) {
    if (b < a) {
        a = b;
    } else if (a < -b) {
        a = -b;
    }

    return a;
}

/**
 * Clamps a vector within a set range about zero.
 */
void gd_clamp_vec3f(struct GdVec3f *vec, f32 limit) {
    if (vec->x > limit) {
        vec->x = limit;
    } else if (vec->x < -limit) {
        vec->x = -limit;
    }

    if (vec->y > limit) {
        vec->y = limit;
    } else if (vec->y < -limit) {
        vec->y = -limit;
    }

    if (vec->z > limit) {
        vec->z = limit;
    } else if (vec->z < -limit) {
        vec->z = -limit;
    }
}

/**
 * Rotates a 2D vector by some angle in degrees.
 */
void gd_rot_2d_vec(f32 deg, f32 *x, f32 *y) {
    f32 xP;
    f32 yP;
    f32 rad;

    rad = deg / DEG_PER_RAD;
    xP = (*x * cosf(rad)) - (*y * sinf(rad));
    yP = (*x * sinf(rad)) + (*y * cosf(rad));
    *x = xP;
    *y = yP;
}

/**
 * Rotates a mat4f matrix about a given axis
 * by a set angle in degrees.
 */
void gd_absrot_mat4(Mat4f *mtx, s32 axisnum, f32 ang) {
    Mat4f rMat;
    struct GdVec3f rot;

    switch (axisnum) {
        case GD_X_AXIS:
            rot.x = 1.0f;
            rot.y = 0.0f;
            rot.z = 0.0f;
            break;
        case GD_Y_AXIS:
            rot.x = 0.0f;
            rot.y = 1.0f;
            rot.z = 0.0f;
            break;
        case GD_Z_AXIS:
            rot.x = 0.0f;
            rot.y = 0.0f;
            rot.z = 1.0f;
            break;
    }

    gd_create_rot_mat_angular(&rMat, &rot, ang / 2.0f); //? 2.0f
    gd_mult_mat4f(mtx, &rMat, mtx);
}


f32 gd_vec3f_magnitude(struct GdVec3f *vec) {
    return (SQ(vec->x) + SQ(vec->y) + SQ(vec->z));
}

f32 gd_vec3f_magnitude_sqrtf(struct GdVec3f *vec) {
    return sqrtf(SQ(vec->x) + SQ(vec->y) + SQ(vec->z));
}

/**
 * Normalizes a vec3f to have a length of 1.
 */
s32 gd_normalize_vec3f(struct GdVec3f *vec) {
    f32 mag;
    if ((mag = SQ(vec->x) + SQ(vec->y) + SQ(vec->z)) == 0.0f) {
        return FALSE;
    }

    if (mag == 0.0f) {
        vec->x = 0.0f;
        vec->y = 0.0f;
        vec->z = 0.0f;
        return FALSE;
    }
    
    mag = 1.0f / sqrtf(mag);
    vec->x *= mag;
    vec->y *= mag;
    vec->z *= mag;

    return TRUE;
}

/**
 * Stores the cross product of 'a' x 'b' in 'dst'.
 */
void gd_cross_vec3f(struct GdVec3f *a, struct GdVec3f *b, struct GdVec3f *dst) {
    struct GdVec3f result;

    result.x = (a->y * b->z) - (a->z * b->y);
    result.y = (a->z * b->x) - (a->x * b->z);
    result.z = (a->x * b->y) - (a->y * b->x);

    dst->x = result.x;
    dst->y = result.y;
    dst->z = result.z;
}

/**
 * Returns the dot product of 'a' and 'b'.
 */
f32 gd_dot_vec3f(struct GdVec3f *a, struct GdVec3f *b) {
    return (a->x * b->x) + (a->y * b->y) + (a->z * b->z);
}

/**
 * Inverts a matrix from src and stores it into dst.
 * Reaches a fatal_print if the determinant is 0.
 */
void gd_inverse_mat4f(Mat4f *src, Mat4f *dst) {
    s32 i;
    s32 j;
    f32 determinant;

    gd_adjunct_mat4f(src, dst);
    determinant = gd_mat4f_det(dst);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            (*dst)[i][j] /= determinant;
        }
    }
}

/**
 * Takes a matrix from src and converts it into its adjunct in dst.
 */
void gd_adjunct_mat4f(Mat4f *src, Mat4f *dst) {
    struct InvMat4 inv;

    inv.r3.c3 = (*src)[0][0];
    inv.r2.c3 = (*src)[0][1];
    inv.r1.c3 = (*src)[0][2];
    inv.r0.c3 = (*src)[0][3];
    inv.r3.c2 = (*src)[1][0];
    inv.r2.c2 = (*src)[1][1];
    inv.r1.c2 = (*src)[1][2];
    inv.r0.c2 = (*src)[1][3];
    inv.r3.c1 = (*src)[2][0];
    inv.r2.c1 = (*src)[2][1];
    inv.r1.c1 = (*src)[2][2];
    inv.r0.c1 = (*src)[2][3];
    inv.r3.c0 = (*src)[3][0];
    inv.r2.c0 = (*src)[3][1];
    inv.r1.c0 = (*src)[3][2];
    inv.r0.c0 = (*src)[3][3];

    (*dst)[0][0] = gd_3x3_det(inv.r2.c2, inv.r2.c1, inv.r2.c0, inv.r1.c2, inv.r1.c1, inv.r1.c0,
                                 inv.r0.c2, inv.r0.c1, inv.r0.c0);
    (*dst)[1][0] = -gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0, inv.r1.c2, inv.r1.c1, inv.r1.c0,
                                  inv.r0.c2, inv.r0.c1, inv.r0.c0);
    (*dst)[2][0] = gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0, inv.r2.c2, inv.r2.c1, inv.r2.c0,
                                 inv.r0.c2, inv.r0.c1, inv.r0.c0);
    (*dst)[3][0] = -gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0, inv.r2.c2, inv.r2.c1, inv.r2.c0,
                                  inv.r1.c2, inv.r1.c1, inv.r1.c0);
    (*dst)[0][1] = -gd_3x3_det(inv.r2.c3, inv.r2.c1, inv.r2.c0, inv.r1.c3, inv.r1.c1, inv.r1.c0,
                                  inv.r0.c3, inv.r0.c1, inv.r0.c0);
    (*dst)[1][1] = gd_3x3_det(inv.r3.c3, inv.r3.c1, inv.r3.c0, inv.r1.c3, inv.r1.c1, inv.r1.c0,
                                 inv.r0.c3, inv.r0.c1, inv.r0.c0);
    (*dst)[2][1] = -gd_3x3_det(inv.r3.c3, inv.r3.c1, inv.r3.c0, inv.r2.c3, inv.r2.c1, inv.r2.c0,
                                  inv.r0.c3, inv.r0.c1, inv.r0.c0);
    (*dst)[3][1] = gd_3x3_det(inv.r3.c3, inv.r3.c1, inv.r3.c0, inv.r2.c3, inv.r2.c1, inv.r2.c0,
                                 inv.r1.c3, inv.r1.c1, inv.r1.c0);
    (*dst)[0][2] = gd_3x3_det(inv.r2.c3, inv.r2.c2, inv.r2.c0, inv.r1.c3, inv.r1.c2, inv.r1.c0,
                                 inv.r0.c3, inv.r0.c2, inv.r0.c0);
    (*dst)[1][2] = -gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c0, inv.r1.c3, inv.r1.c2, inv.r1.c0,
                                  inv.r0.c3, inv.r0.c2, inv.r0.c0);
    (*dst)[2][2] = gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c0, inv.r2.c3, inv.r2.c2, inv.r2.c0,
                                 inv.r0.c3, inv.r0.c2, inv.r0.c0);
    (*dst)[3][2] = -gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c0, inv.r2.c3, inv.r2.c2, inv.r2.c0,
                                  inv.r1.c3, inv.r1.c2, inv.r1.c0);
    (*dst)[0][3] = -gd_3x3_det(inv.r2.c3, inv.r2.c2, inv.r2.c1, inv.r1.c3, inv.r1.c2, inv.r1.c1,
                                  inv.r0.c3, inv.r0.c2, inv.r0.c1);
    (*dst)[1][3] = gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c1, inv.r1.c3, inv.r1.c2, inv.r1.c1,
                                 inv.r0.c3, inv.r0.c2, inv.r0.c1);
    (*dst)[2][3] = -gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c1, inv.r2.c3, inv.r2.c2, inv.r2.c1,
                                  inv.r0.c3, inv.r0.c2, inv.r0.c1);
    (*dst)[3][3] = gd_3x3_det(inv.r3.c3, inv.r3.c2, inv.r3.c1, inv.r2.c3, inv.r2.c2, inv.r2.c1,
                                 inv.r1.c3, inv.r1.c2, inv.r1.c1);
}

/**
 * Returns the determinant of a mat4f matrix.
 */
f32 gd_mat4f_det(Mat4f *mtx) {
    f32 det;
    struct InvMat4 inv;

    inv.r3.c3 = (*mtx)[0][0];
    inv.r2.c3 = (*mtx)[0][1];
    inv.r1.c3 = (*mtx)[0][2];
    inv.r0.c3 = (*mtx)[0][3];
    inv.r3.c2 = (*mtx)[1][0];
    inv.r2.c2 = (*mtx)[1][1];
    inv.r1.c2 = (*mtx)[1][2];
    inv.r0.c2 = (*mtx)[1][3];
    inv.r3.c1 = (*mtx)[2][0];
    inv.r2.c1 = (*mtx)[2][1];
    inv.r1.c1 = (*mtx)[2][2];
    inv.r0.c1 = (*mtx)[2][3];
    inv.r3.c0 = (*mtx)[3][0];
    inv.r2.c0 = (*mtx)[3][1];
    inv.r1.c0 = (*mtx)[3][2];
    inv.r0.c0 = (*mtx)[3][3];

    det = (inv.r3.c3
                * gd_3x3_det(inv.r2.c2, inv.r2.c1, inv.r2.c0,
                             inv.r1.c2, inv.r1.c1, inv.r1.c0,
                             inv.r0.c2, inv.r0.c1, inv.r0.c0)
           - inv.r2.c3
                * gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0,
                             inv.r1.c2, inv.r1.c1, inv.r1.c0,
                             inv.r0.c2, inv.r0.c1, inv.r0.c0))
          + inv.r1.c3
                * gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0,
                             inv.r2.c2, inv.r2.c1, inv.r2.c0,
                             inv.r0.c2, inv.r0.c1, inv.r0.c0)
          - inv.r0.c3
                * gd_3x3_det(inv.r3.c2, inv.r3.c1, inv.r3.c0,
                             inv.r2.c2, inv.r2.c1, inv.r2.c0,
                             inv.r1.c2, inv.r1.c1, inv.r1.c0);

    return det;
}

/**
 * Takes the individual values of a 3 by 3 matrix and
 * returns the determinant.
 */
f32 gd_3x3_det(f32 r0c0, f32 r0c1, f32 r0c2,
               f32 r1c0, f32 r1c1, f32 r1c2, 
               f32 r2c0, f32 r2c1, f32 r2c2) {
    f32 det;

    det = r0c0 * gd_2x2_det(r1c1, r1c2, r2c1, r2c2) - r1c0 * gd_2x2_det(r0c1, r0c2, r2c1, r2c2)
          + r2c0 * gd_2x2_det(r0c1, r0c2, r1c1, r1c2);

    return det;
}

/**
 * Takes the individual values of a 2 by 2 matrix and
 * returns the determinant.
 */
f32 gd_2x2_det(f32 a, f32 b, f32 c, f32 d) {
    f32 det = a * d - b * c;

    return det;
}

/**
 * Shifts a matrix up by one row, putting the top row on bottom.
 */
void gd_shift_mat_up(Mat4f *mtx) {
    s32 i;
    s32 j;
    f32 temp[3];

    for (i = 0; i < 3; i++) {
        temp[i] = (*mtx)[0][i + 1];
    }
    for (i = 1; i < 4; i++) {
        for (j = 1; j < 4; j++) {
            (*mtx)[i - 1][j - 1] = (*mtx)[i][j];
        }
    }

    (*mtx)[0][3] = 0.0f;
    (*mtx)[1][3] = 0.0f;
    (*mtx)[2][3] = 0.0f;
    (*mtx)[3][3] = 1.0f;

    for (i = 0; i < 3; i++) {
        (*mtx)[3][i] = temp[i];
    }

    //! The first row only ever has the first value set to 1, but the
    //! latter portions remain what they were originally. Perhaps this was meant
    //! to call gd_create_neg_vec_zero_first_mat_row?
    (*mtx)[0][0] = 1.0f;
    gd_shift_mat_up(mtx);
}

/**
 * Creates a rotation matrix to multiply the primary matrix by.
 * s/c are sin(angle)/cos(angle). That angular rotation is about vector
 * 'vec'.
 * 
 * Matrix has form-
 *
 * | (1-c)z^2+c (1-c)zy-sx (1-c)xz-sy 0 | 
 * | (1-c)zy-sx (1-c)y^2+c (1-c)xy-sz 0 |
 * | (1-c)xz-sy (1-c)xy-sz (1-c)x^2+c 0 |
 * |      0          0          0     1 |
 */
void gd_create_rot_matrix(Mat4f *mtx, struct GdVec3f *vec, f32 s, f32 c) {
    f32 oneMinusCos;
    struct GdVec3f rev;

    rev.z = vec->x;
    rev.y = vec->y;
    rev.x = vec->z;

    oneMinusCos = 1.0f - c;

    (*mtx)[0][0] = oneMinusCos * rev.z * rev.z + c;
    (*mtx)[0][1] = oneMinusCos * rev.z * rev.y + s * rev.x;
    (*mtx)[0][2] = oneMinusCos * rev.z * rev.x - s * rev.y;
    (*mtx)[0][3] = 0.0f;

    (*mtx)[1][0] = oneMinusCos * rev.z * rev.y - s * rev.x;
    (*mtx)[1][1] = oneMinusCos * rev.y * rev.y + c;
    (*mtx)[1][2] = oneMinusCos * rev.y * rev.x + s * rev.z;
    (*mtx)[1][3] = 0.0f;

    (*mtx)[2][0] = oneMinusCos * rev.z * rev.x + s * rev.y;
    (*mtx)[2][1] = oneMinusCos * rev.y * rev.x - s * rev.z;
    (*mtx)[2][2] = oneMinusCos * rev.x * rev.x + c;
    (*mtx)[2][3] = 0.0f;

    (*mtx)[3][0] = 0.0f;
    (*mtx)[3][1] = 0.0f;
    (*mtx)[3][2] = 0.0f;
    (*mtx)[3][3] = 1.0f;
}

/**
 * Creates a rotation matrix about vector 'vec' with ang in degrees.
 */
void gd_create_rot_mat_angular(Mat4f *mtx, struct GdVec3f *vec, f32 ang) { // 250us
    f32 s;
    f32 c;

    s = sinf(ang / (DEG_PER_RAD / 2.0f));
    c = cosf(ang / (DEG_PER_RAD / 2.0f));

    gd_create_rot_matrix(mtx, vec, s, c);
}

/**
 * Sets a mat4f matrix to an identity matrix.
 */
void gd_set_identity_mat4(Mat4f *mtx) {
    s32 i;
    f32 *dest;
    for (dest = (f32 *) (*mtx) + 1, i = 0; i < 14; dest++, i++) {
        *dest = 0;
    }
    for (dest = (f32 *) (*mtx), i = 0; i < 4; dest += 5, i++) {
        *((u32 *) dest) = 0x3F800000;
    }
}

struct CopyMe {
    f32 a[0x10];
};

/**
 * Copies a mat4f from src to dst.
 */
void gd_copy_mat4f(const Mat4f *src, Mat4f *dst) {
    *((struct CopyMe *) (*dst)) = *((struct CopyMe *) (*src));
}

/**
 * Transforms a vec3f, rotating with the main 3x3 portion of the mat4f
 * and translating with the 4th column.
 */
void gd_rotate_and_translate_vec3f(struct GdVec3f *vec, const Mat4f *mtx) {
    struct GdVec3f out;

    out.x = (*mtx)[0][0] * vec->x + (*mtx)[1][0] * vec->y + (*mtx)[2][0] * vec->z;
    out.y = (*mtx)[0][1] * vec->x + (*mtx)[1][1] * vec->y + (*mtx)[2][1] * vec->z;
    out.z = (*mtx)[0][2] * vec->x + (*mtx)[1][2] * vec->y + (*mtx)[2][2] * vec->z;
    out.x += (*mtx)[3][0];
    out.y += (*mtx)[3][1];
    out.z += (*mtx)[3][2];

    vec->x = out.x;
    vec->y = out.y;
    vec->z = out.z;
}

/**
 * Multiples a vec3f by the main 3x3 portion of a mat4f matrix.
 */
void gd_mat4f_mult_vec3f(struct GdVec3f *vec, const Mat4f *mtx) {
    struct GdVec3f out;

    out.x = (*mtx)[0][0] * vec->x + (*mtx)[1][0] * vec->y + (*mtx)[2][0] * vec->z;
    out.y = (*mtx)[0][1] * vec->x + (*mtx)[1][1] * vec->y + (*mtx)[2][1] * vec->z;
    out.z = (*mtx)[0][2] * vec->x + (*mtx)[1][2] * vec->y + (*mtx)[2][2] * vec->z;

    vec->x = out.x;
    vec->y = out.y;
    vec->z = out.z;
}

/**
 * Multiplies two Mat4f matrices and puts it in dst.
 */
void gd_mult_mat4f(const Mat4f *mA, const Mat4f *mB, Mat4f *dst) {
    f32 entry0, entry1, entry2;
    f32 *temp = (f32 *)*mA;
    f32 *temp2 = (f32 *)*dst;
    f32 *temp3;
    for (u8 i = 0; i < 16; i++) {
        entry0 = temp[0];
        entry1 = temp[1];
        entry2 = temp[2];
        temp3 = (f32 *)*mB;
        for (; (i & 3) !=3; i++) {
            *temp2 = (entry0 * temp3[0]) + (entry1 * temp3[4]) + (entry2 * temp3[8]);
            temp2++;
            temp3++;
        }
        *temp2 = 0;
        temp += 4;
        temp2++;
    }
    (*dst)[3][0] += (*mB)[3][0];
    (*dst)[3][1] += (*mB)[3][1];
    (*dst)[3][2] += (*mB)[3][2];
    ((u32 *) *dst)[15] = 0x3F800000;
}
