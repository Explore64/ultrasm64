#include <PR/ultratypes.h>
#include <stdarg.h>

#include "debug_utils.h"
#include "gd_types.h"
#include "macros.h"
#include "renderer.h"
#include "draw_objects.h"

// types
struct UnkBufThing {
    /* 0x00 */ s32 size;
    /* 0x04 */ char name[0x40];
}; /* sizeof = 0x44 */

// data
static u32 sPrimarySeed = 0x12345678;   // @ 801A82A4
static u32 sSecondarySeed = 0x58374895; // @ 801A82A8

// bss
u8 *gGdStreamBuffer;                                        // @ 801BA190

/**
 * Returns a random floating point number between 0 and 1 (inclusive)
 * TODO: figure out type of rng generator?
 */
f32 gd_rand_float(void) {
    u32 temp;
    u32 i;
    f32 val;

    for (i = 0; i < 4; i++) {
        if (sPrimarySeed & 0x80000000) {
            sPrimarySeed = sPrimarySeed << 1 | 1;
        } else {
            sPrimarySeed <<= 1;
        }
    }
    sPrimarySeed += 4;

    /* Seed Switch */
    if ((sPrimarySeed ^= osGetCount()) & 1) {
        temp = sPrimarySeed;
        sPrimarySeed = sSecondarySeed;
        sSecondarySeed = temp;
    }

    val = (sPrimarySeed & 0xFFFF) / 65535.0f; // 65535.0f

    return val;
}

/**
 * Reimplementation of the standard "atoi" function
 */
s32 gd_atoi(const char *str) {
    char cur;
    s32 curval;
    s32 out = 0;
    s32 isNegative = FALSE;

    while (TRUE) {
        cur = *str++;

        if (cur == '-') {
            isNegative = TRUE;
        } else {
            curval = cur - '0';
            out += curval & 0xFF;

            if (*str == '\0' || *str == '.' || *str < '0' || *str > '9') {
                break;
            }

            out *= 10;
        }
    }

    if (isNegative) {
        out = -out;
    }

    return out;
}

static char sHexNumerals[] = {"0123456789ABCDEF"};

/* 23C018 -> 23C078; orig name: func_8018D848 */
char *format_number_hex(char *str, s32 val) {
    s32 shift;

    for (shift = 28; shift > -4; shift -= 4) {
        *str++ = sHexNumerals[(val >> shift) & 0xF];
    }

    *str = '\0';

    return str;
}

static s32 sPadNumPrint = 0; // @ 801A82C0

/* 23C078 -> 23C174; orig name: func_8018D8A8 */
/* padnum = a decimal number with the max desired output width */
char *format_number_decimal(char *str, s32 val, s32 padnum) {
    s32 i;

    if (val == 0) {
        *str++ = '0';
        *str = '\0';
        return str;
    }

    if (val < 0) {
        val = -val;
        *str++ = '-';
    }

    while (padnum > 0) {
        if (padnum <= val) {
            sPadNumPrint = TRUE;

            for (i = 0; i < 9; i++) {
                val -= padnum;
                if (val < 0) {
                    val += padnum;
                    break;
                }
            }

            *str++ = i + '0';
        } else {
            if (sPadNumPrint) {
                *str++ = '0';
            }
        }

        padnum /= 10;
    }

    *str = '\0';

    return str;
}

/* 23C174 -> 23C1C8; orig name: func_8018D9A4 */
static s32 int_sci_notation(s32 base, s32 significand) {
    s32 i;

    for (i = 1; i < significand; i++) {
        base *= 10;
    }

    return base;
}

/* 23C1C8 -> 23C468; orig name: func_8018D9F8 */
char *sprint_val_withspecifiers(char *str, union PrintVal val, char *specifiers) {
    s32 fracPart; // sp3C
    s32 intPart;  // sp38
    s32 intPrec;  // sp34
    s32 fracPrec; // sp30
    char cur; // sp2B

    fracPrec = 6;
    intPrec = 6;

    while ((cur = *specifiers++)) {
        if (cur == 'd') {
            sPadNumPrint = FALSE;
            str = format_number_decimal(str, val.i, 1000000000);
        } else if (cur == 'x') {
            sPadNumPrint = TRUE; /* doesn't affect hex printing, though... */
            str = format_number_hex(str, val.i);
        } else if (cur == 'f') {
            intPart = (s32) val.f;
            fracPart = (s32)((val.f - (f32) intPart) * (f32) int_sci_notation(10, fracPrec));
            sPadNumPrint = FALSE;
            str = format_number_decimal(str, intPart, int_sci_notation(10, intPrec));
            *str++ = '.';
            sPadNumPrint = TRUE;
            str = format_number_decimal(str, fracPart, int_sci_notation(10, fracPrec - 1));
        } else if (cur >= '0' && cur <= '9') {
            cur = cur - '0';
            intPrec = cur;
            if (*specifiers++) {
                fracPrec = (*specifiers++) - '0';
            }
        } else {
            gd_strcpy(str, "<BAD TYPE>");
            str += 10;
        }
    }

    return str;
}

/* 23C468 -> 23C4AC; orig name: func_8018DC98 */
void gd_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) {
        ;
    }
}

/* 23C52C -> 23C5A8; orig name: func_8018DD5C */
char *gd_strdup(const char *src) {
    char *dst; // sp24

    dst = gd_malloc_perm((gd_strlen(src) + 1) * sizeof(char));

    gd_strcpy(dst, src);

    return dst;
}

/* 23C5A8 -> 23C5FC; orig name: func_8018DDD8 */
u32 gd_strlen(const char *str) {
    u32 len = 0;

    while (*str++) {
        len++;
    }

    return len;
}

/* 23C5FC -> 23C680; orig name: func_8018DE2C */
char *gd_strcat(char *dst, const char *src) {
    while (*dst++) {
        ;
    }

    if (*src) {
        dst--;
        while ((*dst++ = *src++)) {
            ;
        }
    }

    return --dst;
}

/* 23C67C -> 23C728; orig name: func_8018DEB0 */
/* Returns a bool, not the position of the mismatch */
s32 gd_str_not_equal(const char *str1, const char *str2) {
    while (*str1 && *str2) {
        if (*str1++ != *str2++) {
            return TRUE;
        }
    }

    return *str1 != '\0' || *str2 != '\0';
}

/* 23C728 -> 23C7B8; orig name; func_8018DF58 */
s32 gd_str_contains(const char *str1, const char *str2) {
    const char *startsub = str2;

    while (*str1 && *str2) {
        if (*str1++ != *str2++) {
            str2 = startsub;
        }
    }

    return !*str2;
}

/* 23C7B8 -> 23C7DC; orig name: func_8018DFE8 */
s32 gd_feof(struct GdFile *f) {
    return f->flags & 0x4;
}

/* 23C7DC -> 23C7FC; orig name: func_8018E00C */
void gd_set_feof(struct GdFile *f) {
    f->flags |= 0x4;
}

/* 23C7FC -> 23CA0C */
struct GdFile *gd_fopen(const char *filename, const char *mode) {
    struct GdFile *f; // sp74
    char *loadedname; // sp70
    u32 i;            // sp6C
    struct UnkBufThing buf; // sp24
    u8 *bufbytes;           // sp20
    u8 *fileposptr;         // sp1C
    s32 filecsr;            // sp18

    filecsr = 0;

    while (TRUE) {
        bufbytes = (u8 *) &buf;
        for (i = 0; i < sizeof(struct UnkBufThing); i++) {
            *bufbytes++ = gGdStreamBuffer[filecsr++];
        }
        fileposptr = &gGdStreamBuffer[filecsr];
        filecsr += buf.size;

        loadedname = buf.name;

        if (buf.size == 0) {
            break;
        }
        if (!gd_str_not_equal(filename, loadedname)) {
            break;
        }
    }

    f = gd_malloc_perm(sizeof(struct GdFile));

    f->stream = (s8 *) fileposptr;
    f->size = buf.size;
    f->pos = f->flags = 0;
    if (gd_str_contains(mode, "w")) {
        f->flags |= 0x1;
    }
    if (gd_str_contains(mode, "b")) {
        f->flags |= 0x2;
    }

    return f;
}

/* 23CA0C -> 23CB38; orig name: func_8018E23C */
s32 gd_fread(s8 *buf, s32 bytes, UNUSED s32 count, struct GdFile *f) {
    s32 bytesToRead = bytes;
    s32 bytesread;

    if (f->pos + bytesToRead > f->size) {
        bytesToRead = f->size - f->pos;
    }

    if (bytesToRead == 0) {
        gd_set_feof(f);
        return -1;
    }

    bytesread = bytesToRead;
    while (bytesread--) {
        *buf++ = f->stream[f->pos++];
    }

    return bytesToRead;
}

/* 23CB38 -> 23CB54; orig name: func_8018E368 */
void gd_fclose(UNUSED struct GdFile *f) {
    return;
}

/* 23CB54 -> 23CB70; orig name: func_8018E384 */
u32 gd_get_file_size(struct GdFile *f) {
    return f->size;
}

/* 23CB70 -> 23CBA8; orig name: func_8018E3A0 */
s32 is_newline(char c) {
    return c == '\r' || c == '\n';
}

/* 23CBA8 -> 23CCF0; orig name: func_8018E3D8 */
s32 gd_fread_line(char *buf, u32 size, struct GdFile *f) {
    signed char c;
    u32 pos = 0;

    do {
        if (gd_fread(&c, 1, 1, f) == -1) {
            break;
        }
    } while (is_newline(c));

    while (!is_newline(c)) {
        if (c == -1) {
            break;
        }
        if (pos > size) {
            break;
        }
        buf[pos++] = c;
        if (gd_fread(&c, 1, 1, f) == -1) {
            break;
        }
    }
    buf[pos++] = '\0';

    return pos;
}
