#include "quad_decode.h"
#include "test_harness.h"

int main()
{
    // Clockwise detent: A falls to 0 while B already 0 -> +1.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 1, 0), 0);  // A high, B low (setup)
        CHECK_EQ(quad_decode(ah, bh, 0, 0), 1);  // A high->low, B low => CW
    }
    // Counter-clockwise detent: B falls to 0 while A already 0 -> -1.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 0, 1), 0);  // B high, A low (setup)
        CHECK_EQ(quad_decode(ah, bh, 0, 0), -1); // B high->low, A low => CCW
    }
    // No motion (levels stable) -> 0.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 1, 1), 0);
        CHECK_EQ(quad_decode(ah, bh, 1, 1), 0);
    }
    return test_summary("test_quad_encoder");
}
