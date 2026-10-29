#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// ------------------------------------------------------------------ presets usine
const FactoryPreset& CrycatProcessor::factory (int i)
{
    static const FactoryPreset f[kNumFactory] = {
        //  nom                                 range   Q      purr   boost  auto   pédale
        { "GCB95 Classic",                        2,  0.40f, 0.20f, 0.00f, false, -1.0f },
        { "Voodoo Fuzz Wah (V847)",               3,  0.50f, 0.30f, 0.20f, false, -1.0f },
        { "Funk Quack",                           4,  0.65f, 0.10f, 0.15f, false, -1.0f },
        { "Shriek Lead",                          5,  0.78f, 0.35f, 0.35f, false, -1.0f },
        { "Cocked Wah - Mid Honk",                2,  0.55f, 0.25f, 0.20f, false,  0.55f },
        { "Seven-String Growl",                   0,  0.45f, 0.55f, 0.20f, false, -1.0f },
        { "Bass Wah",                             0,  0.30f, 0.20f, 0.10f, false, -1.0f },
        { "Talking Cat (vocal)",                  3,  0.92f, 0.15f, 0.10f, false, -1.0f },
        { "Smooth Dark Sweep",                    1,  0.20f, 0.00f, 0.05f, false, -1.0f },
        { "Auto-Engage Lead",                     2,  0.50f, 0.25f, 0.25f, true,  -1.0f },
    };
    return f[jlimit (0, kNumFactory - 1, i)];
}

// ------------------------------------------------------------------ paramètres
AudioProcessorValueTreeState::ParameterLayout CrycatProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    StringArray names;
    for (int i = 0; i < crycat::kNumRanges; ++i) names.add (crycat::range (i).name);
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pedal", 1 }, "Wah Pedal (heel 0 % / toe 100 %)", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "range", 1 }, "Range (selector)", names, 2));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "q", 1 }, "Q (resonance)", NormalisableRange<float> (0.0f, 1.0f), 0.40f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "purr", 1 }, "Purr (inductor grit)", NormalisableRange<float> (0.0f, 1.0f), 0.20f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "boost", 1 }, "Boost (0 to +15 dB)", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "+" + String (v * 15.0f, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "active", 1 }, "Effect On (footswitch)", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "auto", 1 }, "Auto-Engage (MAN / AUTO switch)", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Pedal Glide (smoothing)",
                                                        NormalisableRange<float> (0.0f, 250.0f, 0.0f, 0.5f), 15.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "autooff", 1 }, "Auto-Engage Release (heel rest time)",
                                                        NormalisableRange<float> (100.0f, 2000.0f, 0.0f, 0.5f), 400.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "heel", 1 }, "Pedal Calibration Heel (min)", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "toe", 1 },  "Pedal Calibration Toe (max)",  NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "invert", 1 }, "MIDI Pedal Invert", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "pc", 1 }, "Respond to MIDI Program Change (PC 1-10 = presets)", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "latch", 1 }, "MIDI Switches Latching (127 = on / 0 = off)", false));
    return { p.begin(), p.end() };
}

CrycatProcessor::CrycatProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CRYCAT", createLayout())
{
    pRange   = apvts.getRawParameterValue ("range");
    pPedal   = apvts.getRawParameterValue ("pedal");
    pQ       = apvts.getRawParameterValue ("q");
    pPurr    = apvts.getRawParameterValue ("purr");
    pBoost   = apvts.getRawParameterValue ("boost");
    pActive  = apvts.getRawParameterValue ("active");
    pAuto    = apvts.getRawParameterValue ("auto");
    pSmooth  = apvts.getRawParameterValue ("smooth");
    pAutoOff = apvts.getRawParameterValue ("autooff");
    pHeel    = apvts.getRawParameterValue ("heel");
    pToe     = apvts.getRawParameterValue ("toe");
    pInvert  = apvts.getRawParameterValue ("invert");
    pPC      = apvts.getRawParameterValue ("pc");
    pLatch   = apvts.getRawParameterValue ("latch");
    apvts.addParameterListener ("pedal", this);
    pedalLive = pPedal->load();
    for (int i = 0; i < numQueued; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    startTimerHz (60);   // anti-gel v2 : aucun appel à l'hôte depuis le thread audio
}

CrycatProcessor::~CrycatProcessor()
{
    stopTimer();
    apvts.removeParameterListener ("pedal", this);
}

bool CrycatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (in != AudioChannelSet::mono() && in != AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();
}

crycat::Settings CrycatProcessor::currentSettings() const
{
    crycat::Settings s;
    s.rangeIdx  = jlimit (0, crycat::kNumRanges - 1, (int) std::lround (pRange->load (std::memory_order_relaxed)));
    s.pedal     = jlimit (0.0f, 1.0f, pedalLive.load());
    s.q         = pQ->load (std::memory_order_relaxed);
    s.purr      = pPurr->load (std::memory_order_relaxed);
    s.boost     = pBoost->load (std::memory_order_relaxed);
    s.active    = pActive->load (std::memory_order_relaxed) > 0.5f;
    s.autoMode  = pAuto->load (std::memory_order_relaxed) > 0.5f;
    s.smoothMs  = pSmooth->load (std::memory_order_relaxed);
    s.autoOffMs = pAutoOff->load (std::memory_order_relaxed);
    return s;
}

bool CrycatProcessor::isEffectOn() const
{
    if (pActive->load (std::memory_order_relaxed) < 0.5f) return false;
    if (pAuto->load (std::memory_order_relaxed) < 0.5f) return true;
    // audio arrêté : on montre ce que ferait l'enclenchement automatique
    if (Time::getMillisecondCounter() - lastBlockMs.load() > 250u) return pedalLive.load() > 0.035f;
    return engagedShown.load();
}

void CrycatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.setSettings (currentSettings());
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (0);
}

void CrycatProcessor::parameterChanged (const String& id, float v)
{
    if (id == "pedal") pedalLive = mapPedal (v, false);
}

// position brute (0..1) -> position pédale (même logique que Wharry 1.6.0) :
//  1. plage de la pédale : calibration manuelle (talon / pointe) si elle a été faite, sinon plage apprise
//     automatiquement sur le MIDI reçu (une M-Audio EX-P envoie souvent ~2..116 au lieu de 0..127) ;
//  2. petite zone morte aux deux butées : talon = exactement 0, pointe = exactement 100 % ;
//  3. inversion éventuelle (MIDI seulement).
float CrycatProcessor::mapPedal (float raw, bool fromMidi) const
{
    float v = raw;
    if (fromMidi)
    {
        float lo = pHeel->load (std::memory_order_relaxed), hi = pToe->load (std::memory_order_relaxed);
        const bool manual = lo > 0.001f || hi < 0.999f;
        if (! manual && autoMax.load() - autoMin.load() > 0.4f) { lo = autoMin.load(); hi = autoMax.load(); }
        v = hi > lo + 0.01f ? (raw - lo) / (hi - lo) : raw;
    }
    constexpr float dz = 0.02f;
    v = jlimit (0.0f, 1.0f, (v - dz) / (1.0f - 2.0f * dz));
    return (fromMidi && pInvert->load (std::memory_order_relaxed) > 0.5f) ? 1.0f - v : v;
}

void CrycatProcessor::resetCalibration()
{
    calibUntil = 0;
    autoMin = 1.0f; autoMax = 0.0f;
    setParamValue ("heel", 0.0f); setParamValue ("toe", 1.0f);
}

String CrycatProcessor::calibrationDescription() const
{
    const float lo = pHeel->load (std::memory_order_relaxed), hi = pToe->load (std::memory_order_relaxed);
    auto cc = [] (float v) { return String (roundToInt (v * 127.0f)); };
    if (lo > 0.001f || hi < 0.999f) return "calibrated " + cc (lo) + " - " + cc (hi);
    if (autoMax.load() - autoMin.load() > 0.4f) return "auto " + cc (autoMin.load()) + " - " + cc (autoMax.load());
    return "auto";
}

// ------------------------------------------------------------------ MIDI
void CrycatProcessor::handleMidiMessage (const MidiMessage& m)
{
    const auto learn = (MidiTarget) learnTarget.load();
    if (learn != MidiTarget::None && ((m.isController() && m.getControllerNumber() < 120) || m.isNoteOn()))
    {
        const int cc = m.isController() ? m.getControllerNumber() : -1;
        const int note = m.isNoteOn() ? m.getNoteNumber() : -1;
        if (learn == MidiTarget::Pedal)           { if (cc >= 0) ccPedal = cc; }
        else if (learn == MidiTarget::AutoSwitch) { ccAuto = cc; noteAuto = note; autoHeld = cc >= 0 && m.getControllerValue() >= 64; }
        else                                      { ccFs = cc; noteFs = note; fsHeld = cc >= 0 && m.getControllerValue() >= 64; }
        if (learn != MidiTarget::Pedal || cc >= 0) learnTarget = 0;
        return;
    }

    const bool latch = pLatch->load (std::memory_order_relaxed) > 0.5f;
    if (m.isController())
    {
        const int c = m.getControllerNumber(), v = m.getControllerValue();
        if (c == ccPedal.load())
        {
            if (isCalibrating())
            {
                if (v < calMin.load()) calMin = v;
                if (v > calMax.load()) calMax = v;
            }
            pedalMsb = v;
            const float raw = (float) v / 127.0f;
            if (raw < autoMin.load()) autoMin = raw;
            if (raw > autoMax.load()) autoMax = raw;
            pedalLive = mapPedal (raw);
        }
        else if (ccPedal.load() < 32 && c == ccPedal.load() + 32 && pedalMsb >= 0)
        {
            // contrôleur 14 bits (CC n + CC n+32) : résolution fine, si la pédale l'envoie
            pedalLive = mapPedal ((float) (pedalMsb * 128 + v) / 16383.0f);
        }
        const bool on = v >= 64;
        // footswitch MIDI : momentané (bascule à chaque appui) ou à accrochage (127 = ON, 0 = OFF)
        // anti-gel v2 : la valeur part dans la file (bascule = valeur en file si elle existe, sinon valeur courante)
        auto handle = [&] (std::atomic<bool>& held, int qi)
        {
            if (latch) setParamFromAudio (qi, on ? 1.0f : 0.0f);
            else if (on && ! held.load()) setParamFromAudio (qi, queuedOrCurrent (qi) > 0.5f ? 0.0f : 1.0f);
            held = on;
        };
        if (c == ccAuto.load()) handle (autoHeld, 1);
        if (c == ccFs.load())   handle (fsHeld, 0);
    }
    else if (m.isNoteOn())
    {
        if (m.getNoteNumber() == noteAuto.load()) setParamFromAudio (1, queuedOrCurrent (1) > 0.5f ? 0.0f : 1.0f);
        if (m.getNoteNumber() == noteFs.load())   setParamFromAudio (0, queuedOrCurrent (0) > 0.5f ? 0.0f : 1.0f);
    }
    else if (m.isProgramChange() && pPC->load (std::memory_order_relaxed) > 0.5f)
    {
        // PC 1 à 10 (valeurs 0 à 9) = presets usine : compatible avec le mode « Program Change » de la M-Vave Chocolate
        const int pc = m.getProgramChangeNumber();
        if (pc < kNumFactory) pendingFactory = pc;     // appliqué par le timer du processeur
    }
}

void CrycatProcessor::clearMidi (MidiTarget t)
{
    if (t == MidiTarget::Pedal)           ccPedal = 11;
    else if (t == MidiTarget::AutoSwitch) { ccAuto = -1; noteAuto = -1; }
    else                                  { ccFs = -1; noteFs = -1; }
    if (getLearnTarget() == t) learnTarget = 0;
}

String CrycatProcessor::getMidiDescription (MidiTarget t) const
{
    if (t == MidiTarget::Pedal) return "CC " + String (ccPedal.load());
    const int cc = t == MidiTarget::AutoSwitch ? ccAuto.load() : ccFs.load();
    const int note = t == MidiTarget::AutoSwitch ? noteAuto.load() : noteFs.load();
    if (cc >= 0) return "CC " + String (cc);
    if (note >= 0) return "Note " + String (note);
    return {};
}

void CrycatProcessor::startCalibration()
{
    calMin = 128; calMax = -1;
    calibUntil = Time::getMillisecondCounter() + 6000u;
    // pendant la calibration, la plage complète est utilisée
    setParamValue ("heel", 0.0f); setParamValue ("toe", 1.0f);
}

// réglages pour un segment de bloc : quand la position demandée change (MIDI, automation, souris), la pédale rejoint
// la nouvelle valeur par une rampe de la durée mesurée entre les deux derniers changements (2 à 40 ms) -> mouvement continu
crycat::Settings CrycatProcessor::settingsAt (int offset)
{
    auto s = currentSettings();
    const juce::int64 now = sampleClock + offset;
    if (s.pedal != lastPedalTarget)
    {
        const double interval = (double) (now - lastPedalChange);
        rampSamples = (int) jlimit (0.002 * sr, 0.040 * sr, interval);
        lastPedalChange = now;
        lastPedalTarget = s.pedal;
    }
    s.pedalRampSamples = rampSamples;
    return s;
}

// fin de la calibration (6 s) : appliquée sur le thread de la fenêtre (appelé par l'audio et par la fenêtre)
// anti-gel v2 : simple drapeau, lu par le timer du processeur (plus de triggerAsyncUpdate depuis l'audio)
void CrycatProcessor::pollCalibration()
{
    if (calibUntil.load() != 0 && ! isCalibrating()) calibPending = true;
}

// Anti-gel v2 (Studio Wiga) : jamais d'appel à l'hôte (setValueNotifyingHost) ni d'allocation depuis le thread audio.
// Le MIDI met la valeur en file (pendingValue / pendingSet) ; le timer du processeur (60 Hz, thread de messages)
// la transmet au DAW et à la fenêtre.
const char* CrycatProcessor::queuedId (int i)
{
    static const char* ids[numQueued] = { "active", "auto" };
    return ids[jlimit (0, numQueued - 1, i)];
}

void CrycatProcessor::setParamFromAudio (int i, float plain)
{
    pendingValue[i].store (plain, std::memory_order_relaxed);
    pendingSet[i].store (true, std::memory_order_release);
}

float CrycatProcessor::queuedOrCurrent (int i) const
{
    if (pendingSet[i].load (std::memory_order_acquire)) return pendingValue[i].load (std::memory_order_relaxed);
    return (i == 0 ? pActive : pAuto)->load (std::memory_order_relaxed);
}

// ------------------------------------------------------------------ audio
void CrycatProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    if (n == 0) return;
    if (nIn == 1 && nOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // guitare mono -> sortie stéréo
    if (nIn == 0) buffer.clear();
    const int nch = jmin (2, nOut);
    float* ch[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    // traitement découpé aux instants des messages MIDI : la pédale suit le contrôleur à l'échantillon près
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, n, meta.samplePosition);
        if (at > pos) { engine.setSettings (settingsAt (pos)); engine.process (ch, nch, pos, at - pos); pos = at; }
        handleMidiMessage (meta.getMessage());
    }
    if (pos < n) { engine.setSettings (settingsAt (pos)); engine.process (ch, nch, pos, n - pos); }
    sampleClock += n;

    pedalShown = engine.smoothedPedal();
    engagedShown = engine.isEngaged();
    lastBlockMs = Time::getMillisecondCounter();

    pollCalibration();
}

void CrycatProcessor::timerCallback()
{
    const int f = pendingFactory.exchange (-1);
    if (f >= 0) loadFactory (f);
    for (int i = 0; i < numQueued; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acquire))
            setParamValue (queuedId (i), pendingValue[i].load (std::memory_order_relaxed));

    if (calibPending.exchange (false) && calibUntil.load() != 0)
    {
        calibUntil = 0;
        const int lo = calMin.load(), hi = calMax.load();
        if (hi - lo >= 12)             // balayage suffisant : on garde une petite marge à chaque bout
        {
            setParamValue ("heel", jlimit (0.0f, 1.0f, ((float) lo + 1.0f) / 127.0f));
            setParamValue ("toe",  jlimit (0.0f, 1.0f, ((float) hi - 1.0f) / 127.0f));
        }
    }
}

// ------------------------------------------------------------------ commandes de la fenêtre
void CrycatProcessor::setParamValue (const String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void CrycatProcessor::togglePedalSwitch (const String& id)
{
    if (auto* p = apvts.getRawParameterValue (id)) setParamValue (id, p->load() > 0.5f ? 0.0f : 1.0f);
}

void CrycatProcessor::loadFactory (int index)
{
    const auto& f = factory (index);
    currentFactory = jlimit (0, kNumFactory - 1, index);
    setParamValue ("range", (float) f.range);
    setParamValue ("q", f.q);
    setParamValue ("purr", f.purr);
    setParamValue ("boost", f.boost);
    setParamValue ("auto", f.autoMode ? 1.0f : 0.0f);
    if (f.pedal >= 0.0f) setParamValue ("pedal", f.pedal);
    setParamValue ("active", 1.0f);
}

void CrycatProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumFactory) return;
    if (MessageManager::getInstanceWithoutCreating() != nullptr && MessageManager::getInstance()->isThisTheMessageThread())
        loadFactory (index);
}

// ------------------------------------------------------------------ état
AudioProcessorEditor* CrycatProcessor::createEditor() { return new CrycatEditor (*this); }

void CrycatProcessor::getStateInformation (MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("ccPedal", ccPedal.load(), nullptr);
    st.setProperty ("ccAuto", ccAuto.load(), nullptr);
    st.setProperty ("noteAuto", noteAuto.load(), nullptr);
    st.setProperty ("ccFs", ccFs.load(), nullptr);
    st.setProperty ("noteFs", noteFs.load(), nullptr);
    st.setProperty ("uiScaleIdx", uiScaleIdx.load(), nullptr);
    st.setProperty ("factory", currentFactory, nullptr);
    st.setProperty ("autoMin", (double) autoMin.load(), nullptr);
    st.setProperty ("autoMax", (double) autoMax.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void CrycatProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto st = ValueTree::fromXml (*xml);
            apvts.replaceState (st);
            ccPedal  = (int) st.getProperty ("ccPedal", 11);
            ccAuto   = (int) st.getProperty ("ccAuto", -1);
            noteAuto = (int) st.getProperty ("noteAuto", -1);
            ccFs     = (int) st.getProperty ("ccFs", -1);
            noteFs   = (int) st.getProperty ("noteFs", -1);
            uiScaleIdx = jlimit (-1, 2, (int) st.getProperty ("uiScaleIdx", -1));
            currentFactory = jlimit (0, kNumFactory - 1, (int) st.getProperty ("factory", 0));
            autoMin = (float) (double) st.getProperty ("autoMin", 1.0);
            autoMax = (float) (double) st.getProperty ("autoMax", 0.0);
            pedalLive = mapPedal (pPedal->load(), false);
            autoHeld = false; fsHeld = false;
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CrycatProcessor(); }
