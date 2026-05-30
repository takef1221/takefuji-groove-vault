/*
  ==============================================================================
    Takefuji Groove Vault -- MIDI Pattern Browser
    PluginEditor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class TakefujiGrooveVaultAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      public juce::DragAndDropContainer,
      private juce::ListBoxModel,
      private juce::ComboBox::Listener,
      private juce::Timer
{
public:
    explicit TakefujiGrooveVaultAudioProcessorEditor (TakefujiGrooveVaultAudioProcessor&);
    ~TakefujiGrooveVaultAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Returns the file to hand off during a DnD gesture.
    // If source != target keymap, creates and returns a converted temp MIDI.
    juce::File getFileToDrag (const juce::File& originalMidi);

private:
    //--- ListBoxModel ----------------------------------------------------------
    int  getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected,
                                             juce::Component* existingComponentToUpdate) override;

    //--- ComboBox::Listener ----------------------------------------------------
    void comboBoxChanged (juce::ComboBox*) override;

    //--- Timer -----------------------------------------------------------------
    void timerCallback() override;

    //--- Helpers ---------------------------------------------------------------
    void populateFilterOptions();
    void applyFilters();
    void togglePreview();
    void populateKeyMapCombos();
    void openKeyMapEditor();

    //--- Data ------------------------------------------------------------------
    TakefujiGrooveVaultAudioProcessor& audioProcessor;
    juce::Array<const PatternInfo*> filteredPatterns;

    //--- Left: filter panel ----------------------------------------------------
    juce::Label    filterTitle;
    juce::Label    sourceLabel,   meterLabel,    categoryLabel,  vibeLabel,   densityLabel, bpmLabel;
    juce::ComboBox sourceFilter,  meterFilter,   categoryFilter, vibeFilter,  densityFilter;
    juce::StringArray sourceFilterIds; // parallel to sourceFilter items (excludes "All")
    juce::Label    bpmMinLabel,   bpmMaxLabel;
    juce::TextEditor bpmMinEditor, bpmMaxEditor;

    //--- Right: pattern list ---------------------------------------------------
    juce::ListBox patternList;

    //--- Keymap bar (SOURCE is always BFD3; only TARGET is selectable) ---------
    juce::Label      targetMapLabel { {}, "TARGET:" };
    juce::ComboBox   targetMapCombo;
    juce::TextButton editMapButton  { "EDIT MAP" };

    //--- Bottom: preview bar ---------------------------------------------------
    juce::Label      previewLabel;
    juce::TextButton playButton { "PLAY" };

    // Progress bar state (updated in timerCallback)
    double               playbackProgress = 0.0;
    bool                 lastPlayingState = false;
    juce::Rectangle<int> progressBarBounds;

    //--- Layout constants ------------------------------------------------------
    static constexpr int kFilterWidth   = 190;
    static constexpr int kPreviewHeight = 72;
    static constexpr int kKeyMapHeight  = 38;
    static constexpr int kHeaderHeight  = 24;
    static constexpr int kRowHeight     = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakefujiGrooveVaultAudioProcessorEditor)
};
