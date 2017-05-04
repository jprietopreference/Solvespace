#include "harness.h"

TEST_CASE(sketch_05) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("sketch.png");

    CHANGE_DIMENSION("c008", 60.0);
    CHECK_RENDER_FIT("sketch_a_60.png");

    CHANGE_DIMENSION("c007", 60.0);
    CHECK_RENDER_FIT("sketch_b_60.png");

    // need to set as small as it should to be
    CHANGE_DIMENSION("c008", 9.0);
    CHECK_RENDER_FIT("sketch_a_9.png");

    // need to set as small as it should to be
    CHANGE_DIMENSION("c007", 9.0);
    CHECK_RENDER_FIT("sketch_b_9.png");

    CHANGE_DIMENSION("c00a", 1000.0);
    CHECK_RENDER_FIT("sketch_l_1000.png");

    // uncomment when better solver
    //CHANGE_DIMENSION("c008", 89.0);
    //CHECK_RENDER_FIT("sketch_a_89.png");

    //CHANGE_DIMENSION("c007", 89.0);
    //CHECK_RENDER_FIT("sketch_b_89.png");

    CHANGE_DIMENSION("c00a", 1.0);
    CHECK_RENDER_FIT("sketch_l_1.png");
}
