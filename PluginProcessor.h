#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "CrycatDSP.h"

// ce que l'on peut assigner en MIDI depuis la fenêtre (clic droit -> MIDI learn)
enum class MidiTarget { None = 0, Pedal = 1, AutoSwitch = 2, Footswitch = 3 };

struct FactoryPreset
{
    const char* name;
    int   range;        // 0..5
    float q, purr, boost;
    bool  autoMode;
    float pedal;        // < 0 : ne touche pas à la position de la pédale
};

class CrycatProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    CrycatProcessor();
    ~CrycatProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Crycat"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    // presets usine visibles dans le menu de presets du DAW (et Program Change 1 à 10)
    static constexpr int kNumFactory = 10;
    static const FactoryPreset& factory (int i);
    int getNumPrograms() override { return kNumFactory; }
    int getCurrentProgram() override { return currentFactory; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return factory (index).name; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- utilisé par la fenêtre ----
    void loadFactory (int index);
    int  getCurrentFactory() const { return currentFactory; }
    void setParamValue (const juce::String& id, float plainValue);
    void togglePedalSwitch (const juce::String& id);
    crycat::Settings currentSettings() const;

    // position lissée de la pédale (0 = talon, 1 = pointe) : celle du moteur audio, ou la consigne directe si l'audio
    // ne tourne pas (transport arrêté, plugin suspendu) -> le visuel suit toujours la pédale MIDI et les automations
    float getPedalShown() const
    {
        const auto now = juce::Time::getMillisecondCounter();
        return (now - lastBlockMs.load() > 250u) ? pedalLive.load() : pedalShown.load();
    }
    bool  isEffectOn() const;          // footswitch ET (mode MAN ou pédale enclenchée en AUTO)

    void startMidiLearn (MidiTarget t) { learnTarget = learnIndexOf (t); }
    void cancelMidiLearn() { learnTarget = -1; }
    MidiTarget getLearnTarget() const
    {
        const int lt = learnTarget.load();
        return lt == lPedal ? MidiTarget::Pedal : lt == lAuto ? MidiTarget::AutoSwitch : lt == lFs ? MidiTarget::Footswitch : MidiTarget::None;
    }
    void clearMidi (MidiTarget t);
    juce::String getMidiDescription (MidiTarget t) const;

    // v1.5 MIDI Learn : toutes les commandes de la facade (clic droit sur la commande)
    enum LearnType { learnContinuous, learnToggle, learnSelector, learnFootswitch };
    struct LearnDef { const char* id; const char* name; int defaultCC; int type; int positions; };
    enum { lPedal = 0, lRange = 1, lQ = 2, lPurr = 3, lBoost = 4, lAuto = 5, lFs = 6 };
    static constexpr int numLearn = 7;
    static const LearnDef& learnDef (int i);
    static int learnIndexOf (MidiTarget t) { return t == MidiTarget::Pedal ? lPedal : t == MidiTarget::AutoSwitch ? lAuto : t == MidiTarget::Footswitch ? lFs : -1; }
    std::atomic<int> ccMap[numLearn];            // numero de CC de chaque commande, -1 = non assigne
    std::atomic<int> learnTarget { -1 };         // commande en attente d'un CC (-1 = pas de learn)
    std::atomic<int> lastLearned { -1 };         // derniere commande apprise (lue et remise a -1 par la fenetre)
    void resetMidiMap();
    void clearLearn (int i);
    juce::String learnAssignText (int i) const;  // "CC n", "Note n" ou "none"

    // calibration de la pédale d'expression : on la balaye en entier pendant 6 s
    void startCalibration();
    void pollCalibration();
    bool isCalibrating() const { return juce::Time::getMillisecondCounter() < calibUntil.load(); }

    std::atomic<int> uiScaleIdx { -1 };    // -1 = taille par défaut (67 %), 1 = 75 %, 2 = 100 %

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;
    void timerCallback() override;          // anti-gel v2 : vide la file des changements demandés par l'audio (60 Hz)
    void handleMidiMessage (const juce::MidiMessage&);
    float mapPedal (float raw01, bool fromMidi = true) const;
    crycat::Settings settingsAt (int offsetInBlock);   // thread audio

    crycat::Engine engine;
    double sr = 44100.0;

    std::atomic<float> *pRange, *pPedal, *pQ, *pPurr, *pBoost, *pActive, *pAuto, *pSmooth, *pAutoOff,
                       *pHeel, *pToe, *pInvert, *pPC, *pLatch;

    std::atomic<float> pedalLive { 0.0f }, pedalShown { 0.0f };
    std::atomic<bool> engagedShown { true };
    std::atomic<juce::uint32> lastBlockMs { 0 }, calibUntil { 0 };
    std::atomic<int> noteAuto { -1 }, noteFs { -1 };          // MAN / AUTO et footswitch : CC dans ccMap, ou note MIDI
    std::atomic<int> calMin { 128 }, calMax { -1 };
    int pedalMsb = -1, rampSamples = 0;                       // thread audio uniquement
    juce::int64 sampleClock = 0, lastPedalChange = 0;
    float lastPedalTarget = -1.0f;
    std::atomic<bool> autoHeld { false }, fsHeld { false }, calibPending { false };
    std::atomic<float> autoMin { 1.0f }, autoMax { 0.0f };          // plage réellement envoyée par la pédale MIDI (apprise automatiquement)
public:
    void resetCalibration();
    juce::String calibrationDescription() const;
private:

    // changements demandés depuis le thread audio (MIDI) et appliqués sur le thread de la fenêtre
    // anti-gel v2 : file de valeurs (aucun appel à l'hôte depuis l'audio), vidée par le timer du processeur
    static constexpr int numQueued = 6;
    static const char* queuedId (int i);              // "active", "auto", "range", "q", "purr", "boost"
    std::atomic<float>* queuedPtr[numQueued] {};
    void setParamFromAudio (int i, float plain);
    float queuedOrCurrent (int i) const;
    std::atomic<float> pendingValue[numQueued];
    std::atomic<bool>  pendingSet[numQueued];
    std::atomic<int> pendingFactory { -1 };
    int currentFactory = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrycatProcessor)
};
