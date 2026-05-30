/*
  ==============================================================================
    Takefuji Groove Vault -- MIDI Pattern Browser
    PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette
//==============================================================================
namespace Palette
{
    static const juce::Colour bgDark      { 0xffF5F5F5 }; // window / main background
    static const juce::Colour bgPanel     { 0xffE8E8E8 }; // side filter panel
    static const juce::Colour bgBottom    { 0xffE0E0E0 }; // bottom bars (preview / keymap)
    static const juce::Colour accent      { 0xffDCDCDC }; // table header / outlines
    static const juce::Colour highlight   { 0xff3D5A8A }; // selected row / PLAY active
    static const juce::Colour textPrimary { 0xff333333 };
    static const juce::Colour textMuted   { 0xff777777 };
    static const juce::Colour rowEven     { 0xffFFFFFF };
    static const juce::Colour rowOdd      { 0xffF0F0F0 };
    static const juce::Colour rowSelected { 0xff3D5A8A };
}

//==============================================================================
// PatternRowComponent -- one reusable component per visible list row
//==============================================================================
class PatternRowComponent : public juce::Component
{
public:
    PatternRowComponent() = default;

    void update (const PatternInfo* info, int row, bool selected)
    {
        patternInfo = info;
        rowIndex    = row;
        isSelected  = selected;
        repaint();
    }

    void setMidiFile (const juce::File& f) { midiFile = f; }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (isSelected ? Palette::rowSelected
                              : (rowIndex % 2 == 0 ? Palette::rowEven : Palette::rowOdd));

        if (patternInfo == nullptr) return;

        // Drag handle: 2x3 dot grid on the left when hovered
        if (isHovered)
        {
            g.setColour ((isSelected ? juce::Colours::white : Palette::textMuted).withAlpha (0.75f));
            const float dotSize = 2.0f;
            const float hx = 4.0f;
            const float hy = static_cast<float> (getHeight()) / 2.0f - 5.5f;
            for (int col = 0; col < 2; ++col)
                for (int row2 = 0; row2 < 3; ++row2)
                    g.fillEllipse (hx + col * 4.5f, hy + row2 * 5.0f, dotSize, dotSize);
        }

        auto bounds = getLocalBounds().toFloat().reduced (6.f, 0.f);
        const float w = static_cast<float> (getWidth());

        struct Col { juce::String text; float frac; bool primary; };
        Col cols[] = {
            { patternInfo->display_name,                        0.33f, true  },
            { patternInfo->meter,                               0.10f, false },
            { patternInfo->category,                            0.19f, false },
            { patternInfo->vibe.joinIntoString (", "),          0.17f, false },
            { patternInfo->density,                             0.13f, false },
            { juce::String (patternInfo->bpm) + " BPM",        0.08f, false },
        };

        g.setFont (juce::Font (juce::FontOptions (13.f)));

        for (auto& c : cols)
        {
            const int colW = static_cast<int> (w * c.frac);
            g.setColour (isSelected ? juce::Colours::white
                                    : (c.primary ? Palette::textPrimary : Palette::textMuted));
            g.drawText (c.text,
                        bounds.removeFromLeft (static_cast<float> (colW)).toNearestInt(),
                        juce::Justification::centredLeft, true);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        isHovered = true;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        isHovered = false;
        repaint();
    }

    juce::MouseCursor getMouseCursor() override
    {
        return isHovered ? juce::MouseCursor (juce::MouseCursor::DraggingHandCursor)
                         : juce::MouseCursor (juce::MouseCursor::NormalCursor);
    }

    // mouseDown selects the row in the ListBox, because the row component consumes
    // mouse events and the ListBox would not see them otherwise.
    void mouseDown (const juce::MouseEvent&) override
    {
        if (auto* lb = findParentComponentOfClass<juce::ListBox>())
            lb->selectRow (rowIndex);
    }

    // mouseDrag triggers an OS-level external DnD.
    // If getDragFile is set, it may return a keymap-converted temp MIDI.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (patternInfo == nullptr || e.getDistanceFromDragStart() < 8) return;

        auto fileToUse = (getDragFile ? getDragFile() : midiFile);
        if (fileToUse.existsAsFile())
        {
            juce::StringArray files;
            files.add (fileToUse.getFullPathName());
            juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
        }
    }

    // Callback provided by the editor; returns original or converted MIDI file.
    std::function<juce::File()> getDragFile;

private:
    const PatternInfo* patternInfo = nullptr;
    juce::File         midiFile;
    int                rowIndex   = 0;
    bool               isSelected = false;
    bool               isHovered  = false;
};

//==============================================================================
// Keymap display helpers
//==============================================================================
// MIDI note -> "C1", "F#2" etc. (Studio One convention: C1 = 36)
static juce::String noteToOctaveName (int note)
{
    if (note < 0 || note > 127) return "---";
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (names[note % 12]) + juce::String ((note / 12) - 2);
}

// BFD3 note -> human-readable part name (from user-supplied mapping table)
static juce::String bfd3PartName (int note)
{
    switch (note)
    {
        case 24: return "Kick";
        case 25: return "Snare Rimshot";
        case 26: return "Snare Hit";
        case 27: return "Snare Sidestick";
        case 28: return "Snare Flam";
        case 29: return "Snare Edge";
        case 30: return "HH Half-open";
        case 31: return "Crash 1";
        case 34: return "HH Hit";
        case 35: return "Ride";
        case 36: return "Kick 2";
        case 37: return "HH Ghost";
        case 38: return "Tom 1";
        case 39: return "HH Sidestick";
        case 40: return "Snare 2";
        case 41: return "Tom 2";
        case 42: return "HH Closed";
        case 43: return "Tom 3 Floor";
        case 44: return "HH Pedal";
        case 45: return "Tom 3";
        case 46: return "HH Open";
        case 47: return "Tom 4 Floor";
        case 48: return "Extra";
        case 50: return "Tom 4";
        case 51: return "HH Bow";
        case 66: return "Ride Bow";
        case 83: return "Crash 2";
        case 84: return "Kick Alt";
        case 86: return "Snare Bow";
        case 88: return "Snare Pedal";
        case 89: return "Crash 1 Edge";
        case 91: return "Ride Edge";
        case 95: return "Aux";
        default: return {};
    }
}

// Format "part" + "artic" for display: "kick"+"hit" -> "Kick", "snare"+"rimshot" -> "Snare Rimshot"
static juce::String formatPartArtic (const juce::String& part, const juce::String& artic)
{
    auto cap = [] (const juce::String& s) -> juce::String
    {
        return s.isEmpty() ? s : s.substring (0, 1).toUpperCase() + s.substring (1);
    };
    return (artic.isEmpty() || artic == "hit") ? cap (part)
                                                : cap (part) + " " + cap (artic);
}

// Build "36 / C1 (Kick)" display string for a note with optional part name
static juce::String noteDisplayStr (int note, const juce::String& partName = {})
{
    auto s = juce::String (note) + " / " + noteToOctaveName (note);
    if (partName.isNotEmpty())
        s += " (" + partName + ")";
    return s;
}

//==============================================================================
// NoteDisplayCell -- composite cell component: [editor] [read-only note label]
//==============================================================================
class NoteDisplayCell : public juce::Component
{
public:
    std::function<void(int)> onValueChanged;

    NoteDisplayCell()
    {
        ed.setInputRestrictions (3, "0123456789");
        ed.setFont (juce::Font (juce::FontOptions (12.f)));
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xffFFFFFF));
        ed.setColour (juce::TextEditor::textColourId,       juce::Colour (0xff333333));
        ed.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xffBBBBBB));
        addAndMakeVisible (ed);

        lbl.setFont (juce::Font (juce::FontOptions (12.f)));
        lbl.setColour (juce::Label::textColourId, juce::Colour (0xff777777));
        addAndMakeVisible (lbl);
    }

    void setNote (int note, const juce::String& rightLabel)
    {
        ed.setText (juce::String (note), false);
        lbl.setText (rightLabel, juce::dontSendNotification);

        ed.onTextChange = [this] ()
        {
            int v = juce::jlimit (0, 127, ed.getText().getIntValue());
            lbl.setText ("/ " + noteToOctaveName (v), juce::dontSendNotification);
            if (onValueChanged) onValueChanged (v);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (2, 1);
        ed.setBounds (area.removeFromLeft (36));
        area.removeFromLeft (4);
        lbl.setBounds (area);
    }

private:
    juce::TextEditor ed;
    juce::Label      lbl;
};

//==============================================================================
// KeyMapEditorContent -- dialog component for viewing and saving keymaps
//==============================================================================
class KeyMapEditorContent : public juce::Component,
                             private juce::TableListBoxModel
{
public:
    KeyMapEditorContent (TakefujiGrooveVaultAudioProcessor& proc,
                         int srcIdx, int dstIdx)
        : audioProcessor (proc), sourceIdx (srcIdx), targetIdx (dstIdx)
    {
        const auto& keymaps = proc.getKeymaps();
        if (keymaps.isEmpty()) return;

        srcIdx = juce::jlimit (0, keymaps.size() - 1, srcIdx);
        dstIdx = juce::jlimit (0, keymaps.size() - 1, dstIdx);
        const auto& srcMap = keymaps[srcIdx];
        const auto& dstMap = keymaps[dstIdx];

        // Build editable target note list from part_to_note
        for (int i = 0; i < srcMap.dstKeys.size(); ++i)
        {
            int note = dstMap.getNoteForPartArtic (
                srcMap.dstKeys[i].upToLastOccurrenceOf ("_", false, false),
                srcMap.dstKeys[i].fromLastOccurrenceOf ("_", false, false));
            editedNotes.add (note >= 0 ? note : srcMap.dstNotes[i]);
        }

        // Table
        table.setModel (this);
        table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xffFFFFFF));
        table.setRowHeight (26);
        table.getHeader().addColumn ("Instrument (BFD3)", 1, 150);
        table.getHeader().addColumn ("BFD3 Note",         2, 200);
        table.getHeader().addColumn ("Target Note",       3, 220);
        table.getHeader().setStretchToFitActive (true);
        addAndMakeVisible (table);

        // Buttons
        auto setupBtn = [&](juce::TextButton& btn, const char* label, std::function<void()> fn)
        {
            btn.setButtonText (label);
            btn.onClick = std::move (fn);
            btn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xffFFFFFF));
            btn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff333333));
            addAndMakeVisible (btn);
        };

        setupBtn (saveBtn, "Save Custom", [this] { onSave(); });
        setupBtn (loadBtn, "Load Custom", [this] { onLoad(); });
        setupBtn (closeBtn, "Close",      [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        });
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto btnRow = area.removeFromBottom (32);
        area.removeFromBottom (6);
        table.setBounds (area);

        closeBtn.setBounds (btnRow.removeFromRight (80));
        btnRow.removeFromRight (6);
        loadBtn.setBounds (btnRow.removeFromRight (100));
        btnRow.removeFromRight (6);
        saveBtn.setBounds (btnRow.removeFromRight (100));
    }

private:
    //--- TableListBoxModel ---
    int getNumRows() override
    {
        const auto& keymaps = audioProcessor.getKeymaps();
        if (keymaps.isEmpty()) return 0;
        int si = juce::jlimit (0, keymaps.size() - 1, sourceIdx);
        return keymaps[si].dstKeys.size(); // one row per part_artic key
    }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? juce::Colour (0xff3D5A8A)
                            : (row % 2 == 0 ? juce::Colour (0xffFFFFFF)
                                            : juce::Colour (0xffF0F0F0)));
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        const auto& keymaps = audioProcessor.getKeymaps();
        if (keymaps.isEmpty() || row >= editedNotes.size()) return;
        int si = juce::jlimit (0, keymaps.size() - 1, sourceIdx);
        const auto& srcMap = keymaps[si];

        g.setFont (juce::Font (juce::FontOptions (13.f)));

        if (col == 0)
        {
            // Instrument column: BFD3 human-readable name for the source note
            int srcNote = (row < srcMap.dstNotes.size()) ? srcMap.dstNotes[row] : -1;
            juce::String name = bfd3PartName (srcNote);
            if (name.isEmpty())
                name = (row < srcMap.dstKeys.size()) ? srcMap.dstKeys[row] : "?";

            g.setColour (juce::Colour (0xff333333));
            g.drawText (name, 4, 0, w - 4, h, juce::Justification::centredLeft, true);
        }
        else if (col == 1)
        {
            // Source Note column: "24 / C0 (Kick)"
            int srcNote = (row < srcMap.dstNotes.size()) ? srcMap.dstNotes[row] : -1;
            juce::String text = noteDisplayStr (srcNote, bfd3PartName (srcNote));
            g.setColour (juce::Colour (0xff333333));
            g.drawText (text, 4, 0, w - 4, h, juce::Justification::centredLeft, true);
        }
        // col == 2 (Target Note) is handled by NoteDisplayCell via refreshComponentForCell
    }

    juce::Component* refreshComponentForCell (int row, int col, bool,
                                               juce::Component* existing) override
    {
        if (col != 2 || row >= editedNotes.size()) return nullptr;

        const auto& keymaps = audioProcessor.getKeymaps();

        // Build the right-side label for the target note cell
        int note = editedNotes[row];
        juce::String partName;

        if (!keymaps.isEmpty())
        {
            int di = juce::jlimit (0, keymaps.size() - 1, targetIdx);
            const auto& dstMap = keymaps[di];
            juce::String part, artic;
            if (dstMap.getInfoForNote (note, part, artic))
                partName = formatPartArtic (part, artic);

            if (partName.isEmpty())
                partName = bfd3PartName (note); // fallback to BFD3 names
        }

        juce::String rightLabel = "/ " + noteToOctaveName (note)
                                  + (partName.isNotEmpty() ? " (" + partName + ")" : "");

        auto* cell = dynamic_cast<NoteDisplayCell*> (existing);
        if (cell == nullptr)
            cell = new NoteDisplayCell();

        cell->onValueChanged = [this, row] (int v) {
            editedNotes.set (row, v);
        };
        cell->setNote (note, rightLabel);
        return cell;
    }

    //--- Button callbacks ---
    void onSave()
    {
        const auto& keymaps = audioProcessor.getKeymaps();
        if (keymaps.isEmpty()) return;
        int si = juce::jlimit (0, keymaps.size() - 1, sourceIdx);
        int di = juce::jlimit (0, keymaps.size() - 1, targetIdx);
        const auto& srcMap = keymaps[si];
        const auto& dstMap = keymaps[di];

        // Build custom keymap: same part keys as source, edited target notes
        KeyMapEntry custom;
        custom.name    = "Custom_" + dstMap.name;
        custom.srcNotes  = srcMap.srcNotes;
        custom.srcParts  = srcMap.srcParts;
        custom.srcArtics = srcMap.srcArtics;
        custom.dstKeys   = srcMap.dstKeys;
        for (int note : editedNotes)
            custom.dstNotes.add (note);

        // Save to APPDATA; ask for name using a file chooser (save panel)
        auto dir = TakefujiGrooveVaultAudioProcessor::getCustomKeymapsDir();
        dir.createDirectory();
        saveChooser = std::make_unique<juce::FileChooser> (
            "Save Custom Keymap", dir.getChildFile (custom.name + ".json"), "*.json");

        saveChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this, custom] (const juce::FileChooser& fc) mutable
            {
                auto f = fc.getResult();
                if (f == juce::File{}) return;
                custom.name = f.getFileNameWithoutExtension();
                if (audioProcessor.saveCustomKeymap (custom, custom.name))
                {
                    audioProcessor.loadCustomKeymaps();
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon,
                        "Saved", "Keymap saved: " + custom.name, "OK", this);
                }
            });
    }

    void onLoad()
    {
        auto dir = TakefujiGrooveVaultAudioProcessor::getCustomKeymapsDir();
        loadChooser = std::make_unique<juce::FileChooser> (
            "Load Custom Keymap", dir, "*.json");

        loadChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (!f.existsAsFile()) return;

                auto parsed = juce::JSON::parse (f);
                if (parsed.isVoid()) return;
                auto loaded = KeyMapEntry::fromJSON (parsed);
                if (loaded.isEmpty()) return;

                const auto& keymaps = audioProcessor.getKeymaps();
                if (keymaps.isEmpty()) return;
                int si = juce::jlimit (0, keymaps.size() - 1, sourceIdx);
                const auto& srcMap = keymaps[si];

                editedNotes.clear();
                for (int i = 0; i < srcMap.dstKeys.size(); ++i)
                {
                    const auto& key   = srcMap.dstKeys[i];
                    auto        part  = key.upToLastOccurrenceOf ("_", false, false);
                    auto        artic = key.fromLastOccurrenceOf ("_", false, false);
                    int note = loaded.getNoteForPartArtic (part, artic);
                    editedNotes.add (note >= 0 ? note : srcMap.dstNotes[i]);
                }
                table.updateContent();
                table.repaint();
            });
    }

    TakefujiGrooveVaultAudioProcessor& audioProcessor;
    int sourceIdx, targetIdx;
    juce::Array<int> editedNotes;

    juce::TableListBox table;
    juce::TextButton   saveBtn, loadBtn, closeBtn;

    // Keep FileChooser alive until the async callback fires
    std::unique_ptr<juce::FileChooser> saveChooser;
    std::unique_ptr<juce::FileChooser> loadChooser;
};

//==============================================================================
// Editor
//==============================================================================
TakefujiGrooveVaultAudioProcessorEditor::TakefujiGrooveVaultAudioProcessorEditor (
    TakefujiGrooveVaultAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (860, 558); // extra height for keymap bar

    //--- Filter title ----------------------------------------------------------
    filterTitle.setText ("FILTERS", juce::dontSendNotification);
    filterTitle.setFont (juce::Font (juce::FontOptions (11.f)).boldened());
    filterTitle.setColour (juce::Label::textColourId, Palette::textMuted);
    addAndMakeVisible (filterTitle);

    //--- Filter labels ---------------------------------------------------------
    auto setupLabel = [&](juce::Label& lbl, const char* text)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
        lbl.setColour (juce::Label::textColourId, Palette::textMuted);
        addAndMakeVisible (lbl);
    };

    setupLabel (sourceLabel,   "TRACK NAME");
    setupLabel (meterLabel,    "METER");
    setupLabel (categoryLabel, "CATEGORY");
    setupLabel (vibeLabel,     "VIBE");
    setupLabel (densityLabel,  "DENSITY");
    setupLabel (bpmLabel,      "BPM RANGE");
    setupLabel (bpmMinLabel,   "Min");
    setupLabel (bpmMaxLabel,   "Max");

    //--- Filter combo boxes ----------------------------------------------------
    auto setupCombo = [&](juce::ComboBox& cb)
    {
        cb.setColour (juce::ComboBox::backgroundColourId, Palette::bgDark);
        cb.setColour (juce::ComboBox::textColourId,       Palette::textPrimary);
        cb.setColour (juce::ComboBox::outlineColourId,    Palette::accent);
        cb.addListener (this);
        addAndMakeVisible (cb);
    };

    setupCombo (sourceFilter);
    setupCombo (meterFilter);
    setupCombo (categoryFilter);
    setupCombo (vibeFilter);
    setupCombo (densityFilter);

    //--- BPM range editors -----------------------------------------------------
    auto setupBpmEditor = [&](juce::TextEditor& ed, const char* init)
    {
        ed.setText (init, false);
        ed.setInputRestrictions (4, "0123456789");
        ed.setColour (juce::TextEditor::backgroundColourId, Palette::bgDark);
        ed.setColour (juce::TextEditor::textColourId,       Palette::textPrimary);
        ed.setColour (juce::TextEditor::outlineColourId,    Palette::accent);
        ed.setFont (juce::Font (juce::FontOptions (13.f)));
        ed.onTextChange = [this] { applyFilters(); };
        addAndMakeVisible (ed);
    };

    setupBpmEditor (bpmMinEditor, "0");
    setupBpmEditor (bpmMaxEditor, "999");

    //--- Pattern list ----------------------------------------------------------
    patternList.setModel (this);
    patternList.setRowHeight (kRowHeight);
    patternList.setColour (juce::ListBox::backgroundColourId, Palette::bgDark);
    patternList.setColour (juce::ListBox::outlineColourId,    Palette::accent);
    patternList.setOutlineThickness (1);
    addAndMakeVisible (patternList);

    //--- Preview bar -----------------------------------------------------------
    previewLabel.setText ("No pattern selected  --  Drag a row to your DAW",
                          juce::dontSendNotification);
    previewLabel.setFont (juce::Font (juce::FontOptions (13.f)));
    previewLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    addAndMakeVisible (previewLabel);

    playButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
    playButton.setColour (juce::TextButton::buttonOnColourId, Palette::highlight);
    playButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    playButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    // PLAY button starts disabled; selectedRowsChanged() enables it when a
    // pattern with a preview file is selected.
    playButton.setEnabled (false);
    playButton.onClick = [this]()
    {
        juce::Logger::writeToLog ("[GrooveVault] PLAY button clicked");
        togglePreview();
    };
    addAndMakeVisible (playButton);

    //--- Keymap bar (SOURCE fixed to BFD3; only TARGET is selectable) ----------
    targetMapLabel.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
    targetMapLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    targetMapLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (targetMapLabel);

    targetMapCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colours::white);
    targetMapCombo.setColour (juce::ComboBox::textColourId,       Palette::textPrimary);
    targetMapCombo.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xffBBBBBB));
    targetMapCombo.addListener (this);
    addAndMakeVisible (targetMapCombo);

    editMapButton.setColour (juce::TextButton::buttonColourId,  juce::Colours::white);
    editMapButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
    editMapButton.onClick = [this] { openKeyMapEditor(); };
    addAndMakeVisible (editMapButton);

    populateFilterOptions();
    populateKeyMapCombos();
    applyFilters();

    startTimerHz (10); // poll isPreviewPlaying() at 10 Hz to update button label
}

TakefujiGrooveVaultAudioProcessorEditor::~TakefujiGrooveVaultAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.stopPreview();
    patternList.setModel (nullptr);
}

//==============================================================================
void TakefujiGrooveVaultAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bgDark);

    // Filter panel background
    g.setColour (Palette::bgPanel);
    g.fillRect (0, 0, kFilterWidth, getHeight() - kPreviewHeight);

    // Column header strip
    const int listX = kFilterWidth;
    const int listW = getWidth() - kFilterWidth;
    g.setColour (Palette::accent);
    g.fillRect (listX, 0, listW, kHeaderHeight);

    // Column header labels
    g.setColour (Palette::textMuted);
    g.setFont (juce::Font (juce::FontOptions (10.f)).boldened());

    struct HCol { const char* name; float frac; };
    constexpr HCol headers[] = {
        { "NAME",     0.33f },
        { "METER",    0.10f },
        { "CATEGORY", 0.19f },
        { "VIBE",     0.17f },
        { "DENSITY",  0.13f },
        { "BPM",      0.08f },
    };

    int hx = listX + 6;
    for (auto& h : headers)
    {
        const int cw = static_cast<int> (listW * h.frac);
        g.drawText (h.name, hx, 0, cw, kHeaderHeight, juce::Justification::centredLeft, true);
        hx += cw;
    }

    // Keymap bar background (above preview bar, right area only)
    g.setColour (Palette::bgBottom);
    g.fillRect (kFilterWidth, getHeight() - kPreviewHeight - kKeyMapHeight,
                getWidth() - kFilterWidth, kKeyMapHeight);

    // Preview bar background
    g.setColour (Palette::bgBottom);
    g.fillRect (0, getHeight() - kPreviewHeight, getWidth(), kPreviewHeight);

    // Playback progress bar
    if (!progressBarBounds.isEmpty())
    {
        const int barH = 6;
        auto bar = progressBarBounds.withSizeKeepingCentre (progressBarBounds.getWidth(), barH).toFloat();
        g.setColour (juce::Colour (0xffCCCCCC)); // progress track
        g.fillRoundedRectangle (bar, barH / 2.0f);
        if (playbackProgress > 0.0)
        {
            auto fill = bar.withWidth (bar.getWidth() * (float) playbackProgress);
            g.setColour (Palette::highlight); // #3D5A8A fill
            g.fillRoundedRectangle (fill, barH / 2.0f);
        }
    }

    // Separator lines
    g.setColour (Palette::accent);
    g.fillRect (kFilterWidth - 1, 0, 1, getHeight());
    g.fillRect (0, getHeight() - kPreviewHeight, getWidth(), 1);
    g.fillRect (kFilterWidth, getHeight() - kPreviewHeight - kKeyMapHeight,
                getWidth() - kFilterWidth, 1);
}

void TakefujiGrooveVaultAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // --- Preview bar (bottom, full width) ---
    auto previewArea = area.removeFromBottom (kPreviewHeight).reduced (12, 14);
    playButton.setBounds (previewArea.removeFromRight (80));
    previewArea.removeFromRight (10);
    progressBarBounds = previewArea.removeFromRight (180);
    previewArea.removeFromRight (10);
    previewLabel.setBounds (previewArea);

    // --- Keymap bar: TARGET + EDIT MAP only (SOURCE is always BFD3) ---
    {
        auto kmRight = area.withTrimmedLeft (kFilterWidth)
                           .removeFromBottom (kKeyMapHeight)
                           .reduced (10, 6);
        targetMapLabel.setBounds (kmRight.removeFromLeft (130));
        targetMapCombo.setBounds (kmRight.removeFromLeft (210));
        kmRight.removeFromLeft (14);
        editMapButton.setBounds  (kmRight.removeFromLeft (90));
    }
    area.removeFromBottom (kKeyMapHeight);

    // --- Filter panel (left) ---
    auto filterArea = area.removeFromLeft (kFilterWidth).reduced (10, 10);

    filterTitle.setBounds (filterArea.removeFromTop (18));
    filterArea.removeFromTop (6);

    auto placeFilter = [&](juce::Label& lbl, juce::ComboBox& cb)
    {
        lbl.setBounds (filterArea.removeFromTop (15));
        cb.setBounds  (filterArea.removeFromTop (24));
        filterArea.removeFromTop (8);
    };

    placeFilter (sourceLabel,   sourceFilter);
    placeFilter (meterLabel,    meterFilter);
    placeFilter (categoryLabel, categoryFilter);
    placeFilter (vibeLabel,     vibeFilter);
    placeFilter (densityLabel,  densityFilter);

    bpmLabel.setBounds (filterArea.removeFromTop (15));
    auto bpmRow = filterArea.removeFromTop (24);
    bpmMinLabel.setBounds (bpmRow.removeFromLeft (26));
    bpmMinEditor.setBounds (bpmRow.removeFromLeft (54));
    bpmRow.removeFromLeft (6);
    bpmMaxLabel.setBounds (bpmRow.removeFromLeft (28));
    bpmMaxEditor.setBounds (bpmRow);

    // --- Pattern list (remaining right area, below header) ---
    area.removeFromTop (kHeaderHeight);
    patternList.setBounds (area);
}

//==============================================================================
// ListBoxModel
//==============================================================================
int TakefujiGrooveVaultAudioProcessorEditor::getNumRows()
{
    return filteredPatterns.size();
}

void TakefujiGrooveVaultAudioProcessorEditor::paintListBoxItem (
    int rowNumber, juce::Graphics& g, int /*width*/, int /*height*/, bool rowIsSelected)
{
    // Row rendering is handled by PatternRowComponent via refreshComponentForRow.
    // This fallback fires only if a row has no component assigned yet.
    g.fillAll (rowIsSelected ? Palette::rowSelected
                             : (rowNumber % 2 == 0 ? Palette::rowEven : Palette::rowOdd));
}

void TakefujiGrooveVaultAudioProcessorEditor::selectedRowsChanged (int lastRow)
{
    // Stop preview whenever the selection changes
    audioProcessor.stopPreview();
    playbackProgress = 0.0;
    lastPlayingState = false;
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setButtonText  ("PLAY");
    previewLabel.setColour (juce::Label::textColourId, Palette::textMuted);

    if (lastRow >= 0 && lastRow < filteredPatterns.size())
    {
        const auto& p = *filteredPatterns[lastRow];

        // Diagnose: log preview field and button enable decision
        juce::Logger::writeToLog ("[GrooveVault] selectedRowsChanged: row=" + juce::String (lastRow)
                                  + "  display_name=" + p.display_name
                                  + "  preview='" + p.preview + "'"
                                  + "  buttonEnabled=" + (p.preview.isNotEmpty() ? "YES" : "NO (preview empty)"));

        if (p.preview.isEmpty())
        {
            previewLabel.setText ("Drag to DAW  |  No preview available",
                                  juce::dontSendNotification);
            playButton.setEnabled (false);
        }
        else
        {
            previewLabel.setText ("Drag to DAW  |  Press PLAY to preview",
                                  juce::dontSendNotification);
            playButton.setEnabled (true);
        }
    }
    else
    {
        juce::Logger::writeToLog ("[GrooveVault] selectedRowsChanged: no row selected");
        // U+2190 (left arrow) encoded as UTF-8 escape sequences to avoid C4819
        previewLabel.setText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90"))
                              + " Select a pattern to preview or drag to DAW",
                              juce::dontSendNotification);
        playButton.setEnabled (false);
    }
}

juce::Component* TakefujiGrooveVaultAudioProcessorEditor::refreshComponentForRow (
    int row, bool isRowSelected, juce::Component* existing)
{
    auto* rowComp = dynamic_cast<PatternRowComponent*> (existing);
    if (rowComp == nullptr)
        rowComp = new PatternRowComponent();

    if (row < filteredPatterns.size())
    {
        const auto* p = filteredPatterns[row];
        rowComp->update (p, row, isRowSelected);

        // Resolve original MIDI file path
        auto resDir  = audioProcessor.getResourcesDirectory();
        auto midiSub = resDir.getChildFile ("midi");
        auto originalMidi = (midiSub.isDirectory()
                             ? midiSub.getChildFile (p->filename)
                             : resDir.getChildFile (p->filename));
        rowComp->setMidiFile (originalMidi);

        // Provide a DnD callback that applies keymap conversion on demand
        rowComp->getDragFile = [this, originalMidi]() {
            return this->getFileToDrag (originalMidi);
        };
    }
    else
    {
        rowComp->update (nullptr, row, isRowSelected);
        rowComp->getDragFile = nullptr;
    }

    return rowComp;
}

//==============================================================================
// Preview playback
//==============================================================================
void TakefujiGrooveVaultAudioProcessorEditor::togglePreview()
{
    juce::Logger::writeToLog ("[GrooveVault] togglePreview called"
                              "  isPlaying=" + juce::String (audioProcessor.isPreviewPlaying() ? "YES" : "NO"));

    if (audioProcessor.isPreviewPlaying())
    {
        audioProcessor.stopPreview();
        return;
    }

    int row = patternList.getSelectedRow();
    juce::Logger::writeToLog ("[GrooveVault] togglePreview: selectedRow=" + juce::String (row)
                              + "  filteredSize=" + juce::String (filteredPatterns.size()));

    if (row < 0 || row >= filteredPatterns.size())
    {
        juce::Logger::writeToLog ("[GrooveVault] togglePreview: no valid row");
        return;
    }

    const auto* p = filteredPatterns[row];
    juce::Logger::writeToLog ("[GrooveVault] togglePreview: pattern='" + p->display_name
                              + "'  preview='" + p->preview + "'");

    if (p->preview.isEmpty())
    {
        juce::Logger::writeToLog ("[GrooveVault] togglePreview: preview field is empty");
        return;
    }

    // Preview files live in Resources/preview/ in both dev and installed layouts.
    auto resourcesDir = audioProcessor.getResourcesDirectory();
    auto previewFile  = resourcesDir.getChildFile ("preview").getChildFile (p->preview);

    juce::Logger::writeToLog ("[GrooveVault] togglePreview: resourcesDir=" + resourcesDir.getFullPathName());
    juce::Logger::writeToLog ("[GrooveVault] togglePreview: previewFile="  + previewFile.getFullPathName()
                              + "  exists=" + juce::String (previewFile.existsAsFile() ? "YES" : "NO"));

    audioProcessor.startPreview (previewFile);
}

void TakefujiGrooveVaultAudioProcessorEditor::timerCallback()
{
    const bool playing     = audioProcessor.isPreviewPlaying();
    const bool stateChange = (playing != lastPlayingState);
    lastPlayingState = playing;

    // --- Progress bar ---
    if (playing)
        playbackProgress = audioProcessor.getPlaybackPosition();
    else if (stateChange)
        playbackProgress = 0.0; // just stopped -- clear bar

    // --- Button text / toggle state ---
    if (stateChange)
    {
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.setButtonText  (playing ? "STOP" : "PLAY");
    }

    // --- Preview label ---
    const int row = patternList.getSelectedRow();
    if (playing && row >= 0 && row < filteredPatterns.size())
    {
        const auto& p = *filteredPatterns[row];
        previewLabel.setText ("Now Playing: " + p.display_name
                              + "  |  " + p.meter
                              + "  |  " + juce::String (p.bpm) + " BPM",
                              juce::dontSendNotification);
        previewLabel.setColour (juce::Label::textColourId, Palette::textPrimary);
    }
    else if (stateChange && !playing)
    {
        // Playback just ended -- restore hint text matching selectedRowsChanged logic
        if (row >= 0 && row < filteredPatterns.size())
        {
            const auto& p = *filteredPatterns[row];
            previewLabel.setText (p.preview.isEmpty()
                                  ? "Drag to DAW  |  No preview available"
                                  : "Drag to DAW  |  Press PLAY to preview",
                                  juce::dontSendNotification);
        }
        else
        {
            previewLabel.setText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90"))
                                  + " Select a pattern to preview or drag to DAW",
                                  juce::dontSendNotification);
        }
        previewLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    }

    // Repaint the progress bar area (lightweight partial repaint)
    repaint (progressBarBounds.expanded (2));
}

//==============================================================================
// Keymap
//==============================================================================
void TakefujiGrooveVaultAudioProcessorEditor::populateKeyMapCombos()
{
    targetMapCombo.clear (juce::dontSendNotification);

    const auto& maps = audioProcessor.getKeymaps();
    for (int i = 0; i < maps.size(); ++i)
        targetMapCombo.addItem (maps[i].name, i + 1);

    // Default = BFD3 (no conversion; index 1 = first map = BFD3)
    targetMapCombo.setSelectedId (1, juce::dontSendNotification);
}

// Returns the index of the BFD3 map (always the source).
static int findBfd3Index (const juce::Array<KeyMapEntry>& maps)
{
    for (int i = 0; i < maps.size(); ++i)
        if (maps[i].name.equalsIgnoreCase ("BFD3"))
            return i;
    return 0;
}

juce::File TakefujiGrooveVaultAudioProcessorEditor::getFileToDrag (const juce::File& originalMidi)
{
    const auto& maps = audioProcessor.getKeymaps();
    if (maps.isEmpty() || !originalMidi.existsAsFile())
        return originalMidi;

    int srcIdx = findBfd3Index (maps);
    int dstIdx = juce::jlimit (0, maps.size() - 1, targetMapCombo.getSelectedId() - 1);

    // TARGET = BFD3 means no conversion
    if (srcIdx == dstIdx)
        return originalMidi;

    return audioProcessor.createConvertedMidiFile (originalMidi, maps[srcIdx], maps[dstIdx]);
}

void TakefujiGrooveVaultAudioProcessorEditor::openKeyMapEditor()
{
    const auto& maps = audioProcessor.getKeymaps();
    if (maps.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
            "No Keymaps", "No keymap files found in Resources/keymaps/.", "OK", this);
        return;
    }

    int srcIdx = findBfd3Index (maps);
    int dstIdx = juce::jlimit (0, maps.size() - 1, targetMapCombo.getSelectedId() - 1);

    auto* content = new KeyMapEditorContent (audioProcessor, srcIdx, dstIdx);
    content->setSize (620, 520);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle              = "Edit Keymap  [ " + maps[srcIdx].name
                                    + " -> " + maps[dstIdx].name + " ]";
    opts.componentToCentreAround  = this;
    opts.dialogBackgroundColour   = juce::Colour (0xffF5F5F5);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar        = false;
    opts.resizable                = true;
    opts.launchAsync();
}

//==============================================================================
// Filtering
//==============================================================================
void TakefujiGrooveVaultAudioProcessorEditor::comboBoxChanged (juce::ComboBox* cb)
{
    if (cb == &targetMapCombo) return; // keymap change doesn't need filter update
    applyFilters();
}

void TakefujiGrooveVaultAudioProcessorEditor::applyFilters()
{
    filteredPatterns.clear();

    // source filter: map selected combo ID back to a source_id string
    juce::String sourceSel;
    {
        const int selId = sourceFilter.getSelectedId();
        const int idx   = selId - 2; // ID 1 = "All", ID 2+ = real entries
        if (idx >= 0 && idx < sourceFilterIds.size())
            sourceSel = sourceFilterIds[idx];
    }

    const auto meterSel    = meterFilter.getText();
    const auto categorySel = categoryFilter.getText();
    const auto vibeSel     = vibeFilter.getText();
    const auto densitySel  = densityFilter.getText();
    const int  bpmMin      = bpmMinEditor.getText().getIntValue();
    int        bpmMax      = bpmMaxEditor.getText().getIntValue();
    if (bpmMax == 0) bpmMax = 9999;

    for (const auto& p : audioProcessor.getPatterns())
    {
        if (sourceSel.isNotEmpty() && p.source_id  != sourceSel)  continue;
        if (meterSel    != "All" && p.meter    != meterSel)    continue;
        if (categorySel != "All" && p.category != categorySel) continue;
        if (vibeSel     != "All" && !p.vibe.contains (vibeSel)) continue;
        if (densitySel  != "All" && p.density  != densitySel)  continue;
        if (p.bpm < bpmMin || p.bpm > bpmMax)                  continue;

        filteredPatterns.add (&p);
    }

    patternList.updateContent();
    patternList.repaint();

    juce::Logger::writeToLog ("[GrooveVault] applyFilters: " + juce::String (filteredPatterns.size())
                              + " results, currentRow=" + juce::String (patternList.getSelectedRow()));

    if (filteredPatterns.isEmpty())
    {
        // No results -- clear preview bar and disable PLAY
        selectedRowsChanged (-1);
    }
    else if (patternList.getSelectedRow() < 0 ||
             patternList.getSelectedRow() >= filteredPatterns.size())
    {
        // No valid selection -- auto-select first row so PLAY is immediately usable.
        // selectRow() internally calls selectedRowsChanged() on the model.
        patternList.selectRow (0, true /*don't scroll*/, true /*deselect others*/);
    }
    else
    {
        // Valid selection retained -- just refresh the preview bar text.
        selectedRowsChanged (patternList.getSelectedRow());
    }
}

void TakefujiGrooveVaultAudioProcessorEditor::populateFilterOptions()
{
    juce::StringArray sourceIds, meters, categories, vibes, densities;

    for (const auto& p : audioProcessor.getPatterns())
    {
        if (p.source_id.isNotEmpty() && !sourceIds.contains (p.source_id))
            sourceIds.add (p.source_id);
        if (!meters.contains     (p.meter))    meters.add     (p.meter);
        if (!categories.contains (p.category)) categories.add (p.category);
        if (!densities.contains  (p.density))  densities.add  (p.density);
        for (const auto& v : p.vibe)
            if (!vibes.contains (v)) vibes.add (v);
    }

    sourceIds.sort (false);   // sort by source_id (alpha/numeric)
    meters.sort    (false);
    categories.sort (false);
    vibes.sort     (false);
    densities.sort (false);

    // Build source filter: ID 1 = "All", ID 2..N = real entries
    sourceFilter.clear (juce::dontSendNotification);
    sourceFilter.addItem ("All", 1);
    sourceFilterIds.clear();
    for (int i = 0; i < sourceIds.size(); ++i)
    {
        const auto& sid = sourceIds[i];
        // Find the matching source_title for this id
        juce::String title;
        for (const auto& p : audioProcessor.getPatterns())
            if (p.source_id == sid) { title = p.source_title; break; }

        juce::String label = sid + (title.isNotEmpty() ? "  \"" + title + "\"" : "");
        sourceFilter.addItem (label, i + 2);
        sourceFilterIds.add (sid);
    }
    sourceFilter.setSelectedId (1, juce::dontSendNotification);

    auto populate = [](juce::ComboBox& cb, const juce::StringArray& items)
    {
        cb.clear (juce::dontSendNotification);
        cb.addItem ("All", 1);
        for (int i = 0; i < items.size(); ++i)
            cb.addItem (items[i], i + 2);
        cb.setSelectedId (1, juce::dontSendNotification);
    };

    populate (meterFilter,    meters);
    populate (categoryFilter, categories);
    populate (vibeFilter,     vibes);
    populate (densityFilter,  densities);
}
