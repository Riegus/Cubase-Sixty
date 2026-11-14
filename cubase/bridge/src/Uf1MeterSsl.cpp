// The UF1's METER view over SSL's own protocol, for a Meter Cubase cannot reach: see Uf1MeterSsl.h.
//
// MEASURED 14.11.2026 (Control Room Monitor 1): a `09 <double>` write on an object the Meter declared changes the
// setting, the plug-in's own window follows, and the plug-in answers with the new value. Enums go as their index
// (the reconstructed SSL 360 enums, analysis/ssl360-protobuf), numbers in their own units (-18.0 = -18.0 dBFS),
// switches as `08 2` on / `08 1` off (the shape SSL's own RESET commands use), unless the plug-in reports them as
// doubles. The declaration type 0x22 (LED overload, loudness target, alerts, history window, dialogue content)
// reports its own text, the number nested in field 3 and a unit (`0a "-23" 1a {09 -23.0} 22 "LUFS"`; Off = 999):
// shown with the plug-in's text, written in the same nested shape.

#include "Uf1MeterSsl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "SslCoreImpersonator.h"
#include "Uf1Meter.h"

namespace cs::uf1meterssl {

namespace {

enum Kind { kNone, kEnum, kSwitch, kNum, kText };   // kText: type 0x22, a number with the plug-in's own text
constexpr double kOff = 999.0;                        // what a type-0x22 object reports for "Off"

struct Spec {
    const char* wire = nullptr;
    Kind kind = kNone;
    const char* const* labels = nullptr;   // enum: one per index; switch: off, on
    int count = 0;
    double lo = 0, hi = 0, step = 0;       // numbers: range, step a notch
    int decimals = 0;
    const char* unit = "";
    bool fixedStep = false;                // FINE does not apply (a number with fixed steps)
    bool off = false;                      // kText: one step past hi is Off (999)
    const double* list = nullptr;          // kText: the values it steps through, instead of lo / hi / step
    int listCount = 0;
};

const char* const kOffOn[] = {"Off", "On"};
const char* const kDigitalPeakHold[] = {"Off", "1 sec", "3 sec", "6 sec", "Infinite"};
const char* const kDigitalType[] = {"Non-Linear", "Non-Linear 2x", "Linear", "Linear 2x", "K-20", "K-14", "K-12"};
const char* const kAnalogueMode[] = {"VU", "PPM"};
const char* const kMaxNeedle[] = {"Off", "2 sec", "Infinite"};
const char* const kLineUp[] = {"+4 dBu", "0 dBu", "-2 dBu"};
const char* const kDualFormat[] = {"Stereo", "Mid-Side", "Custom"};
const char* const kSource[] = {"L+R Sum", "Left", "Right", "Mid", "Side", "Surr Sum", "Height Sum", "All Sum",
                               "Chan 1", "Chan 2", "Chan 3", "Chan 4", "Chan 5", "Chan 6", "Chan 7", "Chan 8",
                               "Chan 9", "Chan 10", "Chan 11", "Chan 12"};
const char* const kFade[] = {"0 sec", "0.25 sec", "0.5 sec", "1 sec", "2 sec", "3 sec", "5 sec"};
const char* const kRtaPeakHold[] = {"Off", "0.5 sec", "1 sec", "2 sec", "3 sec", "10 sec", "30 sec", "Infinite"};
const char* const kWeighting[] = {"None", "A-Weighting", "C-Weighting"};
const char* const kAveraging[] = {"Real Time", "Fast", "Medium", "Slow", "Infinite"};
const char* const kScaleTop[] = {"0 dB", "-10 dB", "-20 dB", "-30 dB", "-40 dB", "-50 dB", "-60 dB", "-70 dB",
                                 "-80 dB", "-90 dB", "-100 dB", "-110 dB"};
const char* const kScaleBottom[] = {"-10 dB", "-20 dB", "-30 dB", "-40 dB", "-50 dB", "-60 dB", "-70 dB", "-80 dB",
                                    "-90 dB", "-100 dB", "-110 dB", "-120 dB"};
const char* const kOperation[] = {"Auto Pause", "Pause+Reset", "DAW Sync", "Clock Sync", "Continuous"};
const char* const kScroll[] = {"AutoScroll Off", "AutoScroll On"};
const char* const kGate[] = {"Gate Off", "Program -8", "Program -10"};
const char* const kLoudMode[] = {"ITU", "Leq(m)"};
const char* const kScaleRange[] = {"-18 to +9", "-36 to +18", "-54 to +27"};
const char* const kDisplay[] = {"Absolute", "Relative"};
const char* const kTerms[] = {"LU(FS)", "LK(FS)"};
const char* const kPlay[] = {"Pause", "Play"};
// The RTA's 31 ISO bands; the text is what the screen's band marker reads (Uf1Meter.cpp, row 51).
const char* const kBand[] = {"No Band", "20 Hz", "25 Hz", "31.5 Hz", "40 Hz", "50 Hz", "63 Hz", "80 Hz", "100 Hz",
                             "125 Hz", "160 Hz", "200 Hz", "250 Hz", "315 Hz", "400 Hz", "500 Hz", "630 Hz", "800 Hz",
                             "1.00 kHz", "1.25 kHz", "1.60 kHz", "2.00 kHz", "2.50 kHz", "3.15 kHz", "4.00 kHz",
                             "5.00 kHz", "6.30 kHz", "8.00 kHz", "10.0 kHz", "12.5 kHz", "16.0 kHz", "20.0 kHz"};

template <size_t N> Spec en(const char* w, const char* const (&l)[N]) { Spec s; s.wire = w; s.kind = kEnum; s.labels = l; s.count = int(N); return s; }
template <size_t N> Spec sw(const char* w, const char* const (&l)[N]) { Spec s; s.wire = w; s.kind = kSwitch; s.labels = l; s.count = int(N); return s; }
Spec num(const char* w, double lo, double hi, double step, int dec, const char* unit, bool fixed = false)
{
    Spec s; s.wire = w; s.kind = kNum; s.lo = lo; s.hi = hi; s.step = step; s.decimals = dec; s.unit = unit; s.fixedStep = fixed;
    return s;
}

Spec txt(const char* w, double lo, double hi, double step, int dec, const char* unit, bool off)
{
    Spec s = num(w, lo, hi, step, dec, unit, true);
    s.kind = kText;
    s.off = off;
    return s;
}
// The loudness history window, in seconds (0 = Auto Condense; the plug-in's own list is not known, these are tried).
const double kHistory[] = {0, 10, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 28800, 43200, 86400};
Spec history()
{
    Spec s = txt("LoudnessHistoryViewSize", 0, 86400, 1, 0, "s", false);
    s.list = kHistory;
    s.listCount = int(sizeof(kHistory) / sizeof(kHistory[0]));
    return s;
}

// By row: the script's METER_PARAMS order (the plug-in's parameter list).
const Spec& spec(int row)
{
    static const Spec kNoSpec;
    static const Spec kRows[53] = {
        Spec{},                                                              // 0 Bypass (not on a page)
        num("GlobalDelay", 0, 1000, 20, 1, "ms"),                            // 1
        sw("TruePeakHighQuality", kOffOn),                                   // 2
        en("DigitalMetersPeakHold", kDigitalPeakHold),                       // 3
        sw("DigitalMetersTruePeak", kOffOn),                                 // 4
        num("DigitalMetersRmsIntegrationTime", 10, 5000, 10, 1, "ms"),       // 5
        en("DigitalMetersType", kDigitalType),                               // 6
        Spec{},                                                              // 7 Channel Format (not on a page)
        en("AnalogueMetersMode", kAnalogueMode),                             // 8
        num("AnalogueMetersRefLevel", -36, 0, 1, 1, "dBFS"),                 // 9
        en("AnalogueMetersPeakHold", kMaxNeedle),                            // 10
        en("AnalogueMetersVUOffset", kLineUp),                               // 11
        en("AnalogueMetersDualFormat", kDualFormat),                         // 12
        en("AnalogueCustomMeterLeft", kSource),                              // 13
        en("AnalogueCustomMeterRight", kSource),                             // 14
        txt("AnalogueMetersLedOverload", 0, 24, 1, 0, "dB", true),           // 15 (type 0x22)
        en("LissajousFadeTime", kFade),                                      // 16
        sw("SumCompensation", kOffOn),                                       // 17
        en("RtaPeakHold", kRtaPeakHold),                                     // 18
        en("RtaSource", kSource),                                            // 19
        en("RtaWeighting", kWeighting),                                      // 20
        en("RtaAveraging", kAveraging),                                      // 21
        en("RtaScaleTop", kScaleTop),                                        // 22
        en("RtaScaleBottom", kScaleBottom),                                  // 23
        en("LoudnessOperation", kOperation),                                 // 24
        history(),                                                           // 25 (type 0x22)
        sw("LoudnessHistoryAutoScroll", kScroll),                            // 26
        sw("LoudnessTruePeakEnabled", kOffOn),                               // 27
        sw("LoudnessDialogueDetection", kOffOn),                             // 28
        txt("LoudnessMinDialogueContent", 0, 100, 2, 0, "%", false),         // 29 (type 0x22)
        en("LoudnessGateModeI", kGate),                                      // 30
        sw("LoudnessOverlapIEnabled", kOffOn),                               // 31
        num("LoudnessIntegrationTimeM", 10, 1000, 20, 1, "ms"),              // 32
        num("LoudnessIntegrationTimeS", 1, 60, 1, 1, "s"),                   // 33
        sw("LoudnessModeLeqm", kLoudMode),                                   // 34
        num("LoudnessSurroundWeighting", -1.5, 1.5, 0.5, 1, "dB", true),     // 35
        txt("LoudnessTargetI", -36, 0, 1, 0, "LUFS", false),                 // 36 (type 0x22)
        num("LoudnessTargetIVariance", 0, 20, 0.5, 1, "LU"),                 // 37
        sw("LoudnessAlertI", kOffOn),                                        // 38
        sw("LoudnessAlertDialogueI", kOffOn),                                // 39
        txt("LoudnessAlertS", -70, 30, 1, 1, "", true),                      // 40 (type 0x22)
        txt("LoudnessAlertM", -70, 30, 1, 1, "", true),                      // 41 (type 0x22)
        num("LoudnessAlertRangeMin", 0, 29, 0.5, 1, "LU"),                   // 42
        num("LoudnessAlertRangeMax", 1, 30, 0.5, 1, "LU"),                   // 43
        num("LoudnessAlertDialogueRangeMin", 0, 29, 0.5, 1, "LU"),           // 44
        num("LoudnessAlertDialogueRangeMax", 1, 30, 0.5, 1, "LU"),           // 45
        txt("LoudnessAlertTruePeak", -20, 0, 0.5, 1, "dBFS", true),          // 46 (type 0x22)
        en("LoudnessMeterScaleRange", kScaleRange),                          // 47
        sw("LoudnessMeterDisplayRelative", kDisplay),                        // 48
        sw("LoudnessTerminologyK", kTerms),                                  // 49
        sw("LoudnessPlay", kPlay),                                           // 50
        en("RtaSelectedBand", kBand),                                        // 51
        sw("SaveLoudnessHistory", kOffOn),                                   // 52
    };
    return row >= 0 && row < 53 ? kRows[row] : kNoSpec;
}

// The pages, as the script's METER_PAGES: {row, label, name only}.
struct Slot { int row; const char* label; bool nameOnly; };
constexpr Slot X{-1, "", false};
const Slot kOverview[3][3] = {{{4, "TruePk", false}, {6, "Type", true}, {16, "Fade", false}},
                              {X, {3, "PkHld", true}, {5, "Int", false}},
                              {{2, "TPkHQ", false}, {17, "SmCp", true}, {1, "Delay", false}}};
const Slot kAnalogue[3][3] = {{{8, "Mode", true}, {11, "0 VU", false}, {9, "Ref", false}},
                              {{12, "Format", true}, {13, "Cust L", false}, {14, "Cust R", false}},
                              {{10, "MaxN", true}, {15, "Overld", false}, X}};
const Slot kRta[3][3] = {{{51, "SlFreq", false}, {22, "SclTop", false}, {23, "SclBtm", false}},
                         {{18, "PkHld", false}, {20, "Wght", false}, {21, "Avg", false}},
                         {{19, "Source", true}, X, X}};
const Slot kLoudness[10][3] = {{{24, "PlayMd", true}, {25, "HistWin", true}, X},
                               {{47, "Range", true}, {48, "Disp", true}, {26, "Scroll", true}},
                               {{30, "Gate", true}, {36, "Target", true}, {28, "Dial", false}},
                               {{37, "Var", false}, {33, "Short", false}, {32, "Mom", false}},
                               {{29, "MinDia", false}, {34, "Mode", false}, {35, "Surr", false}},
                               {{27, "TrueP", false}, {49, "Term", true}, {31, "Ovrlp", false}},
                               {{38, "LdIntA", false}, {39, "IDAlrt", false}, {46, "TPMax", false}},
                               {{40, "Smax", false}, {42, "Rgmin", false}, {43, "Rgmax", false}},
                               {{41, "Mmax", false}, {44, "RDmin", false}, {45, "RDmax", false}},
                               {{52, "Save", false}, X, X}};

const Slot& slotOf(int screen, int page, int k)
{
    k = std::clamp(k, 0, 2);
    switch (std::clamp(screen, 0, 3)) {
        case 0: return kOverview[std::clamp(page, 0, 2)][k];
        case 1: return kAnalogue[std::clamp(page, 0, 2)][k];
        case 2: return kRta[std::clamp(page, 0, 2)][k];
        default: return kLoudness[std::clamp(page, 0, 9)][k];
    }
}

struct Value { bool ok = false; double v = 0; int index = 0; bool on = false; bool varint = true; std::string text, unit; };

Value read(const Spec& s)
{
    Value r;
    sslcore::ObjValue o;
    if (!s.wire || !sslcore::meterObjGet(s.wire, o) || !o.declared) return r;
    r.ok = true;
    r.v = o.num;
    r.text = o.text;
    r.unit = o.unit;
    if (s.kind == kEnum) r.index = std::clamp(int(std::lround(o.num)), 0, s.count - 1);
    if (s.kind == kSwitch) {
        // A switch reported as a whole number is 1 off / 2 on (as SSL's RESET commands); as a double, 0 / 1. Not
        // reported yet, or at its default (no field): off, written as a whole number.
        r.varint = o.form == 0 || o.form == 2;
        r.on = o.form == 2 ? o.num >= 2 : o.form != 0 && o.num >= 0.5;
    }
    return r;
}

std::string format(const Spec& s, const Value& v)
{
    if (!v.ok) return {};
    switch (s.kind) {
        case kEnum: return s.labels[v.index];
        case kSwitch: return s.labels[v.on ? 1 : 0];
        case kText:
            if (!v.text.empty()) return v.unit.empty() ? v.text : v.text + " " + v.unit;   // the plug-in's own
            if (s.off && v.v >= kOff - 0.5) return "Off";
            [[fallthrough]];
        case kNum: {
            char b[32];
            std::snprintf(b, sizeof(b), "%.*f%s%s", s.decimals, v.v, *s.unit ? " " : "", s.unit);
            return b;
        }
        default: return {};
    }
}

double norm(const Spec& s, const Value& v)
{
    switch (s.kind) {
        case kEnum: return s.count > 1 ? double(v.index) / double(s.count - 1) : 0.0;
        case kSwitch: return v.on ? 1.0 : 0.0;
        case kNum:
        case kText: return s.hi > s.lo ? std::clamp((v.v - s.lo) / (s.hi - s.lo), 0.0, 1.0) : 0.0;
        default: return 0.0;
    }
}

void write(const Spec& s, const Value& cur, int notches, bool fine)
{
    if (!cur.ok || !notches) return;
    double nv = 0;
    int form = 0;
    switch (s.kind) {
        case kEnum: {
            const int i = std::clamp(cur.index + notches, 0, s.count - 1);
            if (i == cur.index) return;
            nv = i;
            break;
        }
        case kSwitch: {
            const bool on = notches > 0;
            if (on == cur.on) return;
            if (cur.varint) { form = 1; nv = on ? 2 : 1; } else nv = on ? 1 : 0;
            break;
        }
        case kNum: {
            const double step = s.step * (fine && !s.fixedStep ? 0.25 : 1.0);
            nv = std::clamp(cur.v + notches * step, s.lo, s.hi);
            nv = std::round(nv / step) * step;   // on the step grid, so FINE lands back on whole steps
            nv = std::clamp(nv, s.lo, s.hi);
            if (std::fabs(nv - cur.v) < 1e-9) return;
            break;
        }
        case kText: {
            form = 2;
            if (s.list) {
                // The nearest value in the list, then one a notch.
                int i = 0;
                for (int k = 1; k < s.listCount; ++k) if (std::fabs(s.list[k] - cur.v) < std::fabs(s.list[i] - cur.v)) i = k;
                const int j = std::clamp(i + notches, 0, s.listCount - 1);
                if (j == i && std::fabs(s.list[i] - cur.v) < 1e-9) return;
                nv = s.list[j];
                break;
            }
            const bool isOff = s.off && cur.v >= kOff - 0.5;
            const double step = s.step;   // the plug-in's own steps (whole dB / LU): no FINE
            (void)fine;
            if (isOff) {
                if (notches > 0) return;
                nv = s.hi + (notches + 1) * step;   // the first notch down from Off is the top of the range
            } else {
                nv = cur.v + notches * step;
                if (s.off && nv > s.hi + 1e-9) nv = kOff;
            }
            if (nv != kOff) nv = std::clamp(std::round(nv / step) * step, s.lo, s.hi);
            if (std::fabs(nv - cur.v) < 1e-9) return;
            break;
        }
        default: return;
    }
    sslcore::meterObjWrite(s.wire, nv, form);
}

} // namespace

bool available() { return sslcore::meterObjAvailable(); }

void cells(int screen, int page, Cell out[3])
{
    for (int k = 0; k < 3; ++k) {
        out[k] = Cell{};
        const Slot& sl = slotOf(screen, page, k);
        const Spec& s = spec(sl.row);
        if (sl.row < 0 || !s.wire) continue;
        const Value v = read(s);
        if (!v.ok) continue;   // not on this Meter (the plain one has no loudness)
        out[k].nameOnly = sl.nameOnly;
        out[k].label = sl.label;
        out[k].value = format(s, v);
    }
}

void turn(int screen, int page, int slot, int notches, bool fine)
{
    const Slot& sl = slotOf(screen, page, slot);
    const Spec& s = spec(sl.row);
    if (sl.row < 0 || !s.wire) return;
    write(s, read(s), notches, fine);
}

void togglePlay()
{
    const Spec& s = spec(50);
    const Value v = read(s);
    write(s, v, v.on ? -1 : 1, false);
}

bool playing()
{
    const Value v = read(spec(50));
    return v.ok && v.on;
}

void publishSettings()
{
    static const int kRowsDrawn[] = {6, 8, 9, 10, 22, 23, 36, 47, 48, 50, 51};
    for (int row : kRowsDrawn) {
        const Spec& s = spec(row);
        const Value v = read(s);
        if (v.ok) uf1meter::setSetting(row, norm(s, v), format(s, v));
    }
}

} // namespace cs::uf1meterssl
