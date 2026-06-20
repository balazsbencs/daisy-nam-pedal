#ifndef HOST_BUILD
#include "QuadEncoder.h"
#include "daisy_seed.h"

namespace pedal {

void QuadEncoder::Init(daisy::Pin pin_a, daisy::Pin pin_b)
{
    daisy::GPIO::Config cfg;
    cfg.mode = daisy::GPIO::Mode::INPUT;
    cfg.pull = daisy::GPIO::Pull::PULLUP;
    cfg.pin  = pin_a;
    gpio_a_.Init(cfg);
    cfg.pin  = pin_b;
    gpio_b_.Init(cfg);
    ah_ = 0xFFu;
    bh_ = 0xFFu;
}

int8_t QuadEncoder::Poll()
{
    uint8_t a = gpio_a_.Read() ? 1u : 0u;
    uint8_t b = gpio_b_.Read() ? 1u : 0u;
    return quad_decode(ah_, bh_, a, b);
}

} // namespace pedal
#endif // HOST_BUILD
