#include "harness.h"

TEST_CASE(sketch_01) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");

    CHANGE_DIMENSION("c04e", 100.0);
    CHECK_RENDER_FIT("sketch_a_100.png");

    CHANGE_DIMENSION("c047", 160.0);
    CHECK_RENDER_FIT("sketch_r_80.png");

    CHANGE_DIMENSION("c011", 56.0);
    CHECK_RENDER_FIT("sketch_d_56.png");

    CHANGE_DIMENSION("c047", 400.0);
    CHECK_RENDER_FIT("sketch_r_200.png");

    CHANGE_DIMENSION("c011", 150.0);
    CHECK_RENDER_FIT("sketch_d_150.png");

    CHANGE_DIMENSION("c00f", 100.0);
    CHECK_RENDER_FIT("sketch_w_100.png");

    CHANGE_DIMENSION("c017", 200.0);
    CHECK_RENDER_FIT("sketch_l_200.png");

    CHANGE_DIMENSION("c04e", 30.0);
    CHECK_RENDER_FIT("sketch_a_30.png");
}
