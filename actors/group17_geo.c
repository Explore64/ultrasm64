#include <ultra64.h>
#include "sm64.h"
#include "geo_commands.h"
#include "level_commands.h"

#include "make_const_nonconst.h"

#include "common1.h"
#include "group17.h"
#include "game/farcall_helpers.h"

#include "mr_i_eyeball/geo.inc.c"
#include "mr_i_iris/geo.inc.c"
#include "swoop/geo.inc.c"
#include "snufit/geo.inc.c"
#include "dorrie/geo.inc.c"
#include "scuttlebug/geo.inc.c"

#include "game/behaviors/mr_i.inc.c"
#include "game/behaviors/swoop.inc.c"
#include "game/behaviors/snufit.inc.c"
#include "game/behaviors/dorrie.inc.c"
#include "game/behaviors/scuttlebug.inc.c" // :scuttleeyes:

const LevelScript script_func_global_18[] = {
    LOAD_MODEL_FROM_GEO(MODEL_SWOOP,                   swoop_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SCUTTLEBUG,              scuttlebug_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MR_I_IRIS,               mr_i_iris_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MR_I,                    mr_i_geo),
    LOAD_MODEL_FROM_GEO(MODEL_DORRIE,                  dorrie_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SNUFIT,                  snufit_geo),
    RETURN(),
};
