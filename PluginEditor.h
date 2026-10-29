#pragma once
#include "PluginProcessor.h"

class PedalView;

class CrycatEditor : public juce::AudioProcessorEditor
{
public:
    explicit CrycatEditor (CrycatProcessor&);
    ~CrycatEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    int  getScaleIndex() const;          // 1, 2 = 75 %, 100 %  (-1 = taille par défaut / libre)
    void setScaleIndex (int idx);        // -1 = taille par défaut (67 %, aussi la taille minimale)

private:
    friend class PedalView;              // son timer surveille le moteur de rendu (anti-gel v2)
    void useSoftwareRenderer();          // applique le moteur logiciel (Windows) ; repaint si la fenêtre hôte a changé
    void checkRenderer();                // depuis le timer : seulement si la fenêtre a changé ou au plus 1 fois / s
    void resetRenderer();                // chien de garde : oublie la fenêtre mémorisée et réapplique tout
    void* lastPeer = nullptr;
    double lastRendererCheckMs = 0.0;
    CrycatProcessor& proc;
    std::unique_ptr<PedalView> view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrycatEditor)
};
