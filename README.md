# CRYCAT (Studio Wiga)

Plugin VST3 (Windows / macOS, + AU sur Mac) reproduisant la wah **Dunlop Cry Baby GCB95**, avec modifications,
au format **pédale double** (fenêtre aux hauteurs de Ring Think : 486 x 495 par défaut et au minimum, puis 75 % et 100 %).

- Filtre passe-bande résonant façon inducteur 500 mH : 450 Hz (talon) -> 1,6 kHz (pointe) sur la plage 3 « GCB95 »
- **RANGE** : 6 plages (1 Low / 7 cordes, 2 Dark, 3 GCB95, 4 Vintage V847, 5 Bright, 6 Shriek)
- **Q** (résonance, de large à vocal), **PURR** (saturation de l'inducteur / grain), **BOOST** (0 à +15 dB)
- Inverseur **MAN / AUTO** : en AUTO, la wah s'enclenche dès que la pédale quitte le talon et se coupe après un repos au talon (délai réglable)
- Footswitch ON / OFF (celui d'Echoflex) avec LED, bypass sans clic
- Pédale d'expression MIDI : **CC 11** par défaut (MIDI learn), calibration automatique talon / pointe, inversion ;
  aucune saccade : rampe linéaire sur l'intervalle mesuré entre deux CC, glissé à 2 pôles, fréquence / Q / Purr / Boost lissés, CC 14 bits accepté
- Compatible **M-Vave Chocolate Plus** : MIDI learn des footswitches (CC ou note), mode momentané ou à accrochage (127 / 0),
  Program Change 1-10 = presets usine
- Écran ambre : plage, état, courbe de réponse en direct, fréquence centrale, Q, boost, course de la pédale, ligne d'état en %
- 10 presets usine

v1.4.1 : pédale d'expression MIDI inversée par défaut (option « Invert pedal direction » cochée à l'ouverture)
v1.4.0 : anti-gel v2 (JUCE 8.0.15, caches refaits par le timer, chien de garde, aucun appel à l'hôte depuis le thread audio)

## Visuel
Design de l'utilisateur : `design/crycat_design_source.png`. `python3 tools/design_art.py` génère `background.png` (écran vidé,
repères, LED et levier retirés, textes corrigés, logement sombre sous le plateau), `treadle_down.png` (plateau à plat), les sprites
(images 2x pour les écrans HiDPI) et `tools/layout.txt`. Le plugin calcule la bascule du plateau en perspective, avec tranches,
reflet mobile, ombre portée selon la hauteur et charnière.
`python3 tools/controls_art.py` rend en 3D (champ de hauteurs, ombres par lancer de rayons, lumière unique en haut à gauche)
l'inverseur MAN / AUTO (3 positions) et la façade éclairée par la LED ; le footswitch est celui d'Echoflex (photo réelle,
`design/echoflex_footswitch_source.png`), avec le même fonctionnement (bascule à l'appui, bouton assombri tant qu'on appuie).
Captures réelles de la fenêtre : `tools/render` (outil Linux, `design/renders/`).

## Build
GitHub Actions (`.github/workflows/build.yml`) : artefacts `Crycat-VST3-Windows` et `Crycat-VST3-macOS`.
Tests du moteur audio : `g++ -O2 -std=c++17 tests/test_dsp.cpp -o test_dsp && ./test_dsp`

- v1.4.2 : menu de taille de fenêtre : « 60 % (default) », 75 %, 100 % (plus de taille en pixels)
- v1.5.0 : MIDI Learn au clic droit sur chaque commande (Clear MIDI, assignations sauvegardées avec le projet)
