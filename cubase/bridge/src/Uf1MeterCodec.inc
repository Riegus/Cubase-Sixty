// Rea-Sixty's UF1 meter codecs (extension/src/main.cpp, the "Meter graphic codecs" block), copied
// unchanged. Included by Uf1Meter.cpp inside an anonymous namespace.

// ---- Meter graphic codecs (decoded 2026-07-14 from level-sweep captures) ----
// Each screen paints its own graphic elements from the plugin's real meter
// floats. The scales below are byte-exact lookups measured from SSL 360 driving
// the UF1 (captures cap88–cap94; see captures/cap8*_*.md and memory
// uf1-meter-codec-decoded). We DRIVE these from impersonator protobuf values,
// never from DSP we compute (Frank's hard rule).

// Overview bargraph scale: dBFS -> bar byte. index = round(dBFS) + 93, covering
// -93..0 dBFS. Each DIGITAL TYPE (param 6) is its OWN scale — the K-System and
// Linear/2x variants map the SAME true dBFS to a DIFFERENT bar byte than
// Non-Linear (proven on the wire: at -20 dBFS the rms byte is Non-Linear 108,
// K-20 109, K-12 89; cap110/111/113). With one fixed table the bars mis-align
// under every non-Non-Linear mode ("garbage", Frank 2026-07-24).
//
// cap105 (which claimed the bytes are scale-independent) is a NULL capture: its
// 0x0125/0126/0127 are payload 0x0000, distinct=1 for the whole file — the Types
// were stepped over SILENCE, so it proves nothing about the dBFS->byte curve.
//
// Tables below rebuilt from the RMS bar (0x0125) paired with the RMS readout
// (0x011c), top ~-8..0 region filled from the PEAK bar (0x0126) filtered to a
// RISING readout only (instant attack, no hold-lag); rms & peak share one per-type
// scale (agree +/-3 in overlap). K readouts are the K-scale value; dBFS = readout
// - 20/14/12 (K-offset wire-verified: ramps top at ~0 dBFS true). This replaces
// the reverted 4cc9177 tables, which were built from the peak-HOLD bar and went
// FLAT in the program range. Method validated: Non-Linear rebuilt from cap110
// matches this kUf1BarScale within +/-2 across -55..0 (55/56 bins, 98%).
// Sources: cap110/118/116/117/111/119/113. See scratchpad meter-audit truth doc.
constexpr uint8_t kUf1BarScale[94] = {       // Digital Type 0: Non-Linear (cap89 deep tail; KEEP)
      9,  10,  10,  11,  11,  12,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,
     16,  16,  16,  16,  17,  17,  17,  18,  18,  18,  19,  19,  19,  20,  20,  20,
     21,  21,  22,  24,  25,  27,  28,  30,  31,  33,  34,  35,  36,  38,  39,  40,
     42,  43,  44,  46,  47,  48,  51,  54,  57,  60,  63,  66,  69,  72,  75,  77,
     80,  83,  86,  89,  92,  95,  98, 101, 104, 107, 111, 115, 119, 123, 126, 130,
    134, 138, 142, 147, 151, 156, 162, 167, 172, 178, 183, 188, 194, 199,
};
constexpr uint8_t kUf1BarNonLin2x[94] = {    // Digital Type 1: Non-Linear 2x (cap118)
      0,   4,   8,   8,   8,   8,   8,   9,   9,   9,  10,  10,  10,  11,  11,  11,
     11,  12,  12,  12,  12,  12,  12,  13,  13,  13,  13,  13,  13,  14,  14,  14,
     14,  14,  15,  16,  16,  16,  16,  17,  17,  17,  17,  18,  18,  18,  18,  18,
     18,  18,  19,  19,  20,  20,  20,  20,  20,  20,  21,  21,  21,  21,  22,  22,
     24,  28,  30,  32,  35,  38,  40,  43,  46,  49,  55,  60,  66,  72,  78,  83,
     89,  96, 101, 106, 116, 121, 126, 135, 145, 154, 167, 175, 182, 199,
};
constexpr uint8_t kUf1BarLinear[94] = {      // Digital Type 2: Linear (cap116)
     28,  29,  30,  31,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  42,  43,
     44,  45,  46,  47,  48,  49,  51,  52,  53,  54,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  62,  63,  64,  65,  66,  68,  69,  70,  71,  72,  73,  74,  75,
     76,  77,  78,  79,  79,  80,  81,  82,  84,  85,  86,  87,  87,  88,  89,  90,
     92,  93,  95,  96, 100, 103, 108, 112, 114, 120, 124, 128, 132, 136, 140, 144,
    148, 152, 156, 159, 164, 166, 170, 176, 181, 182, 188, 191, 193, 199,
};
constexpr uint8_t kUf1BarLinear2x[94] = {    // Digital Type 3: Linear 2x (cap117)
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
      2,   4,   6,   8,  11,  15,  19,  24,  32,  39,  47,  56,  64,  72,  79,  88,
     96, 104, 113, 118, 128, 134, 140, 150, 159, 169, 176, 183, 190, 199,
};
constexpr uint8_t kUf1BarK20[94] = {         // Digital Type 4: K-20 (cap111)
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   2,   4,   6,  14,  17,  22,  26,  32,  36,  40,  45,  49,  54,  59,  63,
     68,  73,  76,  82,  86,  91,  96, 101, 106, 109, 114, 117, 122, 127, 132, 132,
    142, 146, 149, 154, 158, 162, 168, 172, 178, 178, 186, 186, 191, 199,
};
constexpr uint8_t kUf1BarK14[94] = {         // Digital Type 5: K-14 (cap119)
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   4,   8,  14,  20,  26,  32,  35,  44,
     47,  52,  59,  64,  68,  74,  79,  84,  88,  93, 100, 101, 106, 110, 115, 119,
    124, 132, 142, 146, 151, 158, 158, 166, 173, 177, 185, 187, 190, 199,
};
constexpr uint8_t kUf1BarK12[94] = {         // Digital Type 6: K-12 (cap113)
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   4,  12,  12,  20,  26,  33,
     37,  46,  50,  57,  62,  68,  72,  77,  84,  88,  95,  99, 105, 109, 116, 124,
    127, 133, 139, 145, 150, 155, 157, 168, 169, 176, 182, 185, 190, 199,
};

// The bar scale for a Digital Type param value (param 6, enum 0..6 in the order
// above). Out-of-range -> Non-Linear.
inline const uint8_t* uf1BarScaleFor_(int digitalType)
{
    switch (digitalType) {
        case 1:  return kUf1BarNonLin2x;
        case 2:  return kUf1BarLinear;
        case 3:  return kUf1BarLinear2x;
        case 4:  return kUf1BarK20;
        case 5:  return kUf1BarK14;
        case 6:  return kUf1BarK12;
        default: return kUf1BarScale;        // 0 = Non-Linear
    }
}

// dBFS -> Overview bar byte for the given scale table (0 = empty below the floor).
uint8_t uf1BarByte_(float dbfs, const uint8_t* tbl)
{
    if (!(dbfs > -93.f)) return 0;          // includes -inf/NaN
    int idx = static_cast<int>(std::lround(dbfs)) + 93;
    if (idx < 0)  idx = 0;
    if (idx > 93) idx = 93;                  // pins at 0 dBFS -> top
    return tbl[idx];
}

// Analogue VU faceplate scale: VU units -> needle byte (cap88). index = VU + 20,
// covering -20..+3 VU. Curved dial (a table, not a formula), pins at 4/180.
constexpr uint8_t kUf1VuScale[24] = {
    4, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 32,
    39, 45, 55, 65, 77, 89, 104, 119, 134, 149, 164, 179,
};

// VU units -> needle byte. Clamps to the printed -20..+3 VU range.
//
// INTERPOLATES between table entries. The table is sampled at whole VU, so
// rounding to the nearest entry gives the needle exactly 24 positions across the
// whole faceplate and it visibly steps between them — Frank, 2026-07-15: "im
// plugin sind die nadeln viel flüssiger". That was quantisation, not ballistics:
// the plug-in hands us a smooth ~300 ms-ballistic value (t0/VuPpm) and we were
// throwing the fraction away. The dial is curved (5 counts/VU at the bottom, 15
// at the top), so interpolate on the byte, not on VU.
uint8_t uf1VuByte_(float vu)
{
    if (!std::isfinite(vu) || vu <= -20.f) return 4;
    if (vu >= 3.f) return 180;
    const float f = vu + 20.f;                    // 0 .. 23
    int i = static_cast<int>(std::floor(f));
    if (i < 0)  i = 0;
    if (i > 22) i = 22;                           // i+1 must stay in range
    const float frac = f - float(i);
    const float a = float(kUf1VuScale[i]);
    const float b = float(kUf1VuScale[i + 1]);
    const float v = a + (b - a) * frac;
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 180.f)));
}

// SSL's peak-hold law, MEASURED from cap89 (analysis/uf1_hold_law.py: 28 snaps
// on the falling channel, median 3.019 s, 26 of 27 intervals inside 2.90-3.10 s;
// the lone 5.96 s is exactly 2x the period, a reset that moved nothing).
//
// The marker does NOT decay. It keeps the running maximum and SNAPS to the
// current value every 3 s. Two independent proofs in the capture:
//   - at t=104.27 the signal stops: 0x0126 collapses to (0,0) while 0x0127 sits
//     at (38,134) completely unmoved to the end of the capture;
//   - mid-sweep 0x0127 sits ABOVE 0x0126, then lands exactly on it 3.0 s later.
//
// This replaces a 26.5 dB/s decay — REAPER's PeakInfo rate (see memory
// reaper-peakinfo-decay-rate), right for a falling peak METER, wrong for a hold
// marker. At 30 Hz it shed only 0.87 dB per tick, so the marker tracked the peak
// and the two elements went out byte-identical 206 of 261 times: there was no
// second line on screen to see. Frank: "bei peak grafik keine peak-lines die
// fallen" (2026-07-14).
constexpr double kUf1HoldResetSec = 3.0;

// How fast the plug-in's own VU value falls, in dB per second — MEASURED, and
// the only number the Analogue VU needle is allowed to move down by since Meter
// Pro 1.3.7 replaced the live VuPpm stream with the TextVuPpm READOUT.
//
// Two sources, same numbers. (1) Our probe run 2026-09-11 (all-types [ndl]
// trace, VU faceplate, analysis/gen_uf1_needle_wav.py, three tone stops):
// eleven continuous falls, 43.3 dB/s over 139 steps on the long ones, every
// hard stop inside 42.2–43.5, 46 dB/s only below -56 VU; linear in dB all the
// way to the -139.2 floor. (2) SSL 360's own localhost capture 2026-09-10: the
// Meter Pro instance streaming to 360 on the Analogue view falls -4.40 → -55.18
// in 2.42 s on decaying music — the audio's own tail, slower than this, never
// faster. What this constant is NOT: a VU norm, a time constant, a guess. It is
// the ceiling the plug-in itself never exceeds.
constexpr float kUf1VuReadoutFallDbPerS = 43.f;

// ---- Goniometer: plug-in raster (t10) -> UF1 diamond (0x0122) --------------
//
// BOTH SIDES ARE DIAMONDS, at different resolutions. Resampling one onto the
// other is all that is left; the codecs are already decoded.
//
//   UF1    (8ecaec5, cap89/cap92): 187 rows, widths 2,1,1,2,3,…,92,92,…,3,2,1,1,
//          sum 8560, one byte per pixel = intensity.
//   plug-in (measured 2026-07-15 from cap97, the corr +1→0→−1 sweep):
//          99 rows, widths 2,4,6,…,100,…,6,4,2, sum EXACTLY 5000, one float per
//          pixel, 0..1.
//
// The proof is the row starts. At high correlation the lit float indices are
// 0, 2, 6, 12, 20, 30, 42, 56, 72, 90 … which are byte-for-byte the row starts
// of that diamond (52 of 99 rows lit in a single frame, identical across frames
// — geometry, not signal). n = 2·50² is not a coincidence: 50 is the half-height.
//
// SOLVED 2026-07-15 (cap99). The plug-in's Lissajous raster is a DIAMOND of ODD
// row widths — 1, 3, 5, …, 185, …, 5, 3, 1: **185 rows, sum 93² + 92² = 17113**,
// one float per pixel, 0..1 in 100 quantised levels (the "Fade 2 sec" trail).
//
// Derive the row count from n; never hardcode 17113 (SSL sizes the raster to its
// view, and a second instance may pick another size):
//   R odd  (= 2k-1) -> n = 2k² - 2k + 1  <=>  2n-1 = (2k-1)² and R = 2k-1
//   R even (= 2k)   -> n = 2k²                (peak row doubled)
// 17113: 2n-1 = 34225 = 185², so R = 185, k = 93. Exact.
//
// ⚠ THE PROOF IS PHYSICS ON THE COMPLETE ARRAY, and it is not a near-fit —
// it is exact (probe WAV, sections whose shape is known by construction):
//   corr +1 (mono, L=R)  -> 185 lit cells, touching ALL 185 rows, every one at
//                           frac 0.500 across its row, min = max = 0.500.
//                           A perfect vertical line down the centre column.
//   corr -1 (L=-R)       -> 185 lit cells in exactly ONE row, frac 0.0..1.0.
//                           The widest row, fully lit = a horizontal line.
//   L only / R only      -> 93 cells over 93 rows = one diamond edge.
// No lit index ever falls outside the diamond.
//
// ⚠ The array arrives CHUNKED (f7/f8/f9) and SslCoreImpersonator reassembles it,
// so src.size() here is the whole 17113 — NOT a chunk. Everything below depends
// on that: every geometry fitted before the reassembler existed was fitted to a
// chunk boundary and was wrong, including my own "odd diamond" for n=2113/5000.
// 5000 is the CHUNK SIZE and 2113 the remainder (17113 - 3*5000); neither was
// ever an array. If this ever paints garbage again, check that src.size() is a
// whole image before touching the geometry.
struct Uf1GonioGeom {
    std::vector<int> w;          // row widths, in floats
    std::vector<int> start;      // row start offsets
    bool ok = false;
};
inline Uf1GonioGeom uf1GonioGeomFor_(size_t n)
{
    Uf1GonioGeom g;
    const long long N = static_cast<long long>(n);
    int rows = 0;

    // Odd row count: 2n - 1 = (2k-1)², R = 2k-1. This is what 17113 is.
    {
        const long long s = std::llround(std::sqrt(double(2 * N - 1)));
        if (s >= 3 && (s % 2) == 1 && s * s == 2 * N - 1) rows = int(s);
    }
    // Even row count: n = 2k², peak row doubled.
    if (rows == 0 && N % 2 == 0) {
        const long long k = std::llround(std::sqrt(double(N) / 2.0));
        if (k >= 2 && 2 * k * k == N) rows = int(2 * k);
    }
    if (rows < 3) return g;                                 // unknown -> paint nothing

    for (int i = 0; i < rows; ++i) {
        const int m = (i < rows - 1 - i) ? i : rows - 1 - i;   // no std::min — MSVC macro trap
        g.w.push_back(2 * m + 1);
    }

    int s = 0;
    for (int x : g.w) { g.start.push_back(s); s += x; }
    g.ok = (size_t(s) == n);                                // table must span n exactly
    return g;
}

// The UF1's own diamond — DERIVED 2026-07-18 from cap92, whose input is known by
// construction (a correlation sweep: at corr=+1 the goniometer MUST be a vertical
// line, at corr=-1 a horizontal one). Nothing here is fitted to our own decoder.
//
// The derivation, so it can be repeated rather than believed:
//   1. corr=+1 lights ONE byte per row. Those bytes are the row CENTRES, not the
//      row starts — proven independently by the corr=-1 frame, whose horizontal
//      line is a contiguous run of 93 bytes at offset 4235, while the centre
//      measured on that same row is 4281 = 4235 + 46.
//   2. Centre-to-centre distance g_r = (w_r + w_r+1)/2, so w_r+1 = 2*g_r - w_r.
//      Anchored on that one measured row (width 93) the recursion closes:
//      185 rows, every width odd, PERFECTLY symmetric, ending exactly on 8560.
//   3. It only closes if the two stray lit bytes at offsets 0 and 2 are dropped
//      first — they are lit in the corr=-1 frame too, OUTSIDE the line, i.e. they
//      belong to the balance/correlation widgets SSL renders into the same buffer
//      (cap92-93 md). Dropping one or three instead leaves 46 asymmetric rows.
//
// So the buffer is:  bytes 0..2 = other widgets,  bytes 3..8559 = the diamond,
// 185 rows of 1,1,3,3,...,91,91,93,91,91,...,3,3,1,1 (sum 8557), ONE BYTE per
// pixel. Independent check that the table cannot fake: under it, the cloud's
// axis ratio tracks sqrt((1-rho)/(1+rho)) across rho = +0.75..-0.5 to a constant
// factor 0.45 — the 93-wide/185-tall aspect. Six values the table knows nothing
// about.
//
// ⚠ The previous table (2,1,1,2,3,4,...,92,92,...,1,1 over 187 rows from offset
// 0) summed to 8560 as well, which is why it survived: an exact sum proves
// NOTHING here. It put every row boundary in the wrong place.
constexpr int kUf1GonioRows   = 185;
constexpr int kUf1GonioOffset = 3;     // bytes 0..2 are not the diamond

inline const std::vector<int>& uf1DiamondWidths_()
{
    static const std::vector<int> w = [] {
        std::vector<int> v;
        for (int k = 1; k <= 91; k += 2) { v.push_back(k); v.push_back(k); }
        v.push_back(93);                                    // peak, single row
        for (int k = 91; k >= 1; k -= 2) { v.push_back(k); v.push_back(k); }
        return v;
    }();
    return w;
}
constexpr int kUf1DiamondBytes = 8560;
constexpr int kUf1DiamondRows  = kUf1GonioRows;

// Resample the plug-in's diamond onto the UF1's and paint it to 0x0122.
//
// Both are diamonds, so the map is per-row proportional: UF1 row i covers the
// same fraction of the face as plug-in row j = i·(rows_p-1)/(rows_u-1), and
// within a row the same again for the column. Nearest-neighbour, because the
// source is already an intensity raster with its own antialiasing — smoothing it
// again would only smear the trace.
//
// Chunking (8ecaec5): byte[0] = chunk index 0..34, then 250 payload bytes; the
// last chunk carries 60. 35 chunks = 8560.
//
// ⚠ SEND THE CHUNKS DESCENDING, 34 FIRST AND 0 LAST. This is not cosmetic and it
// is why the face stayed black through two sessions: cap89 has 2570 complete
// images and every single one goes 34,33,…,1,0 — the 61-byte tail leads, chunk 0
// closes. Zero images ascend. The device evidently latches on chunk 0, so
// sending 0 first published an empty buffer and the 34 chunks that followed were
// never drawn: "correct frames that the device ignores". The earlier check
// compared payload sizes and the 34:1 ratio, called that byte-identical, and
// never looked at the order.
// Chunk one complete 8560-byte diamond image into the 35 0x0122 frames
// (34 -> 0, exactly as cap89/cap101) and append them to `burst`.
void uf1AppendImageChunks_(const uint8_t* img,
                           std::vector<std::vector<uint8_t>>& burst)
{
    burst.reserve(burst.size() + 35);
    for (int c = 34; c >= 0; --c) {
        const int len = (c == 34) ? 60 : 250;
        std::vector<uint8_t> pay;
        pay.reserve(size_t(len) + 1);
        pay.push_back(uint8_t(c));
        pay.insert(pay.end(), img + c * 250, img + c * 250 + len);
        burst.push_back(uf1::buildScreen(0x0122, pay));
    }
}

// DIAGNOSTIC replay (2026-07-17 StoerPC session): if
// <resource>/rea_sixty/gonio_replay.bin exists (N x 8560 bytes, raw images
// lifted from cap101 = SSL's own transmitted goniometer), the Overview cycle
// streams THOSE images instead of our rendered ones — same cadence, same
// framing, same trailer. Discriminates "our image CONTENT is refused" from
// "the image element is gated by something outside the image data": every
// byte-level property of these images is literally SSL's own.
// Enabled by file presence; delete the file to return to live rendering.
bool uf1GonioReplayNext_(std::vector<std::vector<uint8_t>>& burst)
{
    static std::vector<uint8_t> blob;
    static bool tried = false;
    static size_t idx = 0;
    if (!tried) {
        tried = true;
        std::string p = std::string(GetResourcePath()) + "/rea_sixty/gonio_replay.bin";
        if (FILE* f = std::fopen(p.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n >= 8560) {
                blob.resize(size_t(n) - size_t(n) % 8560);
                if (std::fread(blob.data(), 1, blob.size(), f) != blob.size())
                    blob.clear();
            }
            std::fclose(f);
        }
        // Say so in the log: with the replay engaged uf1PaintGoniometer_ never
        // runs, so its content trace goes silent — which reads exactly like a
        // dead render path if you don't know which mode you're in.
        if (FILE* g = std::fopen(uf8::logPath("reasixty_gonio.log").c_str(), "a"))
        { std::fprintf(g, "replay: %s (%zu images from %s)\n",
                       blob.empty() ? "OFF" : "ON", blob.size() / 8560, p.c_str());
          std::fclose(g); }
    }
    if (blob.empty()) return false;
    const size_t nImg = blob.size() / 8560;
    uf1AppendImageChunks_(blob.data() + (idx % nImg) * 8560, burst);
    ++idx;
    return true;
}

// Paints the plug-in's t10 Lissajous raster into the 8560-byte 0x0122 image.
// NO trail accumulator: the persistence is ALREADY in t10 (the plug-in renders
// its own fade; a "5039 cells at once on stop" trace line that once suggested
// otherwise samples only once a SECOND — the full-rate log shows the shape
// holding still while it dims). Accumulating again just fades everything twice.
// The Lissajous-Fade-Time V-Pot sets the plug-in param, which flows into t10 and
// thus into our render for free — so there is no fade to read or apply here.
// What IS ours is the transfer from float to byte: light, not the raw float
// (see the resampling loop). (The dead uf1GonioTrail_ accumulator + a no-op
// fade read were removed 2026-07-22.)
void uf1PaintGoniometer_(const std::vector<float>& src,
                         std::vector<std::vector<uint8_t>>& burst)
{
    if (!g_uf1_dev || src.empty()) return;

    // ★ THE CODEC, derived 2026-07-18 from cap92 (see uf1DiamondWidths_ for the
    // derivation and why the 4-bit reading was wrong): ONE BYTE PER PIXEL over
    // the 185-row UF1 diamond that starts at byte 3.
    //
    // Both rasters have 185 ROWS, so the vertical map is 1:1 — no row
    // resampling, no interpolation, no rounding to chase. Only the width
    // differs: the plug-in's row r is 2r+1 cells wide, the UF1's is half that
    // (93 vs 185 at the peak). So each UF1 pixel covers ~2 source cells and we
    // take their MAX — a goniometer trace is one cell wide, and averaging it
    // with its dark neighbour halves the very thing we are drawing.
    const Uf1GonioGeom sg = uf1GonioGeomFor_(src.size());
    if (!sg.ok) return;

    std::array<uint8_t, kUf1DiamondBytes> img{};

    // Byte 0 is 0x02 in EVERY image SSL sends — 1000 images across cap89,
    // cap92 and cap101, three sessions with three different signals, no other
    // value ever. Bytes 1 and 2 vary and are frequently 0 (they are the
    // balance / correlation widgets that share this buffer — TODO: drive them
    // from Correlation(6) and StereoBalance(7), which we already receive), so
    // zero is legal THERE. It is not legal in byte 0: we sent 0x00 for four
    // days, i.e. every image we ever transmitted carried an invalid marker.
    img[0] = 0x02;

    // Bytes 1 and 2 drive the two scales framing the diamond: the L/R balance
    // strip above it and the -1/+1 correlation strip below. Both are a SIGNED
    // int8 on a ±48 scale, measured against cap92 whose correlation is known by
    // construction — byte 2 reads 48/36/24/12/0/-12/-24/-36/-48 for rho
    // +1.00..-1.00, dead linear — and byte 1 spans the same ±48 in cap89/cap93,
    // where L and R sweep in opposite directions. The plug-in hands us both
    // values outright (Correlation(6), StereoBalance(7)); no DSP here either.
    {
        auto scale48 = [](float v) -> uint8_t {
            if (!std::isfinite(v)) return 0;
            const int n = int(std::lround(std::clamp(v, -1.f, 1.f) * 48.f));
            return uint8_t(n < 0 ? n + 256 : n);
        };
        std::vector<float> cur, pk;
        if (sslcore::getMeter(int(sslmeter::DataType::StereoBalance), cur, pk)
            && !cur.empty())
            img[1] = scale48(cur[0]);
        if (sslcore::getMeter(int(sslmeter::DataType::Correlation), cur, pk)
            && !cur.empty())
            img[2] = scale48(cur[0]);
    }

    size_t litCells = 0, litNibbles = 0;
    float vmax = 0.f;
    std::array<size_t, 16> qhist{};

    // ★ THE CODEC, CORRECTED 2026-09-17: 17113 FOUR-BIT CELLS, TWO PER BYTE,
    // LOW NIBBLE FIRST, in the plug-in's own raster order. The Lissajous goes
    // across ONE TO ONE — no row resampling, no column resampling, no gamma,
    // no brightest-cell rule. All three existed to compensate for reading a
    // PAIR of cells as one 8-bit brightness, and each was measured against
    // SSL's images and tuned until it matched. That is why they looked right.
    //
    // The old reading said 8557 pixels of one byte; this says 17113 cells of
    // four bits, and 17113 = 2 * 8557 - 1. Our own byte table already carried
    // that identity: it is the cell table halved and rounded, which is why it
    // came out as 1,1,3,3,...,93,...,3,3,1,1 (each width twice) rather than the
    // plug-in's plain 1,3,5,...,185,...,5,3,1.
    //
    // MEASURED on SSL's own transmitted images (cap101, 40 images, 234878 lit
    // bytes): the high and low nibble have the SAME distribution to within a
    // tenth of a percent at every one of the fifteen levels (14 is the peak at
    // 11.9 % / 12.0 %, 13 next at 9.7 % / 9.7 %). For an 8-bit brightness the
    // low nibble would be the least significant bits and near uniform; two
    // samples of the same quantity look like this. 12.2 % of the lit mass sits
    // on the fifteen bytes whose nibbles are EQUAL, against 5.9 % by chance —
    // neighbouring cells of a continuous trace share a level. And 0x0d/0x0e sit
    // at the same frequency as 0xd0/0xe0, which under a brightness reading
    // would mean 13 and 208 are equally common.
    //
    // Reported by sollapse in issue #8, finding 3.
    // ⇨ The plug-in's own fade is already in the floats (every trail cell loses
    // 0.01 a frame), so there is nothing to shape here. Truncate, do not round:
    // only a cell at 1.0 earns the top level.
    const size_t cap = size_t(kUf1DiamondBytes - kUf1GonioOffset) * 2;
    const size_t n   = (src.size() < cap) ? src.size() : cap;
    for (size_t i = 0; i < n; ++i) {
        const float v = src[i];
        const float c = (std::isfinite(v) && v > 0.f)
                      ? ((v > 1.f) ? 1.f : v) : 0.f;
        const uint8_t q = uint8_t(c * 15.f);
        if (q) {
            ++litCells; ++litNibbles;
            ++qhist[size_t(q)];
            if (c > vmax) vmax = c;
        }
        uint8_t& b = img[size_t(kUf1GonioOffset) + i / 2];
        if (i & 1) b = uint8_t(b | uint8_t(q << 4));
        else       b = q;
    }

    // Content trace (rea_sixty/uf1_gonio_trace=1 -> <tmp>/reasixty_gonio.log).
    // The Mac has no USBPcap, so this is the wire check the handoff asks for:
    // a drawn frame needs THOUSANDS of lit nibbles. Distinguishes "no source
    // data" (cells 0) from "source too quiet to survive quantising" (cells >
    // 0, nibbles 0) from "we sent content and the device ignored it".
    {
        static int sTrace = -1;
        if (sTrace < 0) {
            const char* v = GetExtState("rea_sixty", "uf1_gonio_trace");
            sTrace = (v && *v && strcmp(v, "0")) ? 1 : 0;
        }
        if (sTrace) {
            static auto sLast = std::chrono::steady_clock::now();
            static size_t sFrames = 0;
            ++sFrames;
            // Cell-level churn against the previous array, accumulated over the
            // logging second (not just the last frame — one frame is noise).
            static std::vector<float> sPrev;
            static size_t sOff = 0, sOn = 0;
            if (sPrev.size() == src.size()) {
                for (size_t i = 0; i < src.size(); ++i) {
                    const bool a = sPrev[i] > 0.f, b = src[i] > 0.f;
                    if (a && !b) ++sOff;
                    else if (!a && b) ++sOn;
                }
            }
            sPrev = src;
            // Same measure on the bytes we are about to transmit.
            static std::array<uint8_t, kUf1DiamondBytes> sPrevImg{};
            static size_t sOutOff = 0, sOutOn = 0;
            for (size_t i = size_t(kUf1GonioOffset); i < img.size(); ++i) {
                const bool a = sPrevImg[i] != 0, b = img[i] != 0;
                if (a && !b) ++sOutOff;
                else if (!a && b) ++sOutOn;
            }
            sPrevImg = img;
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - sLast).count() >= 1.0) {
                sLast = now;
                if (FILE* f = std::fopen(uf8::logPath("reasixty_gonio.log").c_str(), "a")) {
                    // Same clock as the sslcore trace (steady_clock epoch
                    // seconds) so the two logs line up line-for-line.
                    const double ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count() / 1000.0;
                    std::fprintf(f, "[%.1f] src=%zu frames=%zu litCells=%zu litNibbles=%zu max=%.3f",
                                 ts, src.size(), sFrames, litCells, litNibbles, double(vmax));
                    // Quantised-value histogram. SSL's own images (cap101, 200
                    // frames) sit at 12-14 with 60% of cells lit; if ours pile
                    // up at 1-3 the picture is simply too dim to see, and the
                    // fix is the transfer curve, not the geometry.
                    std::fprintf(f, " q=");
                    for (int q = 0; q < 16; ++q)
                        std::fprintf(f, "%zu%s", qhist[size_t(q)], q < 15 ? "," : "");
                    // The SOURCE distribution, before any mapping of ours. SSL's
                    // own images put 14% of their pixels in the faintest byte
                    // class and only 3.5% at full scale; ours came out the other
                    // way round (2% / 34%), which is what "flickers instead of
                    // fading" looks like. This says whether the fade is already
                    // missing in what the plug-in hands us or whether we lose it.
                    std::array<size_t, 16> shist{};
                    for (float v : src)
                        if (v > 0.f)
                            ++shist[size_t(std::clamp(int(v * 16.f), 0, 15))];
                    std::fprintf(f, " src16=");
                    for (int q = 0; q < 16; ++q)
                        std::fprintf(f, "%zu%s", shist[size_t(q)], q < 15 ? "," : "");
                    // Does the SOURCE blink? A 2 s fade at 25 Hz should let
                    // roughly lit/50 cells reach zero per frame and light very
                    // few that were dark. Thousands either way means the raster
                    // the plug-in hands us is not a decaying trail at all, and
                    // no amount of work on our side will make it look like one.
                    // Churn of the EMITTED image, same idea one stage later. If
                    // the source holds still while it dims (on=0) but this is
                    // large, the movement is something we do — pooling, the
                    // instance choice, a stale snapshot — not something we are
                    // handed. Frank sees the cloud drift while it should sit
                    // and fade; these two numbers side by side say whose it is.
                    std::fprintf(f, " off=%zu on=%zu outOff=%zu outOn=%zu\n",
                                 sOff, sOn, sOutOff, sOutOn);
                    std::fclose(f);
                }
                sFrames = 0; sOff = 0; sOn = 0; sOutOff = 0; sOutOn = 0;
            }
        }
    }

    uf1AppendImageChunks_(img.data(), burst);
}

struct Uf1PeakHold {
    float l = -120.f, r = -120.f;
    std::chrono::steady_clock::time_point t{};

    void reset(float vl, float vr, std::chrono::steady_clock::time_point now)
    { l = vl; r = vr; t = now; }

    void step(float vl, float vr, std::chrono::steady_clock::time_point now)
    {
        if (std::chrono::duration<double>(now - t).count() >= kUf1HoldResetSec)
            reset(vl, vr, now);
        if (vl > l) l = vl;
        if (vr > r) r = vr;
    }
};

// The Analogue screen's SECOND needle (0x0127), as SSL 360 2.1.12 draws it —
// MEASURED on cap130 (VU, 14 episodes) and cap131 (PPM, 11 episodes), 2026-09-11:
//   · it rides the running MAXIMUM of the main needle's value;
//   · it stands for ~1.9 s after the value last reached that maximum (the Meter
//     Pro's "Analogue Max Needle", param 10: Off / 2 sec / Infinite);
//   · then it RELEASES continuously, linear in the faceplate's own unit, until
//     it lands on the main needle and rides it again — VU 72 dB/s (median,
//     range 61-82, the whole dial in ~0.25 s), PPM 2.9 marks/s (median, range
//     2.2-3.8, ~2 s from mark 7 down).
// Uf1PeakHold above is the OVERVIEW bar marker's law (hold, then SNAP, cap89);
// on the needle that snap was Frank's "zweite Nadel bleibt jerky" (2026-09-11).
struct Uf1MaxNeedle {
    float l = -120.f, r = -120.f;
    std::chrono::steady_clock::time_point tl{}, tr{}, last{};
    void reset(float vl, float vr, std::chrono::steady_clock::time_point now)
    { l = vl; r = vr; tl = tr = last = now; }
    // holdSec: 0 = Off (the needle sits on the main one), < 0 = Infinite.
    void step(float vl, float vr, std::chrono::steady_clock::time_point now,
              double holdSec, float releasePerS)
    {
        const double dt = std::clamp(
            std::chrono::duration<double>(now - last).count(), 0.0, 0.5);
        last = now;
        auto one = [&](float v, float& h, std::chrono::steady_clock::time_point& t0) {
            if (holdSec == 0.0 || v >= h) { h = v; t0 = now; return; }
            if (holdSec < 0.0) return;
            if (std::chrono::duration<double>(now - t0).count() < holdSec) return;
            h = std::max(v, h - float(releasePerS * dt));
        };
        one(vl, l, tl); one(vr, r, tr);
    }
};
constexpr double kUf1MaxNeedleHoldSec              = 2.0;    // param 10 "2 sec"; measured 1.8-1.95 s
constexpr float  kUf1VuMaxNeedleReleaseDbPerS      = 72.f;   // cap130 median
constexpr float  kUf1PpmMaxNeedleReleaseMarksPerS  = 2.9f;   // cap131 median
// The plug-in's dt=1 hold, as SSL 360 bridges it (see the VU glide in
// uf1PaintMeter_): a value frozen bit for bit this long is being held, and no
// hold lasts longer than the second figure (cap130/131 and /tmp/ssl360.pcap:
// 0.48 to 0.52 s, then the jump to the current value).
constexpr double kUf1VuHoldDetectS = 0.10;
constexpr double kUf1VuHoldMaxS    = 0.60;

// PPM Type-II is linear in marks (cap94, verified vs IEC 60268-10): byte = 9 +
// (mark-1)*27.3, clamped [0,180]. mark relates to dBFS via the reference.
uint8_t uf1PpmByte_(float mark)
{
    if (!std::isfinite(mark)) return 0;
    double b = 9.0 + (static_cast<double>(mark) - 1.0) * 27.3;
    if (b < 0.0)   b = 0.0;
    if (b > 180.0) b = 180.0;
    return static_cast<uint8_t>(std::lround(b));
}

// RTA band value: dBFS -> byte, across the screen's OWN scale. cap90 measured a
// pure linear law at SclTop 0 dB / SclBtm -120 dB — byte = 3 + (dBFS + 120)·5/3 —
// and said in as many words that it "should be DERIVED from SclTop/SclBtm, not
// hardcoded; those are TrackFX params we can read". It never was, so both V-Pots
// moved the plug-in and changed nothing on the UF1 (Frank 2026-08-11).
//
// The span is what generalises: 3 is the header baseline, the bar runs to 200, so
//   byte = 3 + (dBFS - bottom) · 197/(top - bottom)
// ⚠ cap90's 5/3 is 200/120, i.e. it used 200 counts of travel, not 197 — keep
// THAT, or the byte-exact 198-at-3-dBFS check stops matching:
//   byte = 3 + (dBFS - bottom) · 200/(top - bottom), clamped [3, 200].
// At the captured settings this is the same law, digit for digit.
uint8_t uf1RtaByte_(float dbfs, float topDb, float botDb)
{
    // Empty / below-floor band: SSL emits byte 0, NOT the baseline 3. Verified
    // byte-exact against the wire (cap90_uf1_rta_level, cap81_uf1_meter_rta):
    // byte 0 is the dominant empty-band value and byte 2 is never emitted, so the
    // scale bottom is 0 (empty) -> 1 -> law baseline 3+. Emitting 3 here lit a
    // permanent 1px floor across every quiet band.
    if (!std::isfinite(dbfs) || dbfs <= botDb) return 0;
    const double span = double(topDb) - double(botDb);
    if (!(span > 0.0)) return 0;
    double b = 3.0 + (double(dbfs) - double(botDb)) * (200.0 / span);
    if (b < 3.0)   b = 3.0;
    if (b > 200.0) b = 200.0;
    return static_cast<uint8_t>(std::lround(b));
}

// Format a dB value the way the UF1's Meter view does: one decimal, "-inf"
// for silence (cap76: "-6.1", "-16.4", "-inf").
std::string uf1FormatMeterDb_(float db)
{
    if (db <= -120.f) return "-inf";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", db);
    // SSL never prints "-0.0": %.1f rounds any value in (-0.05, 0) to negative zero,
    // but the plug-in shows "0.0" for that band (cap110/cap80/cap107). Normalise so
    // our bytes match the wire. "%.1f" only ever yields the single form "-0.0".
    if (std::strcmp(buf, "-0.0") == 0) return "0.0";
    return std::string(buf);
}
