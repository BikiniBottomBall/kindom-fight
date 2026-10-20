#include "common/rng.h"
#include "test_framework.h"

using landgrab::Rng;

TEST(RngSameSeedSameSequence) {
    Rng a(42);
    Rng b(42);
    for (int i = 0; i < 100; ++i) {
        CHECK(a.next_u64() == b.next_u64());
    }
}

TEST(RngDifferentSeedDiffers) {
    Rng a(1);
    Rng b(2);
    bool differs = false;
    for (int i = 0; i < 8; ++i) {
        if (a.next_u64() != b.next_u64()) {
            differs = true;
        }
    }
    CHECK(differs);
}

TEST(RngNextIntStaysInRange) {
    Rng r(7);
    for (int i = 0; i < 5000; ++i) {
        const int value = r.next_int(3, 9);
        CHECK(value >= 3);
        CHECK(value <= 9);
    }
}

TEST(RngNextIntCoversWholeRange) {
    Rng r(11);
    int seen = 0;
    for (int i = 0; i < 2000; ++i) {
        seen |= 1 << (r.next_int(0, 3));
    }
    CHECK_EQ(seen, 0b1111);
}

TEST(RngJitterFollowsPercentBounds) {
    Rng r(13);
    for (int i = 0; i < 1000; ++i) {
        const int value = r.jitter(100, 90, 110);
        CHECK(value >= 90);
        CHECK(value <= 110);
    }
}
