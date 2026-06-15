/*
  ==============================================================================
    Takefuji Groove Vault -- MIDI Pattern Browser
    PluginEditor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <set>
#include <vector>
#include <map>

//==============================================================================
class MidiRollComponent : public juce::Component
{
public:
    struct NoteEvent {
        double position; // 0.0 - 1.0 relative to pattern total length
    };

    const juce::StringArray DRUM_PARTS {
        "Kick", "Snare", "Hi-Hat", "Tom 1", "Tom 2",
        "Floor Tom", "Ride", "Crash L", "Crash R", "China"
    };

    void setPattern (const std::vector<std::vector<NoteEvent>>& notes,
                     int ppqValue, double totalTicksValue,
                     int beatsPerBarValue, int beatUnitValue);

    // Set MIDI pattern duration in seconds (derived from tempo + ticks).
    // Must be called after setPattern() whenever a new pattern is loaded.
    void setMidiDurationSec (double sec) { midiDurationSec = sec; }

    // currentSec: audio playback position in seconds; negative = hidden
    void setPlayheadSec (double currentSec)
    {
        if (currentSec < 0.0 || midiDurationSec <= 0.0)
        {
            playheadPos = -1.0;
            currentPage = 0;
        }
        else
        {
            // Convert audio seconds to MIDI-relative 0.0-1.0
            playheadPos = juce::jlimit (0.0, 1.0, currentSec / midiDurationSec);
            if (totalTicks > 0.0 && ppq > 0)
            {
                const double ticksPerBeat = (double)ppq * (4.0 / (double)beatUnit);
                const double ticksPerPage = ticksPerBeat * (double)beatsPerBar * (double)kBarsPerPage;
                currentPage = (int)(playheadPos * totalTicks / ticksPerPage);
            }
            else currentPage = 0;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override;

private:
    static constexpr int kBarsPerPage = 2; // bars shown per page

    std::vector<std::vector<NoteEvent>> currentNotes;
    int    ppq            = 480;
    double totalTicks     = 0.0;
    double midiDurationSec = 0.0; // pattern length in seconds (from tempo meta event)
    int    beatsPerBar    = 4;
    int    beatUnit       = 4;
    double playheadPos    = -1.0;
    int    currentPage    = 0;
};

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
    void toggleFavourite (const juce::String& filename);
    void saveFavourites();
    std::set<juce::String> loadFavourites();

    //--- Data ------------------------------------------------------------------
    TakefujiGrooveVaultAudioProcessor& audioProcessor;
    juce::Array<const PatternInfo*> filteredPatterns;
    bool favoritesOnly    = false;
    int  loadingRowIndex  = -1; // row index whose play button shows a spinner (-1 = none)
    std::set<juce::String> favouriteIds;

    //--- Left: filter panel ----------------------------------------------------
    // Wrapped in a Viewport so the filter list can scroll when the window is too short.
    juce::Viewport filterViewport;
    juce::Component filterPanelContent;
    juce::Label    filterTitle;
    juce::TextButton favoritesOnlyToggle;
    juce::Label    sourceLabel,   meterLabel,    categoryLabel,  vibeLabel,   densityLabel, bpmLabel;
    juce::ComboBox sourceFilter,  meterFilter,   categoryFilter, vibeFilter,  densityFilter;
    juce::StringArray sourceFilterIds; // parallel to sourceFilter items (excludes "All")
    juce::Label    bpmMinLabel,   bpmMaxLabel;
    juce::TextEditor bpmMinEditor, bpmMaxEditor;

    //--- Right: pattern list ---------------------------------------------------
    juce::TextEditor searchEditor;
    juce::Label      countLabel;
    juce::ListBox    patternList;

    //--- Keymap bar (SOURCE is always BFD3; only TARGET is selectable) ---------
    juce::Label      targetMapLabel { {}, "Output Sound Library:" };
    juce::ComboBox   targetMapCombo;
    juce::TextButton editMapButton  { "EDIT MAP" };

    //--- Bottom: preview bar ---------------------------------------------------
    juce::Label      previewLabel;
    juce::Slider     volumeSlider;
    juce::Label      volumeLabel;

    // Progress bar state (updated in timerCallback)
    double               playbackProgress = 0.0;
    bool                 lastPlayingState = false;
    juce::Rectangle<int> progressBarBounds;

    //--- MIDI Roll -------------------------------------------------------------
    MidiRollComponent midiRollComponent;
    juce::Label       midiRollTitle;
    void loadMidiRollData (const juce::File& midiFile);

    //--- Layout constants ------------------------------------------------------
    static constexpr int kFilterWidth   = 190;
    static constexpr int kPreviewHeight = 72;
    static constexpr int kKeyMapHeight  = 38;
    static constexpr int kHeaderHeight  = 24;
    static constexpr int kRowHeight     = 32;
    static constexpr int kSearchHeight   = 34;
    static constexpr int kMidiRollHeight = 120;
    static constexpr int kMidiTitleH     = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakefujiGrooveVaultAudioProcessorEditor)
};
