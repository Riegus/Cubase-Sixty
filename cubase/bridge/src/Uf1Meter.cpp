//
// The UF1's METER view, Overview screen. Ported from Rea-Sixty (extension/src/main.cpp): the entry burst is
// uf1MeterScreenBurst_(0), the cycle is the Overview branch of uf1PaintMeter_, and the codecs (bar scales,
// goniometer image, dB text) are Rea-Sixty's own, included unchanged from Uf1MeterCodec.inc.
//
// The other screens (13.11.2026) are Rea-Sixty's too: the entry bursts (uf1MeterScreenBurst_ 1-3), the Analogue
// needles (VU, with the readout glide and the max needle), the RTA bars and the Loudness history with its chrome
// (uf1_loudness_chrome.h). What is not here (yet): the V-pot meter settings, FINE, PRESETS and Loudness PLAY, and
// the plug-in's own settings Rea-Sixty reads as REAPER parameters. Here their defaults: Digital Type Non-Linear,
// Analogue Mode VU, reference -18 dBFS, max needle 2 s, RTA scale 0 to -120 dB with no band selected, Loudness
// range -18..+9 with the target from the history stream.
//

#include "Uf1Meter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "UF1Protocol.h"
#include "LogPath.h"
#include "SslCoreImpersonator.h"
#include "SslMeterProtocol.h"
#include "uf1_loudness_chrome.h"

namespace {
// What the codec asks REAPER for: no replay file (a diagnostic), no trace switch.
const char* GetResourcePath() { return "<none>"; }
const char* GetExtState(const char*, const char*) { return ""; }
const bool g_uf1_dev = true;   // the codec only checks that a device exists
#include "Uf1MeterCodec.inc"
}

namespace cs::uf1meter {

const char* screenName(int screen)
{
    static const char* const k[4] = {"OVERVIEW", "ANALOGUE", "RTA", "LOUDNESS"};
    return k[std::clamp(screen, 0, 3)];
}

int screenCount() { return sslcore::meterProAvailable() ? 4 : 3; }

void sendEntry(::uf1::UF1Device& dev, int screen)
{
    // Byte for byte as Rea-Sixty sends them (uf1MeterScreenBurst_; cap75 / cap101 for the Overview, cap138 for the
    // Analogue, cap76 for the RTA, cap120 for the Loudness).
    struct F { uint16_t addr; std::vector<uint8_t> p; };
    static const std::vector<F> kScreens[4] = {
        {
            {0x0100, {0x04, 0x00}},                                          // layout: Meter, screen Overview
            {0x0101, {0x03}},
            {0x0102, {0x01}},
            {0x0104, {0x00, 'O', 'V', 'E', 'R', 'V', 'I', 'E', 'W'}},
            {0x0104, {0x01, 'R', 'E', 'S', 'E', 'T'}},
            {0x0104, {0x02, 'F', 'I', 'N', 'E'}},
            {0x0104, {0x03, 'P', 'R', 'E', 'S', 'E', 'T', 'S'}},
            {0x0110, {0x0f}},
            {0x010d, {0x0a, 0x0b, 0x0a, 0x0b}},
            {0x010e, {0x00, 'M', 'A', 'S', 'T', 'E', 'R'}},
            {0x010e, {0x01, 'T', 'r', 'u', 'e', 'P', 'k', 0, 0, 'O', 'n', 0, 0, 0, 0, 0, 0, 0, 0}},
            {0x010e, {0x02, 'N', 'o', 'n', '-', 'L', 'i', 'n', 'e', 'a', 'r'}},
            {0x010e, {0x03, 'F', 'a', 'd', 'e', 0, 0, 0, 0, '2', ' ', 's', 'e', 'c', 0, 0, 0, 0, 0}},
            {0x010f, {0, 0, 0, 0, 0, 0, 0, 0}},
            {0x011a, {0x07}},
            {0x011e, {0x10}},
            {0x0120, {0x00}},
            {0x0129, {0xff}},
            {0x011f, {0x00}},
            {0x0125, {0x00, 0x00}},
            {0x0126, {0x00, 0x00}},
            {0x0127, {0x00, 0x00}},
            {0x0128, {0x00}},
        },
        {   // Analogue: VU needles and readouts
            {0x0100, {0x04, 0x01}},
            {0x0101, {0x03}},
            {0x0102, {0x01}},
            {0x0104, {0x00, 'A', 'N', 'A', 'L', 'O', 'G', 'U', 'E'}},
            {0x0104, {0x01, 'R', 'E', 'S', 'E', 'T'}},
            {0x0104, {0x02, 'F', 'I', 'N', 'E'}},
            {0x0104, {0x03, 'P', 'R', 'E', 'S', 'E', 'T', 'S'}},
            {0x0110, {0x0f}},
            {0x010d, {0x0a, 0x0a, 0x0b, 0x0b}},
            {0x010e, {0x00}},
            {0x010e, {0x01, 'V', 'U'}},
            {0x010e, {0x02, '0', 'd', 'B', 'u', 0, 0, 0, 0, 0, '0', ' ', 'd', 'B', 'u', 0, 0, 0, 0, 0}},
            {0x010e, {0x03, 'R', 'e', 'f', 0, 0, 0, 0, 0, 0, '-', '1', '8', '.', '0', 'd', 'B', 0, 0, 0}},
            {0x011a, {0x02}},
            {0x011e, {0x10}},
            {0x0120, {0x00}},
            {0x0129, {0xff}},
            {0x011f, {0x00}},
        },
        {   // RTA: 31 bands
            {0x0100, {0x04, 0x02}},
            {0x0102, {0x01}},
            {0x0009, {0xff, 0xff, 0x00, 0x00}},
            {0x000a, {0x00, 0x00, 0x00, 0x00}},
            {0x0015, {0xff}},
            {0x0016, {0xff}},
            {0x0119, {0x00, 0x00, 0x00, 0x00, 0x0d, 0x0d, 0x7e, 0x7e, 0x00, 0x00, 0x00}},
            {0x0104, {0x00, 'R', 'T', 'A'}},
            {0x0104, {0x01, 'R', 'E', 'S', 'E', 'T'}},
            {0x0104, {0x02, 'F', 'I', 'N', 'E'}},
            {0x0104, {0x03, 'P', 'R', 'E', 'S', 'E', 'T', 'S'}},
            {0x010d, {0x0a, 0x0b, 0x0b, 0x0b}},
            {0x010e, {0x00, 'M', 'A', 'S', 'T', 'E', 'R'}},
            {0x010e, {0x01, 'S', 'l', 'F', 'r', 'e', 'q', 0, 0, 'N', 'o', 'n', 'e', 0, 0, 0, 0, 0, 0}},
            {0x010e, {0x02, 'S', 'c', 'l', 'T', 'o', 'p', 0, 0, '0', ' ', 'd', 'B', 0, 0, 0, 0, 0, 0}},
            {0x010e, {0x03, 'S', 'c', 'l', 'B', 't', 'm', 0, 0, '-', '1', '2', '0', ' ', 'd', 'B', 0, 0, 0}},
            {0x011a, {0x01}},
            {0x0124, {0x00}},
            {0x012a, {0x00, 0x78}},
        },
        {   // Loudness (Meter Pro): the history plot and four readouts. Its 0x0100 selector is 05.
            {0x0100, {0x04, 0x05}},
            {0x0102, {0x05}},
            {0x0009, {0xff, 0xff, 0x00, 0x00}},
            {0x000a, {0x00, 0x00, 0x00, 0x00}},
            {0x0015, {0xff}},
            {0x0016, {0xff}},
            {0x0119, {0x00, 0x00, 0x00, 0x00, 0x0d, 0x0d, 0x7e, 0x7e, 0x00, 0x00, 0x00}},
            {0x0104, {0x00, 'L', 'O', 'U', 'D', 'N', 'E', 'S', 'S'}},
            {0x0104, {0x01, 'R', 'E', 'S', 'E', 'T'}},
            {0x0104, {0x02, 'P', 'L', 'A', 'Y'}},
            {0x0104, {0x03, 'P', 'R', 'E', 'S', 'E', 'T', 'S'}},
            {0x010d, {0x0a, 0x0a, 0x0a, 0x0a}},
            {0x010e, {0x00, 'M', 'A', 'S', 'T', 'E', 'R'}},
            {0x010e, {0x01, 'D', 'A', 'W', ' ', 'S', 'y', 'n', 'c'}},
            {0x010e, {0x02, '3', '0', ' ', 's', 'e', 'c', 's'}},
            {0x010e, {0x03, 'S', 'c', 'r', 'o', 'l', 'l', ' ', 'T', 'i', 'm', 'e', 'l', 'i', 'n', 'e'}},
            {0x011a, {0x01}},
            {0x0123, {0x00}},
        },
    };
    for (const auto& f : kScreens[std::clamp(screen, 0, 3)]) dev.send(::uf1::buildScreen(f.addr, f.p));
}

std::string instanceLabel()
{
    if (sslcore::meterInstanceCount() <= 0) return "NO METER";
    std::string nm = sslcore::currentMeterName();
    if (nm.empty()) nm = "METER";
    for (char& c : nm) if (static_cast<unsigned char>(c) >= 0x80) c = '?';   // the UF1 draws one byte per glyph
    if (nm.size() > 9) nm.resize(9);
    return nm;
}

void buildOverview(uf1pace::Parts& parts, uint8_t lvL, uint8_t lvR, int trackNo, bool stopped, bool resetHolds)
{
    // The plug-in only computes what its selected view needs: on view 0 the Lissajous streams.
    sslcore::setView(0);
    sslcore::setAutoTrackIndex(trackNo);
    sslcore::setTransportStopped(stopped);
    if (resetHolds) sslcore::resetMeter(true, false);

    auto usable = [](float v) { return std::isfinite(v); };
    float dbL = -120.f, dbR = -120.f, rmsL = -120.f, rmsR = -120.f, holdL = -120.f, holdR = -120.f;
    bool haveRms = false, haveHold = false;
    {
        std::vector<float> cur, pk;
        if (sslcore::isRunning() && sslcore::getMeter(int(sslmeter::DataType::BarPeak), cur, pk) &&
            cur.size() >= 2 && (usable(cur[0]) || usable(cur[1]))) {
            dbL = usable(cur[0]) ? cur[0] : -120.f;
            dbR = usable(cur[1]) ? cur[1] : -120.f;
            if (pk.size() >= 2 && (usable(pk[0]) || usable(pk[1]))) {
                holdL = usable(pk[0]) ? pk[0] : -120.f;
                holdR = usable(pk[1]) ? pk[1] : -120.f;
                haveHold = true;
            }
            std::vector<float> rc, rp;
            if (sslcore::getMeter(int(sslmeter::DataType::BarRms), rc, rp) && rc.size() >= 2 &&
                (usable(rc[0]) || usable(rc[1]))) {
                rmsL = usable(rc[0]) ? rc[0] : -120.f;
                rmsR = usable(rc[1]) ? rc[1] : -120.f;
                haveRms = true;
            }
        }
    }
    const float peak = dbL > dbR ? dbL : dbR;
    const float rms = haveRms ? (rmsL > rmsR ? rmsL : rmsR) : -120.f;
    const auto now = std::chrono::steady_clock::now();
    static float sFallbackHold = -120.f;
    if (resetHolds) sFallbackHold = -120.f;
    if (peak > sFallbackHold) sFallbackHold = peak;
    const float holdMax = haveHold ? (holdL > holdR ? holdL : holdR) : sFallbackHold;

    // The numbers have their own data types (TextPeak / TextRms), which do not agree with the bars.
    auto textPair = [&](sslmeter::DataType dt, float& l, float& r, float& hold, bool& have) {
        std::vector<float> c, k;
        if (!sslcore::getMeter(int(dt), c, k) || c.size() < 2) return;
        if (!usable(c[0]) && !usable(c[1])) return;
        l = usable(c[0]) ? c[0] : -120.f;
        r = usable(c[1]) ? c[1] : -120.f;
        hold = -120.f;
        if (k.size() >= 2) {
            const float a = usable(k[0]) ? k[0] : -120.f, b = usable(k[1]) ? k[1] : -120.f;
            hold = a > b ? a : b;
        }
        have = true;
    };
    float tpL = -120.f, tpR = -120.f, tpHold = -120.f, trL = -120.f, trR = -120.f, trHold = -120.f;
    bool haveTp = false, haveTr = false;
    if (sslcore::isRunning()) {
        textPair(sslmeter::DataType::TextPeak, tpL, tpR, tpHold, haveTp);
        textPair(sslmeter::DataType::TextRms, trL, trR, trHold, haveTr);
    }
    if (!haveTp) { tpL = dbL; tpR = dbR; tpHold = holdMax; }
    if (!haveTr) { trL = rmsL; trR = rmsR; trHold = rms; haveTr = haveRms; }

    // 0x011c: six 25-byte fields - peak hold, peak L, peak R, RMS hold, RMS L, RMS R.
    const std::array<std::string, 6> fields = {
        uf1FormatMeterDb_(tpHold), uf1FormatMeterDb_(tpL), uf1FormatMeterDb_(tpR),
        haveTr ? uf1FormatMeterDb_(trHold) : std::string(),
        haveTr ? uf1FormatMeterDb_(trL) : std::string(),
        haveTr ? uf1FormatMeterDb_(trR) : std::string(),
    };
    std::vector<uint8_t> readouts;
    readouts.reserve(6 * 25);
    for (const auto& s : fields)
        for (size_t k = 0; k < 25; ++k) readouts.push_back(k < s.size() ? uint8_t(s[k]) : 0x00);

    // Bars: RMS, peak and the peak-hold marker (the plug-in's own hold when it sends one).
    static Uf1PeakHold sBarHold;
    static bool sHoldInit = false;
    if (!sHoldInit || resetHolds) { sHoldInit = true; sBarHold.reset(dbL, dbR, now); }
    sBarHold.step(dbL, dbR, now);
    const float barHoldL = haveHold ? holdL : sBarHold.l;
    const float barHoldR = haveHold ? holdR : sBarHold.r;
    const uint8_t* scale = kUf1BarScale;
    const std::array<uint8_t, 2> rmsBar{uf1BarByte_(haveRms ? rmsL : -120.f, scale), uf1BarByte_(haveRms ? rmsR : -120.f, scale)};
    const std::array<uint8_t, 2> peakBar{uf1BarByte_(dbL, scale), uf1BarByte_(dbR, scale)};
    const std::array<uint8_t, 2> holdBar{uf1BarByte_(barHoldL, scale), uf1BarByte_(barHoldR, scale)};

    // The goniometer: the plug-in's own Lissajous raster (its fade included). Blank once it stops coming.
    std::vector<float> liss, lisspk;
    uint64_t lseq = 0;
    const bool haveLiss = sslcore::getMeter(int(sslmeter::DataType::Lissajous), liss, lisspk, &lseq);
    parts.seq = lseq;
    static size_t sLissCells = 0;
    static long long sLissMs = 0;
    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (haveLiss && !liss.empty()) { sLissCells = liss.size(); sLissMs = nowMs; }
    if (haveLiss) uf1PaintGoniometer_(liss, parts.img);
    else if (sLissCells && nowMs - sLissMs > 400) uf1PaintGoniometer_(std::vector<float>(sLissCells, 0.0f), parts.img);

    const uint8_t m0009[4] = {lvL, lvR, 0x00, 0x00};
    const uint8_t m000a[4] = {0x00, 0x00, 0x00, 0x00};
    const uint8_t zero = 0x00;
    parts.meters.push_back(::uf1::buildScreen(0x0009, m0009));
    parts.meters.push_back(::uf1::buildScreen(0x000a, m000a));
    parts.meters.push_back(::uf1::buildScreen(0x0015, std::span<const uint8_t>(&zero, 1)));
    parts.meters.push_back(::uf1::buildScreen(0x0016, std::span<const uint8_t>(&zero, 1)));

    auto pair = [](uint16_t addr, const std::array<uint8_t, 2>& v) {
        return ::uf1::buildScreen(addr, std::span<const uint8_t>(v.data(), v.size()));
    };
    parts.tail.push_back(::uf1::buildScreen(::uf1::scr::kHeaderRow, readouts));   // 011c
    parts.tail.push_back(pair(0x0125, rmsBar));
    parts.tail.push_back(pair(0x0126, peakBar));
    parts.tail.push_back(pair(0x0127, holdBar));
    uint8_t mask = 0x00;   // overload: bit 0 / 2 = L / R now, bit 1 / 3 = L / R latched
    {
        std::vector<uint8_t> ovl, ovlHold;
        if (sslcore::getOverload(int(sslmeter::DataType::BarPeak), ovl, ovlHold) ||
            sslcore::getOverload(int(sslmeter::DataType::VuPpm), ovl, ovlHold)) {
            if (ovl.size() >= 2) { if (ovl[0]) mask |= 0x01; if (ovl[1]) mask |= 0x04; }
            if (ovlHold.size() >= 2) { if (ovlHold[0]) mask |= 0x02; if (ovlHold[1]) mask |= 0x08; }
        }
    }
    parts.tail.push_back(::uf1::buildScreen(0x0128, std::span<const uint8_t>(&mask, 1)));
    parts.tail.push_back(::uf1::buildScreen(0x011d, std::span<const uint8_t>(&zero, 1)));   // closes the Overview cycle
}

namespace {
// The Analogue needle's source: VuPpm (dt 0, the live needle) while the plug-in sends it, else TextVuPpm (dt 1,
// the readout with a 0.5 s hold; Meter Pro 1.3.7 and later send only that). Rea-Sixty's uf1NeedleDataType_.
int needleDataType()
{
    static int dt = int(sslmeter::DataType::VuPpm);
    static long long next = 0;
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now >= next) {
        next = now + 2000;
        std::vector<float> c, k;
        dt = sslcore::getMeter(int(sslmeter::DataType::VuPpm), c, k) && c.size() >= 2
                 ? int(sslmeter::DataType::VuPpm) : int(sslmeter::DataType::TextVuPpm);
    }
    return dt;
}

std::vector<uint8_t> fieldsBytes(const std::array<std::string, 6>& fields)
{
    std::vector<uint8_t> p;
    p.reserve(6 * 25);
    for (const auto& s : fields)
        for (size_t k = 0; k < 25; ++k) p.push_back(k < s.size() ? uint8_t(s[k]) : 0x00);
    return p;
}

// The RTA's readouts are the Overview's: peak hold, peak L / R, RMS hold, RMS L / R (TextPeak / TextRms, the
// bars where those do not come).
std::vector<uint8_t> peakRmsReadouts()
{
    auto usable = [](float v) { return std::isfinite(v); };
    auto pair = [&](sslmeter::DataType dt, float& l, float& r, float& hold) {
        std::vector<float> c, k;
        if (!sslcore::isRunning() || !sslcore::getMeter(int(dt), c, k) || c.size() < 2) return false;
        if (!usable(c[0]) && !usable(c[1])) return false;
        l = usable(c[0]) ? c[0] : -120.f;
        r = usable(c[1]) ? c[1] : -120.f;
        hold = l > r ? l : r;
        if (k.size() >= 2) {
            const float a = usable(k[0]) ? k[0] : -120.f, b = usable(k[1]) ? k[1] : -120.f;
            hold = a > b ? a : b;
        }
        return true;
    };
    float pL = -120.f, pR = -120.f, pH = -120.f, rL = -120.f, rR = -120.f, rH = -120.f;
    if (!pair(sslmeter::DataType::TextPeak, pL, pR, pH)) pair(sslmeter::DataType::BarPeak, pL, pR, pH);
    const bool haveR = pair(sslmeter::DataType::TextRms, rL, rR, rH) || pair(sslmeter::DataType::BarRms, rL, rR, rH);
    return fieldsBytes({uf1FormatMeterDb_(pH), uf1FormatMeterDb_(pL), uf1FormatMeterDb_(pR),
                        haveR ? uf1FormatMeterDb_(rH) : std::string(), haveR ? uf1FormatMeterDb_(rL) : std::string(),
                        haveR ? uf1FormatMeterDb_(rR) : std::string()});
}

// The Loudness readouts: four 25-byte fields, value (6) | unit (4) | caption (14) | style. Rea-Sixty's
// uf1BuildLoudnessReadouts_.
std::vector<uint8_t> loudnessReadouts()
{
    struct Slot { int dt; const char* unit; const char* caption; uint8_t style; };
    static const Slot kSlots[4] = {
        {int(sslmeter::DataType::LoudReadout1), "LUFS", "Integrated", 0x01},
        {int(sslmeter::DataType::LoudReadout2), "LUFS", "Short-Term", 0x01},
        {int(sslmeter::DataType::LoudReadout3), "dBFS", "True Peak Max", 0x02},
        {int(sslmeter::DataType::LoudReadout4), "LUFS", "Short-TermMax", 0x01},
    };
    std::vector<uint8_t> p;
    p.reserve(4 * 25);
    auto put = [&](const char* str, int width) {
        int n = 0;
        for (; str && str[n] && n < width; ++n) p.push_back(uint8_t(str[n]));
        for (; n < width; ++n) p.push_back(0x00);
    };
    for (const auto& s : kSlots) {
        std::string val;
        std::vector<float> cur, pk;
        if (sslcore::isRunning() && sslcore::getMeter(s.dt, cur, pk) && !cur.empty() && std::isfinite(cur[0]))
            val = uf1FormatMeterDb_(cur[0]);
        sslcore::MeterInfo mi;
        const bool told = sslcore::isRunning() && sslcore::getMeterInfo(s.dt, mi) && !mi.legend.empty();
        const char* unit = told && !mi.unit.empty() ? mi.unit.c_str() : s.unit;
        const char* caption = told ? mi.legend.c_str() : s.caption;
        const uint8_t style = std::strcmp(unit, "dBFS") == 0 ? 0x02 : s.style;
        put(val.c_str(), 6);
        put(unit, 4);
        put(caption, 14);
        p.push_back(style);
    }
    return p;
}

std::vector<uint8_t> frame(uint16_t addr, const uint8_t* d, size_t n)
{
    return ::uf1::buildScreen(addr, std::span<const uint8_t>(d, n));
}
} // namespace

void buildOther(int screen, uf1pace::Parts& parts, uint8_t lvL, uint8_t lvR, int trackNo, bool stopped,
                bool resetHolds, bool force)
{
    screen = std::clamp(screen, 1, 3);
    // The plug-in computes only what its selected view needs (view 2: the RTA, view 3: the loudness history).
    sslcore::setView(screen);
    sslcore::setAutoTrackIndex(trackNo);
    sslcore::setTransportStopped(stopped);
    if (resetHolds) sslcore::resetMeter(true, screen == 3);
    const auto now = std::chrono::steady_clock::now();
    auto& t = parts.tail;

    // 0x011c, the readouts, every cycle (SSL restates it every cycle too).
    std::vector<uint8_t> readouts;
    if (screen == 1) {
        std::vector<float> c, k;
        float l = -120.f, r = -120.f, hl = -120.f, hr = -120.f;
        bool have = false;
        if (sslcore::isRunning() && sslcore::getMeter(int(sslmeter::DataType::TextVuPpm), c, k) && c.size() >= 2 &&
            (std::isfinite(c[0]) || std::isfinite(c[1]))) {
            l = std::isfinite(c[0]) ? c[0] : -120.f;
            r = std::isfinite(c[1]) ? c[1] : -120.f;
            if (k.size() >= 2) { hl = std::isfinite(k[0]) ? k[0] : -120.f; hr = std::isfinite(k[1]) ? k[1] : -120.f; }
            have = true;
        }
        readouts = fieldsBytes({have ? uf1FormatMeterDb_(l) : std::string(), have ? uf1FormatMeterDb_(hl) : std::string(),
                                have ? uf1FormatMeterDb_(r) : std::string(), have ? uf1FormatMeterDb_(hr) : std::string(),
                                "L", "R"});
    } else if (screen == 2) {
        readouts = peakRmsReadouts();
    } else {
        readouts = loudnessReadouts();
    }
    t.push_back(::uf1::buildScreen(::uf1::scr::kHeaderRow, readouts));

    // The small display's meter: SSL's constant group on the Analogue screen (live bytes there turned its numbers
    // red, Rea-Sixty 2026-09-11), the track level on the others.
    {
        const uint8_t lv[4] = {screen == 1 ? uint8_t(0xff) : lvL, screen == 1 ? uint8_t(0xff) : lvR, 0x00, 0x00};
        const uint8_t z4[4] = {0, 0, 0, 0};
        const uint8_t gr = screen == 1 ? 0xff : 0x00;
        t.push_back(frame(0x0009, lv, 4));
        t.push_back(frame(0x000a, z4, 4));
        t.push_back(frame(0x0015, &gr, 1));
        t.push_back(frame(0x0016, &gr, 1));
    }

    if (screen == 1) {
        // Analogue, VU: the plug-in's own VU (already ballistic and referenced); with the readout (dt 1) its 0.5 s
        // hold is bridged and the needle never falls faster than the plug-in's meter (43 dB/s). The second needle
        // is the measured max-needle law (2 s hold, 72 dB/s release). Rea-Sixty's uf1PaintMeter_, screen 1.
        float vuL = -139.2f, vuR = -139.2f, vuHoldL = -120.f, vuHoldR = -120.f;
        bool haveVuHold = false, readout = false;
        float peak = -120.f;
        bool havePeak = false;
        {
            std::vector<float> c, k;
            if (sslcore::getMeter(int(sslmeter::DataType::BarPeak), c, k) && c.size() >= 2) {
                const float a = std::isfinite(c[0]) ? c[0] : -120.f, b = std::isfinite(c[1]) ? c[1] : -120.f;
                peak = a > b ? a : b;
                havePeak = true;
            }
        }
        {
            std::vector<float> cur, pk;
            const int dt = needleDataType();
            if (sslcore::isRunning() && sslcore::getMeter(dt, cur, pk) && cur.size() >= 2) {
                vuL = std::isfinite(cur[0]) ? cur[0] : -139.2f;
                vuR = std::isfinite(cur[1]) ? cur[1] : -139.2f;
                readout = dt == int(sslmeter::DataType::TextVuPpm);
                if (dt == int(sslmeter::DataType::VuPpm) && pk.size() >= 2 && (std::isfinite(pk[0]) || std::isfinite(pk[1]))) {
                    vuHoldL = std::isfinite(pk[0]) ? pk[0] : -120.f;
                    vuHoldR = std::isfinite(pk[1]) ? pk[1] : -120.f;
                    haveVuHold = true;
                }
            }
        }
        static Uf1MaxNeedle sNdl;
        static float sGlideL = -139.2f, sGlideR = -139.2f, sRawL = NAN, sRawR = NAN;
        static std::chrono::steady_clock::time_point sGlideT{}, sRawSinceL{}, sRawSinceR{};
        if (force) {
            sNdl.reset(vuL, vuR, now);
            sGlideL = vuL; sGlideR = vuR; sGlideT = now;
            sRawL = sRawR = NAN; sRawSinceL = sRawSinceR = now;
        }
        const bool silent = havePeak && peak <= -100.f;
        sNdl.step(vuL, vuR, now, kUf1MaxNeedleHoldSec, kUf1VuMaxNeedleReleaseDbPerS);
        if (readout) {
            const float tgtL = silent ? -139.2f : vuL, tgtR = silent ? -139.2f : vuR;
            const double dtS = std::chrono::duration<double>(now - sGlideT).count();
            sGlideT = now;
            const float drop = kUf1VuReadoutFallDbPerS * float(std::clamp(dtS, 0.0, 0.5));
            if (!(vuL == sRawL)) { sRawL = vuL; sRawSinceL = now; }
            if (!(vuR == sRawR)) { sRawR = vuR; sRawSinceR = now; }
            auto held = [&](std::chrono::steady_clock::time_point since) {
                const double frozen = std::chrono::duration<double>(now - since).count();
                return frozen >= kUf1VuHoldDetectS && frozen <= kUf1VuHoldMaxS;
            };
            sGlideL = std::max(held(sRawSinceL) ? -139.2f : tgtL, sGlideL - drop);
            sGlideR = std::max(held(sRawSinceR) ? -139.2f : tgtR, sGlideR - drop);
            vuL = sGlideL; vuR = sGlideR;
        }
        uint8_t ndl[2] = {uf1VuByte_(vuL), uf1VuByte_(vuR)};
        uint8_t hld[2] = {uf1VuByte_(haveVuHold ? vuHoldL : sNdl.l), uf1VuByte_(haveVuHold ? vuHoldR : sNdl.r)};
        if (silent && !readout) { ndl[0] = ndl[1] = 4; hld[0] = hld[1] = 4; }
        t.push_back(frame(0x0125, ndl, 2));
        t.push_back(frame(0x0127, hld, 2));
        // Overload marks (bit 0 / 2 = L / R now, 1 / 3 = latched): the plug-in's own, as on the Overview.
        uint8_t mask = 0;
        std::vector<uint8_t> ovl, ovlHold;
        if (sslcore::getOverload(needleDataType(), ovl, ovlHold) ||
            sslcore::getOverload(int(sslmeter::DataType::BarPeak), ovl, ovlHold)) {
            if (ovl.size() >= 2) { if (ovl[0]) mask |= 0x01; if (ovl[1]) mask |= 0x04; }
            if (ovlHold.size() >= 2) { if (ovlHold[0]) mask |= 0x02; if (ovlHold[1]) mask |= 0x08; }
        }
        t.push_back(frame(0x0128, &mask, 1));
    } else if (screen == 2) {
        // RTA: 31 bands, each its value and its hold (the plug-in's own, else a 26.5 dB/s fall), on the default
        // scale 0 dB to -120 dB. 0x0122 on a change, 0x0124 (the selected band: none) and 0x012a (the scale) when
        // the screen opens.
        constexpr float kTop = 0.f, kBot = -120.f;
        std::vector<float> cur, pk;
        const bool have = sslcore::isRunning() && sslcore::getMeter(int(sslmeter::DataType::Rta31Band), cur, pk) &&
                          cur.size() >= 31;
        static std::array<float, 31> sHold;
        static std::chrono::steady_clock::time_point sHoldT = now;
        if (force) sHold.fill(-120.f);
        const double dtR = std::chrono::duration<double>(now - sHoldT).count();
        sHoldT = now;
        std::array<uint8_t, 64> rta{};
        rta[0] = 0x00; rta[1] = 0x03;
        for (int i = 0; i < 31; ++i) {
            const float v = have ? cur[size_t(i)] : -120.f;
            float& h = sHold[size_t(i)];
            if (have && pk.size() >= 31) h = pk[size_t(i)];
            else if (v >= h) h = v;
            else { h = float(h - 26.5 * dtR); if (h < v) h = v; }
            rta[size_t(2 + 2 * i)] = uf1RtaByte_(v, kTop, kBot);
            rta[size_t(3 + 2 * i)] = uf1RtaByte_(h, kTop, kBot);
        }
        static std::array<uint8_t, 64> sRta{};
        if (force || rta != sRta) { sRta = rta; t.push_back(frame(0x0122, rta.data(), rta.size())); }
        if (force) {
            const uint8_t sel = 0;
            const uint8_t axis[2] = {uint8_t(-kTop), uint8_t(-kBot)};
            t.push_back(frame(0x0124, &sel, 1));
            t.push_back(frame(0x012a, axis, 2));
        }
    } else {
        // Loudness: the short-term history as one line over the last 313 columns, the target from the stream's
        // trailer (default -23 LUFS), scale range 0 (-18..+9 LU), absolute labels. Rea-Sixty's screen 3.
        std::vector<float> hist, hp;
        const bool haveHist = sslcore::isRunning() &&
                              sslcore::getMeter(int(sslmeter::DataType::LoudScrollableHistory), hist, hp) && hist.size() > 16;
        if (haveHist) {
            double target = -23.0;
            { const float tt = hist[hist.size() - 10]; if (tt > 0.f && tt < 100.f) target = -double(tt); }
            const double rb = -18.0, span = 27.0;
            const size_t histLen = hist.size() > 13 ? hist.size() - 13 : hist.size();
            size_t nowIdx = 0;
            for (size_t q = histLen; q-- > 0;) if (hist[q] > -99.f && hist[q] < 60.f) { nowIdx = q; break; }
            const double step = double(histLen) / 313.0;
            auto colByte = [&](int ageCols) -> uint8_t {
                const long ai = long(nowIdx) - long(std::llround(double(ageCols) * step));
                if (ai >= 0) {
                    const float lufs = hist[size_t(ai)];
                    if (std::isfinite(lufs) && lufs > -99.f && lufs < 60.f) {
                        const double y = (double(lufs) - target - rb) * 180.0 / span;
                        return uint8_t(std::clamp(int(std::llround(y)), 0, 180));
                    }
                }
                return 0;
            };
            std::array<uint8_t, 251> sf{};
            sf[0] = 0x01;
            for (int c = 0; c < 250; ++c) sf[size_t(1 + c)] = colByte(312 - c);
            std::array<uint8_t, 251> sf0{};
            for (int i = 0; i < 251; ++i) sf0[size_t(i)] = kLoudR0Sf0[i];
            static const int kTicksLU[8] = {9, 6, 3, 0, -3, -6, -12, -18};
            for (int k = 0; k < 8; ++k) {
                char lab[16];
                std::snprintf(lab, sizeof lab, "%ld", long(std::lround(double(kTicksLU[k]) + target)));
                for (int c = 0; c < 3; ++c) sf0[size_t(2 + 3 * k + c)] = lab[c] ? uint8_t(lab[c]) : 0;
            }
            auto valByte = [&](int dataType) -> uint8_t {
                std::vector<float> c, q;
                if (!sslcore::getMeter(dataType, c, q) || c.empty()) return 0;
                const float lufs = c[0];
                if (!std::isfinite(lufs) || lufs <= -99.f || lufs >= 60.f) return 0;
                const double y = (double(lufs) - target - rb) * 180.0 / span;
                return uint8_t(std::clamp(int(std::llround(y)), 0, 180));
            };
            sf0[26] = valByte(int(sslmeter::DataType::LoudMomentary));
            sf0[27] = valByte(int(sslmeter::DataType::LoudShortTerm));
            std::array<uint8_t, 251> sf2{};
            for (int i = 0; i < 251; ++i) sf2[size_t(i)] = kLoudR0Sf2[i];
            for (int c = 1; c <= 64; ++c) sf2[size_t(c)] = colByte(64 - c);
            static std::array<uint8_t, 251> sSf{}, sSf0{}, sSf2{};
            if (force || sf != sSf || sf0 != sSf0 || sf2 != sSf2) {
                sSf = sf; sSf0 = sf0; sSf2 = sf2;
                t.push_back(frame(0x0122, sf0.data(), sf0.size()));
                t.push_back(frame(0x0122, sf.data(), sf.size()));
                t.push_back(frame(0x0122, sf2.data(), sf2.size()));
                t.push_back(frame(0x0122, kLoudR0Sf3, 208));
            }
        }
    }
}

} // namespace cs::uf1meter
