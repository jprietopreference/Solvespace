#include "harness.h"

TEST_CASE(sketch_06) {
    CHECK_LOAD("sketch.slvs");
    CHECK_RENDER_FIT("0sketch.png");

    CHANGE_DIMENSION("c022", 90.0);
    CHECK_RENDER_FIT("1sketch_a1_90.png");

    CHANGE_DIMENSION("c01f", 60.0);
    CHECK_RENDER_FIT("2sketch_a0_60.png");

    CHANGE_DIMENSION("c04a", 60.0);
    CHECK_RENDER_FIT("3sketch_a2_60.png");

    CHANGE_DIMENSION("c022", 100.0);
    CHECK_RENDER_FIT("4sketch_a1_100.png");

    CHANGE_DIMENSION("c04a", 20.0);
    CHECK_RENDER_FIT("5sketch_a2_20.png");

    CHANGE_DIMENSION("c049", 90.0);
    CHECK_RENDER_FIT("6sketch_a3_90.png");

    CHANGE_DIMENSION("c01f", 50.0);
    CHECK_RENDER_FIT("7sketch_a0_50.png");

    CHANGE_DIMENSION("c04a", 31.144);
    CHECK_RENDER_FIT("8sketch_a2_31_144.png");

    CHANGE_DIMENSION("c049", 71.974);
    CHECK_RENDER_FIT("9sketch_a3_71_974.png");
}
