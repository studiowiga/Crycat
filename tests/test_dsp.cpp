// Tests du moteur CRYCAT : g++ -O2 -std=c++17 tests/test_dsp.cpp -o test_dsp && ./test_dsp
#include "../CrycatDSP.h"
#include <cstdio>
#include <vector>
#include <cstdlib>

using namespace crycat;
static int fails = 0;
#define CHECK(c, ...) do { if (! (c)) { std::printf ("ECHEC : "); std::printf (__VA_ARGS__); std::printf ("\n"); ++fails; } } while (0)

// gain mesuré (dB) d'un sinus à f, pédale fixe
static float measure (Settings s, float f, float amp = 0.05f, double sr = 48000.0)
{
    Engine e; e.setSettings (s); e.prepare (sr, 512); e.reset();
    const int n = (int) sr;
    std::vector<float> buf ((size_t) n);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = amp * (float) std::sin (2 * kPi * f * i / sr);
    float* p[1] = { buf.data() };
    e.process (p, 1, 0, n);
    double acc = 0; int cnt = 0;
    for (int i = n / 2; i < n; ++i) { acc += buf[(size_t) i] * buf[(size_t) i]; ++cnt; }
    const double rms = std::sqrt (acc / cnt);
    return (float) (20 * std::log10 (rms / (amp / std::sqrt (2.0))));
}

static float peakFreq (Settings s)
{
    float best = -999, bf = 0;
    for (float f = 100; f < 6000; f *= 1.03f) { const float g = measure (s, f); if (g > best) { best = g; bf = f; } }
    return bf;
}

int main()
{
    Settings s; s.purr = 0.0f; s.smoothMs = 0.5f;
    // 1) balayage GCB95 : ~450 Hz au talon, ~1,6 kHz à la pointe
    s.pedal = 0.0f; const float fh = peakFreq (s);
    s.pedal = 1.0f; const float ft = peakFreq (s);
    std::printf ("GCB95 : talon %.0f Hz, pointe %.0f Hz\n", fh, ft);
    CHECK (fh > 380 && fh < 540, "talon %.0f", fh);
    CHECK (ft > 1350 && ft < 1850, "pointe %.0f", ft);

    // 2) la mesure suit le modèle de l'écran (à ±2 dB) pour quelques fréquences
    for (float pos : { 0.0f, 0.5f, 1.0f })
        for (float f : { 150.0f, 500.0f, 1000.0f, 3000.0f })
        {
            s.pedal = pos;
            const float m = measure (s, f), t = magnitudeDb (s, pos, f);
            CHECK (std::abs (m - t) < 2.5f, "pos %.1f f %.0f : mesuré %.1f dB, modèle %.1f dB", pos, f, m, t);
        }
    s.pedal = 0.5f;
    std::printf ("gain au centre (pos 0.5) : %.1f dB, graves 100 Hz : %.1f dB\n", measure (s, centreHz (2, 0.5f)), measure (s, 100));

    // 3) plages : chaque plage monte
    for (int r = 0; r < kNumRanges; ++r)
    {
        Settings t = s; t.rangeIdx = r; t.pedal = 0.0f; const float a = peakFreq (t); t.pedal = 1.0f; const float b = peakFreq (t);
        std::printf ("plage %d : %.0f -> %.0f Hz\n", r + 1, a, b);
        CHECK (b > a * 2.5f, "plage %d", r + 1);
    }

    // 4) stabilité : signal fort, Q maxi, PURR maxi, pédale qui bouge vite
    {
        Settings t; t.q = 1.0f; t.purr = 1.0f; t.boost = 1.0f; t.smoothMs = 1.0f; t.rangeIdx = 5;
        Engine e; e.prepare (44100.0, 512);
        std::vector<float> L (512), Rr (512);
        float* p[2] = { L.data(), Rr.data() };
        float mx = 0;
        for (int b = 0; b < 400; ++b)
        {
            t.pedal = (b % 20) < 10 ? 1.0f : 0.0f; e.setSettings (t);
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = 0.9f * (float) ((std::rand() / (double) RAND_MAX) * 2 - 1); Rr[(size_t) i] = L[(size_t) i]; }
            e.process (p, 2, 0, 512);
            for (float v : L) { CHECK (std::isfinite (v), "NaN"); mx = std::max (mx, std::abs (v)); }
        }
        std::printf ("crête au pire cas : %.2f\n", mx);
        CHECK (mx < 40.0f, "crête %.1f", mx);
    }

    // 5) AUTO : coupé au talon, enclenché dès que la pédale bouge, recoupé après le repos
    {
        Settings t; t.autoMode = true; t.autoOffMs = 300; t.pedal = 0.0f;
        Engine e; e.setSettings (t); e.prepare (48000.0, 480);
        std::vector<float> b (480, 0.0f); float* p[1] = { b.data() };
        e.process (p, 1, 0, 480);
        CHECK (! e.isOn(), "auto : doit être coupé au départ");
        t.pedal = 0.3f; e.setSettings (t); e.process (p, 1, 0, 480);
        CHECK (e.isOn(), "auto : doit s'enclencher");
        t.pedal = 0.0f; e.setSettings (t);
        for (int k = 0; k < 20; ++k) e.process (p, 1, 0, 480);   // 200 ms
        CHECK (e.isOn(), "auto : encore enclenché après 200 ms");
        for (int k = 0; k < 20; ++k) e.process (p, 1, 0, 480);   // 400 ms
        CHECK (! e.isOn(), "auto : coupé après 300 ms au talon");
    }

    // 6) bypass : sortie = entrée
    {
        Settings t; t.active = false;
        Engine e; e.setSettings (t); e.prepare (48000.0, 256); e.reset();
        std::vector<float> b (256); for (int i = 0; i < 256; ++i) b[(size_t) i] = (float) std::sin (i * 0.1);
        auto ref = b; float* p[1] = { b.data() };
        e.process (p, 1, 0, 256);
        float err = 0; for (int i = 0; i < 256; ++i) err = std::max (err, std::abs (b[(size_t) i] - ref[(size_t) i]));
        CHECK (err < 1e-6f, "bypass %.2g", err);
    }

    std::printf (fails ? "%d échec(s)\n" : "tous les tests passent\n", fails);
    return fails ? 1 : 0;
}
