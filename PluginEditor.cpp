#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

namespace
{
    // Repère du dessin : 911 x 928 (design de l'utilisateur, tools/layout.txt). Les images sont livrées en 2x (HiDPI).
    // Format double : mêmes hauteurs de fenêtre que Ring Think (495 / 557 / 743 px), largeur selon les proportions de la pédale
    // -> 486 x 495 par défaut et au minimum.
    constexpr int kW = 911, kH = 928;
    constexpr float kAsset = 2.0f;                               // résolution des images / repère
    constexpr float kRefH = 743.0f;                              // hauteur « 100 % » (= Ring Think à 100 %)
    const float kScales[3] = { 495.0f / 743.0f, 0.75f, 1.0f };
    constexpr float kDefaultScale = 495.0f / 743.0f;             // fenêtre par défaut (= taille minimale)

    // ---- commandes (tools/layout.txt) ----
    const Point<float> kKnob (565.6f, 296.8f), kKnobCap (565.6f, 296.1f);  constexpr float kKnobR = 55.1f, kCapR = 40.7f;   // RANGE
    const Point<float> kNumPos[6] = { { 503.6f, 343.3f }, { 491.0f, 279.2f }, { 528.5f, 227.0f }, { 591.2f, 226.2f }, { 632.6f, 283.2f }, { 625.0f, 346.9f } };
    const Point<float> kSmall[3]    = { { 562.0f, 522.4f }, { 672.9f, 520.9f }, { 783.2f, 523.1f } };     // Q, PURR, BOOST (jupes)
    const Point<float> kSmallCap[3] = { { 563.4f, 524.5f }, { 674.7f, 524.2f }, { 784.6f, 525.6f } };     // chapeaux (vus un peu en biais)
    constexpr float kSmallR = 40.7f, kSmallCapR = 29.9f;
    const char* const kSmallId[3] = { "q", "purr", "boost" };
    const Rectangle<float> kScreen (656.0f, 199.2f, 196.7f, 226.2f);
    constexpr float kScreenRadius = 6.5f;
    // commandes rendues par tools/controls_art.py (tools/controls_layout.txt)
    // footswitch d'ECHOFLEX (photo réelle, mise à l'échelle 0.8657) : même comportement qu'Echoflex
    const Rectangle<float> kFs (586.0f, 636.8f, 170.0f, 170.0f);           // footswitch (sprite, ombre comprise)
    const Point<float> kFsC (671.0f, 721.8f);
    constexpr float kFsHitR = 47.6f;                                        // = footR d'Echoflex (55) x 0.8657
    constexpr float kFsPressR = 25.7f;                                      // disque assombri pendant l'appui (Echoflex : footR x 0.54)
    const Point<float> kFsLed (772.4f, 724.8f);
    const Rectangle<float> kLedGlow (694.4f, 646.8f, 156.0f, 156.0f);      // façade éclairée par la LED
    const Rectangle<float> kToggle (753.4f, 101.7f, 84.0f, 60.0f);         // inverseur MAN / AUTO (sprite, ombre comprise)
    const Rectangle<float> kToggleHit (722.0f, 98.0f, 160.0f, 62.0f);
    constexpr float kLedLensR = 5.8f, kLedGapR = 7.2f, kLedRingR = 9.4f;   // LED 3 mm : lentille, gorge, bague chromée
    constexpr float kLedR = kLedRingR;
    const Colour kInk (0xffe2e0de), kAmber (0xffffb040), kAmberHi (0xffffd690), kScreenBg (0xff0e0a07);
    constexpr int kFrames = 33;                                             // positions précalculées du plateau

    // plateau à plat (orteil enfoncé, tel que dans le design) ; il pivote autour de la charnière, côté talon
    constexpr float kPlateL = 65.9f, kPlateR = 473.0f, kPlateTop = 24.9f, kHingeY = 870.4f;
    const Rectangle<float> kPlate (kPlateL, kPlateTop, kPlateR - kPlateL, kHingeY - kPlateTop);
    constexpr float kRise = 18.0f, kWiden = 1.02f;                          // orteil levé : pointe plus haute et plus large (perspective)
    constexpr float kThick = 7.0f;                                          // épaisseur visible du plateau (tranches)

    Image loadPng (const void* d, int n) { return ImageFileFormat::loadFrom (d, (size_t) n).convertedToFormat (Image::ARGB); }

    // redimensionne une fois (par le timer, jamais dans paint), puis garde en cache tant que la taille ne change pas
    struct ScaledCache
    {
        Image src, img; float scale = -1.0f;
        void prepare (float s)           // s = pixels écran par unité du repère
        {
            if (std::abs (s - scale) <= 1.0e-4f && img.isValid()) return;
            scale = s;
            const float k = s / kAsset;
            const int w = jmax (1, roundToInt ((float) src.getWidth() * k)), h = jmax (1, roundToInt ((float) src.getHeight() * k));
            img = src.rescaled (w, h, Graphics::highResamplingQuality);
        }
        // anti-gel v2 : le cache s'il existe, sinon l'image d'origine en attendant le timer
        const Image& get() const { return img.isValid() ? img : src; }
    };

    //==========================================================================
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

    //==========================================================================
    // Plateau en mouvement. Chaque position est une perspective exacte du calque « orteil enfoncé », avec :
    //   - la face du plateau qui s'assombrit en se relevant (elle s'écarte de la lumière, en haut à gauche) et un reflet
    //     qui glisse sur le chrome pendant la bascule,
    //   - les tranches visibles quand la pointe monte (tranche de pointe et tranche gauche éclairées, tranche droite dans l'ombre),
    //   - une ombre portée qui s'allonge et s'adoucit avec la hauteur (nulle à la charnière, maximale à la pointe),
    //   - la charnière : le talon s'enfonce dans son logement, une fente sombre s'ouvre et l'axe accroche la lumière.
    class TreadleWarp
    {
    public:
        void setSource (Image downImg)
        {
            down = std::move (downImg);
            const float cx = 0.5f * (kPlateL + kPlateR);
            box = Rectangle<float> (cx - (cx - kPlateL) * kWiden - kThick - 6.0f, jmax (0.0f, kPlateTop - kRise - kThick - 4.0f),
                                    (kPlateR - kPlateL) * kWiden + 2.0f * kThick + 34.0f, 0.0f);
            box = box.withBottom (kHingeY + 34.0f).getSmallestIntegerContainer().toFloat().getIntersection ({ 0.0f, 0.0f, (float) kW, (float) kH });
            scale = -1.0f;
        }
        Rectangle<float> designBounds() const { return box; }

        void prepare (float s)
        {
            if (std::abs (s - scale) < 1.0e-4f) return;
            scale = s;
            const int bw = jmax (1, roundToInt (box.getWidth() * s)), bh = jmax (1, roundToInt (box.getHeight() * s));
            src = down.getClippedImage ((box * kAsset).getSmallestIntegerContainer()).rescaled (bw, bh, Graphics::highResamplingQuality)
                      .convertedToFormat (Image::ARGB);
            // anti-gel v2 : les anciennes positions restent affichables en attendant les nouvelles (pas de saut visuel)
            for (size_t i = 0; i < frames.size(); ++i) { if (frames[i].isValid()) oldFrames[i] = frames[i]; frames[i] = {}; }
        }

        // t = 0 : orteil levé (talon enfoncé), t = 1 : orteil enfoncé
        // Anti-gel v2 : le dessin n'utilise que des positions déjà calculées (la plus proche, ou celle de l'ancienne
        // taille) ; aucune n'est calculée dans paint. Renvoie nullptr si rien n'est encore prêt.
        const Image* frame (int i) const
        {
            i = jlimit (0, kFrames - 1, i);
            for (auto* set : { &frames, &oldFrames })
                for (int d = 0; d < kFrames; ++d)
                {
                    if (i - d >= 0 && (*set)[(size_t) (i - d)].isValid()) return &(*set)[(size_t) (i - d)];
                    if (i + d < kFrames && (*set)[(size_t) (i + d)].isValid()) return &(*set)[(size_t) (i + d)];
                }
            return nullptr;
        }
        bool isReady (int i) const { return frames[(size_t) jlimit (0, kFrames - 1, i)].isValid(); }
        // en attendant la première position : le calque d'origine (orteil enfoncé), sans calcul
        void drawOriginal (Graphics& g, Rectangle<float> dest) const
        {
            const auto sr = (box * kAsset).getSmallestIntegerContainer();
            g.drawImage (down, roundToInt (dest.getX()), roundToInt (dest.getY()), roundToInt (dest.getWidth()), roundToInt (dest.getHeight()),
                         sr.getX(), sr.getY(), sr.getWidth(), sr.getHeight());
        }

        // calcule à l'avance une position manquante, en partant de la position actuelle (appelé par le timer)
        bool buildNextMissing (int around)
        {
            if (scale < 0.0f) return false;
            around = jlimit (0, kFrames - 1, around);
            for (int d = 0; d < kFrames; ++d)
                for (int i : { around - d, around + d })
                    if (i >= 0 && i < kFrames && ! frames[(size_t) i].isValid())
                    { frames[(size_t) i] = build ((float) i / (float) (kFrames - 1)); return true; }
            for (auto& f : oldFrames) f = {};      // toutes les positions sont prêtes : on libère les anciennes
            return false;
        }

    private:
        static uint8 u8 (float v) { return (uint8) jlimit (0, 255, roundToInt (v)); }

        Image build (float t) const
        {
            const int w = src.getWidth(), h = src.getHeight();
            const double sx = (double) w / box.getWidth(), sy = (double) h / box.getHeight();
            auto loc = [&] (double x, double y) { return Point<double> ((x - box.getX()) * sx, (y - box.getY()) * sy); };
            // inclinaison (comme Wharry) : 1 = pointe levée, 0 = plateau à plat (pédale à 80 %), < 0 = pointe enfoncée au-delà du plat
            const double sTilt = jlimit (-0.25, 1.0, (0.8 - (double) t) / 0.8);
            const double cx = 0.5 * (kPlateL + kPlateR), lift = jmax (0.0, sTilt);
            const double wid = 1.0 + (kWiden - 1.0) * lift, rise = kRise * lift;
            const Point<double> srcQ[4] = { loc (kPlateL, kPlateTop), loc (kPlateR, kPlateTop), loc (kPlateR, kHingeY), loc (kPlateL, kHingeY) };
            const Point<double> dstQ[4] = { loc (cx - (cx - kPlateL) * wid, kPlateTop - rise), loc (cx + (kPlateR - cx) * wid, kPlateTop - rise),
                                            loc (kPlateR, kHingeY), loc (kPlateL, kHingeY) };
            Homography H;
            const bool warp = lift > 1.0e-4 && H.solve (dstQ, srcQ);

            // ---------- 1) face du plateau en perspective ----------
            Image plate (Image::ARGB, w, h, true);
            {
                const Image::BitmapData bs (src, Image::BitmapData::readOnly);
                Image::BitmapData bo (plate, Image::BitmapData::writeOnly);
                const double topY = dstQ[0].y, botY = dstQ[3].y, span = jmax (1.0, botY - topY);
                // reflet : bande douce et légèrement oblique (lumière en haut à gauche) qui glisse le long du plateau
                const double bandC = 0.10 + 0.55 * (double) t;                 // 0 = pointe, 1 = charnière
                const double bandW = 0.16, bandK = 0.16 + 0.10 * lift;
                for (int y = 0; y < h; ++y)
                {
                    const double gy = jlimit (0.0, 1.0, (botY - (double) y) / span);   // 1 = pointe, 0 = charnière
                    // diffus : la pointe levée se tourne vers la lumière, enfoncée elle s'en détourne ; creux sombre près de la charnière
                    const float gain = (float) (1.0 + 0.13 * sTilt * std::pow (gy, 1.3) - 0.07 * lift * std::pow (1.0 - gy, 5.0));
                    for (int x = 0; x < w; ++x)
                    {
                        double u = x, v = y;
                        if (warp) { const auto q = H.map (x + 0.5, y + 0.5); u = q.x - 0.5; v = q.y - 0.5; }
                        if (u < -1.0 || v < -1.0 || u > w || v > h) continue;
                        const PixelARGB p = sample (bs, w, h, (float) u, (float) v);
                        if (p.getAlpha() == 0) continue;
                        const double xr = (double) x / (double) w;
                        const double bpos = (1.0 - gy) + 0.22 * (xr - 0.5);
                        const float bnd = (float) (bandK * std::exp (-0.5 * std::pow ((bpos - bandC) / bandW, 2.0)));
                        const float a = (float) p.getAlpha();
                        auto ch = [&] (uint8 c)
                        {
                            const float v1 = jmin (a, (float) c * gain);
                            return u8 (v1 + (a - v1) * bnd);                                    // le reflet éclaircit vers le blanc
                        };
                        PixelARGB o; o.setARGB (p.getAlpha(), ch (p.getRed()), ch (p.getGreen()), ch (p.getBlue()));
                        *(PixelARGB*) bo.getPixelPointer (x, y) = o;
                    }
                }
            }

            // ---------- 2) tranches, extrudées depuis la silhouette réelle du plateau (coins arrondis compris) ----------
            Image out (Image::ARGB, w, h, true);
            {
                const Image::BitmapData bp (plate, Image::BitmapData::readOnly);
                Image::BitmapData bo (out, Image::BitmapData::readWrite);
                auto alphaAt = [&] (int x, int y) { return ((const PixelARGB*) bp.getPixelPointer (x, y))->getAlpha(); };
                auto put = [&] (int x, int y, float a, int r, int g, int b)
                {
                    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
                    auto* px = (PixelARGB*) bo.getPixelPointer (x, y);
                    const float k = jmin (1.0f, a);
                    PixelARGB o;   // prémultiplié
                    o.setARGB (u8 (255.0f * k + px->getAlpha() * (1.0f - k)), u8 (r * k + px->getRed() * (1.0f - k)),
                               u8 (g * k + px->getGreen() * (1.0f - k)), u8 (b * k + px->getBlue() * (1.0f - k)));
                    *px = o;
                };
                const double th = kThick * sy * lift;                        // épaisseur apparente à la pointe
                const double topY = dstQ[0].y, botY = dstQ[3].y;
                // tranche de pointe : tournée vers le haut, elle accroche la lumière
                for (int x = 0; x < w; ++x)
                {
                    int yt = -1;
                    for (int y = 0; y < h; ++y) if (alphaAt (x, y) >= 128) { yt = y; break; }
                    if (yt < 0 || yt > topY + 40.0 * sy) continue;
                    const double ux = (double) x / (double) w;
                    const double fh = th * jlimit (0.0, 1.0, 1.0 - ((double) yt - topY) / (34.0 * sy));
                    for (int k = 1; k <= (int) std::ceil (fh) + 1; ++k)
                    {
                        const float cov = (float) jlimit (0.0, 1.0, fh - (k - 1) + 0.3);
                        const double u = (double) k / jmax (0.5, fh);
                        const int base = (int) (208 - 100 * u - 28 * ux);
                        put (x, yt - k, cov, base + 3, base + 2, base + 5);
                    }
                }
                // tranches latérales : épaisses à la pointe, nulles à la charnière ; gauche éclairée, droite dans l'ombre
                for (int y = 0; y < h; ++y)
                {
                    const double v = jlimit (0.0, 1.0, ((double) y - topY) / jmax (1.0, botY - topY));
                    const double fw = 0.55 * th * (1.0 - v);
                    if (fw < 0.25) continue;
                    int xl = -1, xr = -1;
                    for (int x = 0; x < w; ++x) if (alphaAt (x, y) >= 128) { xl = x; break; }
                    for (int x = w - 1; x >= 0; --x) if (alphaAt (x, y) >= 128) { xr = x; break; }
                    if (xl < 0) continue;
                    for (int k = 1; k <= (int) std::ceil (fw) + 1; ++k)
                    {
                        const float cov = (float) jlimit (0.0, 1.0, fw - (k - 1) + 0.3);
                        const double u = (double) k / jmax (0.5, fw);
                        const int bl = (int) (178 - 72 * u - 25 * v), br = (int) (64 - 24 * u + 10 * v);
                        put (xl - k, y, cov, bl + 4, bl + 3, bl + 6);
                        put (xr + k, y, cov, br + 4, br + 3, br + 6);
                    }
                }
                // face du plateau posée sur les tranches
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const PixelARGB o = *(const PixelARGB*) bp.getPixelPointer (x, y);
                        if (o.getAlpha() == 0) continue;
                        auto* px = (PixelARGB*) bo.getPixelPointer (x, y);
                        const float ka = (float) o.getAlpha() / 255.0f;
                        PixelARGB m;
                        m.setARGB (u8 (o.getAlpha() + px->getAlpha() * (1.0f - ka)), u8 (o.getRed() + px->getRed() * (1.0f - ka)),
                                   u8 (o.getGreen() + px->getGreen() * (1.0f - ka)), u8 (o.getBlue() + px->getBlue() * (1.0f - ka)));
                        *px = m;
                    }
            }

            // ---------- 3) charnière : le talon plonge, une fente sombre s'ouvre, l'axe accroche la lumière ----------
            {
                Image::BitmapData bo (out, Image::BitmapData::readWrite);
                const int yh = (int) std::round (dstQ[3].y);
                const int x0 = (int) std::ceil (dstQ[3].x + 18 * sx), x1 = (int) std::floor (dstQ[2].x - 18 * sx);
                const int gap = jmax (1, (int) std::round (3.5 * sy * lift));
                for (int x = jmax (0, x0); x <= jmin (w - 1, x1); ++x)
                {
                    const float fx = (float) jmin (x - x0, x1 - x) / (float) (12 * sx);
                    const float edgeK = jlimit (0.0f, 1.0f, fx);
                    for (int k = -gap; k <= gap; ++k)                      // fente sombre à cheval sur l'arête du talon
                    {
                        const int y = yh + k;
                        if (y < 0 || y >= h) continue;
                        auto* px = (PixelARGB*) bo.getPixelPointer (x, y);
                        const float a = (float) (0.75 * lift) * edgeK * (1.0f - std::abs ((float) k) / (float) (gap + 1));
                        PixelARGB o;
                        o.setARGB (u8 (px->getAlpha() + (255 - px->getAlpha()) * a), u8 (px->getRed() * (1 - a)), u8 (px->getGreen() * (1 - a)), u8 (px->getBlue() * (1 - a)));
                        *px = o;
                    }
                    // fin liseré clair : l'axe de la charnière éclairé juste sous la fente
                    const int yl = yh + gap + 1;
                    if (yl >= 0 && yl < h)
                    {
                        auto* px = (PixelARGB*) bo.getPixelPointer (x, yl);
                        const float a = (float) (0.35 * lift) * edgeK;
                        PixelARGB o;
                        o.setARGB (u8 (px->getAlpha() + (255 - px->getAlpha()) * a), u8 (px->getRed() + (210 - px->getRed()) * a),
                                   u8 (px->getGreen() + (208 - px->getGreen()) * a), u8 (px->getBlue() + (205 - px->getBlue()) * a));
                        *px = o;
                    }
                }
            }

            // ---------- 4) ombre portée : décalage et flou proportionnels à la hauteur au-dessus du boîtier ----------
            std::vector<float> sh ((size_t) (w * h), 0.0f), tmp ((size_t) (w * h), 0.0f);
            {
                const Image::BitmapData bp (out, Image::BitmapData::readOnly);
                const double topY = dstQ[0].y, botY = dstQ[3].y;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        // le point du plateau qui projette ici est plus haut d'environ h (lumière en haut à gauche -> ombre en bas à droite)
                        const double hgt = jlimit (0.0, 1.0, (botY - (double) y) / jmax (1.0, botY - topY)) * lift;
                        const int xs = (int) std::round (x - (2.0 + 9.0 * hgt) * sx), ys = (int) std::round (y - (3.0 + 13.0 * hgt) * sy);
                        if (xs >= 0 && ys >= 0 && xs < w && ys < h)
                            sh[(size_t) (y * w + x)] = (float) ((const PixelARGB*) bp.getPixelPointer (xs, ys))->getAlpha() / 255.0f;
                    }
            }
            const int rad = jmax (1, roundToInt ((3.0 + 5.0 * lift) * sx));
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
            Image res (Image::ARGB, w, h, true);
            {
                const Image::BitmapData bp (out, Image::BitmapData::readOnly);
                Image::BitmapData bo (res, Image::BitmapData::writeOnly);
                const float strength = (float) (0.32 + 0.30 * lift - 0.10 * jmin (0.0, sTilt));
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const PixelARGB pp = *(const PixelARGB*) bp.getPixelPointer (x, y);
                        const float pa = (float) pp.getAlpha() / 255.0f;
                        const float sa = sh[(size_t) (y * w + x)] * strength * (1.0f - pa);
                        PixelARGB o;   // prémultiplié : l'ombre noire n'ajoute que de l'alpha
                        o.setARGB (u8 ((pa + sa) * 255.0f), pp.getRed(), pp.getGreen(), pp.getBlue());
                        *(PixelARGB*) bo.getPixelPointer (x, y) = o;
                    }
            }
            return res;
        }

        static PixelARGB sample (const Image::BitmapData& bd, int w, int h, float x, float y)
        {
            const int x0 = (int) std::floor (x), y0 = (int) std::floor (y);
            const float fx = x - (float) x0, fy = y - (float) y0;
            auto px = [&] (int xx, int yy) -> PixelARGB
            {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) { PixelARGB z; z.setARGB (0, 0, 0, 0); return z; }
                return *(const PixelARGB*) bd.getPixelPointer (xx, yy);
            };
            const PixelARGB a = px (x0, y0), b = px (x0 + 1, y0), c = px (x0, y0 + 1), d = px (x0 + 1, y0 + 1);
            auto mix = [&] (int ca, int cb, int cc, int cd)
            {
                const float t = (float) ca + ((float) cb - (float) ca) * fx, u = (float) cc + ((float) cd - (float) cc) * fx;
                return u8 (t + (u - t) * fy);
            };
            PixelARGB o;
            o.setARGB (mix (a.getAlpha(), b.getAlpha(), c.getAlpha(), d.getAlpha()), mix (a.getRed(), b.getRed(), c.getRed(), d.getRed()),
                       mix (a.getGreen(), b.getGreen(), c.getGreen(), d.getGreen()), mix (a.getBlue(), b.getBlue(), c.getBlue(), d.getBlue()));
            return o;
        }

        Image down, src;
        Rectangle<float> box { 0, 0, (float) kW, (float) kH };
        float scale = -1.0f;
        std::array<Image, kFrames> frames, oldFrames;
    };

    //==========================================================================
    // menu de taille de fenêtre (identique à Ring Think / Wharry)
    bool isDefaultSize (const CrycatEditor* ed) { return ed != nullptr && std::abs ((float) ed->getHeight() / kRefH - kDefaultScale) < 0.02f; }

    void addSizeItems (PopupMenu& m, const CrycatEditor* ed)
    {
        const int cur = ed != nullptr ? ed->getScaleIndex() : -1;
        m.addItem (904, "60 % (default)", true, isDefaultSize (ed));
        m.addItem (902, "75 %", true, cur == 1);
        m.addItem (903, "100 %", true, cur == 2);
    }

    void handleSizeChoice (int r, CrycatEditor* ed)
    {
        if (ed == nullptr || r < 902 || r > 904) return;
        ed->setScaleIndex (r == 904 ? -1 : r - 901);
    }

    Point<float> rangeNumberPos (int i) { return kNumPos[jlimit (0, crycat::kNumRanges - 1, i)]; }
}

//==============================================================================
class PedalView : public Component, private Timer
{
public:
    explicit PedalView (CrycatProcessor& p) : proc (p)
    {
        bg.src    = loadPng (BinaryData::background_png, BinaryData::background_pngSize);
        fsOff.src     = loadPng (BinaryData::fs_off_png, BinaryData::fs_off_pngSize);
        fsOn.src      = loadPng (BinaryData::fs_on_png, BinaryData::fs_on_pngSize);
        ledGlow.src   = loadPng (BinaryData::led_glow_png, BinaryData::led_glow_pngSize);
        tgL.src   = loadPng (BinaryData::toggle_l_png, BinaryData::toggle_l_pngSize);
        tgM.src   = loadPng (BinaryData::toggle_m_png, BinaryData::toggle_m_pngSize);
        tgR.src   = loadPng (BinaryData::toggle_r_png, BinaryData::toggle_r_pngSize);
        treadle.setSource (loadPng (BinaryData::treadle_down_png, BinaryData::treadle_down_pngSize));
        typeface = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
        typefaceBold = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
        setOpaque (true);
        startTimerHz (30);
    }

    // anti-gel v2 : jamais de calcul lourd ici, les caches seront refaits par le timer quand la taille sera stable
    void resized() override { s = (float) getWidth() / (float) kW; resizeSettle = 4; repaint(); }

    // caches d'images à la taille réelle en pixels (fenêtre x échelle Windows / Retina) : appelé par le timer
    // (et une fois à l'ouverture de la fenêtre), jamais par paint
    void rebuildCaches()
    {
        if (getWidth() <= 0) return;
        const float ps = s * dispScale;
        for (auto* c : { &bg, &fsOff, &fsOn, &ledGlow, &tgL, &tgM, &tgR }) c->prepare (ps);
        treadle.prepare (ps);
        askRepaint (getLocalBounds());
    }

    void paint (Graphics& g) override
    {
        // anti-gel v2 : aucun rescale ni calcul de cache ici, on dessine les caches existants (ou les originaux)
        lastPaintMs = Time::getMillisecondCounterHiRes();
        repaintAskedMs = 0.0;
        g.fillAll (Colours::black);
        g.drawImage (bg.get(), getLocalBounds().toFloat());

        // ---- plateau (deux positions voisines fondues -> mouvement continu) : talon = pointe levée ----
        const float fpos = jlimit (0.0f, 1.0f, drawnPedal) * (float) (kFrames - 1);
        const int f0 = jlimit (0, kFrames - 1, (int) std::floor (fpos)), f1 = jmin (kFrames - 1, f0 + 1);
        const float fr = fpos - (float) f0;
        const auto tb = r (treadle.designBounds());
        g.setOpacity (1.0f);
        if (auto* im0 = treadle.frame (f0))
        {
            g.drawImage (*im0, tb);
            if (fr > 0.02f && f1 != f0) if (auto* im1 = treadle.frame (f1)) { g.setOpacity (fr); g.drawImage (*im1, tb); g.setOpacity (1.0f); }
        }
        else treadle.drawOriginal (g, tb);

        // ---- sélecteur RANGE : repère gravé + numéro choisi éclairé en ambre ----
        {
            const float ang = rangeAngle (drawnRange);
            drawPointer (g, kKnobCap, kCapR, ang, 4.2f);
            const auto np = r (rangeNumberPos (drawnRange));
            g.setGradientFill (ColourGradient (kAmber.withAlpha (0.35f), np.x, np.y, kAmber.withAlpha (0.0f), np.x + 18.0f * s, np.y, true));
            g.fillEllipse (np.x - 18.0f * s, np.y - 18.0f * s, 36.0f * s, 36.0f * s);
            g.setColour (kAmberHi);
            g.setFont (Font (FontOptions (typeface).withHeight (16.6f * 1.40f * s)));
            g.drawText (String (drawnRange + 1), Rectangle<float> (np.x - 15.0f * s, np.y - 15.0f * s, 30.0f * s, 30.0f * s), Justification::centred);
        }

        // ---- Q / PURR / BOOST ----
        for (int i = 0; i < 3; ++i)
            drawPointer (g, kSmallCap[i], kSmallCapR, degreesToRadians (-135.0f + 270.0f * drawnSmall[i]), 3.4f);

        // ---- inverseur MAN / AUTO : position intermédiaire affichée brièvement pendant la bascule ----
        {
            const bool moving = Time::getMillisecondCounterHiRes() < toggleMidUntil;
            g.drawImage ((moving ? tgM : (drawnAuto ? tgR : tgL)).get(), r (kToggle));
        }

        // ---- LED ambre : la façade prend la lumière (calque photo), puis footswitch, puis la LED elle-même ----
        if (ledLevel > 0.003f)
        {
            g.setOpacity (jmin (1.0f, ledLevel));
            g.drawImage (ledGlow.get(), r (kLedGlow));
            g.setOpacity (1.0f);
        }
        {
            g.drawImage (fsOff.get(), r (kFs));
            if (ledLevel > 0.003f)    // flanc de l'écrou tourné vers la LED : reflet ambre
            {
                g.setOpacity (jmin (1.0f, ledLevel));
                g.drawImage (fsOn.get(), r (kFs));
                g.setOpacity (1.0f);
            }
            if (pressed)              // comme Echoflex : le bouton s'assombrit tant qu'on appuie
            {
                const auto c = r (kFsC); const float pr = kFsPressR * s;
                g.setColour (Colours::black.withAlpha (0.25f));
                g.fillEllipse (c.x - pr, c.y - pr, pr * 2.0f, pr * 2.0f);
            }
        }
        drawLed (g, kFsLed, ledLevel);

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
            if (p.getDistanceFrom (rangeNumberPos (i)) < 13.0f) { proc.setParamValue ("range", (float) i); return; }
        if (p.getDistanceFrom (kKnob) < kKnobR) { drag = Drag::Knob; dragStartY = p.y; dragStartRange = currentRange(); return; }
        for (int i = 0; i < 3; ++i)
            if (p.getDistanceFrom (kSmall[i]) < kSmallR * 1.15f)
            {
                drag = (Drag) ((int) Drag::Small0 + i); dragStartY = p.y;
                if (auto* par = proc.apvts.getParameter (kSmallId[i])) { dragStartVal = par->getValue(); par->beginChangeGesture(); }
                return;
            }
        if (kToggleHit.contains (p)) { proc.togglePedalSwitch ("auto"); return; }   // l'animation suit le paramètre (timer)
        if (kFsC.getDistanceFrom (p) < kFsHitR) { pressed = true; proc.togglePedalSwitch ("active"); repaint (r (kFs).toNearestInt().expanded (4)); return; }   // Echoflex : bascule à l'appui
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

    // anti-gel v2 : chaque redessin demandé par le timer est noté pour le chien de garde
    void askRepaint (Rectangle<int> area)
    {
        if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
        repaint (area);
    }
    int currentRange() const { return jlimit (0, crycat::kNumRanges - 1, (int) std::lround (proc.apvts.getRawParameterValue ("range")->load())); }
    bool param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }
    static float rangeAngle (int i)
    {
        const auto d = rangeNumberPos (i) - kKnobCap;
        return std::atan2 (d.x, -d.y);
    }

    // repère gravé : trait noir mat avec un fin liseré clair côté lumière (comme sur le design)
    void drawPointer (Graphics& g, Point<float> c0, float capR, float ang, float width)
    {
        const Point<float> dir (std::sin (ang), -std::cos (ang));
        const auto c = r (c0);
        const auto a = c + dir * (capR * 0.30f * s), b = c + dir * (capR * 0.90f * s);
        const Point<float> off (-0.6f * s, -0.6f * s);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawLine ({ a + off, b + off }, (width + 0.6f) * s);
        g.setColour (Colour (0xf2141212));
        Path p; p.startNewSubPath (a); p.lineTo (b);
        g.strokePath (p, PathStrokeType (width * s, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // LED 3 mm (comme Holyverb) : bague chromée éclairée en haut à gauche, gorge sombre, lentille ambre ;
    // allumée : cœur presque blanc, corps orangé, bord plus sombre ; reflet fixe de la lumière ambiante sur le dôme.
    // Le halo n'est PAS un disque coloré posé dessus : c'est la façade qui reçoit la lumière (calque led_glow).
    void drawLed (Graphics& g, Point<float> c0, float lvl)
    {
        const auto c = r (c0);
        // ombre portée de la bague (lumière en haut à gauche)
        const float rr = kLedRingR * s;
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - rr + 0.9f * s, c.y - rr + 1.4f * s, rr * 2.0f, rr * 2.0f);
        ColourGradient chrome (Colour (0xffd2d4d8), c.x - rr * 0.8f, c.y - rr * 0.9f, Colour (0xff2a2c31), c.x + rr * 0.8f, c.y + rr * 0.9f, false);
        chrome.addColour (0.45, Colour (0xff74777e));
        g.setGradientFill (chrome);
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
        if (lvl > 0.003f)       // la bague reflète un peu la LED
        {
            g.setGradientFill (ColourGradient (kAmber.withAlpha (0.35f * lvl), c.x, c.y, kAmber.withAlpha (0.0f), c.x + rr, c.y, true));
            g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
        }
        // gorge sombre
        const float gr = kLedGapR * s;
        g.setGradientFill (ColourGradient (Colour (0xff020101), c.x - gr * 0.6f, c.y - gr * 0.7f, Colour (0xff1c1612), c.x + gr * 0.7f, c.y + gr * 0.8f, false));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
        // lentille éteinte : ambre foncé translucide
        const float lr = kLedLensR * s;
        g.setGradientFill (ColourGradient (Colour (0xff5a2f0c), c.x - lr * 0.3f, c.y - lr * 0.4f, Colour (0xff160903), c.x + lr, c.y + lr, true));
        g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        if (lvl > 0.003f)
        {
            const float hot = jlimit (0.0f, 1.0f, lvl * 1.05f);
            ColourGradient body (Colour (0xfffff6e2).withAlpha (hot), c.x + lr * 0.03f, c.y + lr * 0.02f, Colour (0xffb44a06).withAlpha (lvl), c.x + lr, c.y, true);
            body.addColour (0.18, Colour (0xffffe0a6).withAlpha (hot));
            body.addColour (0.40, Colour (0xffffa22e).withAlpha (lvl));
            body.addColour (0.75, Colour (0xffe9760f).withAlpha (lvl));
            g.setGradientFill (body);
            g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        }
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f, 0.7f * s);
        ColourGradient sp (Colours::white.withAlpha (0.30f + 0.15f * lvl), c.x - lr * 0.38f, c.y - lr * 0.50f,
                           Colours::white.withAlpha (0.0f), c.x - lr * 0.38f + lr * 0.36f, c.y - lr * 0.50f, true);
        g.setGradientFill (sp);
        g.fillEllipse (c.x - lr * 0.74f, c.y - lr * 0.80f, lr * 0.72f, lr * 0.56f);
    }

    // écran ambre : plage + état, courbe de réponse en direct, fréquence centrale, course de la pédale, ligne d'état
    void drawScreen (Graphics& g)
    {
        const auto sc = r (kScreen);
        g.setColour (kScreenBg); g.fillRoundedRectangle (sc, kScreenRadius * s);
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
        const bool special = proc.learnTarget.load() >= 0 || proc.isCalibrating() || Time::getMillisecondCounterHiRes() < learnMsgUntil;
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
        const int lt = proc.learnTarget.load();
        if (lt >= 0) return String ("MIDI LEARN: move a control for ") + CrycatProcessor::learnDef (lt).name;
        if (Time::getMillisecondCounterHiRes() < learnMsgUntil) return learnMsg;      // "<Commande> = CC n" pendant 2 s
        auto pc = [this] (const char* id) { return String (roundToInt (proc.apvts.getParameter (id)->getValue() * 100.0f)) + " %"; };
        return "PEDAL " + String (roundToInt (proc.getPedalShown() * 100.0f)) + " %"
             + "  ·  Q " + pc ("q") + "  ·  PURR " + pc ("purr") + "  ·  BOOST " + pc ("boost");
    }

    void timerCallback() override
    {
        const double now = Time::getMillisecondCounterHiRes();
        auto* ed = findParentComponentOfClass<CrycatEditor>();
        if (ed != nullptr) ed->checkRenderer();          // fenêtre hôte recréée, ou au plus 1 fois par seconde

        // caches : refaits ici seulement (jamais dans paint), après stabilisation de la taille ou changement d'échelle
        if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
        else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
        {
            lastScaleCheckMs = now;
            // échelle de l'écran (Windows / Retina) ou mise à l'échelle imposée par l'hôte : caches refaits si elle change
            const float sc = physScale(), ap = (float) Component::getApproximateScaleFactorForComponent (this);
            if (std::abs (sc - dispScale) > 0.01f || std::abs (ap - approxScale) > 0.01f) { dispScale = sc; approxScale = ap; rebuildCaches(); }
        }

        // chien de garde : un redessin demandé n'arrive pas -> on relance le moteur de rendu et on redessine tout
        if (repaintAskedMs > 0.0 && isShowing() && now - repaintAskedMs > 1500.0 && lastPaintMs < repaintAskedMs)
        {
            repaintAskedMs = 0.0;
            if (ed != nullptr) ed->resetRenderer();
            repaint();
        }

        proc.pollCalibration();
        // anti-gel : positions du plateau précalculées au fil des ticks (budget de quelques ms), jamais dans paint
        if (getWidth() > 0 && resizeSettle == 0)
        {
            const int cur = roundToInt (jlimit (0.0f, 1.0f, proc.getPedalShown()) * (float) (kFrames - 1));
            const double t0 = Time::getMillisecondCounterHiRes();
            bool built = false;
            while (Time::getMillisecondCounterHiRes() - t0 < 6.0 && treadle.buildNextMissing (cur)) built = true;
            if (built) askRepaint (r (treadle.designBounds()).toNearestInt().expanded (2));
        }

        // MIDI Learn : confirmation 2 s dans la ligne d'état de l'écran
        const int learned = proc.lastLearned.exchange (-1);
        if (learned >= 0)
        {
            learnMsg = String (CrycatProcessor::learnDef (learned).name) + " = " + proc.learnAssignText (learned);
            learnMsgUntil = Time::getMillisecondCounterHiRes() + 2000.0;
        }

        const int rg = currentRange();
        const bool autoM = param ("auto"), on = proc.isEffectOn(), armed = autoM && param ("active");
        const float ped = proc.getPedalShown();
        const String st = statusText();
        bool screen = false;

        if (std::abs (ped - drawnPedal) > 0.002f)
        {
            drawnPedal = ped; screen = true;
            askRepaint (r (treadle.designBounds()).toNearestInt().expanded (2));
        }
        if (rg != drawnRange)
        {
            const float h = kKnobR + 32.0f;
            askRepaint (r (Rectangle<float> (kKnob.x - h, kKnob.y - h, 2 * h, 2 * h)).toNearestInt().expanded (2));
            drawnRange = rg; screen = true;
        }
        const double nowMs = Time::getMillisecondCounterHiRes();
        if (drawnAuto != autoM && drawnStarted) toggleMidUntil = nowMs + 55.0;
        if (autoM != drawnAuto || on != drawnOn || armed != drawnArmed)
        {
            drawnAuto = autoM; drawnOn = on; drawnArmed = armed; screen = true;
            askRepaint (r (kToggle).toNearestInt().expanded (3));
        }
        drawnStarted = true;
        const bool toggleMoving = nowMs < toggleMidUntil;
        if (toggleMoving != drawnToggleMoving) { drawnToggleMoving = toggleMoving; askRepaint (r (kToggle).toNearestInt().expanded (3)); }
        // LED (comme Echoflex) : allumage / extinction en fondu (0.45 / 0.28 par image) ; veille AUTO = faible lueur
        const float ledTarget = on ? 1.0f : (armed ? 0.10f : 0.0f);
        ledLevel += (ledTarget - ledLevel) * (ledTarget > ledLevel ? 0.45f : 0.28f);
        if (std::abs (ledLevel - ledTarget) < 0.003f) ledLevel = ledTarget;
        if (std::abs (ledLevel - drawnLed) > 0.003f)
        {
            drawnLed = ledLevel;
            askRepaint (r (kLedGlow).toNearestInt().expanded (2));
            askRepaint (r (kFs).toNearestInt().expanded (2));
        }
        for (int i = 0; i < 3; ++i)
        {
            const float v = proc.apvts.getParameter (kSmallId[i])->getValue();
            if (std::abs (v - drawnSmall[i]) > 0.002f)
            {
                drawnSmall[i] = v; screen = true;      // Q et BOOST changent la courbe
                askRepaint (r (Rectangle<float> (kSmall[i].x - kSmallR, kSmall[i].y - kSmallR, 2 * kSmallR, 2 * kSmallR)).toNearestInt().expanded (2));
            }
        }
        if (st != drawnStatus) { drawnStatus = st; screen = true; }
        if (screen) askRepaint (r (kScreen).toNearestInt().expanded (2));
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

    // commande sous la souris (même ordre de test que mouseDown) : index MIDI Learn, -1 = hors commande
    int learnIndexAt (Point<float> p) const
    {
        if (kScreen.contains (p)) return -1;
        for (int i = 0; i < crycat::kNumRanges; ++i)
            if (p.getDistanceFrom (rangeNumberPos (i)) < 13.0f) return CrycatProcessor::lRange;
        if (p.getDistanceFrom (kKnob) < kKnobR) return CrycatProcessor::lRange;
        for (int i = 0; i < 3; ++i)
            if (p.getDistanceFrom (kSmall[i]) < kSmallR * 1.15f) return CrycatProcessor::lQ + i;
        if (kToggleHit.contains (p)) return CrycatProcessor::lAuto;
        if (kFsC.getDistanceFrom (p) < kFsHitR * 1.2f) return CrycatProcessor::lFs;
        if (kPlate.contains (p)) return CrycatProcessor::lPedal;
        return -1;
    }

    // clic droit sur une commande : MIDI Learn de cette commande en tête du menu
    void addLearnSection (PopupMenu& m, int i)
    {
        const String name = CrycatProcessor::learnDef (i).name;
        const int lt = proc.learnTarget.load();
        m.addSectionHeader (name);
        m.addItem (1000 + i, "MIDI Learn: " + name + " (" + proc.learnAssignText (i) + ")", true, lt == i);
        m.addItem (1100 + i, "Clear MIDI: " + name, proc.learnAssignText (i) != "none");
        if (lt >= 0) m.addItem (1200, "Cancel MIDI learn");
        m.addSeparator();
    }

    void addMidiSubmenu (PopupMenu& m)
    {
        PopupMenu midi;
        for (int i = 0; i < CrycatProcessor::numLearn; ++i)
            midi.addItem (1000 + i, String ("Learn: ") + CrycatProcessor::learnDef (i).name + " (" + proc.learnAssignText (i) + ")",
                          true, proc.learnTarget.load() == i);
        midi.addSeparator();
        if (proc.learnTarget.load() >= 0) midi.addItem (1200, "Cancel MIDI learn");
        midi.addItem (1201, "Reset MIDI to defaults (pedal CC 11, others none)");
        midi.addSeparator();
        midi.addItem (1202, "MAN / AUTO and footswitch also learn a MIDI note", false);
        m.addSubMenu ("MIDI", midi);
        m.addSeparator();
    }

    void handleLearnChoice (int r)
    {
        if (r >= 1000 && r < 1000 + CrycatProcessor::numLearn)          // re-cliquer sur la commande en attente annule
            proc.learnTarget.store (proc.learnTarget.load() == r - 1000 ? -1 : r - 1000);
        else if (r >= 1100 && r < 1100 + CrycatProcessor::numLearn) proc.clearLearn (r - 1100);
        else if (r == 1200) proc.learnTarget.store (-1);
        else if (r == 1201) proc.resetMidiMap();
    }

    void showMenu (Point<float> p)
    {
        auto* ed = findParentComponentOfClass<CrycatEditor>();
        PopupMenu m;
        const int learnIdx = learnIndexAt (p);
        if (learnIdx >= 0) addLearnSection (m, learnIdx);
        const bool onFs = learnIdx == CrycatProcessor::lFs;
        const bool onToggle = learnIdx == CrycatProcessor::lAuto;
        if (onFs || onToggle)
        {
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
            m.addItem (35, "Calibrate (sweep heel <-> toe for 6 s)  -  " + proc.calibrationDescription());
            m.addItem (34, "Reset pedal calibration");
            m.addItem (33, "Invert pedal direction", true, param ("invert"));
            PopupMenu glide;
            const float gl = proc.apvts.getRawParameterValue ("smooth")->load();
            for (int i = 0; i < 6; ++i) { const int ms[6] = { 0, 8, 15, 30, 60, 120 }; glide.addItem (61 + i, String (ms[i]) + " ms", true, std::abs (gl - (float) ms[i]) < 1.0f); }
            m.addSubMenu ("Pedal glide (smoothing)", glide);
            m.addSeparator();
            m.addSectionHeader ("Auto-engage");
            PopupMenu rel;
            const float ao = proc.apvts.getRawParameterValue ("autooff")->load();
            for (int i = 0; i < 5; ++i) { const int ms[5] = { 150, 300, 400, 800, 1500 }; rel.addItem (71 + i, String (ms[i]) + " ms", true, std::abs (ao - (float) ms[i]) < 1.0f); }
            m.addSubMenu ("Switch off after resting on the heel", rel);
            m.addItem (53, "Respond to MIDI Program Change (PC 1-10)", true, param ("pc"));
            m.addSeparator();
            if (learnIdx < 0) addMidiSubmenu (m);
        }
        m.addSectionHeader ("Plugin size");
        addSizeItems (m, ed);

        Component::SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp] (int res)
        {
            if (sp == nullptr || res == 0) return;
            auto& pr = sp->proc;
            const int glideMs[6] = { 0, 8, 15, 30, 60, 120 }, relMs[5] = { 150, 300, 400, 800, 1500 };
            if (res >= 1000) { sp->handleLearnChoice (res); return; }
            switch (res)
            {
                case 31: pr.startMidiLearn (MidiTarget::Pedal); break;
                case 32: pr.clearMidi (MidiTarget::Pedal); break;
                case 33: pr.togglePedalSwitch ("invert"); break;
                case 34: pr.resetCalibration(); break;
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
    ScaledCache bg, fsOff, fsOn, ledGlow, tgL, tgM, tgR;
    TreadleWarp treadle;
    Typeface::Ptr typeface, typefaceBold;
    float s = kDefaultScale * kRefH / (float) kH;

    // anti-gel v2 : échelle d'affichage, stabilisation après redimensionnement, suivi des redessins
    float dispScale = 1.0f, approxScale = 1.0f;
    int resizeSettle = 0;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0;
public:
    void initCaches() { dispScale = physScale(); approxScale = (float) Component::getApproximateScaleFactorForComponent (this); resizeSettle = 0; rebuildCaches(); }
private:

    Drag drag = Drag::None;
    float dragStartY = 0, dragStartVal = 0;
    int dragStartRange = 0;
    bool pressed = false, drawnStarted = false, drawnToggleMoving = false;
    double toggleMidUntil = 0.0;
    float ledLevel = 0.0f, drawnLed = -1.0f;

    float drawnPedal = -1.0f, drawnSmall[3] = { -1.0f, -1.0f, -1.0f };
    int drawnRange = -1;
    bool drawnAuto = false, drawnOn = true, drawnArmed = false;
    String drawnStatus;
    String learnMsg;                       // "Q = CC 21" pendant 2 s après un MIDI Learn
    double learnMsgUntil = 0.0;
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
    setOpaque (true);                    // anti-gel : l'hôte ne redessine jamais ce qui est derrière la fenêtre
    setScaleIndex (jlimit (-1, 2, proc.uiScaleIdx.load()));
    view->initCaches();
}

CrycatEditor::~CrycatEditor() = default;

//==============================================================================
// Anti-gel de la fenêtre (Windows) : JUCE 8 dessine par défaut avec Direct2D ; avec certains pilotes (Intel)
// la fenêtre cesse de se rafraîchir alors que le DAW continue. Le moteur logiciel est stable partout.
//==============================================================================
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel forcé sur Windows (Direct2D peut figer la fenêtre dans certains hôtes),
//     appliqué à chaque nouvelle fenêtre hôte (peer) et revérifié toutes les secondes, jamais en boucle ;
//  2. aucun calcul lourd dans paint() : les caches d'images (fond, commandes, positions du plateau) sont refaits
//     par le timer seulement, après stabilisation de la taille ou quand l'échelle d'affichage change (au plus toutes les 250 ms) ;
//  3. chien de garde : si un redessin demandé n'est pas arrivé au bout de 1,5 s alors que la fenêtre est
//     visible, le moteur de rendu est réappliqué et toute la fenêtre est redessinée ;
//  4. côté audio : aucun appel à l'hôte ni allocation depuis le thread audio (voir PluginProcessor).
void CrycatEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheckMs = Time::getMillisecondCounterHiRes();
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void CrycatEditor::checkRenderer()
{
    if ((void*) getPeer() != lastPeer || Time::getMillisecondCounterHiRes() - lastRendererCheckMs > 1000.0)
        useSoftwareRenderer();
}

void CrycatEditor::resetRenderer()
{
    lastPeer = nullptr;
    useSoftwareRenderer();
    repaint();
}

void CrycatEditor::paint (Graphics& g) { g.fillAll (Colours::black); }   // la pédale recouvre tout ; fenêtre opaque
void CrycatEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void CrycatEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

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
