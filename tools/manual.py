# CRYCAT (Studio Wiga) - notice PDF bilingue (anglais puis français), illustrée avec le rendu réel du plugin.
# Usage : python3 tools/manual.py <dossier des captures (tools/render)> <fichier PDF de sortie>
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = "1.1.1"
TMP = os.path.dirname(os.path.abspath(OUT))

for n, f in (("Pop", "Poppins-Regular"), ("PopL", "Poppins-Light"), ("PopM", "Poppins-Medium"), ("PopB", "Poppins-Bold")):
    pdfmetrics.registerFont(TTFont(n, os.path.join(ROOT, "fonts", f + ".ttf")))

ACC = colors.HexColor("#D9881A"); DARK = colors.HexColor("#1C1A1E"); GREY = colors.HexColor("#6A6470"); LIGHT = colors.HexColor("#F6F0E6")

# ---------- capture annotée ----------
src = Image.open(os.path.join(SHOTS, "crycat_100pc_milieu.png")).convert("RGB")
k = src.height / 928.0
marks = [  # (numéro, x, y) dans le repère 1020 x 928 du plugin
    (1, 270, 450), (2, 566, 297), (3, 754, 252), (4, 795, 129), (5, 562, 522), (6, 673, 521), (7, 783, 523),
    (8, 670, 722), (9, 772, 725), (10, 893, 444), (11, 16, 444)]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(ROOT, "fonts", "Poppins-Bold.ttf"), 22)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 17
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(217, 136, 26), width=4)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(150, 80, 10), anchor="mm")
annotated = os.path.join(TMP, "crycat_annotated.png"); ann.save(annotated)

# talon / pointe côte à côte
a = Image.open(os.path.join(SHOTS, "crycat_100pc_talon.png")).convert("RGB")
b = Image.open(os.path.join(SHOTS, "crycat_100pc_pointe.png")).convert("RGB")
pa, pb = a.crop((0, 0, int(500 * k), a.height)), b.crop((0, 0, int(500 * k), b.height))
both = Image.new("RGB", (pa.width * 2 + 24, pa.height), (255, 255, 255)); both.paste(pa, (0, 0)); both.paste(pb, (pa.width + 24, 0))
heeltoe = os.path.join(TMP, "crycat_heeltoe.png"); both.save(heeltoe)

# gros plan de l'écran (milieu de course)
scr = src.crop((int(646 * k), int(189 * k), int(863 * k), int(435 * k))).resize((434, 492), Image.LANCZOS)
screen_img = os.path.join(TMP, "crycat_screen.png"); scr.save(screen_img)
cover = os.path.join(SHOTS, "crycat_100pc_milieu.png")

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
    "cover": ParagraphStyle("cover", fontName="PopL", fontSize=40, leading=46, textColor=DARK, alignment=TA_CENTER),
    "coversub": ParagraphStyle("coversub", fontName="Pop", fontSize=12, leading=16, textColor=GREY, alignment=TA_CENTER),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#E2D8CA")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

# ---------- données (identiques au code livré : CrycatDSP.h et PluginProcessor.cpp) ----------
RANGES = [("1", "Low / 7-string", 220, 900), ("2", "Dark", 320, 1250), ("3", "GCB95 Classic", 450, 1600),
          ("4", "Vintage (V847)", 480, 2100), ("5", "Bright", 600, 2600), ("6", "Shriek", 750, 3200)]
RANGE_DESC = {
    "en": ["Lowest sweep: bass, baritone and 7-string guitars tuned to G or lower.", "Darker, rounder sweep, jazz and smooth rhythm parts.",
           "The original Cry Baby GCB95 sweep (default).", "Wider, more open sweep in the spirit of the early Vox V847.",
           "Brighter toe for funk and clean parts.", "Highest sweep: screaming leads, cuts through dense mixes."],
    "fr": ["Balayage le plus grave : basse, baryton et 7 cordes accordée en G ou plus bas.", "Balayage plus sombre et rond, jazz et rythmiques douces.",
           "Le balayage d'origine de la Cry Baby GCB95 (par défaut).", "Balayage plus large et ouvert, dans l'esprit de la Vox V847 des débuts.",
           "Pointe plus brillante pour le funk et les sons clairs.", "Balayage le plus aigu : solos hurlants, passe à travers un mix chargé."]}
FACTORY = [  # nom, plage, Q, purr, boost, auto, pédale
    ("GCB95 Classic", 3, 40, 20, 0, False, None), ("Voodoo Fuzz Wah (V847)", 4, 50, 30, 20, False, None),
    ("Funk Quack", 5, 65, 10, 15, False, None), ("Shriek Lead", 6, 78, 35, 35, False, None),
    ("Cocked Wah - Mid Honk", 3, 55, 25, 20, False, 55), ("Seven-String Growl", 1, 45, 55, 20, False, None),
    ("Bass Wah", 1, 30, 20, 10, False, None), ("Talking Cat (vocal)", 4, 92, 15, 10, False, None),
    ("Smooth Dark Sweep", 2, 20, 0, 5, False, None), ("Auto-Engage Lead", 3, 50, 25, 25, True, None)]
FACT_DESC = {
    "en": ["The reference: the stock Cry Baby voice.", "Open vintage sweep, a bit of grit and boost: made for a fuzz in front.",
           "Bright, tight quack for funk rhythm.", "High, very resonant sweep with boost for solos.",
           "Fixed half-way 'cocked wah' (treadle at 55 %): nasal mid honk.", "Low range and plenty of Purr: thick growl for low-tuned guitars.",
           "Low range, moderate Q: bass-friendly wah.", "Very high Q for a vocal, talking effect.", "Soft, dark, almost filter-like sweep.",
           "Classic voice with AUTO-ENGAGE: just move the pedal to turn the wah on."],
    "fr": ["La référence : la voix de la Cry Baby d'origine.", "Balayage vintage ouvert, un peu de grain et de boost : fait pour une fuzz placée devant.",
           "« Quack » brillant et serré pour la rythmique funk.", "Balayage aigu, très résonant, avec boost pour les solos.",
           "Wah « bloquée » à mi-course (plateau à 55 %) : médium nasal.", "Plage grave et beaucoup de Purr : grognement épais pour guitares accordées bas.",
           "Plage grave, Q modéré : wah adaptée à la basse.", "Q très élevé pour un effet vocal, qui « parle ».", "Balayage doux et sombre, presque comme un filtre.",
           "Voix classique avec AUTO-ENGAGE : il suffit de bouger la pédale pour activer la wah."]}

# ---------- textes ----------
T = {
"en": dict(
 lang="ENGLISH", sub="Inductor wah pedal &nbsp;·&nbsp; VST3 for Windows and macOS", ver="User manual &nbsp;·&nbsp; version " + VERSION,
 intro_t="1. Introduction",
 intro=["<b>CRYCAT</b> is a Studio Wiga plugin modelled on the most famous wah pedal of all, the <b>Dunlop Cry Baby GCB95</b>. Like the original, it is a resonant band-pass filter built around a 500 mH inductor: the peak sweeps from about <b>450 Hz with the heel down to 1.6 kHz with the toe down</b>, with up to about +18 dB of resonance on the original circuit, while part of the low end always passes so the guitar keeps its body.",
        "CRYCAT keeps that voice and adds the most wanted modifications from later Cry Baby models and DIY mods: six sweep <b>RANGES</b>, an adjustable <b>Q</b>, an inductor saturation control (<b>PURR</b>), a clean <b>BOOST</b> up to +15 dB, and an <b>AUTO-ENGAGE</b> mode that turns the wah on as soon as you move the pedal, ideal with an expression pedal plugged into an M-Vave Chocolate Plus.",
        "Sound: vocal, throaty and singing; from the dark 'cocked wah' honk to the funk quack and the screaming lead."],
 inst_t="2. Installation",
 inst=[("Windows", "Download the <b>CRYCAT-VST3-Windows</b> artifact from the Actions tab of the GitHub repository Wiga0ne/Crycat, unzip it and copy the <b>CRYCAT.vst3</b> folder into <b>C:\\Program Files\\Common Files\\VST3</b>."),
       ("macOS", "Download <b>CRYCAT-VST3-macOS</b>, unzip it and copy <b>CRYCAT.vst3</b> into <b>~/Library/Audio/Plug-Ins/VST3</b> (or /Library/Audio/Plug-Ins/VST3 for all users). If macOS blocks it, run <b>xattr -cr ~/Library/Audio/Plug-Ins/VST3/CRYCAT.vst3</b> in the Terminal. The binary is universal (Apple Silicon and Intel)."),
       ("Reaper", "Options &gt; Preferences &gt; Plug-ins &gt; VST &gt; <b>Re-scan</b>. The plugin appears as <b>CRYCAT (Studio Wiga)</b>. To control it with your MIDI pedal, the track must receive MIDI from the controller (record-arm or input monitoring, MIDI input set to your device, e.g. the M-Vave Chocolate Plus in Bluetooth or USB).")],
 ov_t="3. Interface overview",
 ov_rows=[["#", "Control", "Role"],
   ["1", "Treadle", "The wah pedal. Heel down = 0 % (low frequency), toe down = 100 % (high frequency). Moves with the mouse (drag down to press the toe, wheel, double-click = heel / toe), with your MIDI expression pedal (CC 11 by default) and with DAW automation."],
   ["2", "RANGE selector", "6 sweep ranges. Drag up / down, mouse wheel, or click a number directly. The selected number lights up in amber."],
   ["3", "Screen", "Range, state, live frequency response curve, centre frequency, Q and boost, treadle travel and the status line. Click it to open the preset menu."],
   ["4", "MAN · AUTO switch", "MAN: the footswitch alone turns the wah on / off. AUTO: automatic engage when the treadle leaves the heel."],
   ["5", "Q", "Resonance of the peak, from wide and smooth (Q 2) to very vocal (Q 14). Default 40 % (Q about 4.4, the original Cry Baby)."],
   ["6", "PURR", "Inductor saturation and input grit (default 20 %)."],
   ["7", "BOOST", "Output gain, 0 to +15 dB (default 0 dB)."],
   ["8", "Footswitch", "Effect ON / OFF, click-free crossfade."],
   ["9", "LED", "Amber LED: lit when the wah is active; glows faintly in AUTO while waiting for the pedal to move."],
   ["10", "IN", "Input jack (right side)."],
   ["11", "OUT", "Output jack (left side)."]],
 ctl_t="4. Controls in detail",
 ctl=[("Treadle", "The peak frequency follows the treadle on a logarithmic scale, so the sweep feels even across the whole travel, like the audio-taper pot of a real wah. The heel keeps more low end (lows about 12 dB under the peak), the toe thins them out (about 24 dB under the peak) for the classic 'wah' contrast. The resonance tightens slightly towards the toe, as on the original. On screen the treadle really tilts around its hinge: as the toe rises its face turns away from the light, its edges appear, a reflection slides over the chrome and its shadow grows longer and softer."),
      ("RANGE", "Sets where the sweep starts and ends. Range 3 is the GCB95 (450 Hz to 1.6 kHz). Lower ranges suit bass and low-tuned guitars; higher ranges cut through and scream."),
      ("Q", "On a real Cry Baby the Q is set by a single resistor (R7); raising it gives the more vocal tone that many players mod for. Low Q: smooth, filter-like. High Q: talking, nasal, very expressive."),
      ("PURR", "Models the saturation of the inductor and of the input stage: at 0 % the filter is perfectly clean, higher values add a slightly asymmetric grit, compress the resonance on hard picking and give a throatier, 'meowing' voice. Very nice before a clean or crunch amp."),
      ("BOOST", "Like the booster of the Cry Baby 535Q: compensates the volume drop of the wah or pushes the amp for solos. The boost only applies when the wah is on."),
      ("MAN / AUTO", "<b>AUTO-ENGAGE</b>: the wah turns on as soon as the treadle leaves the heel (above 3.5 %) and turns off after it has rested on the heel for a short time (400 ms by default, adjustable: right-click &gt; <b>Auto-engage</b>). The footswitch stays the master switch: if it is OFF, the wah stays off.")],
 scr_t="The screen",
 scr="The screen shows the <b>range</b> and the <b>state</b> (ON, BYPASS, AUTO = waiting, AUTO ON = engaged by the pedal), the <b>frequency response</b> from 80 Hz to 5 kHz (the shaded band is the sweep of the selected range, the dot is the current peak), the <b>centre frequency</b>, the effective <b>Q</b> and the <b>boost</b>, the treadle position bars and the <b>status line</b> with the live values in % (pedal, Q, PURR, BOOST) or the instructions during a MIDI learn / calibration.",
 heeltoe_cap="Treadle heel down (left, toe raised) and toe down (right): the drawing follows the MIDI pedal and automation in real time.",
 fs_t="5. Footswitch, MIDI, expression pedal and M-Vave Chocolate Plus",
 fs=["<b>Expression pedal</b>: CC 11 by default. Right-click the treadle: <b>MIDI learn pedal controller</b> (move your pedal), <b>Reset to CC 11</b>, <b>Calibrate</b> (sweep the pedal fully heel &lt;-&gt; toe for 6 seconds: CRYCAT records its real minimum and maximum, so a pedal that does not reach 0 or 127 still covers the whole sweep), <b>Reset heel / toe calibration</b>, <b>Invert pedal direction</b>, and <b>Pedal glide</b> (0 to 120 ms, 15 ms by default). Movements are never stepped: CRYCAT measures the time between two messages from your pedal and glides linearly to each new position over that time, then smooths the result with a two-stage filter; frequency, Q, Purr and Boost are also smoothed, so the 128 steps of a 7-bit pedal (even over Bluetooth) and DAW automation sound perfectly continuous. 14-bit pedals (CC n + CC n+32) are supported.",
     "<b>Footswitch and MAN / AUTO switch</b>: right-click them to <b>MIDI learn</b> a CC or a note, or to clear the assignment. In the same menu choose how your controller sends its switches: <b>Momentary</b> (each press toggles; for switches that send 127 on press, 0 on release) or <b>Latching</b> (127 = ON, 0 = OFF; for switches set to 'toggle' that alternate 127 / 0).",
     "<b>M-Vave Chocolate Plus</b>: plug your expression pedal into its EXP input, then use MIDI learn on the treadle (or set the EXP to send CC 11). Assign one Chocolate footswitch to the CRYCAT footswitch by MIDI learn, and optionally another one to MAN / AUTO. Its Program Change mode works directly: <b>PC 1 to 10 load the 10 factory presets</b> (can be disabled in the right-click menu).",
     "Tip: with AUTO-ENGAGE you can play hands- and switch-free: just rock the expression pedal, CRYCAT turns itself off when you return to the heel.",
     "Every control is also a DAW parameter (Wah Pedal, Range, Q, Purr, Boost, Effect On, Auto-Engage, Pedal Glide, Auto-Engage Release, MIDI pedal calibration heel / toe, MIDI Pedal Invert, Respond to MIDI Program Change, MIDI Switches Latching) and can be automated or linked to any controller in Reaper."],
 win_t="6. Window",
 win=["CRYCAT uses the double pedal format, like Wharry: the window opens at its default size, <b>486 x 495 pixels</b> (same height as Ring Think), and can never be made smaller than that. Right-click &gt; <b>Plugin size</b>: Default, 75 % (557 px high), 100 % (743 px high); you can also drag the bottom-right corner (proportions are kept)."],
 pre_t="7. Presets",
 pre=["Click the screen (or use the preset menu of your DAW) to load a factory preset or choose a range. The sweep ranges:"],
 rng_hdr=["#", "Range", "Heel", "Toe", "Use"],
 fac_t="Studio Wiga factory presets",
 fac_hdr=["PC", "Preset", "Range", "Q / Purr / Boost", "Description"],
 tips_t="8. Tips",
 tips=["In Reaper place CRYCAT <b>before</b> your amp simulator (for example before Two Notes Genome), as you would put a real wah in front of the amp. With a fuzz, try both orders: wah before fuzz is smoother, fuzz before wah screams more.",
       "7-string tuned to G or lower: use range 1 (220 Hz to 900 Hz) and a little PURR for a thick, growling low end; range 2 for rhythm parts.",
       "For a 'cocked wah' sound, park the treadle around 40-60 % (or load the Cocked Wah preset) and raise Q.",
       "If your pedal feels too jumpy, raise Pedal glide to 30 ms; for fast funk rhythms keep it at 8-15 ms."],
 tech_t="9. Specifications and version history",
 tech=[["Format", "VST3 (Windows 64-bit, macOS universal), AU also built on macOS"], ["Channels", "Mono or stereo in, mono or stereo out (each channel filtered)"],
       ["Latency", "0 samples"], ["Sweep", "220 Hz to 3.2 kHz depending on the range (GCB95: 450 Hz to 1.6 kHz)"],
       ["Q", "2 to 14"], ["Boost", "0 to +15 dB"], ["CPU", "less than 1 % of one core at 48 kHz"]],
 hist=[["1.0.0", "First version: Cry Baby GCB95 model, 6 ranges, Q, Purr, Boost, Auto-engage, live response screen, MIDI learn, calibration, momentary / latching MIDI switches, Program Change presets, double pedal format"],
       ["1.0.1", "Cat emblem on the treadle and cat ears on the name removed: ribbed rubber over the whole treadle"],
       ["1.1.0", "New user design; treadle tilt with lighting, visible edges, height-dependent shadow and hinge detail; no more stepping with MIDI expression pedals (adaptive ramp + 2-stage glide, 14-bit CC); window 486 x 495"],
       ["1.1.1", "Treadle lighting like Wharry (face turns towards / away from the light, sliding reflection, contact shadow when flat), IN / OUT aligned with the jacks, right-hand screws removed, treadle positions precomputed in the background (no freeze)"]],
 ver_h=["Version", "Changes"]),
"fr": dict(
 lang="FRANÇAIS", sub="Pédale wah à inducteur &nbsp;·&nbsp; VST3 pour Windows et macOS", ver="Notice d'utilisation &nbsp;·&nbsp; version " + VERSION,
 intro_t="1. Présentation",
 intro=["<b>CRYCAT</b> est un plugin Studio Wiga inspiré de la wah la plus célèbre, la <b>Dunlop Cry Baby GCB95</b>. Comme l'originale, c'est un filtre passe-bande résonant construit autour d'un inducteur de 500 mH : la pointe balaye d'environ <b>450 Hz talon enfoncé à 1,6 kHz pointe enfoncée</b>, avec jusqu'à environ +18 dB de résonance sur le circuit d'origine, tandis qu'une partie des graves passe toujours pour garder le corps de la guitare.",
        "CRYCAT garde cette voix et ajoute les modifications les plus recherchées des Cry Baby plus récentes et des mods DIY : six <b>PLAGES</b> de balayage (RANGE), un <b>Q</b> réglable, une saturation de l'inducteur (<b>PURR</b>), un <b>BOOST</b> propre jusqu'à +15 dB et un mode <b>AUTO-ENGAGE</b> qui active la wah dès que vous bougez la pédale, idéal avec une pédale d'expression branchée sur une M-Vave Chocolate Plus.",
        "Son : vocal, guttural et chantant ; du médium nasal de la « wah bloquée » au « quack » funk et au solo hurlant."],
 inst_t="2. Installation",
 inst=[("Windows", "Téléchargez l'artefact <b>CRYCAT-VST3-Windows</b> dans l'onglet Actions du dépôt GitHub Wiga0ne/Crycat, décompressez-le et copiez le dossier <b>CRYCAT.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>."),
       ("macOS", "Téléchargez <b>CRYCAT-VST3-macOS</b>, décompressez-le et copiez <b>CRYCAT.vst3</b> dans <b>~/Bibliothèque/Audio/Plug-Ins/VST3</b> (ou /Bibliothèque/Audio/Plug-Ins/VST3 pour tous les utilisateurs). Si macOS le bloque, tapez <b>xattr -cr ~/Library/Audio/Plug-Ins/VST3/CRYCAT.vst3</b> dans le Terminal. Le binaire est universel (Apple Silicon et Intel)."),
       ("Reaper", "Options &gt; Préférences &gt; Plug-ins &gt; VST &gt; <b>Re-scan</b>. Le plugin apparaît sous le nom <b>CRYCAT (Studio Wiga)</b>. Pour le piloter avec votre pédale MIDI, la piste doit recevoir le MIDI du contrôleur (armement ou monitoring d'entrée, entrée MIDI réglée sur votre appareil, par exemple la M-Vave Chocolate Plus en Bluetooth ou USB).")],
 ov_t="3. Vue d'ensemble de l'interface",
 ov_rows=[["#", "Commande", "Rôle"],
   ["1", "Plateau", "La pédale wah. Talon enfoncé = 0 % (grave), pointe enfoncée = 100 % (aigu). Se déplace à la souris (tirer vers le bas = appuyer sur la pointe, molette, double-clic = talon / pointe), avec votre pédale d'expression MIDI (CC 11 par défaut) et avec les automations du DAW."],
   ["2", "Sélecteur RANGE", "6 plages de balayage. Glisser vers le haut / le bas, molette, ou clic direct sur un numéro. Le numéro choisi s'allume en ambre."],
   ["3", "Écran", "Plage, état, courbe de réponse en direct, fréquence centrale, Q et boost, course du plateau et ligne d'état. Un clic ouvre le menu des presets."],
   ["4", "Inverseur MAN · AUTO", "MAN : seul le footswitch active / coupe la wah. AUTO : enclenchement automatique quand le plateau quitte le talon."],
   ["5", "Q", "Résonance de la pointe, de large et douce (Q 2) à très vocale (Q 14). 40 % par défaut (Q d'environ 4,4, la Cry Baby d'origine)."],
   ["6", "PURR", "Saturation de l'inducteur et grain de l'entrée (20 % par défaut)."],
   ["7", "BOOST", "Gain de sortie, de 0 à +15 dB (0 dB par défaut)."],
   ["8", "Footswitch", "Effet ON / OFF, fondu sans clic."],
   ["9", "LED", "LED ambre : allumée quand la wah est active ; faible lueur en AUTO tant que la pédale n'a pas bougé."],
   ["10", "IN", "Jack d'entrée (côté droit)."],
   ["11", "OUT", "Jack de sortie (côté gauche)."]],
 ctl_t="4. Les commandes en détail",
 ctl=[("Plateau", "La fréquence de la pointe suit le plateau sur une échelle logarithmique : le balayage paraît régulier sur toute la course, comme avec le potentiomètre logarithmique d'une vraie wah. Le talon garde davantage de graves (environ 12 dB sous le niveau de la pointe), la pointe les amincit (environ 24 dB sous la pointe) pour le contraste « wah » classique. La résonance se resserre un peu vers la pointe, comme sur l'originale. À l'écran, le plateau bascule vraiment autour de sa charnière : quand la pointe monte, sa face s'écarte de la lumière, ses tranches apparaissent, un reflet glisse sur le chrome et son ombre s'allonge en s'adoucissant."),
      ("RANGE", "Fixe le début et la fin du balayage. La plage 3 est celle de la GCB95 (450 Hz à 1,6 kHz). Les plages graves conviennent à la basse et aux guitares accordées bas ; les plages aiguës percent et hurlent."),
      ("Q", "Sur une vraie Cry Baby, le Q dépend d'une seule résistance (R7) ; l'augmenter donne le son plus vocal que beaucoup de guitaristes obtiennent par modification. Q faible : doux, façon filtre. Q élevé : parlant, nasal, très expressif."),
      ("PURR", "Reproduit la saturation de l'inducteur et de l'étage d'entrée : à 0 % le filtre est parfaitement propre, au-delà il ajoute un grain légèrement asymétrique, comprime la résonance sur les attaques fortes et donne une voix plus gutturale, qui « miaule ». Très beau devant un ampli clair ou crunch."),
      ("BOOST", "Comme le booster de la Cry Baby 535Q : compense la perte de volume de la wah ou pousse l'ampli pour les solos. Le boost ne s'applique que lorsque la wah est active."),
      ("MAN / AUTO", "<b>AUTO-ENGAGE</b> : la wah s'active dès que le plateau quitte le talon (au-delà de 3,5 %) et se coupe après un court repos au talon (400 ms par défaut, réglable : clic droit &gt; <b>Auto-engage</b>). Le footswitch reste l'interrupteur principal : s'il est sur OFF, la wah reste coupée.")],
 scr_t="L'écran",
 scr="L'écran affiche la <b>plage</b> et l'<b>état</b> (ON, BYPASS, AUTO = en attente, AUTO ON = enclenchée par la pédale), la <b>courbe de réponse</b> de 80 Hz à 5 kHz (la bande ombrée correspond au balayage de la plage choisie, le point à la pointe actuelle), la <b>fréquence centrale</b>, le <b>Q</b> effectif et le <b>boost</b>, les barres de position du plateau et la <b>ligne d'état</b> avec les valeurs en direct en % (pédale, Q, PURR, BOOST) ou les consignes pendant un MIDI learn / une calibration.",
 heeltoe_cap="Plateau talon enfoncé (à gauche, pointe levée) et pointe enfoncée (à droite) : le dessin suit la pédale MIDI et les automations en temps réel.",
 fs_t="5. Footswitch, MIDI, pédale d'expression et M-Vave Chocolate Plus",
 fs=["<b>Pédale d'expression</b> : CC 11 par défaut. Clic droit sur le plateau : <b>MIDI learn pedal controller</b> (bougez votre pédale), <b>Reset to CC 11</b>, <b>Calibrate</b> (balayez la pédale entièrement talon &lt;-&gt; pointe pendant 6 secondes : CRYCAT enregistre son minimum et son maximum réels, une pédale qui n'atteint pas 0 ou 127 couvre donc tout le balayage), <b>Reset heel / toe calibration</b>, <b>Invert pedal direction</b> et <b>Pedal glide</b> (de 0 à 120 ms, 15 ms par défaut). Les mouvements ne sont jamais saccadés : CRYCAT mesure le temps entre deux messages de votre pédale et glisse linéairement vers chaque nouvelle position pendant ce temps, puis lisse le résultat avec un filtre à deux étages ; la fréquence, le Q, le Purr et le Boost sont aussi lissés : les 128 pas d'une pédale 7 bits (même en Bluetooth) et les automations du DAW sonnent parfaitement continus. Les pédales 14 bits (CC n + CC n+32) sont prises en charge.",
     "<b>Footswitch et inverseur MAN / AUTO</b> : clic droit pour faire un <b>MIDI learn</b> d'un CC ou d'une note, ou supprimer l'assignation. Dans le même menu, choisissez comment votre contrôleur envoie ses commutateurs : <b>Momentary</b> (chaque appui bascule ; pour les footswitches qui envoient 127 à l'appui et 0 au relâchement) ou <b>Latching</b> (127 = ON, 0 = OFF ; pour les footswitches réglés en « toggle » qui alternent 127 / 0).",
     "<b>M-Vave Chocolate Plus</b> : branchez votre pédale d'expression sur son entrée EXP, puis faites un MIDI learn sur le plateau (ou réglez l'EXP pour envoyer le CC 11). Assignez un footswitch de la Chocolate au footswitch de CRYCAT par MIDI learn, et éventuellement un autre à MAN / AUTO. Son mode Program Change fonctionne directement : <b>PC 1 à 10 chargent les 10 presets usine</b> (désactivable dans le menu clic droit).",
     "Astuce : avec AUTO-ENGAGE, plus besoin de footswitch : basculez simplement la pédale d'expression, CRYCAT se coupe tout seul quand vous revenez au talon.",
     "Chaque commande est aussi un paramètre du DAW (Wah Pedal, Range, Q, Purr, Boost, Effect On, Auto-Engage, Pedal Glide, Auto-Engage Release, MIDI Pedal Calibration Heel / Toe, MIDI Pedal Invert, Respond to MIDI Program Change, MIDI Switches Latching), automatisable ou reliable à n'importe quel contrôleur dans Reaper."],
 win_t="6. Fenêtre",
 win=["CRYCAT utilise le format pédale double, comme Wharry : la fenêtre s'ouvre à sa taille par défaut, <b>486 x 495 pixels</b> (même hauteur que Ring Think), et ne peut jamais être réduite en dessous. Clic droit &gt; <b>Plugin size</b> : Default, 75 % (557 px de haut), 100 % (743 px de haut) ; on peut aussi tirer le coin en bas à droite (les proportions sont conservées)."],
 pre_t="7. Presets",
 pre=["Cliquez sur l'écran (ou utilisez le menu de presets de votre DAW) pour charger un preset usine ou choisir une plage. Les plages de balayage :"],
 rng_hdr=["#", "Plage", "Talon", "Pointe", "Usage"],
 fac_t="Presets usine Studio Wiga",
 fac_hdr=["PC", "Preset", "Plage", "Q / Purr / Boost", "Description"],
 tips_t="8. Conseils d'utilisation",
 tips=["Dans Reaper, placez CRYCAT <b>avant</b> votre simulateur d'ampli (par exemple avant Two Notes Genome), comme une vraie wah devant l'ampli. Avec une fuzz, essayez les deux ordres : wah avant la fuzz = plus doux, fuzz avant la wah = plus hurlant.",
       "7 cordes accordée en G ou plus bas : utilisez la plage 1 (220 Hz à 900 Hz) et un peu de PURR pour des graves épais et grognants ; la plage 2 pour les rythmiques.",
       "Pour un son de « wah bloquée », laissez le plateau vers 40-60 % (ou chargez le preset Cocked Wah) et montez le Q.",
       "Si la pédale vous semble trop nerveuse, montez Pedal glide à 30 ms ; pour les rythmiques funk rapides, gardez 8-15 ms."],
 tech_t="9. Caractéristiques techniques et historique des versions",
 tech=[["Format", "VST3 (Windows 64 bits, macOS universel), AU également compilé sur macOS"], ["Canaux", "Entrée mono ou stéréo, sortie mono ou stéréo (chaque canal est filtré)"],
       ["Latence", "0 échantillon"], ["Balayage", "de 220 Hz à 3,2 kHz selon la plage (GCB95 : 450 Hz à 1,6 kHz)"],
       ["Q", "de 2 à 14"], ["Boost", "de 0 à +15 dB"], ["Processeur", "moins de 1 % d'un cœur à 48 kHz"]],
 hist=[["1.0.0", "Première version : modèle Cry Baby GCB95, 6 plages, Q, Purr, Boost, enclenchement automatique, écran avec courbe de réponse en direct, MIDI learn, calibration, commutateurs MIDI momentanés / à accrochage, presets par Program Change, format pédale double"],
       ["1.0.1", "Tête de chat du plateau et oreilles de chat sur le nom supprimées : caoutchouc nervuré sur tout le plateau"],
       ["1.1.0", "Nouveau design de l'utilisateur ; bascule du plateau avec jeu d'ombres et lumières, tranches visibles, ombre selon la hauteur et charnière soignée ; plus aucune saccade avec les pédales d'expression MIDI (rampe adaptative + glissé à 2 étages, CC 14 bits) ; fenêtre 486 x 495"],
       ["1.1.1", "Éclairage du plateau comme Wharry (la face se tourne vers / s'écarte de la lumière, reflet mobile, ombre de contact à plat), IN / OUT alignés sur les jacks, vis de droite supprimées, positions du plateau précalculées en arrière-plan (anti-gel)"]],
 ver_h=["Version", "Changements"]),
}

W = A4[0] - 36 * mm
def section(lang):
    t = T[lang]; f = []
    f.append(Spacer(1, 12 * mm))
    f.append(P(t["lang"], "small"))
    f.append(Spacer(1, 4 * mm))
    f.append(img(cover, W * 0.86))
    f.append(Spacer(1, 6 * mm))
    f.append(P("C R Y C A T", "cover"))
    f.append(P(t["sub"], "coversub"))
    f.append(Spacer(1, 2 * mm))
    f.append(P(t["ver"], "coversub"))
    f.append(Spacer(1, 6 * mm))
    f.append(img(os.path.join(TMP, "logo_dark.png"), 52 * mm))
    f.append(PageBreak())
    f.append(P(t["intro_t"], "h1")); f += [P(x) for x in t["intro"]]
    f.append(P(t["inst_t"], "h1"))
    for h, x in t["inst"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(PageBreak())
    f.append(P(t["ov_t"], "h1")); f.append(img(annotated, W * 0.78)); f.append(Spacer(1, 3 * mm))
    f.append(table(t["ov_rows"], [10 * mm, 36 * mm, W - 46 * mm]))
    f.append(PageBreak())
    f.append(P(t["ctl_t"], "h1"))
    for h, x in t["ctl"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(KeepTogether([Spacer(1, 3 * mm), img(heeltoe, W * 0.56), P(t["heeltoe_cap"], "small")]))
    scr_tbl = Table([[img(screen_img, 46 * mm), P(t["scr"])]], colWidths=[52 * mm, W - 52 * mm])
    scr_tbl.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
    f.append(KeepTogether([P(t["scr_t"], "h2"), scr_tbl]))
    f.append(P(t["fs_t"], "h1")); f += bullets(t["fs"])
    f.append(P(t["win_t"], "h1")); f += [P(x) for x in t["win"]]
    f.append(PageBreak())
    f.append(P(t["pre_t"], "h1")); f += [P(x) for x in t["pre"]]
    rows = [t["rng_hdr"]] + [[n, nm, f"{lo} Hz", f"{hi / 1000:.2f} kHz".replace(".", "," if lang == "fr" else "."), RANGE_DESC[lang][i]] for i, (n, nm, lo, hi) in enumerate(RANGES)]
    f.append(table(rows, [8 * mm, 32 * mm, 18 * mm, 20 * mm, W - 78 * mm]))
    f.append(P(t["fac_t"], "h2"))
    rows = [t["fac_hdr"]]
    for i, (n, rg, q, pu, bo, au, ped) in enumerate(FACTORY):
        extra = (" · AUTO" if au else "") + ((" · " + ("pedal" if lang == "en" else "pédale") + f" {ped} %") if ped is not None else "")
        rows.append([str(i + 1), n, str(rg), f"{q} % · {pu} % · {bo} %" + extra, FACT_DESC[lang][i]])
    f.append(table(rows, [9 * mm, 38 * mm, 12 * mm, 36 * mm, W - 95 * mm]))
    f.append(P(t["tips_t"], "h1")); f += bullets(t["tips"])
    f.append(KeepTogether([P(t["tech_t"], "h1"), table(t["tech"], [30 * mm, W - 30 * mm], header=False)])); f.append(Spacer(1, 4 * mm))
    f.append(table([t["ver_h"]] + t["hist"], [20 * mm, W - 20 * mm]))
    f.append(PageBreak())
    return f

# logo Studio Wiga en foncé pour les pages blanches
lg = Image.open(os.path.join(ROOT, "logo_studio_wiga_blanc.png")).convert("RGBA")
la = np.array(lg); la[..., :3] = (28, 26, 30); Image.fromarray(la).save(os.path.join(TMP, "logo_dark.png"))

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "CRYCAT (Studio Wiga) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="CRYCAT - Manual / Notice v" + VERSION, author="Studio Wiga")
doc.addPageTemplates([PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
story = section("en") + section("fr")
story = story[:-1]
doc.build(story)
print(OUT)
