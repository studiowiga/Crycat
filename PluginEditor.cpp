#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

namespace
{
    // Repère du dessin = taille de background.png. Toutes les coordonnées sont dans ces pixels (tools/layout.txt).
    // Mêmes dimensions de fenêtre que Wharry : hauteurs de Ring Think (495 / 557 / 743 px) -> 544 x 495 par défaut et au minimum
    constexpr int kW = 1020, kH = 928;
    constexpr float kRefH = 743.0f;                              // hauteur « 100 % » (= Ring Think à 100 %)
    const float kScales[3] = { 495.0f / 743.0f, 0.75f, 1.0f };
    constexpr float kDefaultScale = 495.0f / 743.0f;             // fenêtre par défaut (= taille minimale)

    const Point<float> kKnob (633.0f, 300.0f);  constexpr float kKnobR = 60.0f, kCapR = 44.0f;   // sélecteur RANGE 6 crans
    constexpr float kRangeA0 = -125.0f, kRangeSpan = 250.0f;
    const Point<float> kSmall[3] = { { 632.0f, 528.0f }, { 752.0f, 528.0f }, { 872.0f, 528.0f } };   // Q, PURR, BOOST
    constexpr float kSmallR = 42.0f, kSmallCapR = 32.0f;
    const char* const kSmallId[3] = { "q", "purr", "boost" };
    const Rectangle<float> kScreen (744.0f, 186.0f, 204.0f, 232.0f);
    const Rectangle<float> kFs (681.0f, 668.0f, 138.0f, 138.0f);           // footswitch ON / OFF
    const Point<float> kFsLed (857.0f, 737.0f);
    const Rectangle<float> kToggle (858.0f, 84.0f, 64.0f, 64.0f);          // inverseur MAN / AUTO (sprite 64 x 64)
    const Rectangle<float> kToggleHit (812.0f, 88.0f, 160.0f, 70.0f);
    const Rectangle<float> kPlate (84.0f, 35.0f, 444.0f, 845.0f);          // plateau
    constexpr float kLedR = 7.0f;
    const Colour kInk (0xffece5e0), kAmber (0xffffb040), kAmberHi (0xffffd690), kScreenBg (0xff0e0906);
    constexpr int kFrames = 33;                                             // positions précalculées du plateau

    // plateau : il pivote autour de la charnière (côté talon, en bas)
    constexpr float kPlateL = 84.0f, kPlateR = 528.0f, kPlateTop = 35.0f, kHingeY = 880.0f;
    constexpr float kRise = 26.0f, kWiden = 1.035f;                                            // orteil levé : pointe plus haute et plus large

    Image loadPng (const void* d, int n) { return ImageFileFormat::loadFrom (d, (size_t) n).convertedToFormat (Image::ARGB); }

    // redimensionne une fois, puis garde en cache tant que la taille ne change pas
    struct ScaledCache
    {
        Image src, img; float scale = -1.0f;
        const Image& get (float s)
        {
            if (std::abs (s - scale) > 1.0e-4f || ! img.isValid())
            {
                scale = s;
                const int w = jmax (1, roundToInt ((float) src.getWidth() * s)), h = jmax (1, roundToInt ((float) src.getHeight() * s));
                img = src.rescaled (w, h, Graphics::highResamplingQuality);
            }
            return img;
        }
    };

    //==========================================================================
    // Plateau de la pédale : il pivote autour de la charnière (côté talon, en bas). Chaque position est une
    // perspective exacte du calque « orteil enfoncé » (pas de fondu entre deux images -> aucun dédoublement).

    struct Homography
    {
        double h[9] {};
        // calcule H tel que H(dst[i]) = src[i] (4 couples de points)
        bool solve (const Point<double> dst[4], const Point<double> src[4])
        {
            double A[8][9] {};
            for (int i = 0; i < 4; ++i)
            {
                const double x = dst[i].x, y = dst[i].y, u = src[i].x, v = src[i].y;
                double r1[9] = { x, y, 1, 0, 0, 0, -u * x, -u * y, u };
                double r2[9] = { 0, 0, 0, x, y, 1, -v * x, -v * y, v };
                std::copy (r1, r1 + 9, A[2 * i]); std::copy (r2, r2 + 9, A[2 * i + 1]);
            }
            for (int c = 0; c < 8; ++c)
            {
                int piv = c;
                for (int r = c + 1; r < 8; ++r) if (std::abs (A[r][c]) > std::abs (A[piv][c])) piv = r;
                if (std::abs (A[piv][c]) < 1e-12) return false;
                for (int k = 0; k < 9; ++k) std::swap (A[c][k], A[piv][k]);
                for (int r = 0; r < 8; ++r)
                    if (r != c)
                    {
                        const double f = A[r][c] / A[c][c];
                        for (int k = c; k < 9; ++k) A[r][k] -= f * A[c][k];
                    }
            }
            for (int i = 0; i < 8; ++i) h[i] = A[i][8] / A[i][i];
            h[8] = 1.0;
            return true;
        }
        Point<double> map (double x, double y) const
        {
            const double w = h[6] * x + h[7] * y + h[8];
            return { (h[0] * x + h[1] * y + h[2]) / w, (h[3] * x + h[4] * y + h[5]) / w };
        }
    };

    class TreadleWarp
    {
    public:
        void setSource (Image downImg)
        {
            down = std::move (downImg);
            const float cx = 0.5f * (kPlateL + kPlateR);
            box = Rectangle<float> (cx - (cx - kPlateL) * kWiden - 4.0f, kPlateTop - kRise - 4.0f,
                                    (kPlateR - kPlateL) * kWiden + 40.0f, kHingeY - kPlateTop + kRise + 40.0f)
                      .getSmallestIntegerContainer().getIntersection ({ 0, 0, kW, kH });
            scale = -1.0f;
        }
        Rectangle<int> designBounds() const { return box; }

        void prepare (float s)
        {
            if (std::abs (s - scale) < 1.0e-4f) return;
            scale = s;
            const int bw = jmax (1, roundToInt ((float) box.getWidth() * s)), bh = jmax (1, roundToInt ((float) box.getHeight() * s));
            src = down.getClippedImage (box).rescaled (bw, bh, Graphics::highResamplingQuality).convertedToFormat (Image::ARGB);
            for (auto& f : frames) f = {};
            }

        // t = 0 : orteil levé, t = 1 : orteil enfoncé
        const Image& frame (int i)
        {
            i = jlimit (0, kFrames - 1, i);
            if (! frames[(size_t) i].isValid()) frames[(size_t) i] = build ((float) i / (float) (kFrames - 1));
            return frames[(size_t) i];
        }

    private:
        Image build (float t) const
        {
            const int w = src.getWidth(), h = src.getHeight();
            const double sx = (double) w / box.getWidth(), sy = (double) h / box.getHeight();
            auto loc = [&] (double x, double y) { return Point<double> ((x - box.getX()) * sx, (y - box.getY()) * sy); };
            const double cx = 0.5 * (kPlateL + kPlateR), lift = 1.0 - t;
            const double wid = 1.0 + (kWiden - 1.0) * lift, rise = kRise * lift;
            const Point<double> srcQ[4] = { loc (kPlateL, kPlateTop), loc (kPlateR, kPlateTop), loc (kPlateR, kHingeY), loc (kPlateL, kHingeY) };
            const Point<double> dstQ[4] = { loc (cx - (cx - kPlateL) * wid, kPlateTop - rise), loc (cx + (kPlateR - cx) * wid, kPlateTop - rise),
                                            loc (kPlateR, kHingeY), loc (kPlateL, kHingeY) };
            Image plate (Image::ARGB, w, h, true);
            Homography H;
            if (lift < 1.0e-4 || ! H.solve (dstQ, srcQ)) plate = src.createCopy();
            else
            {
                const Image::BitmapData bs (src, Image::BitmapData::readOnly);
                Image::BitmapData bo (plate, Image::BitmapData::writeOnly);
                const double topY = dstQ[0].y, botY = dstQ[3].y;
                for (int y = 0; y < h; ++y)
                {
                    // la partie avant, inclinée vers la lumière (en haut à gauche), s'éclaircit un peu
                    const double gy = jlimit (0.0, 1.0, (botY - (double) y) / jmax (1.0, botY - topY));
                    const float gain = (float) (1.0 + 0.10 * lift * std::pow (gy, 1.5));
                    for (int x = 0; x < w; ++x)
                    {
                        const auto q = H.map (x + 0.5, y + 0.5);
                        const double u = q.x - 0.5, v = q.y - 0.5;
                        if (u < -1.0 || v < -1.0 || u > w || v > h) continue;
                        *(PixelARGB*) bo.getPixelPointer (x, y) = sample (bs, w, h, (float) u, (float) v, gain);
                    }
                }
            }
            if (lift < 1.0e-4) return plate;
            // tranche droite du plateau, visible quand la pointe se lève (épaisse côté pointe, nulle à la charnière)
            {
                Image::BitmapData bo (plate, Image::BitmapData::readWrite);
                const double maxFace = 9.0 * sx * lift;
                const double ty = dstQ[1].y, by = dstQ[2].y;
                for (int y = jmax (0, (int) std::ceil (ty)); y <= jmin (h - 1, (int) std::floor (by)); ++y)
                {
                    const double v = ((double) y - ty) / jmax (1.0, by - ty);          // 0 = pointe, 1 = charnière
                    const double xr = dstQ[1].x + (dstQ[2].x - dstQ[1].x) * v;
                    const double fw = maxFace * (1.0 - v);
                    if (fw < 0.3) continue;
                    for (int x = (int) std::floor (xr); x <= jmin (w - 1, (int) std::ceil (xr + fw)); ++x)
                    {
                        if (x < 0) continue;
                        const double u = ((double) x - xr) / fw;                     // 0 = arête, 1 = bas de la tranche
                        if (u < 0.0 || u > 1.0) continue;
                        auto* px = (PixelARGB*) bo.getPixelPointer (x, y);
                        const float k = (float) jlimit (0.0, 1.0, std::min (u * fw, (1.0 - u) * fw) + 0.5);   // bords adoucis
                        const int g0 = (int) (34 + 46 * (1.0 - u) + 25 * (1.0 - v));   // métal sombre, plus clair près de l'arête
                        // tranche opaque posée par-dessus (pixels prémultipliés : out = tranche * k + existant * (1 - k))
                        auto mixc = [k] (int a, int b) { return (uint8) jlimit (0, 255, roundToInt ((float) a * k + (float) b * (1.0f - k))); };
                        PixelARGB o;
                        o.setARGB (mixc (255, px->getAlpha()), mixc (jmin (255, g0 + 6), px->getRed()), mixc (g0, px->getGreen()), mixc (jmin (255, g0 + 4), px->getBlue()));
                        *px = o;
                    }
                }
            }
            // ombre portée sous la pointe levée (lumière en haut à gauche -> ombre vers la droite / le bas)
            const int ox = roundToInt (8.0 * sx * lift), oy = roundToInt (11.0 * sy * lift), rad = jmax (1, roundToInt (6.0 * sx));
            std::vector<float> sh ((size_t) (w * h), 0.0f), tmp ((size_t) (w * h), 0.0f);
            {
                const Image::BitmapData bp (plate, Image::BitmapData::readOnly);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const int xs = x - ox, ys = y - oy;
                        if (xs >= 0 && ys >= 0 && xs < w && ys < h)
                            sh[(size_t) (y * w + x)] = (float) ((const PixelARGB*) bp.getPixelPointer (xs, ys))->getAlpha() / 255.0f;
                    }
            }
            const float norm = 1.0f / (float) (2 * rad + 1);
            for (int pass = 0; pass < 2; ++pass)      // flou boîte séparable, 2 passes (~ gaussien)
            {
                for (int y = 0; y < h; ++y)
                {
                    float acc = 0.0f;
                    for (int x = -rad; x < w + rad; ++x)
                    {
                        if (x + rad < w) acc += sh[(size_t) (y * w + x + rad)];
                        if (x - rad - 1 >= 0) acc -= sh[(size_t) (y * w + x - rad - 1)];
                        if (x >= 0 && x < w) tmp[(size_t) (y * w + x)] = acc * norm;
                    }
                }
                for (int x = 0; x < w; ++x)
                {
                    float acc = 0.0f;
                    for (int y = -rad; y < h + rad; ++y)
                    {
                        if (y + rad < h) acc += tmp[(size_t) ((y + rad) * w + x)];
                        if (y - rad - 1 >= 0) acc -= tmp[(size_t) ((y - rad - 1) * w + x)];
                        if (y >= 0 && y < h) sh[(size_t) (y * w + x)] = acc * norm;
                    }
                }
            }
            Image out (Image::ARGB, w, h, true);
            {
                const Image::BitmapData bp (plate, Image::BitmapData::readOnly);
                Image::BitmapData bo (out, Image::BitmapData::writeOnly);
                const float strength = (float) (0.55 * lift);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const PixelARGB pp = *(const PixelARGB*) bp.getPixelPointer (x, y);
                        const float pa = (float) pp.getAlpha() / 255.0f;
                        const float sa = sh[(size_t) (y * w + x)] * strength * (1.0f - pa);
                        PixelARGB o;   // prémultiplié : l'ombre noire n'ajoute que de l'alpha
                        o.setARGB ((uint8) jlimit (0, 255, roundToInt ((pa + sa) * 255.0f)), pp.getRed(), pp.getGreen(), pp.getBlue());
                        *(PixelARGB*) bo.getPixelPointer (x, y) = o;
                    }
            }
            return out;
        }

        static PixelARGB sample (const Image::BitmapData& bd, int w, int h, float x, float y, float gain)
        {
            const int x0 = (int) std::floor (x), y0 = (int) std::floor (y);
            const float fx = x - (float) x0, fy = y - (float) y0;
            auto px = [&] (int xx, int yy) -> PixelARGB
            {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) { PixelARGB z; z.setARGB (0, 0, 0, 0); return z; }
                return *(const PixelARGB*) bd.getPixelPointer (xx, yy);
            };
            const PixelARGB a = px (x0, y0), b = px (x0 + 1, y0), c = px (x0, y0 + 1), d = px (x0 + 1, y0 + 1);
            auto mix = [&] (int ca, int cb, int cc, int cd, float k)
            {
                const float t = (float) ca + ((float) cb - (float) ca) * fx, u = (float) cc + ((float) cd - (float) cc) * fx;
                return (uint8) jlimit (0, 255, roundToInt ((t + (u - t) * fy) * k));
            };
            PixelARGB o;   // pixels prémultipliés : la couleur ne peut pas dépasser l'alpha
            const uint8 al = mix (a.getAlpha(), b.getAlpha(), c.getAlpha(), d.getAlpha(), 1.0f);
            o.setARGB (al, jmin (al, mix (a.getRed(), b.getRed(), c.getRed(), d.getRed(), gain)),
                       jmin (al, mix (a.getGreen(), b.getGreen(), c.getGreen(), d.getGreen(), gain)),
                       jmin (al, mix (a.getBlue(), b.getBlue(), c.getBlue(), d.getBlue(), gain)));
            return o;
        }

        Image down, src;
        Rectangle<int> box { 0, 0, kW, kH };
        float scale = -1.0f;
        std::array<Image, kFrames> frames;
    };

    //==========================================================================
    // menu de taille de fenêtre (identique à Ring Think / Wharry)
    bool isDefaultSize (const CrycatEditor* ed) { return ed != nullptr && std::abs ((float) ed->getHeight() / kRefH - kDefaultScale) < 0.02f; }

    void addSizeItems (PopupMenu& m, const CrycatEditor* ed)
    {
        const int cur = ed != nullptr ? ed->getScaleIndex() : -1;
        m.addItem (904, "Default (same height as Ring Think)", true, isDefaultSize (ed));
        m.addItem (902, "75 %", true, cur == 1);
        m.addItem (903, "100 %", true, cur == 2);
    }

    void handleSizeChoice (int r, CrycatEditor* ed)
    {
        if (ed == nullptr || r < 902 || r > 904) return;
        ed->setScaleIndex (r == 904 ? -1 : r - 901);
    }

    Point<float> rangeNumberPos (int i)
    {
        const float a = degreesToRadians (kRangeA0 + kRangeSpan * (float) i / (float) (crycat::kNumRanges - 1));
        return kKnob + Point<float> (std::sin (a), -std::cos (a)) * (kKnobR + 24.0f);
    }
}

//==============================================================================
class PedalView : public Component, private Timer
{
public:
    explicit PedalView (CrycatProcessor& p) : proc (p)
    {
        bg.src    = loadPng (BinaryData::background_png, BinaryData::background_pngSize);
        fsOn.src  = loadPng (BinaryData::fs_on_png, BinaryData::fs_on_pngSize);
        fsOff.src = loadPng (BinaryData::fs_off_png, BinaryData::fs_off_pngSize);
        tgL.src   = loadPng (BinaryData::toggle_l_png, BinaryData::toggle_l_pngSize);
        tgR.src   = loadPng (BinaryData::toggle_r_png, BinaryData::toggle_r_pngSize);
        treadle.setSource (loadPng (BinaryData::treadle_down_png, BinaryData::treadle_down_pngSize));
        typeface = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
        typefaceBold = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
        setOpaque (true);
        startTimerHz (30);
    }

    void resized() override { s = (float) getWidth() / (float) kW; repaint(); }

    void paint (Graphics& g) override
    {
        const float ps = s * physScale();
        g.drawImage (bg.get (ps), getLocalBounds().toFloat());

        // ---- plateau (deux positions voisines fondues -> mouvement continu) : talon = pointe levée ----
        treadle.prepare (ps);
        const float fpos = jlimit (0.0f, 1.0f, drawnPedal) * (float) (kFrames - 1);
        const int f0 = jlimit (0, kFrames - 1, (int) std::floor (fpos)), f1 = jmin (kFrames - 1, f0 + 1);
        const float fr = fpos - (float) f0;
        const auto tb = r (treadle.designBounds().toFloat());
        g.setOpacity (1.0f);
        g.drawImage (treadle.frame (f0), tb);
        if (fr > 0.02f && f1 != f0) { g.setOpacity (fr); g.drawImage (treadle.frame (f1), tb); g.setOpacity (1.0f); }

        // ---- sélecteur RANGE : repère gravé + numéro choisi éclairé en ambre ----
        {
            const float ang = rangeAngle (drawnRange);
            const Point<float> dir (std::sin (ang), -std::cos (ang));
            const auto c = r (kKnob);
            const auto a = c + dir * (kCapR * 0.28f * s), b = c + dir * (kCapR * 0.88f * s);
            g.setColour (Colours::white.withAlpha (0.5f)); g.drawLine ({ a.translated (-0.8f * s, -0.8f * s), b.translated (-0.8f * s, -0.8f * s) }, 2.6f * s);
            g.setColour (Colour (0xe0201a1a)); g.drawLine ({ a, b }, 3.4f * s);
            // pointe du repère sur la jupe moletée
            const auto t0 = c + dir * (kCapR * 1.02f * s), t1 = c + dir * (kKnobR * 0.97f * s);
            g.setColour (kInk.withAlpha (0.9f)); g.drawLine ({ t0, t1 }, 3.0f * s);

            const auto np = r (rangeNumberPos (drawnRange));
            g.setGradientFill (ColourGradient (kAmber.withAlpha (0.35f), np.x, np.y, kAmber.withAlpha (0.0f), np.x + 18.0f * s, np.y, true));
            g.fillEllipse (np.x - 18.0f * s, np.y - 18.0f * s, 36.0f * s, 36.0f * s);
            g.setColour (kAmberHi);
            g.setFont (Font (FontOptions (typeface).withHeight (17.0f * 1.36f * s)));
            g.drawText (String (drawnRange + 1), Rectangle<float> (np.x - 15.0f * s, np.y - 15.0f * s, 30.0f * s, 30.0f * s), Justification::centred);
        }

        // ---- Q / PURR / BOOST ----
        for (int i = 0; i < 3; ++i)
        {
            const float ang = degreesToRadians (-135.0f + 270.0f * drawnSmall[i]);
            const Point<float> dir (std::sin (ang), -std::cos (ang));
            const auto c = r (kSmall[i]);
            g.setColour (Colours::white.withAlpha (0.45f));
            g.drawLine ({ c + dir * (kSmallCapR * 0.30f * s) + Point<float> (-0.7f, -0.7f) * s, c + dir * (kSmallCapR * 0.90f * s) + Point<float> (-0.7f, -0.7f) * s }, 2.0f * s);
            g.setColour (Colour (0xe0201a1a));
            g.drawLine ({ c + dir * (kSmallCapR * 0.30f * s), c + dir * (kSmallCapR * 0.90f * s) }, 2.8f * s);
            g.setColour (kInk.withAlpha (0.85f));
            g.drawLine ({ c + dir * (kSmallCapR * 1.04f * s), c + dir * (kSmallR * 0.96f * s) }, 2.4f * s);
        }

        // ---- inverseur MAN / AUTO ----
        g.drawImage ((drawnAuto ? tgR : tgL).get (ps), r (kToggle));

        // ---- footswitch + LED ambre ----
        {
            auto fr2 = r (kFs);
            if (pressed) fr2 = fr2.withSizeKeepingCentre (fr2.getWidth() * 0.965f, fr2.getHeight() * 0.965f).translated (0, 1.5f * s);
            g.drawImage ((drawnOn ? fsOn : fsOff).get (ps), fr2);
            drawLed (g, kFsLed, drawnOn ? 1.0f : (drawnArmed ? 0.18f : 0.0f));
        }

        drawScreen (g);
    }

    // ------------------------------------------------------------ souris
    void mouseDown (const MouseEvent& e) override
    {
        const auto p = e.position / s;
        if (e.mods.isPopupMenu()) { showMenu (p); return; }
        drag = Drag::None;
        if (kScreen.contains (p)) { showPresetMenu(); return; }
        for (int i = 0; i < crycat::kNumRanges; ++i)          // clic sur un numéro : plage directe
            if (p.getDistanceFrom (rangeNumberPos (i)) < 15.0f) { proc.setParamValue ("range", (float) i); return; }
        if (p.getDistanceFrom (kKnob) < kKnobR) { drag = Drag::Knob; dragStartY = p.y; dragStartRange = currentRange(); return; }
        for (int i = 0; i < 3; ++i)
            if (p.getDistanceFrom (kSmall[i]) < kSmallR * 1.15f)
            {
                drag = (Drag) ((int) Drag::Small0 + i); dragStartY = p.y;
                if (auto* par = proc.apvts.getParameter (kSmallId[i])) { dragStartVal = par->getValue(); par->beginChangeGesture(); }
                return;
            }
        if (kToggleHit.contains (p)) { proc.togglePedalSwitch ("auto"); return; }
        if (kFs.getCentre().getDistanceFrom (p) < kFs.getWidth() * 0.45f) { pressed = true; proc.togglePedalSwitch ("active"); repaint (r (kFs).toNearestInt().expanded (4)); return; }
        if (kPlate.contains (p))
        {
            drag = Drag::Pedal; dragStartY = p.y;
            dragStartVal = proc.apvts.getRawParameterValue ("pedal")->load();
            if (auto* par = proc.apvts.getParameter ("pedal")) par->beginChangeGesture();
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const auto p = e.position / s;
        if (drag == Drag::Knob)
        {
            const int nr = jlimit (0, crycat::kNumRanges - 1, dragStartRange + (int) std::lround ((dragStartY - p.y) / 22.0f));
            if (nr != currentRange()) proc.setParamValue ("range", (float) nr);
        }
        else if (drag == Drag::Pedal)
        {
            // tirer vers le bas = appuyer sur la pointe (son aigu)
            const float v = jlimit (0.0f, 1.0f, dragStartVal + (p.y - dragStartY) / (kPlate.getHeight() * 0.7f));
            if (auto* par = proc.apvts.getParameter ("pedal")) par->setValueNotifyingHost (v);
        }
        else if (drag >= Drag::Small0)
        {
            const float v = jlimit (0.0f, 1.0f, dragStartVal + (dragStartY - p.y) / 220.0f);
            if (auto* par = proc.apvts.getParameter (kSmallId[(int) drag - (int) Drag::Small0])) par->setValueNotifyingHost (v);
        }
    }

    void mouseUp (const MouseEvent&) override
    {
        if (drag == Drag::Pedal) if (auto* par = proc.apvts.getParameter ("pedal")) par->endChangeGesture();
        if (drag >= Drag::Small0) if (auto* par = proc.apvts.getParameter (kSmallId[(int) drag - (int) Drag::Small0])) par->endChangeGesture();
        drag = Drag::None;
        if (pressed) { pressed = false; repaint (r (kFs).toNearestInt().expanded (4)); }
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const auto p = e.position / s;
        if (kPlate.contains (p))              // double-clic sur le plateau : talon / pointe
            proc.setParamValue ("pedal", proc.apvts.getRawParameterValue ("pedal")->load() > 0.5f ? 0.0f : 1.0f);
        for (int i = 0; i < 3; ++i)           // double-clic sur un bouton : valeur par défaut
            if (p.getDistanceFrom (kSmall[i]) < kSmallR * 1.15f)
                if (auto* par = proc.apvts.getParameter (kSmallId[i])) { par->beginChangeGesture(); par->setValueNotifyingHost (par->getDefaultValue()); par->endChangeGesture(); }
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const auto p = e.position / s;
        if (p.getDistanceFrom (kKnob) < kKnobR)
            proc.setParamValue ("range", (float) jlimit (0, crycat::kNumRanges - 1, currentRange() + (w.deltaY > 0 ? 1 : -1)));
        else if (kPlate.contains (p))
            proc.setParamValue ("pedal", jlimit (0.0f, 1.0f, proc.apvts.getRawParameterValue ("pedal")->load() - w.deltaY * 0.25f));
        else
            for (int i = 0; i < 3; ++i)
                if (p.getDistanceFrom (kSmall[i]) < kSmallR * 1.15f)
                    if (auto* par = proc.apvts.getParameter (kSmallId[i]))
                    { par->beginChangeGesture(); par->setValueNotifyingHost (jlimit (0.0f, 1.0f, par->getValue() + w.deltaY * 0.1f)); par->endChangeGesture(); }
    }

private:
    enum class Drag { None, Knob, Pedal, Small0, Small1, Small2 };

    Rectangle<float> r (Rectangle<float> d) const { return d * s; }
    Point<float> r (Point<float> d) const { return d * s; }
    float physScale() const
    {
        if (auto* d = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) return jmax (1.0f, (float) d->scale);
        return 1.0f;
    }
    int currentRange() const { return jlimit (0, crycat::kNumRanges - 1, (int) std::lround (proc.apvts.getRawParameterValue ("range")->load())); }
    bool param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }
    static float rangeAngle (int i)
    {
        i = jlimit (0, crycat::kNumRanges - 1, i);
        return degreesToRadians (kRangeA0 + kRangeSpan * (float) i / (float) (crycat::kNumRanges - 1));
    }

    // LED : bille sombre éteinte, cœur blanc chaud + halo ambré allumée
    void drawLed (Graphics& g, Point<float> c0, float level)
    {
        const auto c = r (c0);
        const float rr = kLedR * s;
        g.setGradientFill (ColourGradient (Colour (0xff5d5250), c.x - rr * 0.35f, c.y - rr * 0.4f, Colour (0xff120d0c), c.x + rr, c.y + rr, true));
        g.fillEllipse (c.x - rr, c.y - rr, 2 * rr, 2 * rr);
        if (level > 0.01f)
        {
            g.setGradientFill (ColourGradient (kAmber.withAlpha (0.55f * level), c.x, c.y, kAmber.withAlpha (0.0f), c.x + rr * 3.8f, c.y, true));
            g.fillEllipse (c.x - rr * 3.8f, c.y - rr * 3.8f, rr * 7.6f, rr * 7.6f);
            g.setGradientFill (ColourGradient (Colour (0xfffff3dc).withAlpha (level), c.x, c.y, kAmber.withAlpha (level), c.x + rr, c.y, true));
            g.fillEllipse (c.x - rr * 0.95f, c.y - rr * 0.95f, rr * 1.9f, rr * 1.9f);
        }
    }

    // écran ambre : plage + état, courbe de réponse en direct, fréquence centrale, course de la pédale, ligne d'état
    void drawScreen (Graphics& g)
    {
        const auto sc = r (kScreen);
        g.setColour (kScreenBg); g.fillRoundedRectangle (sc, 4.0f * s);
        auto st = proc.currentSettings();
        const float pos = jlimit (0.0f, 1.0f, drawnPedal);
        const float alpha = drawnOn ? 1.0f : 0.42f;
        auto area = sc.reduced (8.0f * s);
        const float fh = sc.getHeight();

        // ligne du haut : plage + état
        g.setFont (Font (FontOptions (typefaceBold).withHeight (fh * 0.064f)));
        auto top = area.removeFromTop (fh * 0.08f);
        g.setColour (kAmber.withAlpha (0.9f * jmax (alpha, 0.6f)));
        g.drawText ("RANGE " + String (drawnRange + 1) + "  " + crycat::range (drawnRange).label, top, Justification::centredLeft);
        String state = drawnOn ? "ON" : "BYPASS";
        if (drawnAuto && param ("active")) state = drawnOn ? "AUTO ON" : "AUTO";
        g.drawText (state, top, Justification::centredRight);
        area.removeFromTop (fh * 0.02f);
        g.setColour (kAmber.withAlpha (0.22f)); g.fillRect (area.getX(), area.getY(), area.getWidth(), jmax (1.0f, 0.8f * s));
        area.removeFromTop (fh * 0.03f);

        // courbe de réponse : 80 Hz .. 5 kHz (log), -24 .. +24 dB
        auto plot = area.removeFromTop (fh * 0.40f);
        const float f0 = 80.0f, f1 = 5000.0f, dbMax = 24.0f;
        auto xOf = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / f0) / std::log (f1 / f0); };
        auto yOf = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, db / dbMax); };
        {
            // plage de balayage (talon -> pointe)
            const auto& rg = crycat::range (drawnRange);
            g.setColour (kAmber.withAlpha (0.07f * alpha + 0.03f));
            g.fillRect (Rectangle<float>::leftTopRightBottom (xOf (rg.fLo), plot.getY(), xOf (rg.fHi), plot.getBottom()));
            // grille : 0 dB + décades
            g.setColour (kAmber.withAlpha (0.16f));
            g.fillRect (plot.getX(), yOf (0.0f), plot.getWidth(), jmax (1.0f, 0.7f * s));
            for (float f : { 100.0f, 1000.0f })
                g.fillRect (xOf (f), plot.getY(), jmax (1.0f, 0.7f * s), plot.getHeight());
            g.setColour (kAmber.withAlpha (0.12f));
            for (float f : { 200.0f, 500.0f, 2000.0f })
                g.fillRect (xOf (f), plot.getY(), jmax (1.0f, 0.6f * s), plot.getHeight());

            Path curve, fill;
            const int np = 90;
            for (int i = 0; i <= np; ++i)
            {
                const float f = f0 * std::pow (f1 / f0, (float) i / (float) np);
                const float x = xOf (f), y = yOf (crycat::magnitudeDb (st, pos, f));
                if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); }
                else curve.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (plot.getRight(), plot.getBottom()); fill.closeSubPath();
            g.setGradientFill (ColourGradient (kAmber.withAlpha (0.38f * alpha), 0, plot.getY(), kAmber.withAlpha (0.02f), 0, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (kAmberHi.withAlpha (0.95f * alpha));
            g.strokePath (curve, PathStrokeType (1.8f * s, PathStrokeType::curved, PathStrokeType::rounded));
            // repère de la fréquence centrale
            const float fc = crycat::centreHz (drawnRange, pos);
            const float xc = xOf (fc), yc = yOf (crycat::magnitudeDb (st, pos, fc));
            g.setColour (kAmber.withAlpha (0.45f * alpha));
            g.fillRect (xc - 0.5f * s, yc, jmax (1.0f, 1.0f * s), plot.getBottom() - yc);
            g.setColour (kAmberHi.withAlpha (alpha));
            g.fillEllipse (xc - 3.2f * s, yc - 3.2f * s, 6.4f * s, 6.4f * s);
        }
        area.removeFromTop (fh * 0.02f);

        // fréquence centrale en grand
        {
            const float fc = crycat::centreHz (drawnRange, pos);
            g.setFont (Font (FontOptions (typefaceBold).withHeight (fh * 0.13f)));
            g.setColour (Colour (0xfffff1dc).withAlpha (alpha));
            const String txt = fc >= 1000.0f ? String (fc / 1000.0f, 2) + " kHz" : String (roundToInt (fc)) + " Hz";
            g.drawText (txt, area.removeFromTop (fh * 0.14f), Justification::centred);
            g.setFont (Font (FontOptions (typeface).withHeight (fh * 0.06f)));
            g.setColour (kAmber.withAlpha (0.85f * alpha));
            g.drawText ("Q " + String (crycat::effectiveQ (st.q, pos), 1) + "   ·   BOOST +" + String (st.boost * 15.0f, 1) + " dB",
                        area.removeFromTop (fh * 0.075f), Justification::centred);
        }

        // ligne d'état (valeurs des réglages en direct, ou consigne MIDI)
        auto stRow = area.removeFromBottom (fh * 0.07f);
        g.setFont (Font (FontOptions (typeface).withHeight (fh * 0.052f)));
        const bool special = proc.getLearnTarget() != MidiTarget::None || proc.isCalibrating();
        g.setColour (kAmber.withAlpha (special ? 1.0f : 0.72f * jmax (alpha, 0.6f)));
        g.drawFittedText (drawnStatus, stRow.toNearestInt(), Justification::centred, 1, 0.5f);
        area.removeFromBottom (fh * 0.015f);

        // course de la pédale : barres du talon (gauche) à la pointe (droite)
        auto bars = area.reduced (0, fh * 0.012f);
        const int nb = 16;
        const float bw = bars.getWidth() / (float) nb;
        for (int i = 0; i < nb; ++i)
        {
            const float t = ((float) i + 0.5f) / (float) nb;
            const float hgt = bars.getHeight() * (0.3f + 0.7f * t);
            const bool lit = t <= pos + 0.001f;
            g.setColour (kAmber.withAlpha ((lit ? 0.9f : 0.16f) * alpha));
            g.fillRect (bars.getX() + (float) i * bw + bw * 0.16f, bars.getBottom() - hgt, bw * 0.68f, hgt);
        }
    }

    String statusText() const
    {
        if (proc.isCalibrating()) return "CALIBRATION : sweep the pedal heel <-> toe";
        const auto learn = proc.getLearnTarget();
        if (learn == MidiTarget::Pedal)      return "MIDI LEARN : move your expression pedal";
        if (learn == MidiTarget::AutoSwitch) return "MIDI LEARN : MAN / AUTO switch";
        if (learn == MidiTarget::Footswitch) return "MIDI LEARN : ON / OFF footswitch";
        auto pc = [this] (const char* id) { return String (roundToInt (proc.apvts.getParameter (id)->getValue() * 100.0f)) + " %"; };
        return "PEDAL " + String (roundToInt (proc.getPedalShown() * 100.0f)) + " %"
             + "  ·  Q " + pc ("q") + "  ·  PURR " + pc ("purr") + "  ·  BOOST " + pc ("boost");
    }

    void timerCallback() override
    {
        if (auto* ed = findParentComponentOfClass<CrycatEditor>()) ed->useSoftwareRenderer();
        proc.pollCalibration();

        const int rg = currentRange();
        const bool autoM = param ("auto"), on = proc.isEffectOn(), armed = autoM && param ("active");
        const float ped = proc.getPedalShown();
        const String st = statusText();
        bool screen = false;

        if (std::abs (ped - drawnPedal) > 0.002f)
        {
            drawnPedal = ped; screen = true;
            repaint (r (treadle.designBounds().toFloat()).toNearestInt().expanded (2));
        }
        if (rg != drawnRange)
        {
            const float h = kKnobR + 50.0f;
            repaint (r (Rectangle<float> (kKnob.x - h, kKnob.y - h, 2 * h, 2 * h)).toNearestInt().expanded (2));
            drawnRange = rg; screen = true;
        }
        if (autoM != drawnAuto || on != drawnOn || armed != drawnArmed)
        {
            drawnAuto = autoM; drawnOn = on; drawnArmed = armed; screen = true;
            repaint (r (kToggle).toNearestInt().expanded (3));
            repaint (r (kFs).toNearestInt().expanded (4));
            repaint (ledArea (kFsLed));
        }
        for (int i = 0; i < 3; ++i)
        {
            const float v = proc.apvts.getParameter (kSmallId[i])->getValue();
            if (std::abs (v - drawnSmall[i]) > 0.002f)
            {
                drawnSmall[i] = v; screen = true;      // Q et BOOST changent la courbe
                repaint (r (Rectangle<float> (kSmall[i].x - kSmallR, kSmall[i].y - kSmallR, 2 * kSmallR, 2 * kSmallR)).toNearestInt().expanded (2));
            }
        }
        if (st != drawnStatus) { drawnStatus = st; screen = true; }
        if (screen) repaint (r (kScreen).toNearestInt().expanded (2));
    }
    Rectangle<int> ledArea (Point<float> c) const { const float h = kLedR * 4.0f; return r (Rectangle<float> (c.x - h, c.y - h, 2 * h, 2 * h)).toNearestInt().expanded (1); }

    void showPresetMenu()
    {
        PopupMenu m, rgs, fac;
        const int cur = currentRange();
        for (int i = 0; i < crycat::kNumRanges; ++i) rgs.addItem (1 + i, crycat::range (i).name, true, i == cur);
        for (int i = 0; i < CrycatProcessor::kNumFactory; ++i) fac.addItem (101 + i, String (i + 1) + "  " + CrycatProcessor::factory (i).name);
        m.addSectionHeader ("Presets");
        m.addSubMenu ("Studio Wiga factory presets (PC 1-10)", fac);
        m.addSubMenu ("Sweep range", rgs);
        Component::SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (r (kScreen).toNearestInt())), [sp] (int res)
        {
            if (sp == nullptr || res == 0) return;
            if (res >= 101) sp->proc.loadFactory (res - 101);
            else sp->proc.setParamValue ("range", (float) (res - 1));
        });
    }

    void showMenu (Point<float> p)
    {
        auto* ed = findParentComponentOfClass<CrycatEditor>();
        PopupMenu m;
        const bool onFs = kFs.getCentre().getDistanceFrom (p) < kFs.getWidth() * 0.6f;
        const bool onToggle = kToggleHit.contains (p);
        if (onFs || onToggle)
        {
            const auto t = onToggle ? MidiTarget::AutoSwitch : MidiTarget::Footswitch;
            const auto d = proc.getMidiDescription (t);
            m.addSectionHeader (onToggle ? "MAN / AUTO switch" : "ON / OFF footswitch");
            m.addItem (onToggle ? 11 : 21, "MIDI learn" + (d.isNotEmpty() ? " (now: " + d + ")" : String()));
            m.addItem (onToggle ? 12 : 22, "Clear MIDI assignment", d.isNotEmpty());
            m.addSeparator();
            m.addSectionHeader ("MIDI switch messages");
            m.addItem (41, "Momentary (each press toggles)", true, ! param ("latch"));
            m.addItem (42, "Latching (127 = ON, 0 = OFF)", true, param ("latch"));
            m.addSeparator();
        }
        else
        {
            m.addSectionHeader ("Expression pedal (" + proc.getMidiDescription (MidiTarget::Pedal) + ")");
            m.addItem (31, "MIDI learn pedal controller");
            m.addItem (32, "Reset to CC 11");
            m.addItem (35, "Calibrate (sweep heel <-> toe for 6 s)");
            m.addItem (34, "Reset heel / toe calibration");
            m.addItem (33, "Invert pedal direction", true, param ("invert"));
            PopupMenu glide;
            const float gl = proc.apvts.getRawParameterValue ("smooth")->load();
            for (int i = 0; i < 6; ++i) { const int ms[6] = { 0, 10, 20, 40, 80, 150 }; glide.addItem (61 + i, String (ms[i]) + " ms", true, std::abs (gl - (float) ms[i]) < 1.0f); }
            m.addSubMenu ("Pedal glide (smoothing)", glide);
            m.addSeparator();
            m.addSectionHeader ("Auto-engage");
            PopupMenu rel;
            const float ao = proc.apvts.getRawParameterValue ("autooff")->load();
            for (int i = 0; i < 5; ++i) { const int ms[5] = { 150, 300, 400, 800, 1500 }; rel.addItem (71 + i, String (ms[i]) + " ms", true, std::abs (ao - (float) ms[i]) < 1.0f); }
            m.addSubMenu ("Switch off after resting on the heel", rel);
            m.addItem (53, "Respond to MIDI Program Change (PC 1-10)", true, param ("pc"));
            m.addSeparator();
        }
        m.addSectionHeader ("Plugin size");
        addSizeItems (m, ed);

        Component::SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp] (int res)
        {
            if (sp == nullptr || res == 0) return;
            auto& pr = sp->proc;
            const int glideMs[6] = { 0, 10, 20, 40, 80, 150 }, relMs[5] = { 150, 300, 400, 800, 1500 };
            switch (res)
            {
                case 11: pr.startMidiLearn (MidiTarget::AutoSwitch); break;
                case 12: pr.clearMidi (MidiTarget::AutoSwitch); break;
                case 21: pr.startMidiLearn (MidiTarget::Footswitch); break;
                case 22: pr.clearMidi (MidiTarget::Footswitch); break;
                case 31: pr.startMidiLearn (MidiTarget::Pedal); break;
                case 32: pr.clearMidi (MidiTarget::Pedal); break;
                case 33: pr.togglePedalSwitch ("invert"); break;
                case 34: pr.setParamValue ("heel", 0.0f); pr.setParamValue ("toe", 1.0f); break;
                case 35: pr.startCalibration(); break;
                case 41: pr.setParamValue ("latch", 0.0f); break;
                case 42: pr.setParamValue ("latch", 1.0f); break;
                case 53: pr.togglePedalSwitch ("pc"); break;
                default:
                    if (res >= 61 && res <= 66) pr.setParamValue ("smooth", (float) glideMs[res - 61]);
                    else if (res >= 71 && res <= 75) pr.setParamValue ("autooff", (float) relMs[res - 71]);
                    else handleSizeChoice (res, sp->findParentComponentOfClass<CrycatEditor>());
                    break;
            }
        });
    }

    CrycatProcessor& proc;
    ScaledCache bg, fsOn, fsOff, tgL, tgR;
    TreadleWarp treadle;
    Typeface::Ptr typeface, typefaceBold;
    float s = kDefaultScale * kRefH / (float) kH;

    Drag drag = Drag::None;
    float dragStartY = 0, dragStartVal = 0;
    int dragStartRange = 0;
    bool pressed = false;

    float drawnPedal = -1.0f, drawnSmall[3] = { -1.0f, -1.0f, -1.0f };
    int drawnRange = -1;
    bool drawnAuto = false, drawnOn = true, drawnArmed = false;
    String drawnStatus;
};

//==============================================================================
CrycatEditor::CrycatEditor (CrycatProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    view = std::make_unique<PedalView> (proc);
    addAndMakeVisible (*view);
    setResizable (true, true);
    const int minH = (int) std::lround (kRefH * kDefaultScale);          // jamais plus petit que la taille par défaut
    const int maxH = 1120;
    setResizeLimits ((int) std::lround ((double) minH * kW / kH), minH, (int) std::lround ((double) maxH * kW / kH), maxH);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setScaleIndex (jlimit (-1, 2, proc.uiScaleIdx.load()));
}

CrycatEditor::~CrycatEditor() = default;

// Anti-gel de la fenêtre (Windows) : JUCE 8 dessine par défaut avec Direct2D ; avec certains pilotes (Intel)
// la fenêtre cesse de se rafraîchir alors que le DAW continue. Le moteur logiciel est stable partout.
void CrycatEditor::useSoftwareRenderer()
{
   #if JUCE_WINDOWS
    if (auto* peer = getPeer())
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void CrycatEditor::parentHierarchyChanged() { useSoftwareRenderer(); }

void CrycatEditor::resized()
{
    // tolérance de 2 px (mise à l'échelle Windows 125 / 150 %) : les limites du constrainer suffisent, pas de boucle avec l'hôte
    view->setBounds (getLocalBounds());
}

int CrycatEditor::getScaleIndex() const
{
    const float sc = (float) getHeight() / kRefH;
    for (int i = 1; i < 3; ++i) if (std::abs (sc - kScales[i]) < 0.02f) return i;
    return -1;
}

void CrycatEditor::setScaleIndex (int idx)
{
    idx = jlimit (-1, 2, idx);
    if (idx == 0) idx = -1;
    proc.uiScaleIdx = idx;
    const float sc = idx < 0 ? kDefaultScale : kScales[idx];
    const int h = (int) std::lround (kRefH * sc);
    setSize ((int) std::lround ((double) h * kW / kH), h);
}
