# CRYCAT : génère tout le visuel du plugin (aucun design source : rendu procédural, lumière en haut à gauche)
#   background.png   : fond 1020 x 928 (boîtier noir craquelé, panneau de commandes, écran, jacks Studio Wiga)
#   treadle_down.png : calque du plateau à plat (orteil enfoncé) ; le plugin calcule les autres positions en perspective
#   fs_on.png / fs_off.png, toggle_l.png / toggle_r.png : sprites des commandes
#   tools/layout.txt : coordonnées des commandes (reprises dans PluginEditor.cpp)
#   design/apercu_treadle_up.png : aperçu orteil levé
import os, math
import numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1020, 928
SS = 2                                    # suréchantillonnage du rendu
WW, HH = W * SS, H * SS
rng = np.random.default_rng(7)
FR = os.path.join(ROOT, "fonts", "Poppins-Regular.ttf"); FL = os.path.join(ROOT, "fonts", "Poppins-Light.ttf")
FM = os.path.join(ROOT, "fonts", "Poppins-Medium.ttf"); FB = os.path.join(ROOT, "fonts", "Poppins-Bold.ttf")
INK = (236, 229, 224)
AMBER = (255, 176, 64)

yy, xx = np.mgrid[0:HH, 0:WW].astype(np.float32)
xx = (xx + 0.5) / SS; yy = (yy + 0.5) / SS      # coordonnées dans le repère 1020 x 928

LIGHT = np.array([-0.42, -0.58, 0.70], np.float32); LIGHT /= np.linalg.norm(LIGHT)
HALF = LIGHT + np.array([0, 0, 1], np.float32); HALF /= np.linalg.norm(HALF)

# ------------------------------------------------------------------ outils
def sd_rrect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    bx, by = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(xx - cx) - bx, np.abs(yy - cy) - by
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r

def sd_circle(cx, cy, r): return np.hypot(xx - cx, yy - cy) - r

def cover(d, aa=0.6):                           # couverture anti-crénelée (d < 0 à l'intérieur)
    return np.clip(0.5 - d / aa, 0, 1).astype(np.float32)

def bevel(d, width, height, profile="round"):
    t = np.clip(-d / width, 0, 1)
    p = np.sqrt(1 - (1 - t) ** 2) if profile == "round" else t * t * (3 - 2 * t)
    return (p * height).astype(np.float32)

def normals(h):
    gy, gx = np.gradient(h * SS)                 # h en pixels 1x -> pente
    n = np.dstack([-gx, -gy, np.ones_like(h)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)

def shade(n, albedo, amb=0.22, diff=0.85, spec=0.25, shin=30.0, spec_col=(1, 1, 1)):
    ndl = np.clip(n @ LIGHT, 0, 1)[..., None]
    ndh = np.clip(n @ HALF, 0, 1)[..., None]
    a = np.asarray(albedo, np.float32)
    return a * (amb + diff * ndl) + 255.0 * spec * ndh ** shin * np.asarray(spec_col, np.float32)

def chrome(n, tint=(1.0, 1.0, 1.0), bands=1.0, pos_wave=True):
    # chrome poli : environnement de studio (ciel clair en haut, horizon sombre, sol gris), lu par le vecteur réfléchi
    nz = n[..., 2]
    rx, ry, rz = 2 * nz * n[..., 0], 2 * nz * n[..., 1], 2 * nz * nz - 1
    t = ry / np.maximum(np.hypot(ry, rz), 1e-4)  # -1 : vers le haut, +1 : vers le bas
    env = np.interp(t, [-1.0, -0.55, -0.15, 0.05, 0.18, 0.5, 1.0], [250, 236, 205, 70, 115, 150, 105]).astype(np.float32)
    if pos_wave:
        # grands reflets diagonaux qui varient sur la surface (évite un aplat sur les faces planes)
        env += bands * (34 * np.sin((xx * 0.9 + yy * 0.55) / 70.0) + 18 * np.sin((xx * 0.4 - yy) / 31.0))
    env += 0.35 * (rx * -60)
    col = np.clip(env, 0, 255)[..., None] * np.asarray(tint, np.float32)[None, None, :]
    spec = np.clip(n @ HALF, 0, 1) ** 60 * 255
    return col + spec[..., None]

def blur(a, s): return cv2.GaussianBlur(a, (0, 0), s * SS)

def noise(sigma, amp=1.0, seed=None):
    r = np.random.default_rng(seed) if seed is not None else rng
    nz = r.standard_normal((HH, WW)).astype(np.float32)
    nz = cv2.GaussianBlur(nz, (0, 0), sigma * SS)
    return nz / (nz.std() + 1e-6) * amp

def over(base, col, alpha):
    a = np.clip(alpha, 0, 1)[..., None]
    return base * (1 - a) + col * a

def shadow(mask, dx, dy, s, strength):
    m = np.roll(np.roll(mask, int(dy * SS), 0), int(dx * SS), 1)
    return np.clip(blur(m, s) * strength, 0, 1)

def to_img(arr): return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))

def down(im): return im.resize((W, H), Image.LANCZOS)

# ------------------------------------------------------------------ géométrie (repère 1020 x 928)
BODY = (34, 12, 986, 916, 46)
PLATE = (84.0, 35.0, 528.0, 880.0)          # plateau à plat : gauche, haut, droite, charnière
PANEL_X0 = 556
KNOB = (633.0, 300.0); KNOB_R, KNOB_CAP = 60.0, 44.0           # sélecteur RANGE (6 crans)
SMALL = [(632.0, 528.0), (752.0, 528.0), (872.0, 528.0)]        # Q, PURR, BOOST
SMALL_R, SMALL_CAP = 42.0, 32.0
SCREEN = (744.0, 186.0, 948.0, 418.0)                           # vitre (zone dessinée par le plugin)
FS = (681.0, 668.0, 819.0, 806.0)                               # footswitch (carré du sprite)
FS_LED = (857.0, 737.0)
TOGGLE = (858.0, 92.0, 922.0, 140.0)                             # inverseur MAN / AUTO
JACK_Y = 470.0

# ------------------------------------------------------------------ 1) fond (béton sombre)
bgn = noise(1.2, 9, 1) + noise(6, 7, 2) + noise(40, 10, 3)
vign = 1.0 - 0.45 * np.clip(np.hypot((xx - W / 2) / (W * 0.62), (yy - H / 2) / (H * 0.62)), 0, 1.4) ** 2
img = np.dstack([(46 + bgn) * vign, (47 + bgn) * vign, (50 + bgn) * vign]).astype(np.float32)

# ombre portée du boîtier
d_body = sd_rrect(*BODY)
body_m = cover(d_body)
img *= (1 - 0.75 * shadow(body_m, 10, 16, 14, 1.0))[..., None]

# ------------------------------------------------------------------ 2) boîtier : fonte peinte en noir craquelé
crk1 = noise(0.9, 1.0, 11); crk2 = noise(2.2, 1.0, 12)
crinkle = (1 - np.abs(crk1)) * 0.55 + (1 - np.abs(crk2)) * 0.45        # rides du craquelé
h_body = bevel(d_body, 16, 14) + crinkle * 0.55 + noise(25, 0.6, 13)
# filet gravé qui entoure le panneau de commandes
d_panel = sd_rrect(PANEL_X0, 34, 962, 894, 26)
groove = np.exp(-(d_panel / 1.4) ** 2)
h_body -= groove * 1.6
n_body = normals(h_body)
albedo_body = np.array([27, 25, 30], np.float32) * (1 + 0.08 * noise(60, 1, 14))[..., None]
body_col = shade(n_body, albedo_body, amb=0.30, diff=0.9, spec=0.16, shin=14, spec_col=(0.95, 0.92, 1.0))
body_col += shade(n_body, 0, amb=0, diff=0, spec=0.10, shin=80)              # vernis
# arête supérieure du biseau qui accroche la lumière
edge = np.exp(-((d_body + 2.5) / 1.6) ** 2) * np.clip(-(xx - W / 2) / W - (yy - H / 2) / H + 0.6, 0, 1.2)
body_col += (edge * 70)[..., None]
img = over(img, body_col, body_m)

# ------------------------------------------------------------------ 3) logement du plateau (creux sombre) + charnière
L0, T0, R0, B0 = PLATE
d_well = sd_rrect(L0 - 10, T0 - 6, R0 + 10, B0 + 18, 34)
well = cover(d_well)
img = over(img, img * 0.35, well * 0.9)
img *= (1 - 0.6 * blur(well, 5) * (1 - cover(d_well + 10)))[..., None] ** 0.5
# charnière chromée (axe visible sous le talon)
hx0, hx1, hy = L0 + 40, R0 - 40, B0 + 6
d_h = np.maximum(np.abs(yy - hy) - 9, np.maximum(hx0 - xx, xx - hx1))
hm = cover(d_h)
cyl = np.clip((yy - hy) / 9.0, -1, 1)
n_h = np.dstack([np.zeros_like(cyl), cyl, np.sqrt(np.clip(1 - cyl ** 2, 0, 1))])
img = over(img, chrome(n_h, (0.95, 0.95, 1.0), pos_wave=False) * 0.9, hm)

# ------------------------------------------------------------------ 4) panneau : écran, boutons, textes
canvas = img.copy()

# sérigraphie (dessinée sur un calque PIL suréchantillonné)
def text_layer():
    return Image.new("L", (WW, HH), 0)

def F(path, size): return ImageFont.truetype(path, round(size * SS))

ink_l = text_layer(); dI = ImageDraw.Draw(ink_l)
amb_l = text_layer(); dA = ImageDraw.Draw(amb_l)

def T(d, x, y, s, font, size, anchor="mm", spacing=0.0):
    f = F(font, size)
    if not spacing:
        d.text((x * SS, y * SS), s, font=f, fill=255, anchor=anchor); return
    total = sum(f.getlength(ch) for ch in s) + spacing * SS * (len(s) - 1)
    cx = x * SS - (total / 2 if anchor[0] == "m" else 0)
    for ch in s:
        d.text((cx, y * SS), ch, font=f, fill=255, anchor="l" + anchor[1]); cx += f.getlength(ch) + spacing * SS

# filets de séparation
for (y, x0, x1) in ((452, 586, 932), (628, 586, 932)):
    dI.line((x0 * SS, y * SS, x1 * SS, y * SS), fill=150, width=SS)
dI.line((586 * SS, 70 * SS, 838 * SS, 70 * SS), fill=150, width=SS)

# graduations du sélecteur RANGE : 6 crans sur 250°
RANGE_A0, RANGE_SPAN = -125.0, 250.0
for i in range(6):
    a = math.radians(RANGE_A0 + RANGE_SPAN * i / 5)
    sx, sy = math.sin(a), -math.cos(a)
    r0, r1 = KNOB_R + 6, KNOB_R + 15
    dI.line(((KNOB[0] + sx * r0) * SS, (KNOB[1] + sy * r0) * SS, (KNOB[0] + sx * r1) * SS, (KNOB[1] + sy * r1) * SS), fill=255, width=round(2.2 * SS))
    rt = KNOB_R + 24
    T(dI, KNOB[0] + sx * rt, KNOB[1] + sy * rt, str(i + 1), FM, 17)
T(dI, KNOB[0], KNOB[1] + KNOB_R + 34, "RANGE", FR, 15, spacing=3)
# petits arcs « bass » / « bright »
T(dI, KNOB[0] - 62, KNOB[1] + KNOB_R + 4, "LOW", FL, 11)
T(dI, KNOB[0] + 62, KNOB[1] + KNOB_R + 4, "HIGH", FL, 11)

# graduations des petits boutons (11 traits sur 270°)
for (cx, cy), name in zip(SMALL, ("Q", "PURR", "BOOST")):
    for k in range(11):
        a = math.radians(-135 + 27 * k)
        sx, sy = math.sin(a), -math.cos(a)
        r0, r1 = SMALL_R + 4, SMALL_R + (12 if k in (0, 5, 10) else 8)
        dI.line(((cx + sx * r0) * SS, (cy + sy * r0) * SS, (cx + sx * r1) * SS, (cy + sy * r1) * SS), fill=230, width=round(1.6 * SS))
    T(dI, cx, cy + SMALL_R + 30, name, FR, 16, spacing=1.5)

# inverseur MAN / AUTO
T(dI, TOGGLE[0] - 8, (TOGGLE[1] + TOGGLE[3]) / 2, "MAN", FR, 12, anchor="rm")
T(dI, TOGGLE[2] + 8, (TOGGLE[1] + TOGGLE[3]) / 2, "AUTO", FR, 12, anchor="lm")
T(dI, (TOGGLE[0] + TOGGLE[2]) / 2, TOGGLE[3] + 16, "ENGAGE", FL, 10, spacing=2)

# nom + sous-titre
T(dI, 590, 846, "CRYCAT", FL, 46, anchor="lm", spacing=14)
T(dI, 592, 884, "VOCAL WAH  ·  6 RANGES  ·  AUTO-ENGAGE", FR, 12.5, anchor="lm", spacing=0.6)
dI.line((592 * SS, 902 * SS, 930 * SS, 902 * SS), fill=110, width=SS)
# oreilles de chat au-dessus du premier C (clin d'œil discret, en ambre)
ex, ey = 595, 833
for ox in (0, 17):
    dA.polygon([((ex + ox) * SS, ey * SS), ((ex + ox + 5) * SS, (ey - 11) * SS), ((ex + ox + 11) * SS, ey * SS)], fill=255)

ink = np.asarray(ink_l, np.float32) / 255.0
ambm = np.asarray(amb_l, np.float32) / 255.0
canvas = over(canvas, np.array(INK, np.float32), ink * 0.93)
canvas = over(canvas, np.array(AMBER, np.float32), ambm * 0.95)

# logo Studio Wiga (kit)
logo = Image.open(os.path.join(ROOT, "logo_studio_wiga_blanc.png")).convert("RGBA")
lw = round(150 * SS); logo = logo.resize((lw, round(lw * logo.height / logo.width)), Image.LANCZOS)
la = np.zeros((HH, WW), np.float32)
lx, ly = round(586 * SS), round(52 * SS - logo.height / 2)
la[ly:ly + logo.height, lx:lx + logo.width] = np.asarray(logo)[..., 3] / 255.0
canvas = over(canvas, np.array(INK, np.float32), la * 0.95)

# ---- écran : cadre noir en creux + vitre ----
sx0, sy0, sx1, sy1 = SCREEN
d_bz = sd_rrect(sx0 - 11, sy0 - 11, sx1 + 11, sy1 + 11, 16)
bz = cover(d_bz)
canvas *= (1 - 0.6 * shadow(bz, 4, 7, 6, 1.0))[..., None]
h_bz = bevel(d_bz, 6, 5) - bevel(sd_rrect(sx0 - 2, sy0 - 2, sx1 + 2, sy1 + 2, 9), 3, 6)
bz_col = shade(normals(h_bz), np.array([20, 19, 22], np.float32), amb=0.4, spec=0.35, shin=40)
canvas = over(canvas, bz_col, bz)
glass = cover(sd_rrect(sx0, sy0, sx1, sy1, 9))
canvas = over(canvas, np.array([14, 9, 6], np.float32), glass)

# ---- boutons chromés (moletage + chapeau tourné) ----
def knob(cx, cy, R, cap, knurl=36):
    global canvas
    d_o = sd_circle(cx, cy, R)
    m = cover(d_o)
    canvas *= (1 - 0.7 * shadow(m, 5, 9, 7, 1.0))[..., None]
    ang = np.arctan2(yy - cy, xx - cx)
    rr = np.hypot(xx - cx, yy - cy)
    # jupe moletée
    h = bevel(d_o, R - cap + 2, 10) + 0.9 * np.cos(ang * knurl * 2) * np.clip((rr - cap) / 3, 0, 1) * (rr < R)
    n = normals(h)
    skirt = chrome(n, (0.92, 0.93, 0.97), bands=0.6) * 0.82
    canvas = over(canvas, skirt, m)
    # chapeau : métal tourné (reflets en ailes de papillon fixes)
    d_c = sd_circle(cx, cy, cap)
    cm = cover(d_c)
    ring = 0.5 + 0.5 * np.cos(2 * (ang - math.radians(-128)))
    spun = 120 + 95 * ring ** 1.6 + 7 * np.sin(rr * 2.3) + noise(0.35, 4, None) * 0.6
    edge = bevel(d_c, 4, 3)
    n_c = normals(edge)
    rim = np.clip(n_c @ LIGHT, 0, 1)
    cap_col = np.dstack([spun, spun, spun * 1.03]) * (0.82 + 0.3 * rim[..., None])
    cap_col = cap_col + 30 * (1 - rr / cap)[..., None] * (rr < cap)[..., None]
    canvas = over(canvas, cap_col, cm)
    # liseré sombre entre jupe et chapeau
    canvas = over(canvas, np.array([30, 30, 34], np.float32), np.exp(-((d_c - 0.5) / 0.9) ** 2) * 0.7)

knob(*KNOB, KNOB_R, KNOB_CAP, knurl=48)
for c in SMALL: knob(*c, SMALL_R, SMALL_CAP, knurl=30)

# ---- vis de façade ----
def screw(cx, cy, r=8.5, slot_a=0.5):
    global canvas
    d = sd_circle(cx, cy, r); m = cover(d)
    canvas *= (1 - 0.5 * shadow(m, 2, 3, 2, 1.0))[..., None]
    h = bevel(d, r, 3.5)
    # empreinte cruciforme
    u = (xx - cx) * math.cos(slot_a) + (yy - cy) * math.sin(slot_a)
    v = -(xx - cx) * math.sin(slot_a) + (yy - cy) * math.cos(slot_a)
    slot = ((np.abs(u) < 1.3) | (np.abs(v) < 1.3)) & (np.hypot(u, v) < r * 0.6)
    h = h - slot * 2.0
    canvas = over(canvas, chrome(normals(h), (0.9, 0.9, 0.95), pos_wave=False) * 0.8, m)

for (x, y) in ((952, 48), (952, 880)): screw(x, y, slot_a=0.4 + x * 0.01)

# ---- jacks (kit Studio Wiga) + inscriptions sur la tranche ----
cimg = to_img(canvas).convert("RGBA")
jo = Image.open(os.path.join(ROOT, "embase_jack_gauche_OUT.png")); ji = Image.open(os.path.join(ROOT, "embase_jack_droite_IN.png"))
js = 0.36 * SS
jo = jo.resize((round(jo.width * js), round(jo.height * js)), Image.LANCZOS); ji = ji.resize((round(ji.width * js), round(ji.height * js)), Image.LANCZOS)
cimg.alpha_composite(jo, (round(BODY[0] * SS - jo.width + 4 * SS), round(JACK_Y * SS - jo.height / 2)))
cimg.alpha_composite(ji, (round(BODY[2] * SS - 4 * SS), round(JACK_Y * SS - ji.height / 2)))

def side_label(x, y, s_, rot):
    f = F(FM, 19)
    tw = int(f.getlength(s_)) + 10; th = round(30 * SS)
    im = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((tw / 2, th / 2), s_, font=f, fill=INK + (235,), anchor="mm")
    im = im.rotate(rot, expand=True, resample=Image.BICUBIC)
    cimg.alpha_composite(im, (round(x * SS - im.width / 2), round(y * SS - im.height / 2)))
side_label(BODY[0] + 20, JACK_Y, "OUT", 90)
side_label(BODY[2] - 20, JACK_Y, "IN", -90)

background = down(cimg)
background.convert("RGB").save(os.path.join(ROOT, "background.png"))

# ------------------------------------------------------------------ 5) plateau (orteil enfoncé), calque séparé
d_pl = sd_rrect(L0, T0, R0, B0, 30)
pm = cover(d_pl)
# cadre chromé
h_pl = bevel(d_pl, 14, 12)
IN0, INT0, INR, INB = L0 + 30, T0 + 34, R0 - 30, B0 - 38
d_rub = sd_rrect(IN0, INT0, INR, INB, 18)
h_pl -= bevel(-d_rub - 0.0, 1, 0) * 0      # (garde la forme)
lip = np.exp(-((d_rub - 1.5) / 2.2) ** 2)    # lèvre arrondie autour du caoutchouc
h_pl += lip * 2.5
n_pl = normals(h_pl)
frame_col = chrome(n_pl, (0.93, 0.94, 0.98), bands=1.0)
# léger brossé dans le sens de la longueur
frame_col += (noise(0.4, 1, 21) * 4)[..., None]
plate = frame_col.copy()

# caoutchouc : nervures transversales, interrompues par un médaillon à l'orteil
rub = cover(d_rub)
ry0, ry1 = INT0 + 18, INB - 14
period = 19.0
ph = (yy - ry0) / period
rib = 0.5 - 0.5 * np.cos(2 * math.pi * ph)
rib = rib ** 0.6 * 4.0
rib *= np.clip((xx - IN0 - 14) / 6, 0, 1) * np.clip((INR - 14 - xx) / 6, 0, 1)
rib *= np.clip((yy - ry0) / 4, 0, 1) * np.clip((ry1 - yy) / 4, 0, 1)
MED = ((L0 + R0) / 2, T0 + 140.0); MED_R = (92.0, 70.0)
med_d = np.hypot((xx - MED[0]) / MED_R[0], (yy - MED[1]) / MED_R[1]) - 1.0
rib *= np.clip(med_d * 12, 0, 1)                                # pas de nervures dans le médaillon
h_rub = bevel(d_rub, 6, 3) + rib + noise(0.5, 0.18, 22)

# médaillon : tête de chat en chrome poli, yeux en émail ambre (fente noire)
cat_l = Image.new("L", (WW, HH), 0); dc = ImageDraw.Draw(cat_l)
cx, cy = MED[0], MED[1] + 10
dc.ellipse(((cx - 46) * SS, (cy - 33) * SS, (cx + 46) * SS, (cy + 37) * SS), fill=255)
dc.polygon([((cx - 45) * SS, (cy - 4) * SS), ((cx - 40) * SS, (cy - 66) * SS), ((cx - 6) * SS, (cy - 30) * SS)], fill=255)
dc.polygon([((cx + 45) * SS, (cy - 4) * SS), ((cx + 40) * SS, (cy - 66) * SS), ((cx + 6) * SS, (cy - 30) * SS)], fill=255)
catm = np.asarray(cat_l, np.float32) / 255.0
h_cat = np.clip(cv2.GaussianBlur(catm, (0, 0), 4.5 * SS) * 1.25, 0, 1) ** 0.8 * 8.0
h_cat += cv2.GaussianBlur(catm, (0, 0), 14 * SS) * 6.0     # face légèrement bombée
eyes_l = Image.new("L", (WW, HH), 0); de = ImageDraw.Draw(eyes_l)
pup_l = Image.new("L", (WW, HH), 0); dp = ImageDraw.Draw(pup_l)
for ex in (-19, 19):     # yeux en amande, pupille en fente
    de.polygon([((cx + ex - 13) * SS, (cy - 1) * SS), ((cx + ex) * SS, (cy - 9) * SS), ((cx + ex + 13) * SS, (cy - 1) * SS), ((cx + ex) * SS, (cy + 7) * SS)], fill=255)
    dp.ellipse(((cx + ex - 2.2) * SS, (cy - 8) * SS, (cx + ex + 2.2) * SS, (cy + 6) * SS), fill=255)
eyes = cv2.GaussianBlur(np.asarray(eyes_l, np.float32) / 255.0, (0, 0), 0.6 * SS)
pup = cv2.GaussianBlur(np.asarray(pup_l, np.float32) / 255.0, (0, 0), 0.5 * SS)
nose_l = Image.new("L", (WW, HH), 0)
ImageDraw.Draw(nose_l).polygon([((cx - 5) * SS, (cy + 13) * SS), ((cx + 5) * SS, (cy + 13) * SS), (cx * SS, (cy + 19) * SS)], fill=255)
nose = cv2.GaussianBlur(np.asarray(nose_l, np.float32) / 255.0, (0, 0), 0.5 * SS)
h_rub += cv2.GaussianBlur(catm, (0, 0), 1.2 * SS) * 1.5        # le chat est posé sur le caoutchouc
n_rub = normals(h_rub)
rub_col = shade(n_rub, np.array([22, 21, 23], np.float32), amb=0.35, diff=0.95, spec=0.10, shin=10)
rub_col += shade(n_rub, 0, amb=0, diff=0, spec=0.06, shin=60)
# ombre du chat sur le caoutchouc
rub_col *= (1 - 0.6 * shadow(catm, 3, 5, 3, 1.0))[..., None]
cat_cover = cv2.GaussianBlur(catm, (0, 0), 0.4 * SS)
cat_col = chrome(normals(h_cat), (0.96, 0.95, 0.93), bands=0.8)
rub_col = over(rub_col, cat_col, cat_cover)
eye_col = np.array(AMBER, np.float32) * (0.75 + 0.45 * np.clip(1 - (yy - (cy - 8)) / 16, 0, 1))[..., None]
rub_col = over(rub_col, eye_col, eyes)
rub_col = over(rub_col, np.array([12, 8, 6], np.float32), pup)
rub_col = over(rub_col, np.array([40, 34, 34], np.float32), nose)
plate = over(plate, rub_col, rub)
# ombre interne du caoutchouc (il est en retrait du cadre)
inner_sh = np.clip(blur(1 - rub, 3) * rub, 0, 1)
plate *= (1 - 0.5 * inner_sh)[..., None]
# vis du cadre
pimg = Image.fromarray(np.dstack([np.clip(plate, 0, 255), pm * 255]).astype(np.uint8), "RGBA")
canvas_backup = canvas
canvas = plate
for (x, y) in ((L0 + 15, T0 + 16), (R0 - 15, T0 + 16), (L0 + 15, B0 - 18), (R0 - 15, B0 - 18)):
    screw(x, y, r=7.5, slot_a=0.3 + y * 0.01)
plate = canvas; canvas = canvas_backup
pimg = Image.fromarray(np.dstack([np.clip(plate, 0, 255), pm * 255]).astype(np.uint8), "RGBA")
treadle = down(pimg)
treadle.save(os.path.join(ROOT, "treadle_down.png"))

# ------------------------------------------------------------------ 6) sprites footswitch / inverseur (rendus à part)
def render_sprite(size, fn):
    global xx, yy, HH, WW
    sv = (xx, yy, HH, WW)
    WW = HH = size * SS
    yy, xx = np.mgrid[0:HH, 0:WW].astype(np.float32)
    xx = (xx + 0.5) / SS; yy = (yy + 0.5) / SS
    out = fn(size)
    xx, yy, HH, WW = sv
    return out

def fs_sprite(sz):
    c = sz / 2
    col = np.zeros((HH, WW, 3), np.float32); a = np.zeros((HH, WW), np.float32)
    # écrou hexagonal
    ang = np.arctan2(yy - c, xx - c); rr = np.hypot(xx - c, yy - c)
    hexr = 54 * math.cos(math.pi / 6) / np.cos(((ang + math.pi / 6) % (math.pi / 3)) - math.pi / 6)
    d_hex = rr - hexr
    h = bevel(d_hex, 5, 4)
    col = over(col, chrome(normals(h), (0.9, 0.9, 0.94), pos_wave=False) * 0.85, cover(d_hex)); a = np.maximum(a, cover(d_hex))
    # bague
    d_r = rr - 44; h2 = bevel(d_r, 7, 6)
    col = over(col, col * 0.4, np.clip(blur(cover(d_r), 2) - cover(d_r - 2), 0, 1))
    col = over(col, chrome(normals(h2), (0.94, 0.94, 0.98), pos_wave=False), cover(d_r))
    # capuchon bombé
    d_cp = rr - 32
    dome = np.sqrt(np.clip(1 - (rr / 32) ** 2, 0, 1)) * 7
    col = over(col, chrome(normals(dome + bevel(d_cp, 2, 1)), (0.96, 0.96, 1.0), pos_wave=False), cover(d_cp))
    return Image.fromarray(np.dstack([np.clip(col, 0, 255), a * 255]).astype(np.uint8), "RGBA").resize((sz, sz), Image.LANCZOS)

fs = render_sprite(round(FS[2] - FS[0]), fs_sprite)
fs.save(os.path.join(ROOT, "fs_on.png")); fs.save(os.path.join(ROOT, "fs_off.png"))

def toggle_sprite(right):
    def f(sz):
        c = sz / 2
        col = np.zeros((HH, WW, 3), np.float32); a = np.zeros((HH, WW), np.float32)
        rr = np.hypot(xx - c, yy - c)
        ang = np.arctan2(yy - c, xx - c)
        hexr = 15 * math.cos(math.pi / 6) / np.cos(((ang + math.pi / 6) % (math.pi / 3)) - math.pi / 6)
        d_hex = rr - hexr
        col = over(col, chrome(normals(bevel(d_hex, 2.5, 2)), (0.9, 0.9, 0.94), pos_wave=False) * 0.85, cover(d_hex)); a = np.maximum(a, cover(d_hex))
        d_b = rr - 9
        col = over(col, chrome(normals(bevel(d_b, 4, 4)), (0.95, 0.95, 0.98), pos_wave=False), cover(d_b))
        # levier (batte) incliné vers la gauche ou la droite, vu de dessus
        sgn = 1 if right else -1
        x0, x1 = c, c + sgn * 27
        t = np.clip((xx - x0) / (x1 - x0), 0, 1)
        wdt = 3.2 + 2.6 * t
        d_lv = np.maximum(np.abs(yy - c) - wdt, -np.minimum((xx - x0) * sgn, (x1 - xx) * sgn))
        d_lv = np.minimum(d_lv, np.hypot(xx - x1, yy - c) - 5.8)
        lv = cover(d_lv)
        # ombre du levier
        sh = shadow(lv, 3, 5, 2, 0.7)
        col = col * (1 - 0.6 * sh[..., None]); a = np.maximum(a, sh * 0.6)
        cyl = np.clip((yy - c) / np.maximum(wdt, 1), -1, 1)
        n = np.dstack([np.zeros_like(cyl), cyl, np.sqrt(np.clip(1 - cyl ** 2, 0, 1))])
        col = over(col, chrome(n, (0.97, 0.97, 1.0), pos_wave=False) * (0.85 + 0.2 * t[..., None]), lv); a = np.maximum(a, lv)
        return Image.fromarray(np.dstack([np.clip(col, 0, 255), a * 255]).astype(np.uint8), "RGBA").resize((sz, sz), Image.LANCZOS)
    return f

TG_SZ = 64
render_sprite(TG_SZ, toggle_sprite(False)).save(os.path.join(ROOT, "toggle_l.png"))
render_sprite(TG_SZ, toggle_sprite(True)).save(os.path.join(ROOT, "toggle_r.png"))
TGC = ((TOGGLE[0] + TOGGLE[2]) / 2, (TOGGLE[1] + TOGGLE[3]) / 2)

# ------------------------------------------------------------------ 7) aperçu orteil levé (même calcul que le plugin)
rise, widen = 26.0, 1.035
cxm = (L0 + R0) / 2
M = cv2.getPerspectiveTransform(np.float32([[L0, T0], [R0, T0], [R0, B0], [L0, B0]]),
                                np.float32([[cxm - (cxm - L0) * widen, T0 - rise], [cxm + (R0 - cxm) * widen, T0 - rise], [R0, B0], [L0, B0]]))
up = cv2.warpPerspective(np.array(treadle), M, (W, H), flags=cv2.INTER_CUBIC)
prev = background.convert("RGBA"); prev.alpha_composite(Image.fromarray(up, "RGBA"))
for (sp, box) in ((fs, FS), (render_sprite(TG_SZ, toggle_sprite(True)), (TGC[0] - 32, TGC[1] - 32, TGC[0] + 32, TGC[1] + 32))):
    prev.alpha_composite(sp, (round(box[0]), round(box[1])))
prev.convert("RGB").save(os.path.join(ROOT, "design", "apercu_treadle_up.png"))

with open(os.path.join(ROOT, "tools", "layout.txt"), "w") as fo:
    fo.write(f"knob {KNOB[0]} {KNOB[1]} R {KNOB_R} cap {KNOB_CAP} arc {RANGE_A0} {RANGE_SPAN}\n")
    for n, (x, y) in zip(("q", "purr", "boost"), SMALL): fo.write(f"{n} {x} {y}\n")
    fo.write(f"smallR {SMALL_R} smallCap {SMALL_CAP}\nscreen {SCREEN}\nfs {FS}\nfsled {FS_LED}\ntoggle centre {TGC} size {TG_SZ}\nplate {PLATE}\n")
print("ok")
