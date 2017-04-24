#include "harness.h"

TEST_CASE(sketch_01) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");

    CHANGE_DIMENSION("c01d", 16.0);
    CHECK_RENDER_FIT("sketch_d_16.png");

    CHANGE_DIMENSION("c01d", 64.0);
    CHECK_RENDER_FIT("sketch_d_64.png");

    CHANGE_DIMENSION("c022", 120.0);
    CHECK_RENDER_FIT("sketch_a_120.png");

    CHANGE_DIMENSION("c022", 30.0);
    CHECK_RENDER_FIT("sketch_a_30.png");

    //CHANGE_DIMENSION("c01c", 5.0);
    //CHECK_RENDER_FIT("sketch_h_5.png");

    //CHANGE_DIMENSION("c01c", 20.0);
    //CHECK_RENDER_FIT("sketch_h_20.png");

    CHANGE_DIMENSION("c025", 1.0);
    CHECK_RENDER_FIT("sketch_w_1.png");

    CHANGE_DIMENSION("c025", 20.0);
    CHECK_RENDER_FIT("sketch_w_20.png");
}
