# CRYCAT : prépare les images du plugin à partir du design de l'utilisateur (design/crycat_design_source.png, 2528 x 2576)
#   repère de dessin du plugin : 911 x 928 (= design x 0.36025) ; les images sont livrées en 2x (1822 x 1856) pour les écrans HiDPI
#   background.png   : fond nettoyé (écran vidé, repères des boutons, LED, levier et textes corrigés)
#   treadle_down.png : calque du plateau à plat (orteil enfoncé, tel que dans le design) ; le plugin calcule la bascule
#   fs_on.png / fs_off.png, toggle_l.png / toggle_r.png : sprites des commandes
#   tools/layout.txt : coordonnées des commandes dans le repère 911 x 928 (reprises dans PluginEditor.cpp)
import os, math
import numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = Image.open(os.path.join(ROOT, "design", "crycat_design_source.png")).convert("RGB")
A = np.array(src)
DH, DW = A.shape[:2]
FH = 928.0
S = FH / DH                                  # design -> repère du plugin
FW = round(DW * S)                           # 911
AS = 2                                       # échelle des images livrées
OW, OH = FW * AS, int(FH) * AS
def F(v): return v * S                      # coordonnée design -> repère
FR = os.path.join(ROOT, "fonts", "Poppins-Regular.ttf"); FM = os.path.join(ROOT, "fonts", "Poppins-Medium.ttf")

# ------------------------------------------------------------------ mesures dans le design (pixels du design)
PLATE = (183, 69, 1313, 2416); PLATE_R = 55                 # plateau à plat (cadre chromé extérieur)
KNOB = (1570, 824, 153, 113)                                 # RANGE : centre, rayon jupe, rayon chapeau
SMALL = [(1560, 1450), (1868, 1446), (2174, 1452)]           # Q, PURR, BOOST
SMALL_R, SMALL_CAP = 113, 83
CAPS = [(1570, 822), (1564, 1456), (1873, 1455), (2178, 1459)]           # centres des chapeaux (vus un peu en biais)
POINTERS = [(1570, 822, -24, 113), (1564, 1456, -24, 83), (1873, 1455, -85, 83), (2178, 1459, -134, 83)]
NUMBERS = [(1398, 953), (1363, 775), (1467, 630), (1641, 628), (1756, 786), (1735, 963)]   # « 1 » à « 6 »
SCREEN = (1821, 553, 2367, 1181)                             # vitre
FSW = (1859, 2006); FS_HALF = 150                            # footswitch
LED = (2144, 2012)
TOG = (2208, 360)                                            # axe de l'inverseur

# ------------------------------------------------------------------ 1) nettoyage
out = A.copy().astype(np.float32)
yy, xx = np.mgrid[0:DH, 0:DW].astype(np.float32)

# 1a) repères gravés des boutons : remplacés par la moyenne des pixels voisins sur le même cercle (métal tourné)
def remove_pointer(cx, cy, adeg, cap):
    global out
    x0, x1, y0, y1 = cx - cap, cx + cap, cy - cap, cy + cap
    sx, sy = xx[y0:y1, x0:x1] - cx, yy[y0:y1, x0:x1] - cy
    r = np.hypot(sx, sy); th = np.arctan2(sx, -sy)
    a = math.radians(adeg)
    hw = np.arctan2(11.0, np.maximum(r, 1)) + 0.03
    d = np.angle(np.exp(1j * (th - a)))
    m = (np.abs(d) < hw) & (r > cap * 0.06) & (r < cap * 0.98)
    mf = cv2.GaussianBlur(m.astype(np.float32), (0, 0), 1.2)
    mf = np.maximum(mf, m.astype(np.float32))
    # interpolation entre les deux bords du repère (continuité des reflets du métal tourné)
    side = []
    for sgn in (-1, 1):
        t2 = a + sgn * (hw + 0.03)
        mx = (cx + r * np.sin(t2)).astype(np.float32); my = (cy - r * np.cos(t2)).astype(np.float32)
        side.append(cv2.remap(A.astype(np.float32), mx, my, cv2.INTER_LINEAR))
    t = np.clip((d + hw) / (2 * hw), 0, 1)[..., None]
    acc = side[0] * (1 - t) + side[1] * t
    out[y0:y1, x0:x1] = out[y0:y1, x0:x1] * (1 - mf[..., None]) + acc * mf[..., None]
for p in POINTERS: remove_pointer(*p)

# 1b) halo orange (chiffre « 3 » et LED) : retour à la teinte neutre du boîtier
def neutralise(cx, cy, rad, digit=False):
    global out
    x0, x1, y0, y1 = cx - rad, cx + rad, cy - rad, cy + rad
    reg = out[y0:y1, x0:x1]
    r = np.hypot(xx[y0:y1, x0:x1] - cx, yy[y0:y1, x0:x1] - cy)
    w = np.clip((rad - r) / (rad * 0.25), 0, 1)[..., None]
    base = reg[..., 2:3] * np.array([0.93, 0.92, 1.0], np.float32)       # le canal bleu n'est presque pas touché par l'orange
    if digit:
        dm = np.clip((reg[..., 0:1] - 150) / 50, 0, 1)                     # tracé du chiffre : très clair
        base = base * (1 - dm) + np.array([240, 238, 238], np.float32) * (reg[..., 0:1] / 255.0) * dm
    out[y0:y1, x0:x1] = reg * (1 - w) + base * w
neutralise(*NUMBERS[2], 80)
neutralise(*LED, 115)
led_m = np.zeros((DH, DW), np.uint8); cv2.circle(led_m, LED, 30, 255, -1)
out = cv2.inpaint(np.clip(out, 0, 255).astype(np.uint8), led_m, 9, cv2.INPAINT_TELEA).astype(np.float32)

# 1c) inverseur : le levier devient un sprite ; dessous, texture du boîtier recopiée juste au-dessus
tx0, ty0, tx1, ty1 = TOG[0] - 100, TOG[1] - 62, TOG[0] + 100, TOG[1] + 62
crop = A[ty0:ty1, tx0:tx1].astype(np.float32)
lum = crop.mean(2)
hw_m = (lum > 92).astype(np.uint8)
hw_m = cv2.morphologyEx(hw_m, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
n, lab, stats, _ = cv2.connectedComponentsWithStats(hw_m)
keep = np.zeros_like(hw_m)
big = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
keep[lab == big] = 1                                       # levier + écrou : la plus grande zone claire
keep = cv2.morphologyEx(keep, cv2.MORPH_CLOSE, np.ones((15, 15), np.uint8))
keep = cv2.dilate(keep, np.ones((5, 5), np.uint8))
tog_alpha = cv2.GaussianBlur(keep.astype(np.float32), (0, 0), 1.0)
patch = A[ty0 - 112:ty1 - 112, tx0:tx1].astype(np.float32)          # zone propre au-dessus
band = np.zeros(keep.shape, bool); band[:14, :] = True                              # bande propre au-dessus de l'inverseur
patch *= (out[ty0:ty1, tx0:tx1][band].mean(0) / np.maximum(patch[band].mean(0), 1))   # même luminosité que l'entourage
zone = np.zeros_like(keep); cv2.ellipse(zone, (96, 70), (94, 52), 0, 0, 360, 1, -1)
zone = np.maximum(zone, cv2.dilate(keep, np.ones((25, 25), np.uint8)))
fill = cv2.GaussianBlur(zone.astype(np.float32), (0, 0), 5)[..., None]
out[ty0:ty1, tx0:tx1] = out[ty0:ty1, tx0:tx1] * (1 - fill) + patch * fill
# ombre douce sous l'écrou (le sprite garde levier + écrou ; l'ombre reste sur le fond)

# 1d) vitre de l'écran vidée (le plugin dessine l'affichage)
gl = np.zeros((DH, DW), np.uint8)
x0, y0, x1, y1 = SCREEN
cv2.rectangle(gl, (x0 + 18, y0), (x1 - 18, y1), 255, -1); cv2.rectangle(gl, (x0, y0 + 18), (x1, y1 - 18), 255, -1)
for (cx_, cy_) in ((x0 + 18, y0 + 18), (x1 - 18, y0 + 18), (x0 + 18, y1 - 18), (x1 - 18, y1 - 18)): cv2.circle(gl, (cx_, cy_), 18, 255, -1)
glf = cv2.GaussianBlur(gl.astype(np.float32) / 255, (0, 0), 1.0)[..., None]
out = out * (1 - glf) + np.array([14, 10, 7], np.float32) * glf

# 1e) textes déformés : LOW / HIGH / ENGAGE effacés puis réécrits proprement
def erase_text(x0, y0, x1, y1, thr=95):
    global out
    reg = np.clip(out[y0:y1, x0:x1], 0, 255).astype(np.uint8)
    m = (reg.mean(2) > thr).astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((5, 5), np.uint8))
    out[y0:y1, x0:x1] = cv2.inpaint(reg, m, 5, cv2.INPAINT_TELEA).astype(np.float32)
erase_text(1370, 985, 1456, 1018)
erase_text(1668, 988, 1760, 1022)
erase_text(2118, 455, 2290, 492)
erase_text(NUMBERS[2][0] - 26, NUMBERS[2][1] - 30, NUMBERS[2][0] + 26, NUMBERS[2][1] + 30, thr=80)

# 1f) vis de droite (en haut et en bas) supprimées : structure par inpainting + grain du boîtier repris à côté
def remove_screw(cx, cy, rad, dx):
    global out
    m = np.zeros((DH, DW), np.uint8); cv2.circle(m, (cx, cy), rad, 255, -1)
    base = cv2.inpaint(np.clip(out, 0, 255).astype(np.uint8), m, 15, cv2.INPAINT_TELEA).astype(np.float32)
    src_ = out[cy - rad - 8:cy + rad + 8, cx - rad - 8 + dx:cx + rad + 8 + dx]
    grain = src_ - cv2.GaussianBlur(src_, (0, 0), 3)
    reg = base[cy - rad - 8:cy + rad + 8, cx - rad - 8:cx + rad + 8] + grain
    mf = cv2.GaussianBlur(m.astype(np.float32) / 255, (0, 0), 3)[cy - rad - 8:cy + rad + 8, cx - rad - 8:cx + rad + 8][..., None]
    out[cy - rad - 8:cy + rad + 8, cx - rad - 8:cx + rad + 8] = out[cy - rad - 8:cy + rad + 8, cx - rad - 8:cx + rad + 8] * (1 - mf) + reg * mf
remove_screw(2366, 177, 40, -95)
remove_screw(2385, 2407, 38, -110)

# 1g) IN / OUT : effacés puis regravés au centre des jacks (lecture verticale, comme sur le design)
JACK_Y = 1234
def erase_label(x0, y0, x1, y1, dy):
    # texte clair gravé : masque large (lettres + leur liseré), structure par inpainting, grain repris plus haut sur la tranche
    global out
    reg = np.clip(out[y0:y1, x0:x1], 0, 255).astype(np.uint8)
    m = (reg.mean(2) > 75).astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((9, 9), np.uint8))
    base = cv2.inpaint(reg, m, 9, cv2.INPAINT_TELEA).astype(np.float32)
    src_ = out[y0 + dy:y1 + dy, x0:x1]
    grain = src_ - cv2.GaussianBlur(src_, (0, 0), 3)
    mf = cv2.GaussianBlur(m.astype(np.float32) / 255, (0, 0), 2)[..., None]
    out[y0:y1, x0:x1] = out[y0:y1, x0:x1] * (1 - mf) + (base + grain) * mf
erase_label(92, 1205, 160, 1345, -170)
erase_label(2384, 1274, 2442, 1348, 140)

img = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
d = ImageDraw.Draw(img)
INK = (226, 224, 222)
def text(x, y, s, size, font=FR, spacing=0.0):
    f = ImageFont.truetype(font, size)
    total = sum(f.getlength(ch) for ch in s) + spacing * (len(s) - 1)
    cx = x - total / 2
    for ch in s:
        d.text((cx, y), ch, font=f, fill=INK, anchor="lm"); cx += f.getlength(ch) + spacing
text(1412, 1001, "LOW", 26, FR, 1.0)
text(1714, 1005, "HIGH", 26, FR, 1.0)
text(2203, 473, "ENGAGE", 25, FR, 6.0)
text(NUMBERS[2][0] + 1, NUMBERS[2][1] + 1, "3", 46, FM)
def side_label(cx, cy, label, angle, size=50):
    f = ImageFont.truetype(FM, size)
    tw = int(f.getlength(label)) + 16; th_ = size + 16
    lay = Image.new("RGBA", (tw, th_), (0, 0, 0, 0))
    ImageDraw.Draw(lay).text((tw / 2, th_ / 2), label, font=f, fill=(236, 234, 232, 255), anchor="mm")
    lay = lay.rotate(angle, expand=True, resample=Image.BICUBIC)
    img.paste(lay, (round(cx - lay.width / 2), round(cy - lay.height / 2)), lay)
side_label(128, JACK_Y, "OUT", 90)
side_label(2412, JACK_Y, "IN", -90)

# ------------------------------------------------------------------ 2) images livrées (2x)
def to_asset(im): return im.resize((OW, OH), Image.LANCZOS)
plate_src = img.copy()                       # le plateau garde le design d'origine
# sous le plateau : logement sombre du boîtier (visible quand la pointe se lève ; le plateau du calque le recouvre à plat)
wm = Image.new("L", (DW, DH), 0)
ImageDraw.Draw(wm).rounded_rectangle((PLATE[0] - 2, PLATE[1] - 2, PLATE[2] + 2, PLATE[3] + 1), PLATE_R + 2, fill=255)
wm = np.asarray(wm.filter(ImageFilter.GaussianBlur(1.5)), np.float32)[..., None] / 255.0
wy = (yy - PLATE[1]) / (PLATE[3] - PLATE[1])
well = np.dstack([np.full((DH, DW), 15.0), np.full((DH, DW), 14.0), np.full((DH, DW), 17.0)]) * (0.75 + 0.5 * np.clip(wy, 0, 1))[..., None]
well += np.random.default_rng(3).normal(0, 2.0, (DH, DW, 1))
bgarr = np.asarray(img, np.float32) * (1 - wm) + well * wm
img = Image.fromarray(np.clip(bgarr, 0, 255).astype(np.uint8))
background = to_asset(img)
background.save(os.path.join(ROOT, "background.png"), optimize=True)

# plateau à plat : cadre arrondi détouré dans le design d'origine
pm = Image.new("L", (DW, DH), 0)
ImageDraw.Draw(pm).rounded_rectangle(PLATE, PLATE_R, fill=255)
pm = pm.filter(ImageFilter.GaussianBlur(1.2))
plate = plate_src.convert("RGBA"); plate.putalpha(pm)
to_asset(plate).save(os.path.join(ROOT, "treadle_down.png"), optimize=True)

# footswitch : disque adouci autour de l'écrou hexagonal
fx0, fy0 = FSW[0] - FS_HALF, FSW[1] - FS_HALF
fs = img.crop((fx0, fy0, fx0 + 2 * FS_HALF, fy0 + 2 * FS_HALF)).convert("RGBA")
fa = Image.new("L", fs.size, 0); ImageDraw.Draw(fa).ellipse((6, 6, fs.width - 6, fs.height - 6), fill=255)
fs.putalpha(fa.filter(ImageFilter.GaussianBlur(5)))
fsz = round(2 * FS_HALF * S * AS)
fs = fs.resize((fsz, fsz), Image.LANCZOS)
fs.save(os.path.join(ROOT, "fs_on.png")); fs.save(os.path.join(ROOT, "fs_off.png"))

# inverseur : levier + écrou détourés ; position AUTO = miroir
def toggle_sprite(rgb, alpha):
    shd = np.roll(np.roll(alpha, 9, 0), 6, 1)
    shd = cv2.GaussianBlur(shd, (0, 0), 5) * 0.6
    a = np.clip(alpha + shd * (1 - alpha), 0, 1)
    col = rgb * (alpha / np.maximum(a, 1e-3))[..., None]           # l'ombre est noire
    return Image.fromarray(np.dstack([col, a * 255]).clip(0, 255).astype(np.uint8), "RGBA")
tsz = (round(200 * S * AS), round(124 * S * AS))
toggle_sprite(crop, tog_alpha).resize(tsz, Image.LANCZOS).save(os.path.join(ROOT, "toggle_l.png"))
toggle_sprite(crop[:, ::-1].copy(), tog_alpha[:, ::-1].copy()).resize(tsz, Image.LANCZOS).save(os.path.join(ROOT, "toggle_r.png"))

# ------------------------------------------------------------------ 3) coordonnées pour PluginEditor.cpp
with open(os.path.join(ROOT, "tools", "layout.txt"), "w") as fo:
    fo.write(f"frame {FW} x {int(FH)}  (assets x{AS})  S {S:.6f}\n")
    fo.write(f"plate L {F(PLATE[0]):.1f} T {F(PLATE[1]):.1f} R {F(PLATE[2]):.1f} hinge {F(PLATE[3]):.1f} radius {F(PLATE_R):.1f}\n")
    fo.write(f"knob {F(KNOB[0]):.1f} {F(KNOB[1]):.1f} R {F(KNOB[2]):.1f} cap {F(KNOB[3]):.1f}\n")
    for n_, (x, y) in zip(("capRange", "capQ", "capPurr", "capBoost"), CAPS): fo.write(f"{n_} {F(x):.1f} {F(y):.1f}\n")
    for i, (x, y) in enumerate(NUMBERS): fo.write(f"num{i + 1} {F(x):.1f} {F(y):.1f}\n")
    for n_, (x, y) in zip(("q", "purr", "boost"), SMALL): fo.write(f"{n_} {F(x):.1f} {F(y):.1f}\n")
    fo.write(f"smallR {F(SMALL_R):.1f} smallCap {F(SMALL_CAP):.1f}\n")
    fo.write(f"screen {F(SCREEN[0]):.1f} {F(SCREEN[1]):.1f} {F(SCREEN[2] - SCREEN[0]):.1f} {F(SCREEN[3] - SCREEN[1]):.1f}\n")
    fo.write(f"fs {F(fx0):.1f} {F(fy0):.1f} {F(2 * FS_HALF):.1f}\nled {F(LED[0]):.1f} {F(LED[1]):.1f}\n")
    fo.write(f"toggle {F(tx0):.1f} {F(ty0):.1f} {F(200):.1f} {F(124):.1f}\n")
print("ok", FW, OW, OH)
