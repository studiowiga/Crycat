# CRYCAT (Studio Wiga)

Plugin VST3 (Windows / macOS, + AU sur Mac) reproduisant la wah **Dunlop Cry Baby GCB95**, avec modifications,
au format **pédale double** (fenêtre aux hauteurs de Ring Think, comme Wharry : 544 x 495 par défaut et au minimum, puis 75 % et 100 %).

- Filtre passe-bande résonant façon inducteur 500 mH : 450 Hz (talon) -> 1,6 kHz (pointe) sur la plage 3 « GCB95 »
- **RANGE** : 6 plages (1 Low / 7 cordes, 2 Dark, 3 GCB95, 4 Vintage V847, 5 Bright, 6 Shriek)
- **Q** (résonance, de large à vocal), **PURR** (saturation de l'inducteur / grain), **BOOST** (0 à +15 dB)
- Inverseur **MAN / AUTO** : en AUTO, la wah s'enclenche dès que la pédale quitte le talon et se coupe après un repos au talon (délai réglable)
- Footswitch ON / OFF avec LED, bypass sans clic
- Pédale d'expression MIDI : **CC 11** par défaut (MIDI learn), calibration automatique talon / pointe, inversion, lissage
- Compatible **M-Vave Chocolate Plus** : MIDI learn des footswitches (CC ou note), mode momentané ou à accrochage (127 / 0),
  Program Change 1-10 = presets usine
- Écran ambre : plage, état, courbe de réponse en direct, fréquence centrale, Q, boost, course de la pédale, ligne d'état en %
- 10 presets usine

## Visuel
Rendu procédural : `python3 tools/design_art.py` génère `background.png`, `treadle_down.png` (plateau à plat),
les sprites, `tools/layout.txt` et `design/apercu_treadle_up.png`. Le plugin calcule la bascule du plateau en perspective.
Captures réelles de la fenêtre : `tools/render` (outil Linux, `design/renders/`).

## Build
GitHub Actions (`.github/workflows/build.yml`) : artefacts `CRYCAT-VST3-Windows` et `CRYCAT-VST3-macOS`.
Tests du moteur audio : `g++ -O2 -std=c++17 tests/test_dsp.cpp -o test_dsp && ./test_dsp`
