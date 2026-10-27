# CRYCAT : rendu physique (champ de hauteurs + éclairage) des commandes mobiles, cohérent avec la lumière du design
#   Lumière principale : en haut à gauche, élévation ~45° (comme les reflets des boutons du design) ; éclairage d'ambiance faible.
#   Chaque position est rendue avec la MÊME lumière (jamais un miroir d'une autre position : la lumière serait inversée).
#   Sorties (images 2x, repère 911 x 928 du plugin) :
#     toggle_l.png / toggle_m.png / toggle_r.png : inverseur MAN / (en mouvement) / AUTO, ombres portées comprises
#     fs_off.png / fs_on.png : footswitch d'ECHOFLEX (photo), LED éteinte / allumée (reflet ambre sur le flanc tourné vers la LED)
#     led_glow.png : façade éclairée par la LED (le grain du boîtier prend la lumière, décroissance physique) ; affiché avec
#       une opacité = intensité de la LED
#   Les coordonnées sont écrites dans tools/controls_layout.txt (reprises dans PluginEditor.cpp).
import os, math
import numpy as np, cv2
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS, SSAA = 2, 3                     # images livrées en 2x, rendu suréchantillonné 3x
PX = AS * SSAA                      # pixels de rendu par unité du repère

L = np.array([-0.46, -0.62, 0.64]); L /= np.linalg.norm(L)        # vers la lumière (x droite, y bas, z vers l'œil)
V = np.array([0.0, 0.0, 1.0])
H = (L + V) / np.linalg.norm(L + V)

# ------------------------------------------------------------------ outils
class Canvas:
    def __init__(self, w_u, h_u, cx_u, cy_u):
        self.W, self.H = round(w_u * PX), round(h_u * PX)
        yy, xx = np.mgrid[0:self.H, 0:self.W].astype(np.float64)
        self.x = (xx + 0.5) / PX - cx_u          # repère local en unités, origine sur l'axe de la commande
        self.y = (yy + 0.5) / PX - cy_u
        self.h = np.zeros((self.H, self.W))      # hauteur (unités)
        self.mat = np.zeros((self.H, self.W), np.int32)   # 0 = façade (transparente), sinon id de matière
        self.extra = {}

    def put(self, hgt, mask, mat_id):
        """ pose une pièce : elle ne remplace que là où elle est plus haute """
        m = mask & (hgt > self.h - 1e-9)
        self.h[m] = hgt[m]; self.mat[m] = mat_id
        return m

def normals(c, hmap):
    gy, gx = np.gradient(hmap, 1.0 / PX)
    n = np.dstack([-gx, -gy, np.ones_like(hmap)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)

def soft_shadow(c, samples=10, spread=0.10, max_u=40.0):
    """ ombre douce par lancer de rayons dans le champ de hauteurs (vers la lumière, plusieurs directions) """
    hmap = c.h
    vis = np.zeros_like(hmap)
    rng = np.random.default_rng(1)
    step = 0.5 / PX * 2
    nsteps = int(max_u / step)
    for s in range(samples):
        d = L + rng.normal(0, spread, 3) * np.array([1, 1, 0.5]); d /= np.linalg.norm(d)
        horiz = math.hypot(d[0], d[1]); tanE = d[2] / max(horiz, 1e-6)
        dx, dy = d[0] / horiz, d[1] / horiz
        occl = np.zeros_like(hmap, bool)
        for k in range(1, nsteps):
            t = k * step
            sx, sy = int(round(dx * t * PX)), int(round(dy * t * PX))
            if abs(sx) >= c.W or abs(sy) >= c.H: break
            sh = np.full_like(hmap, -1.0)
            ys0, ys1 = max(0, -sy), min(c.H, c.H - sy); xs0, xs1 = max(0, -sx), min(c.W, c.W - sx)
            sh[ys0:ys1, xs0:xs1] = hmap[ys0 + sy:ys1 + sy, xs0 + sx:xs1 + sx]
            occl |= sh > hmap + t * tanE + 0.05
        vis += ~occl
    return vis / samples

def env_chrome(n, gloss=1.0):
    """ métal poli : environnement de studio (boîte à lumière en haut à gauche, sol sombre) lu par le vecteur réfléchi """
    nz = n[..., 2]
    r = np.dstack([2 * nz * n[..., 0], 2 * nz * n[..., 1], 2 * nz * nz - 1])
    # direction de la boîte à lumière principale
    k = np.clip((r @ L), -1, 1)
    sky = 0.5 - 0.5 * r[..., 1]                          # haut de l'image = ciel clair
    base = 40 + 120 * np.clip(sky, 0, 1) ** 1.6
    box = 210 * np.clip((k - 0.55) / 0.45, 0, 1) ** (1.5 / gloss)
    rim = 40 * np.clip(-r[..., 0] * 0.6 - r[..., 1] * 0.3, 0, 1)    # petite boîte d'appoint
    return base + box + rim

def shade(c, n, vis, tint, kind, rough=0.35, amb=0.18, led=None):
    """ kind : 'chrome' (miroir), 'satin' (nickel brossé), 'dark' (zamak sombre) """
    ndl = np.clip(n @ L, 0, 1) * (0.25 + 0.75 * vis)
    ndh = np.clip(n @ H, 0, 1)
    shin = 12 + 220 * (1 - rough) ** 2
    spec = ndh ** shin * (0.35 + 0.65 * vis)
    if kind == "chrome":
        col = env_chrome(n) * (0.55 + 0.45 * vis) + 255 * spec * 0.9
        col = col[..., None] * np.asarray(tint)
    elif kind == "satin":
        e = env_chrome(n, gloss=0.6)
        col = (0.45 * e + 150 * ndl + 30 * amb)[..., None] * np.asarray(tint) + (210 * spec)[..., None]
    else:
        col = (60 * ndl + 25 * amb)[..., None] * np.asarray(tint) + (90 * spec)[..., None]
    if led is not None:                                   # lumière ambre de la LED (point lumineux bas, à droite)
        lp, lcol, lpow = led
        dxl = lp[0] - c.x; dyl = lp[1] - c.y; dzl = lp[2] - c.h
        d2 = dxl ** 2 + dyl ** 2 + dzl ** 2
        ldir = np.dstack([dxl, dyl, dzl]) / np.sqrt(d2)[..., None]
        ndl2 = np.clip((n * ldir).sum(2), 0, 1)
        hl = ldir + V; hl /= np.linalg.norm(hl, axis=2, keepdims=True)
        sp2 = np.clip((n * hl).sum(2), 0, 1) ** shin
        e = lpow / d2
        col = col + (e * (0.5 * ndl2 + 2.5 * sp2))[..., None] * np.asarray(lcol)
    return col

def finish(c, rgb, shadow_on_panel, out, size_px):
    """ sprite RGBA : pièce opaque + ombre portée sur la façade (noire semi-transparente) ; réduction anti-crénelée """
    obj = (c.mat > 0).astype(np.float64)
    obj = cv2.GaussianBlur(obj, (0, 0), 0.35 * SSAA)
    alpha = np.clip(obj + shadow_on_panel * (1 - obj), 0, 1)
    rgbp = rgb * obj[..., None]                          # couleur prémultipliée (l'ombre est noire)
    rgba = np.dstack([rgbp, alpha * 255])
    # réduction en espace prémultiplié
    small = cv2.resize(rgba, size_px, interpolation=cv2.INTER_AREA)
    a = small[..., 3:4] / 255.0
    col = np.where(a > 1e-4, small[..., :3] / np.maximum(a, 1e-4), 0)
    img = np.dstack([np.clip(col, 0, 255), np.clip(small[..., 3], 0, 255)]).astype(np.uint8)
    Image.fromarray(img, "RGBA").save(os.path.join(ROOT, out))

def panel_shadow(c, vis_panel, amount=0.62):
    # là où il n'y a pas de pièce : ombre = 1 - visibilité de la lumière, adoucie, + ombre de contact (occlusion ambiante)
    sh = (1 - vis_panel) * amount
    occ = cv2.GaussianBlur((c.mat > 0).astype(np.float64), (0, 0), 1.6 * PX) * 0.35
    return np.clip(sh + occ, 0, 0.85) * (c.mat == 0)

def hexagon_r(x, y, inr, pointy=True):
    """ « rayon » d'hexagone (distance normalisée à la manière d'un cercle : hex = inr quand la valeur vaut inr) """
    a = np.arctan2(y, x) + (math.pi / 2 if pointy else 0)
    return np.hypot(x, y) * np.cos(((a + math.pi / 6) % (math.pi / 3)) - math.pi / 6)

# ------------------------------------------------------------------ positions dans le repère du plugin
FS_C = (671.0, 721.8)          # axe du footswitch (mesuré sur le design)
LED_C = (772.4, 724.8)
TG_C = (795.4, 129.7)          # axe de l'inverseur

# ------------------------------------------------------------------ 1) footswitch : celui d'ECHOFLEX (photo réelle)
# Footswitch de référence Studio Wiga : écrou hexagonal moleté + bouton en métal tourné, photographié sur la pédale Echoflex
# (design/echoflex_footswitch_source.png = extrait de background.jpg d'Echoflex, 2 px par unité, centre au milieu).
# Mis à l'échelle de la pédale, détouré sur son hexagone, ombre portée ajoutée selon la lumière en haut à gauche.
EF_PTS = [(1.7, -56.7), (51.7, -26.7), (51.7, 31.7), (5.0, 58.3), (-48.3, 30.0), (-49.3, -26.7)]   # hexagone (unités Echoflex)
EF_CENTROID = (1.5, 1.5)
EF_FLATS = 100.5                       # largeur entre plats (unités Echoflex)
FS_FLATS = 87.0                        # largeur voulue sur Crycat (celle du design)
FS_K = FS_FLATS / EF_FLATS
FS_W = 170.0                           # sprite carré (unités Crycat), ombre comprise

def footswitch_echoflex(led_on):
    src = np.asarray(Image.open(os.path.join(ROOT, "design", "echoflex_footswitch_source.png")).convert("RGB")).astype(np.float64)
    n = round(FS_W * AS)                       # taille du sprite (px, 2x)
    sc = FS_K * AS / 2.0                       # px source -> px sprite
    # source recentrée sur le centroïde de l'hexagone, mise à l'échelle (Lanczos)
    M = np.float32([[sc, 0, n / 2 - (150 + 2 * EF_CENTROID[0]) * sc], [0, sc, n / 2 - (150 + 2 * EF_CENTROID[1]) * sc]])
    rgb = cv2.warpAffine(src, M, (n, n), flags=cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_REFLECT)
    # détourage sur l'hexagone (léger retrait, bord adouci)
    pts = np.array([[n / 2 + (x - EF_CENTROID[0]) * FS_K * AS * 0.992, n / 2 + (y - EF_CENTROID[1]) * FS_K * AS * 0.992] for x, y in EF_PTS])
    m = np.zeros((n * 4, n * 4), np.uint8); cv2.fillPoly(m, [np.round(pts * 4).astype(np.int32)], 255)
    a = cv2.resize(m, (n, n), interpolation=cv2.INTER_AREA).astype(np.float64) / 255.0
    a = cv2.GaussianBlur(a, (0, 0), 0.6)
    if led_on:
        # la LED ambre (à droite) se reflète sur les flancs et la bague moletée tournés vers elle
        yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
        ux = (xx - n / 2) / (AS * FS_K * 50.0)
        w = np.clip(ux, 0, 1) ** 2 * (rgb.mean(2) / 255.0)
        rgb = rgb + (w * 70.0)[..., None] * np.array([1.0, 0.58, 0.18])
    # ombre portée (lumière en haut à gauche) + ombre de contact
    def shift(img, dx, dy): return cv2.warpAffine(img, np.float32([[1, 0, dx], [0, 1, dy]]), (n, n))
    cast = cv2.GaussianBlur(shift(a, 4.5 * AS, 6.5 * AS), (0, 0), 4.5 * AS) * 0.55
    contact = cv2.GaussianBlur(shift(a, 1.0 * AS, 1.5 * AS), (0, 0), 1.6 * AS) * 0.45
    sh = np.clip(cast + contact, 0, 0.8) * (1 - a)
    alpha = np.clip(a + sh, 0, 1)
    col = np.where(alpha[..., None] > 1e-4, rgb * (a / np.maximum(alpha, 1e-4))[..., None], 0)    # l'ombre est noire
    out = np.dstack([np.clip(col, 0, 255), alpha * 255]).astype(np.uint8)
    Image.fromarray(out, "RGBA").save(os.path.join(ROOT, "fs_on.png" if led_on else "fs_off.png"))

for l_ in (False, True):
    footswitch_echoflex(l_)

# ------------------------------------------------------------------ 2) inverseur (bat toggle) : 3 positions
def toggle(pos):            # pos : -1 = MAN (levier à gauche), 0 = en mouvement (vertical), 1 = AUTO (à droite)
    W_, H_ = 84.0, 60.0
    c = Canvas(W_, H_, W_ / 2, H_ / 2 - 2)
    x, y = c.x, c.y
    r = np.hypot(x, y)
    # rondelle crantée sombre, écrou hexagonal satiné, fût fileté chromé
    washer = r < 11.5
    c.put(np.full_like(r, 1.0) + 0.25 * np.cos(np.arctan2(y, x) * 24) * (r > 10.0), washer, 4)
    hx = hexagon_r(x, y, 8.6, pointy=False)
    nut = np.minimum(4.2, (8.6 - hx) * 3.0)
    nut = np.minimum(nut, 4.2 - np.clip(r - 7.6, 0, None) * 1.2)
    c.put(np.clip(nut, 0, None) + 1.0, (hx < 8.6) & (nut > 0), 1)
    bush = np.where(r < 5.6, 7.2 - 0.6 * np.clip((r - 4.6), 0, None) ** 2, 0)
    c.put(bush + 0.08 * np.sin(np.arctan2(y, x) * 2 + r * 6) * (r < 5.6), r < 5.6, 5)
    # levier : capsule effilée du pivot (z = 7) vers l'extrémité, inclinée de 62° (vue de dessus) ; extrémité en boule
    tilt = math.radians(62) * pos
    Lb = 30.0
    ax = np.array([math.sin(tilt), 0.0, math.cos(tilt)])
    p0 = np.array([0.0, 0.0, 7.0])
    if pos == 0:
        ax = np.array([0.0, 0.0, 1.0])
        Lb = 22.0
    # échantillonnage le long de l'axe : chaque section est un disque de rayon r(t), on garde la hauteur max
    best = np.full_like(r, -1.0)
    for t in np.linspace(0, 1, 120):
        cen = p0 + ax * Lb * t
        rad = 3.3 + 1.0 * t
        if t > 0.70: rad = max(rad, 6.0 * math.sqrt(max(0.0, 1 - ((t - 0.85) / 0.15) ** 2)))
        d2 = (x - cen[0]) ** 2 + (y - cen[1]) ** 2
        zz = cen[2] + np.sqrt(np.clip(rad ** 2 - d2, 0, None))
        best = np.where(d2 < rad ** 2, np.maximum(best, zz), best)
    c.put(best, best > 0, 2)
    vis = soft_shadow(c, samples=16, spread=0.12, max_u=45)
    n = normals(c, c.h)
    lever = shade(c, n, vis, (1.0, 0.99, 0.98), "satin", rough=0.15) * 1.18      # nickel poli
    nutc = shade(c, n, vis, (0.95, 0.94, 0.93), "satin", rough=0.4) * 1.08
    wash = shade(c, n, vis, (0.55, 0.55, 0.57), "dark")
    bushc = shade(c, n, vis, (0.98, 0.98, 0.99), "satin", rough=0.2)
    m = c.mat[..., None]
    rgb = np.where(m == 2, lever, np.where(m == 1, nutc, np.where(m == 4, wash, bushc)))
    sh = panel_shadow(c, vis, amount=0.50)
    finish(c, rgb, sh, {-1: "toggle_l.png", 0: "toggle_m.png", 1: "toggle_r.png"}[pos], (round(W_ * AS), round(H_ * AS)))
    return W_, H_, W_ / 2, H_ / 2 - 2

for p_ in (-1, 0, 1):
    TG = toggle(p_)

# ------------------------------------------------------------------ 3) façade éclairée par la LED
bg = np.asarray(Image.open(os.path.join(ROOT, "background.png")).convert("RGB")).astype(np.float64)
GR = 78.0                                  # rayon de la zone (unités)
gx0, gy0 = LED_C[0] - GR, LED_C[1] - GR
x0p, y0p = round(gx0 * AS), round(gy0 * AS)
size = round(2 * GR * AS)
reg = bg[y0p:y0p + size, x0p:x0p + size]
yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
d = np.hypot((xx + 0.5) / AS - GR, (yy + 0.5) / AS - GR)
# éclairement d'une petite source au-dessus de la façade (loi en cos / d²) : tache chaude serrée + diffusion large du dôme
h1, h2 = 7.0, 22.0
E = 1.0 * h1 ** 3 / (d ** 2 + h1 ** 2) ** 1.5 + 0.18 * h2 ** 3 / (d ** 2 + h2 ** 2) ** 1.5
E *= np.clip((GR - d) / 18.0, 0, 1)        # retombe à zéro au bord de la zone (pas de raccord visible)
amber = np.array([1.0, 0.56, 0.16])
# la façade réfléchit la lumière ambre en proportion de son propre albédo (le grain s'allume, les creux restent sombres)
lum = reg.mean(2, keepdims=True)
lit = reg + (reg * 2.6 + 14.0 * np.clip(lum / 60.0, 0.4, 1.6)) * E[..., None] * amber
# léger halo optique (diffusion dans la lentille de l'œil / de l'objectif), très faible
bloom = np.exp(-(d / 6.0) ** 2) * 55 + np.exp(-(d / 16.0) ** 2) * 10
lit += bloom[..., None] * amber
Image.fromarray(np.clip(lit, 0, 255).astype(np.uint8)).save(os.path.join(ROOT, "led_glow.png"))

with open(os.path.join(ROOT, "tools", "controls_layout.txt"), "w") as fo:
    fo.write(f"fs (Echoflex) centre {FS_C} sprite {FS_W} x {FS_W}  -> rect {FS_C[0] - FS_W / 2:.2f} {FS_C[1] - FS_W / 2:.2f} {FS_W} {FS_W}  echelle {FS_K:.4f}  bouton r {35.0 * FS_K:.1f}  appui r {0.54 * 55.0 * FS_K:.1f}\n")
    W_, H_, ox, oy = TG
    fo.write(f"toggle pivot {TG_C} sprite {W_} x {H_} -> rect {TG_C[0] - ox:.2f} {TG_C[1] - oy:.2f} {W_} {H_}\n")
    fo.write(f"led centre {LED_C} glow rect {gx0:.2f} {gy0:.2f} {2 * GR} {2 * GR}\n")
print(open(os.path.join(ROOT, "tools", "controls_layout.txt")).read())
