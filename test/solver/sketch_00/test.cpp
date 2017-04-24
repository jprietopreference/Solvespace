#include "harness.h"

TEST_CASE(sketch_00) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");
    CHANGE_DIMENSION("c00d", 10.0);
    CHECK_RENDER_FIT("sketch_r_10.png");
    CHANGE_DIMENSION("c00e", 7.0);
    CHECK_RENDER_FIT("sketch_d_7.png");
    CHANGE_DIMENSION("c00e", 70.0);
    CHECK_RENDER_FIT("sketch_d_70.png");
    CHANGE_DIMENSION("c00d", 1000.0);
    CHECK_RENDER_FIT("sketch_r_1000.png");
}
