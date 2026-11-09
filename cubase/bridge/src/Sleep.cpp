#include "Sleep.h"

#include "Settings.h"
#include "SurfaceSleep.h"   // Rea-Sixty's clock and frames (extension/src)

#include <chrono>
#include <cstdlib>
#include <string>

namespace cs::sleep {
namespace {

surfsleep::Clock g_clock;
surfsleep::WakeSwallow g_swallow[3];

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool activity(bool arrives) { return g_clock.wake(nowMs(), arrives); }

bool key(int surface, uint8_t id, bool pressed)
{
    const bool woke = pressed && activity(true);
    if (!pressed) activity(false);
    return g_swallow[surface < 0 || surface > 2 ? 0 : surface].button(id, pressed, woke);
}

void tick(bool busy, const std::function<void(bool)>& push)
{
    // The setting (off / minutes), looked at again when the Settings page changed something or every 2 s.
    static unsigned gen = ~0u;
    static int64_t lookedAt = 0;
    const int64_t now = nowMs();
    if (gen != settings::generation() || now - lookedAt > 2000) {
        gen = settings::generation();
        lookedAt = now;
        const std::string v = settings::get("sleep", "off");
        const int minutes = v == "off" ? 0 : std::atoi(v.c_str());
        g_clock.enabled = minutes > 0;
        if (minutes > 0) g_clock.minutes = minutes;
        if (minutes <= 0 && g_clock.asleep.load()) g_clock.set(false, now, push);   // switched off while dark
    }
    g_clock.tick(now, busy, push);
}

bool asleep() { return g_clock.asleep.load(); }

} // namespace cs::sleep
