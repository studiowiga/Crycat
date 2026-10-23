#pragma once
#include "PluginProcessor.h"

class PedalView;

class CrycatEditor : public juce::AudioProcessorEditor
{
public:
    explicit CrycatEditor (CrycatProcessor&);
    ~CrycatEditor() override;
    void resized() override;
    void parentHierarchyChanged() override;
    int  getScaleIndex() const;          // 1, 2 = 75 %, 100 %  (-1 = taille par défaut / libre)
    void setScaleIndex (int idx);        // -1 = taille par défaut (67 %, aussi la taille minimale)

private:
    friend class PedalView;              // son timer relance useSoftwareRenderer()
    void useSoftwareRenderer();
    CrycatProcessor& proc;
    std::unique_ptr<PedalView> view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrycatEditor)
};
