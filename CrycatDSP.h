#pragma once
// CRYCAT (Studio Wiga) : moteur audio de la wah, sans dépendance à JUCE (testable seul : tests/test_dsp.cpp)
//
// Modèle de la Dunlop Cry Baby GCB95 : filtre passe-bande résonant (inducteur 500 mH + 0.01 µF dans la boucle
// de contre-réaction) dont la fréquence centrale balaye de 450 Hz (talon) à 1,6 kHz (pointe), avec une partie
// des graves qui passe toujours (plus au talon qu'à la pointe). Modifications CRYCAT :
//   - RANGE : 6 plages de balayage (de la wah basse / 7 cordes à la wah « shriek »), la 3 = GCB95 d'origine
//   - Q     : résonance (équivalent de la résistance R7 de la Cry Baby, réglable de « large » à « vocal »)
//   - PURR  : saturation de l'inducteur + attaque de l'étage d'entrée (grain, « miaulement » plus rauque)
//   - BOOST : gain de sortie 0 à +15 dB (comme le booster de la 535Q)
//   - AUTO  : enclenchement automatique quand la pédale quitte le talon, coupure après un repos au talon
#include <cmath>
#include <algorithm>

namespace crycat
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kNumRanges = 6;

    struct Range { const char* name; const char* label; float fLo, fHi; };

    inline const Range& range (int i)
    {
        static const Range r[kNumRanges] = {
            { "1 - Low / 7-string",    "LOW",     220.0f,  900.0f },
            { "2 - Dark",              "DARK",    320.0f, 1250.0f },
            { "3 - GCB95 Classic",     "GCB95",   450.0f, 1600.0f },
            { "4 - Vintage (V847)",    "VINTAGE", 480.0f, 2100.0f },
            { "5 - Bright",            "BRIGHT",  600.0f, 2600.0f },
            { "6 - Shriek",            "SHRIEK",  750.0f, 3200.0f },
        };
        return r[std::clamp (i, 0, kNumRanges - 1)];
    }

    struct Settings
    {
        int   rangeIdx = 2;
        float pedal    = 0.0f;     // 0 = talon (grave), 1 = pointe (aigu), déjà calibrée
        float q        = 0.4f;     // 0..1
        float purr     = 0.2f;     // 0..1
        float boost    = 0.0f;     // 0..1 -> 0..15 dB
        bool  active   = true;     // footswitch
        bool  autoMode = false;    // AUTO-ENGAGE
        float smoothMs = 20.0f;    // glissé de la pédale
        float autoOffMs = 400.0f;  // temps de repos au talon avant coupure (mode AUTO)
    };

    // ---------- relations communes au moteur et à l'affichage ----------
    inline float qValue (float q01)                { return 2.0f * std::pow (7.0f, std::clamp (q01, 0.0f, 1.0f)); }   // 2 .. 14
    inline float effectiveQ (float q01, float pos) { return qValue (q01) * (0.85f + 0.30f * pos); }                     // un peu plus pointu vers la pointe
    inline float centreHz (int rangeIdx, float pos)
    {
        const auto& r = range (rangeIdx);
        return r.fLo * std::pow (r.fHi / r.fLo, std::clamp (pos, 0.0f, 1.0f));
    }
    inline float peakGain (float qEff)             { return 2.2f * std::pow (qEff / 4.4f, 0.35f); }
    inline float lowLeak (float pos)               { return 0.60f + (0.15f - 0.60f) * std::clamp (pos, 0.0f, 1.0f); }
    inline float boostGain (float b01)             { return std::pow (10.0f, 15.0f * std::clamp (b01, 0.0f, 1.0f) / 20.0f); }

    // réponse (partie linéaire) en dB, pour la courbe de l'écran
    inline float magnitudeDb (const Settings& s, float pos, float f)
    {
        const double fc = centreHz (s.rangeIdx, pos), Q = effectiveQ (s.q, pos);
        const double u = f / fc;
        // passe-bande normalisé : H = j u/Q / (1 - u² + j u/Q)
        const double re = 1.0 - u * u, im = u / Q;
        const double bpRe = (im * im) / (re * re + im * im), bpIm = (im * re) / (re * re + im * im);
        // passe-bas 1 pôle (graves qui passent) à 0,7 fc
        const double v = f / (0.7 * fc);
        const double lpRe = 1.0 / (1.0 + v * v), lpIm = -v / (1.0 + v * v);
        const double pg = peakGain ((float) Q), lk = lowLeak (pos);
        const double hr = pg * bpRe + lk * lpRe, hi = pg * bpIm + lk * lpIm;
        const double mag = std::sqrt (hr * hr + hi * hi) * boostGain (s.boost);
        return (float) (20.0 * std::log10 (std::max (mag, 1.0e-6)));
    }

    // ---------- moteur ----------
    class Engine
    {
    public:
        static constexpr int kMaxCh = 2;

        void prepare (double sampleRate, int /*maxBlock*/)
        {
            sr = sampleRate;
            reset();
        }

        void reset()
        {
            for (auto& c : ch) c = {};
            smoothPos = target.pedal;
            engaged = ! target.autoMode || target.pedal > 0.035f;
            restCount = 0;
            wet = (target.active && (! target.autoMode || engaged)) ? 1.0f : 0.0f;
            boostLin = boostGain (target.boost);
        }

        void setSettings (const Settings& s) { target = s; }

        // traite n échantillons de nch canaux (en place)
        void process (float* const* data, int nch, int start, int n)
        {
            nch = std::min (nch, kMaxCh);
            const float sm = (float) std::exp (-1.0 / (std::max (0.5f, target.smoothMs) * 0.001 * sr));
            const float xf = (float) (1.0 / (0.012 * sr));                    // fondu de bypass 12 ms
            const float gs = (float) std::exp (-1.0 / (0.02 * sr));           // lissage du boost
            const float bTarget = boostGain (target.boost);
            const float purr = std::clamp (target.purr, 0.0f, 1.0f);
            const float drive = 1.0f + 5.0f * purr * purr + 1.5f * purr;
            const float k = 0.15f + 2.6f * purr;                               // saturation de l'état de l'inducteur
            const float dcA = (float) std::exp (-2.0 * kPi * 18.0 / sr);
            const int autoOffSamples = (int) (std::max (50.0f, target.autoOffMs) * 0.001 * sr);

            for (int i = start; i < start + n; ++i)
            {
                // ---- enclenchement automatique (sur la position demandée, pas la position lissée) ----
                if (target.autoMode)
                {
                    if (target.pedal > 0.035f) { engaged = true; restCount = 0; }
                    else if (engaged && target.pedal < 0.02f && ++restCount >= autoOffSamples) engaged = false;
                }
                else { engaged = true; restCount = 0; }
                const bool on = target.active && engaged;
                wet += on ? std::min (xf, 1.0f - wet) : -std::min (xf, wet);

                smoothPos = target.pedal + (smoothPos - target.pedal) * sm;
                boostLin = bTarget + (boostLin - bTarget) * gs;

                // coefficients (recalculés tous les 4 échantillons)
                if ((coefCount++ & 3) == 0)
                {
                    const float pos = smoothPos;
                    fc = centreHz (target.rangeIdx, pos);
                    const float Q = effectiveQ (target.q, pos);
                    g = (float) std::tan (kPi * std::min ((double) fc, 0.45 * sr) / sr);
                    R = 0.5f / Q;
                    hNorm = 1.0f / (1.0f + 2.0f * R * g + g * g);
                    pg = peakGain (Q);
                    lk = lowLeak (pos);
                    const double fl = 0.7 * fc;
                    lpA = (float) std::exp (-2.0 * kPi * fl / sr);
                }

                for (int c = 0; c < nch; ++c)
                {
                    auto& st = ch[c];
                    const float x = data[c][i];
                    // étage d'entrée : légère asymétrie + saturation douce (PURR)
                    float xi = x * drive;
                    xi = xi + 0.12f * purr * xi * xi;
                    xi = xi / (1.0f + purr * std::abs (xi));               // PURR à 0 : entrée parfaitement linéaire
                    xi *= (1.0f + 0.4f * purr) / drive;

                    // filtre d'état (TPT) : passe-bande résonant
                    const float hp = (xi - (2.0f * R + g) * st.s1 - st.s2) * hNorm;
                    const float bp = g * hp + st.s1;
                    st.s1 = g * hp + bp;
                    const float lp = g * bp + st.s2;
                    st.s2 = g * bp + lp;
                    // saturation de l'inducteur : l'énergie stockée est limitée doucement
                    st.s1 = std::tanh (k * st.s1) / k;
                    st.s2 = std::tanh (0.5f * k * st.s2) / (0.5f * k);

                    // graves qui passent toujours
                    st.lo = xi + (st.lo - xi) * lpA;

                    float y = pg * (2.0f * R * bp) + lk * st.lo;
                    y *= boostLin;
                    // bloqueur de continu
                    const float dc = y - st.dcX + dcA * st.dcY;
                    st.dcX = y; st.dcY = dc;
                    y = dc;
                    if (! std::isfinite (y)) { st = {}; y = 0.0f; }
                    data[c][i] = x + (y - x) * wet;
                }
            }
        }

        float smoothedPedal() const { return smoothPos; }
        float currentHz() const     { return fc; }
        bool  isEngaged() const     { return engaged; }
        bool  isOn() const          { return target.active && engaged; }

    private:
        struct Chan { float s1 = 0, s2 = 0, lo = 0, dcX = 0, dcY = 0; };
        Chan ch[kMaxCh];
        Settings target;
        double sr = 44100.0;
        float smoothPos = 0.0f, wet = 1.0f, boostLin = 1.0f;
        float fc = 450.0f, g = 0.03f, R = 0.1f, hNorm = 1.0f, pg = 2.2f, lk = 0.6f, lpA = 0.9f;
        unsigned coefCount = 0;
        bool engaged = true;
        int restCount = 0;
    };
}
