//
// The UF1's METER view, Overview screen. Ported from Rea-Sixty (extension/src/main.cpp): the entry burst is
// uf1MeterScreenBurst_(0), the cycle is the Overview branch of uf1PaintMeter_, and the codecs (bar scales,
// goniometer image, dB text) are Rea-Sixty's own, included unchanged from Uf1MeterCodec.inc.
//
// What is not here (yet): the other screens (Analogue, RTA, Loudness), the V-pot meter settings, FINE and
// PRESETS, and the plug-in's Digital Type (Rea-Sixty reads it as a REAPER parameter; here the bars always use
// the default Non-Linear scale).
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

namespace {
// What the codec asks REAPER for: no replay file (a diagnostic), no trace switch.
const char* GetResourcePath() { return "<none>"; }
const char* GetExtState(const char*, const char*) { return ""; }
const bool g_uf1_dev = true;   // the codec only checks that a device exists
#include "Uf1MeterCodec.inc"
}

namespace cs::uf1meter {

void sendOverviewEntry(::uf1::UF1Device& dev)
{
    // Byte for byte as Rea-Sixty sends it (cap75 / cap101: SSL 360 entering the Overview from the channel view).
    struct F { uint16_t addr; std::vector<uint8_t> p; };
    static const std::vector<F> kEntry = {
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
    };
    for (const auto& f : kEntry) dev.send(::uf1::buildScreen(f.addr, f.p));
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

} // namespace cs::uf1meter
