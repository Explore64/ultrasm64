#include "stdarg.h"
#include "PR/os.h"
#include "sm64.h"

void __osSyncVPrintf(UNUSED const char *fmt, UNUSED va_list args) {
    // these functions intentionally left blank.  ifdeffed out in rom release
}

void osSyncPrintf(UNUSED const char *fmt, ...) {
    UNUSED int ans;
    UNUSED va_list ap;
    // these functions intentionally left blank.  ifdeffed out in rom release
}

void rmonPrintf(UNUSED const char *fmt, ...) {
    UNUSED int ans;
    UNUSED va_list ap;
}
