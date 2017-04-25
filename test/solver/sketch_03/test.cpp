#include "harness.h"

TEST_CASE(sketch_03) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");

    //CHANGE_DIMENSION("c00d", 15.0);
    //CHECK_RENDER_FIT("sketch_d_15.png");
}
