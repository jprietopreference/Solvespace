#include "harness.h"

TEST_CASE(sketch_07) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("0sketch.png");

    CHANGE_DIMENSION("c031", 60.0);
    CHECK_RENDER_FIT("1sketch_a_60.png");

    CHANGE_DIMENSION("c031", 30.0);
    CHECK_RENDER_FIT("2sketch_a_30.png");

    // 0 must not to be a problem here
    CHANGE_DIMENSION("c031", 1.0);
    CHECK_RENDER_FIT("3sketch_a_1.png");

    CHANGE_DIMENSION("c031", 90.0);
    CHECK_RENDER_FIT("4sketch_a_90.png");

    // 180 must not to be a problem here
    CHANGE_DIMENSION("c031", 179.0);
    CHECK_RENDER_FIT("5sketch_a_179.png");
}
