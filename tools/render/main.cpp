#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const String out = argc > 1 ? argv[1] : ".";
    CrycatProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
    std::printf ("taille par défaut : %d x %d\n", ed->getWidth(), ed->getHeight());
    auto shot = [&] (const String& name, float pedal, int range, float q, float purr, float boost, bool autoM, bool active)
    {
        proc.setParamValue ("range", (float) range); proc.setParamValue ("q", q); proc.setParamValue ("purr", purr);
        proc.setParamValue ("boost", boost); proc.setParamValue ("auto", autoM ? 1.f : 0.f); proc.setParamValue ("active", active ? 1.f : 0.f);
        proc.setParamValue ("pedal", pedal);
        AudioBuffer<float> b (2, 256); MidiBuffer m;
        for (int i = 0; i < 200; ++i) { b.clear(); proc.processBlock (b, m); }
        for (int i = 0; i < 40; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        File f (out + "/" + name + ".png"); f.deleteFile();
        FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    shot ("crycat_talon", 0.0f, 2, 0.4f, 0.2f, 0.0f, false, true);
    shot ("crycat_milieu", 0.5f, 2, 0.4f, 0.2f, 0.0f, false, true);
    shot ("crycat_pointe", 1.0f, 4, 0.7f, 0.3f, 0.3f, false, true);
    shot ("crycat_bypass", 0.3f, 2, 0.4f, 0.2f, 0.0f, false, false);
    shot ("crycat_auto_veille", 0.0f, 0, 0.45f, 0.55f, 0.2f, true, true);
    static_cast<CrycatEditor*> (ed.get())->setScaleIndex (2);
    shot ("crycat_100pc_talon", 0.0f, 2, 0.4f, 0.2f, 0.0f, false, true);
    shot ("crycat_100pc_quart", 0.25f, 2, 0.4f, 0.2f, 0.0f, false, true);
    shot ("crycat_100pc_milieu", 0.55f, 3, 0.55f, 0.3f, 0.2f, false, true);
    shot ("crycat_100pc_pointe", 1.0f, 2, 0.4f, 0.2f, 0.0f, true, true);
    shot ("crycat_100pc_bypass", 0.7f, 5, 0.8f, 0.35f, 0.35f, false, false);
    ed.reset();
    return 0;
}
