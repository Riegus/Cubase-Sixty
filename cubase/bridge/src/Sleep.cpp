#include "Sleep.h"

#include "Settings.h"
#include "SurfaceSleep.h"   // Rea-Sixty's clock and frames (extension/src)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace cs::sleep {
namespace {

surfsleep::Clock g_clock;
surfsleep::WakeSwallow g_swallow[3];
std::atomic<const char*> g_last{"start"};   // the input that counted last (for the log)

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool activity(bool arrives, const char* what) { g_last = what; return g_clock.wake(nowMs(), arrives); }

bool key(int surface, uint8_t id, bool pressed, const char* what)
{
    const bool woke = pressed && activity(true, what);
    if (!pressed) activity(false, what);
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
    // Once a minute while it is on and awake: how long idle, and what counted last (bridge.log).
    static int64_t reportAt = 0;
    if (g_clock.enabled.load() && !g_clock.asleep.load() && now >= reportAt) {
        reportAt = now + 60000;
        const int64_t last = g_clock.lastInputMs.load();
        std::printf("Sleep when idle: idle %lld s of %d min%s (last activity: %s)\n", last ? (long long)((now - last) / 1000) : 0LL,
                    g_clock.minutes.load(), busy ? ", transport running" : "", g_last.load());
    }
}

bool asleep() { return g_clock.asleep.load(); }

} // namespace cs::sleep
