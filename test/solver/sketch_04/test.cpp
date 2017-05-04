#include "harness.h"

TEST_CASE(sketch_04) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");

    CHANGE_DIMENSION("c005", 30.0);
    CHECK_RENDER_FIT("sketch_a_30.png");

    CHANGE_DIMENSION("c004", 40.0);
    CHECK_RENDER_FIT("sketch_b_40.png");

    CHANGE_DIMENSION("c006", 40.0);
    CHECK_RENDER_FIT("sketch_c_40.png");

    CHANGE_DIMENSION("c004", 50.0);
    CHECK_RENDER_FIT("sketch_b_50.png");
}
