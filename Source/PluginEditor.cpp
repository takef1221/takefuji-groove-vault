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
    static const juce::Colour bgDark       { 0xFFFFFFFF }; // BG_PRIMARY: white
    static const juce::Colour bgPanel      { 0xFFF5F5F3 }; // BG_SECONDARY: sidebar / hover
    static const juce::Colour bgBottom     { 0xFFF5F5F3 }; // BG_SECONDARY: bottom bars
    static const juce::Colour accent       { 0xFFF5F5F3 }; // header strip background
    static const juce::Colour highlight    { 0xFF639922 }; // ACCENT_GREEN
    static const juce::Colour textPrimary  { 0xFF1A1A1A }; // TEXT_PRIMARY
    static const juce::Colour textMuted    { 0xFF666666 }; // TEXT_SECONDARY
    static const juce::Colour textTertiary { 0xFF999999 }; // TEXT_TERTIARY
    static const juce::Colour borderSubtle { 0x26000000 }; // BORDER_SUBTLE
    static const juce::Colour borderNormal { 0x4D000000 }; // BORDER_NORMAL
    static const juce::Colour rowEven      { 0xFFFFFFFF }; // BG_PRIMARY
    static const juce::Colour rowOdd       { 0xFFF5F5F3 }; // BG_SECONDARY
    static const juce::Colour rowSelected  { 0xFFEAF3DE }; // BG_SELECTED (light green)
}

//==============================================================================
// Pattern list column layout
//
// Shared by PatternRowComponent::paint() (row cells) and
// TakefujiGrooveVaultAudioProcessorEditor::paint() (column headers) so both
// stay in sync as the window is resized.
//==============================================================================
struct ColumnLayout
{
    int  nameW, meterW, categoryW, vibeW, densityW, bpmW;
    bool showVibe, showDensity;
};

// textW = width available for the NAME..BPM columns (list width minus the
// fixed play-button and star columns and the row insets).
static ColumnLayout computeColumnLayout (int textW)
{
    constexpr int kMinName     = 100;
    constexpr int kMinMeter    = 40;
    constexpr int kMinCategory = 60;
    constexpr int kMinBpm      = 40;
    constexpr int kMinDensity  = 50;
    constexpr int kMinVibe     = 50;

    ColumnLayout cl;
    // Hide the least-important columns first as space runs out: VIBE, then DENSITY.
    cl.showVibe    = textW >= 560;
    cl.showDensity = textW >= 420;

    // Original design fractions (sum to 1.0 when all six columns are shown).
    constexpr float fName = 0.33f, fMeter = 0.10f, fCategory = 0.19f,
                     fVibe = 0.17f, fDensity = 0.13f, fBpm = 0.08f;

    const float total = fName + fMeter + fCategory + fBpm
                       + (cl.showVibe    ? fVibe    : 0.0f)
                       + (cl.showDensity ? fDensity : 0.0f);

    cl.nameW     = juce::roundToInt (textW * fName     / total);
    cl.meterW    = juce::roundToInt (textW * fMeter    / total);
    cl.categoryW = juce::roundToInt (textW * fCategory / total);
    cl.bpmW      = juce::roundToInt (textW * fBpm      / total);
    cl.densityW  = cl.showDensity ? juce::roundToInt (textW * fDensity / total) : 0;
    cl.vibeW     = cl.showVibe    ? juce::roundToInt (textW * fVibe    / total) : 0;

    // Enforce minimum widths on the higher-priority columns, taking the
    // difference from NAME (which absorbs all remaining/extra space).
    auto clampUp = [&] (int& w, int minW)
    {
        if (w < minW) { cl.nameW -= (minW - w); w = minW; }
    };
    clampUp (cl.meterW,    kMinMeter);
    clampUp (cl.categoryW, kMinCategory);
    clampUp (cl.bpmW,      kMinBpm);
    if (cl.showDensity) clampUp (cl.densityW, kMinDensity);
    if (cl.showVibe)    clampUp (cl.vibeW,    kMinVibe);

    cl.nameW = juce::jmax (cl.nameW, kMinName);

    // Give any rounding leftover/shortfall to NAME so columns exactly fill textW.
    const int used = cl.nameW + cl.meterW + cl.categoryW + cl.bpmW + cl.densityW + cl.vibeW;
    cl.nameW += (textW - used);

    return cl;
}

//==============================================================================
// MidiRollComponent
//==============================================================================
void MidiRollComponent::setPattern (const std::vector<std::vector<NoteEvent>>& notes,
                                     int ppqValue, double totalTicksValue,
                                     int beatsPerBarValue, int beatUnitValue)
{
    currentNotes    = notes;
    ppq             = ppqValue;
    totalTicks      = totalTicksValue;
    beatsPerBar     = beatsPerBarValue;
    beatUnit        = beatUnitValue;
    midiDurationSec = 0.0; // will be set by setMidiDurationSec() after this call
    playheadPos     = -1.0;
    currentPage     = 0;
    repaint();
}

void MidiRollComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const int   labelW = 56;
    const int   rows   = DRUM_PARTS.size();
    const float rowH   = (float)bounds.getHeight() / (float)rows;
    const float gridW  = (float)(bounds.getWidth() - labelW);

    // Background
    g.setColour (Palette::bgDark);
    g.fillRect (bounds);

    // Label area
    g.setColour (Palette::bgPanel);
    g.fillRect (0, 0, labelW, bounds.getHeight());
    g.setColour (Palette::borderSubtle);
    g.drawLine ((float)labelW, 0.0f, (float)labelW, (float)bounds.getHeight(), 0.5f);

    // Drum part labels and row dividers
    for (int r = 0; r < rows; r++)
    {
        float y = (float)r * rowH;
        g.setColour (Palette::borderSubtle);
        g.drawLine (0.0f, y + rowH, (float)bounds.getWidth(), y + rowH, 0.5f);
        g.setColour (Palette::textTertiary);
        g.setFont (juce::Font (juce::FontOptions (9.f)));
        g.drawText (DRUM_PARTS[r], 2, (int)y, labelW - 6, (int)rowH,
                    juce::Justification::centredRight, false);
    }

    // Grid area alternating row shading
    for (int r = 0; r < rows; r++)
    {
        if (r % 2 == 0)
        {
            g.setColour (juce::Colour (0x08000000));
            g.fillRect ((float)labelW, (float)r * rowH, gridW, rowH);
        }
    }

    if (totalTicks <= 0.0 || ppq <= 0) return;

    // Tick-space geometry: 1 page = kBarsPerPage bars while playing.
    // While stopped (no playhead), show the whole pattern on a single page.
    const double ticksPerBeat = (double)ppq * (4.0 / (double)beatUnit);
    const double ticksPerBar  = ticksPerBeat * (double)beatsPerBar;
    const bool   isPlaying    = playheadPos >= 0.0;
    const double ticksPerPage = isPlaying ? (ticksPerBar * (double)kBarsPerPage) : totalTicks;
    const double totalBeats   = totalTicks / ticksPerBeat;
    const int    numBeats     = juce::jmax (1, (int)std::ceil (totalBeats));

    // Current page window in tick space
    const double pageStartTick = (double)currentPage * ticksPerPage;
    const double pageEndTick   = pageStartTick + ticksPerPage;

    // Convert to 0.0-1.0 for note-position comparisons
    const double pageStart = pageStartTick / totalTicks;
    const double pageEnd   = pageEndTick   / totalTicks; // may exceed 1.0 for last page
    // Scale: pattern-relative pos → page-relative pos (0.0 = left edge, 1.0 = right edge)
    const double pageScale = totalTicks / ticksPerPage;

    // Beat grid lines — only those within the current page tick window
    for (int b = 0; b <= numBeats; b++)
    {
        const double beatTick = (double)b * ticksPerBeat;
        if (beatTick < pageStartTick - 0.5 || beatTick > pageEndTick + 0.5) continue;
        const float x     = (float)labelW
                            + (float)((beatTick - pageStartTick) / ticksPerPage) * gridW;
        const bool  isBar = (b % beatsPerBar == 0);
        g.setColour (isBar ? juce::Colour (0x22000000) : juce::Colour (0x0E000000));
        g.drawLine (x, 0.0f, x, (float)bounds.getHeight(), isBar ? 1.0f : 0.5f);
    }

    // Notes — only those within [pageStart, pageEnd)
    const float noteW = 4.0f;
    g.setColour (Palette::highlight);
    for (int r = 0; r < rows && r < (int)currentNotes.size(); r++)
    {
        for (const auto& note : currentNotes[r])
        {
            if (note.position < pageStart || note.position >= pageEnd) continue;
            float x  = (float)labelW
                        + (float)((note.position - pageStart) * pageScale) * gridW
                        - noteW * 0.5f;
            float y  = (float)r * rowH + 2.0f;
            float nH = rowH - 4.0f;
            g.fillRoundedRectangle (x, y, noteW, nH, 1.5f);
        }
    }

    // Playhead in page-relative coordinates
    if (playheadPos >= 0.0 && playheadPos <= 1.0)
    {
        const double pageRel = juce::jlimit (0.0, 1.0,
                                             (playheadPos - pageStart) * pageScale);
        const float px = (float)labelW + (float)pageRel * gridW;
        g.setColour (juce::Colour (0xCC000000));
        g.drawLine (px, 0.0f, px, (float)bounds.getHeight(), 1.5f);
    }
}

//==============================================================================
// PatternRowComponent -- one reusable component per visible list row
//==============================================================================
class PatternRowComponent : public juce::Component,
                            private juce::Timer
{
public:
    PatternRowComponent() = default;

    void update (const PatternInfo* info, int row, bool selected,
                 bool favourite = false, bool playing = false, bool loading = false)
    {
        patternInfo = info;
        rowIndex    = row;
        isSelected  = selected;
        isFavourite = favourite;
        isPlaying   = playing;
        if (loading != isLoading)
        {
            isLoading = loading;
            if (isLoading) startTimer (30);
            else           stopTimer();
        }
        repaint();
    }

    void setMidiFile (const juce::File& f) { midiFile = f; }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (isSelected ? Palette::rowSelected
                              : (rowIndex % 2 == 0 ? Palette::rowEven : Palette::rowOdd));

        if (patternInfo == nullptr) return;

        auto bounds = getLocalBounds().toFloat().reduced (6.f, 0.f);

        // Play / Stop button (22px fixed width)
        {
            auto playRect = bounds.removeFromLeft (22.f);
            const float cx = playRect.getCentreX();
            const float cy = playRect.getCentreY();
            const float r  = 10.5f;

            if (isLoading)
            {
                // Spinner: thin grey ring + rotating green arc (quarter circle)
                g.setColour (juce::Colour (0x20000000));
                g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

                const float arcR = r - 2.5f;
                juce::Path arc;
                arc.addArc (cx - arcR, cy - arcR, arcR * 2.0f, arcR * 2.0f,
                             spinnerAngle,
                             spinnerAngle + juce::MathConstants<float>::halfPi, true);
                juce::PathStrokeType stroke (2.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);
                g.setColour (juce::Colour (0xFF639922));
                g.strokePath (arc, stroke);
            }
            else if (isPlaying)
            {
                g.setColour (juce::Colour (0xFF639922));
                g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
                g.setColour (juce::Colours::white);
                const float sq = 7.0f;
                g.fillRect (cx - sq * 0.5f, cy - sq * 0.5f, sq, sq);
            }
            else
            {
                // Pressed: dark fill inside circle
                if (isButtonPress)
                {
                    g.setColour (juce::Colour (0x25000000));
                    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
                }
                // Border: green on hover, normal otherwise
                g.setColour (isButtonHover ? juce::Colour (0xFF639922)
                                           : juce::Colour (0x4D000000));
                g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
                // Triangle icon
                g.setColour (juce::Colour (0xFF666666));
                juce::Path tri;
                tri.addTriangle (cx - 3.5f, cy - 5.0f,
                                 cx - 3.5f, cy + 5.0f,
                                 cx + 5.5f, cy);
                g.fillPath (tri);
            }
        }

        // Star column (20px fixed width)
        {
            g.setFont (juce::Font (juce::FontOptions (14.f)));
            g.setColour (isFavourite ? juce::Colour (0xFFBA7517) : juce::Colour (0xFFCCCCCC));
            const juce::String star = isFavourite
                ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85"))
                : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86"));
            g.drawText (star, bounds.removeFromLeft (20.f).toNearestInt(),
                        juce::Justification::centred, false);
        }

        const int textW = (int) bounds.getWidth();
        const ColumnLayout cl = computeColumnLayout (textW);

        struct Col { juce::String text; int width; bool primary; bool isCat; };
        Col cols[] = {
            { patternInfo->display_name,                cl.nameW,     true,  false },
            { patternInfo->meter,                       cl.meterW,    false, false },
            { patternInfo->category,                    cl.categoryW, false, true  },
            { patternInfo->vibe.joinIntoString (", "),   cl.vibeW,     false, false },
            { patternInfo->density,                      cl.densityW,  false, false },
            { juce::String (patternInfo->bpm) + " BPM",  cl.bpmW,      false, false },
        };

        g.setFont (juce::Font (juce::FontOptions (13.f)));

        for (auto& c : cols)
        {
            if (c.width <= 0) continue; // column hidden at this width

            auto cellBounds = bounds.removeFromLeft (static_cast<float> (c.width)).toNearestInt();

            if (c.isCat && c.text.isNotEmpty())
            {
                // Category badge
                juce::Colour bg, fg;
                if      (c.text == "Groove") { bg = juce::Colour (0xFFEAF3DE); fg = juce::Colour (0xFF3B6D11); }
                else if (c.text == "Fill")   { bg = juce::Colour (0xFFFAEEDA); fg = juce::Colour (0xFF854F0B); }
                else if (c.text == "Build")  { bg = juce::Colour (0xFFE6F1FB); fg = juce::Colour (0xFF185FA5); }
                else                         { bg = Palette::bgPanel;          fg = Palette::textMuted;         }
                auto pill = cellBounds.reduced (2).withSizeKeepingCentre (
                    juce::jmin (cellBounds.getWidth(), 60), 18);
                g.setColour (bg);
                g.fillRoundedRectangle (pill.toFloat(), 9.0f);
                g.setColour (fg);
                g.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
                g.drawText (c.text, pill, juce::Justification::centred, false);
                g.setFont (juce::Font (juce::FontOptions (13.f))); // restore
            }
            else
            {
                g.setColour (c.primary ? Palette::textPrimary : Palette::textMuted);
                // Leave a small gap so truncated text never touches the next column.
                g.drawText (c.text, cellBounds.withTrimmedRight (4), juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseEnter (const juce::MouseEvent& e) override
    {
        isHovered     = true;
        isButtonHover = (e.x >= 6 && e.x < 28);
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool overBtn = (e.x >= 6 && e.x < 28);
        if (overBtn != isButtonHover)
        {
            isButtonHover = overBtn;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        isHovered     = false;
        isButtonHover = false;
        isButtonPress = false;
        repaint();
    }

    juce::MouseCursor getMouseCursor() override
    {
        return isHovered ? juce::MouseCursor (juce::MouseCursor::DraggingHandCursor)
                         : juce::MouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Play button area: x∈[6, 28) — show press state; fire on mouseUp
        if (e.x >= 6 && e.x < 28 && patternInfo != nullptr)
        {
            isButtonPress = true;
            repaint();
            return;
        }
        // Star area: x∈[28, 48)
        if (e.x >= 28 && e.x < 48 && patternInfo != nullptr)
        {
            if (onStarClicked) onStarClicked();
            return;
        }
        if (auto* lb = findParentComponentOfClass<juce::ListBox>())
            lb->selectRow (rowIndex);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isButtonPress)
        {
            isButtonPress = false;
            repaint();
            // Fire only if mouse released within the button area
            if (e.x >= 6 && e.x < 28 && patternInfo != nullptr)
                if (onPlayClicked) onPlayClicked();
        }
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

    // Called when the play/stop button is clicked.
    std::function<void()> onPlayClicked;

    // Called when the star icon is clicked.
    std::function<void()> onStarClicked;

private:
    void timerCallback() override
    {
        // Advance spinner angle: ~1 full rotation per second at 30ms interval
        spinnerAngle += juce::MathConstants<float>::twoPi * 0.030f;
        if (spinnerAngle >= juce::MathConstants<float>::twoPi)
            spinnerAngle -= juce::MathConstants<float>::twoPi;
        repaint();
    }

    const PatternInfo* patternInfo  = nullptr;
    juce::File         midiFile;
    int                rowIndex      = 0;
    bool               isSelected    = false;
    bool               isHovered     = false;
    bool               isFavourite   = false;
    bool               isPlaying     = false;
    bool               isLoading     = false;
    float              spinnerAngle  = 0.0f;
    bool               isButtonHover = false;
    bool               isButtonPress = false;
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
        g.fillAll (selected ? Palette::rowSelected
                            : (row % 2 == 0 ? Palette::rowEven : Palette::rowOdd));
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
    // Load persisted favourites
    favouriteIds = loadFavourites();

    setSize (860, 558); // extra height for keymap bar

    //--- Filter panel scroll container ------------------------------------------
    addAndMakeVisible (filterViewport);
    filterViewport.setViewedComponent (&filterPanelContent, false);
    filterViewport.setScrollBarsShown (true, false);

    //--- Filter title ----------------------------------------------------------
    filterTitle.setText ("FILTERS", juce::dontSendNotification);
    filterTitle.setFont (juce::Font (juce::FontOptions (11.f)).boldened());
    filterTitle.setColour (juce::Label::textColourId, Palette::textTertiary);
    filterPanelContent.addAndMakeVisible (filterTitle);

    //--- Filter labels ---------------------------------------------------------
    auto setupLabel = [&](juce::Label& lbl, const char* text)
    {
        lbl.setText (text, juce::dontSendNotification);
        lbl.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
        lbl.setColour (juce::Label::textColourId, Palette::textTertiary);
        filterPanelContent.addAndMakeVisible (lbl);
    };

    setupLabel (sourceLabel,   "TRACK NAME");
    setupLabel (meterLabel,    "METER");
    setupLabel (categoryLabel, "CATEGORY");
    setupLabel (vibeLabel,     "VIBE");
    setupLabel (densityLabel,  "DENSITY");
    setupLabel (bpmLabel,      "BPM RANGE");
    setupLabel (bpmMinLabel,   "Min");
    setupLabel (bpmMaxLabel,   "Max");

    //--- Favorites Only toggle -------------------------------------------------
    // U+2605 ★ encoded as UTF-8 escape to avoid CP932 source-file warning
    favoritesOnlyToggle.setButtonText (
        juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) + "  Favorites only");
    favoritesOnlyToggle.setClickingTogglesState (true);
    favoritesOnlyToggle.setColour (juce::TextButton::buttonColourId,   Palette::bgPanel);
    favoritesOnlyToggle.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFEAF3DE));
    favoritesOnlyToggle.setColour (juce::TextButton::textColourOffId,  Palette::textMuted);
    favoritesOnlyToggle.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xFF3B6D11));
    favoritesOnlyToggle.onClick = [this]()
    {
        favoritesOnly = favoritesOnlyToggle.getToggleState();
        applyFilters();
    };
    filterPanelContent.addAndMakeVisible (favoritesOnlyToggle);

    //--- Filter combo boxes ----------------------------------------------------
    auto setupCombo = [&](juce::ComboBox& cb)
    {
        cb.setColour (juce::ComboBox::backgroundColourId, Palette::bgDark);
        cb.setColour (juce::ComboBox::textColourId,       Palette::textPrimary);
        cb.setColour (juce::ComboBox::outlineColourId,    Palette::borderSubtle);
        cb.addListener (this);
        filterPanelContent.addAndMakeVisible (cb);
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
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xffFFFFFF));
        ed.setColour (juce::TextEditor::textColourId,       juce::Colour (0xff333333));
        ed.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xffBBBBBB));
        ed.setFont (juce::Font (juce::FontOptions (13.f)));
        ed.onTextChange = [this] { applyFilters(); };
        filterPanelContent.addAndMakeVisible (ed);
    };

    setupBpmEditor (bpmMinEditor, "0");
    setupBpmEditor (bpmMaxEditor, "999");

    //--- Search bar -----------------------------------------------------------
    searchEditor.setTextToShowWhenEmpty ("Search patterns...", Palette::textTertiary);
    searchEditor.setFont (juce::Font (juce::FontOptions (12.f)));
    searchEditor.setColour (juce::TextEditor::backgroundColourId, Palette::bgDark);
    searchEditor.setColour (juce::TextEditor::textColourId,       Palette::textPrimary);
    searchEditor.setColour (juce::TextEditor::outlineColourId,    Palette::borderSubtle);
    searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::borderNormal);
    searchEditor.onTextChange = [this] { applyFilters(); };
    addAndMakeVisible (searchEditor);

    countLabel.setFont (juce::Font (juce::FontOptions (11.f)));
    countLabel.setColour (juce::Label::textColourId, Palette::textTertiary);
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (countLabel);

    //--- Pattern list ----------------------------------------------------------
    patternList.setModel (this);
    patternList.setRowHeight (kRowHeight);
    patternList.setColour (juce::ListBox::backgroundColourId, Palette::bgDark);
    patternList.setColour (juce::ListBox::outlineColourId,    Palette::borderSubtle);
    patternList.setOutlineThickness (0);
    addAndMakeVisible (patternList);

    //--- Preview bar -----------------------------------------------------------
    previewLabel.setText ("No pattern selected  --  Drag a row to your DAW",
                          juce::dontSendNotification);
    previewLabel.setFont (juce::Font (juce::FontOptions (13.f)));
    previewLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    addAndMakeVisible (previewLabel);

    //--- Volume slider -----------------------------------------------------------
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.setColour (juce::Slider::trackColourId,     Palette::borderNormal);
    volumeSlider.setColour (juce::Slider::thumbColourId,     Palette::textPrimary);
    volumeSlider.setColour (juce::Slider::backgroundColourId, Palette::bgPanel);
    volumeSlider.onValueChange = [this]()
    {
        float vol = (float) volumeSlider.getValue();
        audioProcessor.setPreviewVolume (vol);
        volumeLabel.setText (juce::String (juce::roundToInt (vol * 100.0f)) + "%",
                             juce::dontSendNotification);
    };
    addAndMakeVisible (volumeSlider);

    volumeLabel.setText ("80%", juce::dontSendNotification);
    volumeLabel.setFont (juce::Font (juce::FontOptions (10.f)));
    volumeLabel.setColour (juce::Label::textColourId, Palette::textTertiary);
    addAndMakeVisible (volumeLabel);

    //--- Keymap bar (SOURCE fixed to BFD3; only TARGET is selectable) ----------
    targetMapLabel.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
    targetMapLabel.setColour (juce::Label::textColourId, Palette::textTertiary);
    targetMapLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (targetMapLabel);

    targetMapCombo.setColour (juce::ComboBox::backgroundColourId, Palette::bgDark);
    targetMapCombo.setColour (juce::ComboBox::textColourId,       Palette::textPrimary);
    targetMapCombo.setColour (juce::ComboBox::outlineColourId,    Palette::borderSubtle);
    targetMapCombo.addListener (this);
    addAndMakeVisible (targetMapCombo);

    editMapButton.setColour (juce::TextButton::buttonColourId,  Palette::bgDark);
    editMapButton.setColour (juce::TextButton::textColourOffId, Palette::textPrimary);
    editMapButton.onClick = [this] { openKeyMapEditor(); };
    addAndMakeVisible (editMapButton);

    //--- MIDI Roll -----------------------------------------------------------
    midiRollTitle.setText ("MIDI PREVIEW", juce::dontSendNotification);
    midiRollTitle.setFont (juce::Font (juce::FontOptions (10.f)).boldened());
    midiRollTitle.setColour (juce::Label::textColourId, Palette::textTertiary);
    addAndMakeVisible (midiRollTitle);
    addAndMakeVisible (midiRollComponent);

    populateFilterOptions();
    populateKeyMapCombos();
    applyFilters();

    startTimer (30); // 30ms ≈ 33fps — drives progress bar, playhead, and spinner detection

    // Register download-complete callback (runs on message thread)
    {
        juce::Component::SafePointer<TakefujiGrooveVaultAudioProcessorEditor> safe (this);
        audioProcessor.setPreviewReadyCallback ([safe] (bool offline)
        {
            if (safe == nullptr) return;
            auto* ed = safe.getComponent();
            // Clear loading state (success or failure)
            if (ed->loadingRowIndex >= 0)
            {
                ed->loadingRowIndex = -1;
                ed->patternList.updateContent();
            }
            if (offline)
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Preview Error",
                    juce::String (juce::CharPointer_UTF8 (
                        "\xe3\x83\x97\xe3\x83\xac\xe3\x83\x93\xe3\x83\xa5\xe3\x83\xbc\xe3\x82\x92"
                        "\xe5\x86\x8d\xe7\x94\x9f\xe3\x81\xa7\xe3\x81\x8d\xe3\x81\xbe\xe3\x81\x9b"
                        "\xe3\x82\x93\xe3\x80\x82\x0a"
                        "\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xbf\xe3\x83\xbc\xe3\x83\x8d\xe3\x83\x83"
                        "\xe3\x83\x88\xe6\x8e\xa5\xe7\xb6\x9a\xe3\x82\x92\xe7\xa2\xba\xe8\xaa\x8d"
                        "\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84"
                        "\xe3\x80\x82")));
        });
    }
}

TakefujiGrooveVaultAudioProcessorEditor::~TakefujiGrooveVaultAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.setPreviewReadyCallback (nullptr); // prevent callback on destroyed editor
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

    // Column header strip (below search bar)
    const int listX  = kFilterWidth;
    const int listW  = getWidth() - kFilterWidth;
    const int headerY = kSearchHeight;
    g.setColour (Palette::accent);
    g.fillRect (listX, headerY, listW, kHeaderHeight);

    // Column header labels
    g.setColour (Palette::textTertiary);
    g.setFont (juce::Font (juce::FontOptions (10.f)).boldened());

    int hx = listX + 6;
    // Play button column header (22px — left blank, icon only as visual hint)
    hx += 22;
    // Star column header (20px)
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")),
                hx, headerY, 20, kHeaderHeight, juce::Justification::centred, false);
    hx += 20;
    const int textListW = listW - 54; // 12px insets + 22px play + 20px star
    const ColumnLayout cl = computeColumnLayout (textListW);

    struct HCol { const char* name; int width; };
    HCol headers[] = {
        { "NAME",     cl.nameW },
        { "METER",    cl.meterW },
        { "CATEGORY", cl.categoryW },
        { "VIBE",     cl.vibeW },
        { "DENSITY",  cl.densityW },
        { "BPM",      cl.bpmW },
    };

    for (auto& h : headers)
    {
        if (h.width > 0)
            g.drawText (h.name, hx, headerY, h.width, kHeaderHeight, juce::Justification::centredLeft, true);
        hx += h.width;
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

    // Separator lines (0.5px thin)
    g.setColour (Palette::borderSubtle);
    g.drawLine ((float)kFilterWidth, 0.0f, (float)kFilterWidth, (float)getHeight(), 0.5f);
    g.drawLine (0.0f, (float)(getHeight() - kPreviewHeight),
                (float)getWidth(), (float)(getHeight() - kPreviewHeight), 0.5f);
    g.drawLine (0.0f, (float)(getHeight() - kPreviewHeight - kMidiTitleH - kMidiRollHeight),
                (float)getWidth(), (float)(getHeight() - kPreviewHeight - kMidiTitleH - kMidiRollHeight), 0.5f);
    g.drawLine ((float)kFilterWidth, (float)(getHeight() - kPreviewHeight - kMidiTitleH - kMidiRollHeight - kKeyMapHeight),
                (float)getWidth(), (float)(getHeight() - kPreviewHeight - kMidiTitleH - kMidiRollHeight - kKeyMapHeight), 0.5f);
}

void TakefujiGrooveVaultAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // --- MIDI Roll (above preview bar, full width) ---
    {
        auto midiArea = area.removeFromBottom (kMidiTitleH + kMidiRollHeight);
        midiRollTitle.setBounds (midiArea.removeFromTop (kMidiTitleH).withTrimmedLeft (8));
        midiRollComponent.setBounds (midiArea);
    }

    // --- Preview bar (bottom, full width) ---
    auto previewArea = area.removeFromBottom (kPreviewHeight).reduced (12, 14);
    // Right side: volume label + slider
    volumeLabel.setBounds  (previewArea.removeFromRight (32));
    volumeSlider.setBounds (previewArea.removeFromRight (68));
    previewArea.removeFromRight (8);
    // Progress bar
    progressBarBounds = previewArea.removeFromRight (180);
    previewArea.removeFromRight (8);
    // Preview label (leftmost, flexible width)
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

    // --- Filter panel (left, scrollable if window is too short) ---
    auto filterOuter = area.removeFromLeft (kFilterWidth);
    filterViewport.setBounds (filterOuter);

    // -8 reserves space for the vertical scrollbar so no horizontal scrollbar appears.
    constexpr int contentW = kFilterWidth - 8;
    constexpr int contentH = 10 + 18 + 6 + 28 + 8 + 5 * (15 + 24 + 8) + 15 + 24 + 10;
    filterPanelContent.setSize (contentW, contentH);

    auto filterArea = filterPanelContent.getLocalBounds().reduced (10, 10);

    filterTitle.setBounds (filterArea.removeFromTop (18));
    filterArea.removeFromTop (6);
    favoritesOnlyToggle.setBounds (filterArea.removeFromTop (28));
    filterArea.removeFromTop (8);

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

    // --- Search bar + count label (full-width, above header) ---
    {
        auto searchRow = area.removeFromTop (kSearchHeight).reduced (4, 4);
        auto countArea = searchRow.removeFromRight (100);
        countLabel.setBounds (countArea);
        searchEditor.setBounds (searchRow);
    }

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
    // Stop any running preview when selection changes
    audioProcessor.stopPreview();
    playbackProgress  = 0.0;
    lastPlayingState  = false;
    loadingRowIndex   = -1;
    midiRollComponent.setPlayheadSec (-1.0); // hide playhead on row switch
    previewLabel.setColour (juce::Label::textColourId, Palette::textMuted);

    if (lastRow >= 0 && lastRow < filteredPatterns.size())
    {
        const auto& p = *filteredPatterns[lastRow];

        // Load MIDI roll
        {
            auto resDir  = audioProcessor.getResourcesDirectory();
            auto midiSub = resDir.getChildFile ("midi");
            auto midiF   = midiSub.isDirectory()
                           ? midiSub.getChildFile (p.filename)
                           : resDir.getChildFile (p.filename);
            loadMidiRollData (midiF);
        }

        juce::Logger::writeToLog ("[GrooveVault] selectedRowsChanged: row=" + juce::String (lastRow)
                                  + "  display_name=" + p.display_name
                                  + "  preview='" + p.preview + "'");

        if (p.preview.isEmpty())
        {
            previewLabel.setText ("Drag to DAW  |  No preview available",
                                  juce::dontSendNotification);
        }
        else
        {
            previewLabel.setText ("Drag to DAW  |  Press "
                                  + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6"))
                                  + " to preview",
                                  juce::dontSendNotification);
        }
    }
    else
    {
        juce::Logger::writeToLog ("[GrooveVault] selectedRowsChanged: no row selected");
        previewLabel.setText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90"))
                              + " Select a pattern to preview or drag to DAW",
                              juce::dontSendNotification);
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
        bool isFav        = favouriteIds.count (p->filename) > 0;
        bool isRowPlaying = audioProcessor.isPreviewPlaying()
                            && (row == patternList.getSelectedRow());
        bool isRowLoading = (row == loadingRowIndex);
        rowComp->update (p, row, isRowSelected, isFav, isRowPlaying, isRowLoading);

        // Resolve original MIDI file path
        auto resDir  = audioProcessor.getResourcesDirectory();
        auto midiSub = resDir.getChildFile ("midi");
        auto originalMidi = (midiSub.isDirectory()
                             ? midiSub.getChildFile (p->filename)
                             : resDir.getChildFile (p->filename));
        rowComp->setMidiFile (originalMidi);

        // DnD callback
        rowComp->getDragFile = [this, originalMidi]() {
            return this->getFileToDrag (originalMidi);
        };

        // Star toggle callback
        juce::String filename = p->filename;
        rowComp->onStarClicked = [this, filename]() { toggleFavourite (filename); };

        // Play/stop button callback
        rowComp->onPlayClicked = [this, row]()
        {
            if (patternList.getSelectedRow() != row)
                patternList.selectRow (row); // select first (stops current preview)
            togglePreview(); // always toggle play/stop for this row
        };
    }
    else
    {
        rowComp->update (nullptr, row, isRowSelected, false, false);
        rowComp->getDragFile   = nullptr;
        rowComp->onStarClicked = nullptr;
        rowComp->onPlayClicked = nullptr;
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

    if (audioProcessor.isPreviewLoading())
    {
        juce::Logger::writeToLog ("[GrooveVault] togglePreview: download in progress, ignoring");
        return;
    }

    juce::Logger::writeToLog ("[GrooveVault] togglePreview: startPreviewFromUrl -> " + p->preview);
    audioProcessor.startPreviewFromUrl (p->preview);

    if (audioProcessor.isPreviewLoading())
    {
        loadingRowIndex = row;
        patternList.updateContent(); // show spinner on the loading row
    }
}

void TakefujiGrooveVaultAudioProcessorEditor::timerCallback()
{
    // Detect when loading finishes (download complete or cache hit)
    if (loadingRowIndex >= 0 && !audioProcessor.isPreviewLoading())
    {
        loadingRowIndex = -1;
        patternList.updateContent(); // clear spinner
    }

    if (audioProcessor.isPreviewLoading())
        return; // still downloading — skip playback state update

    const bool playing     = audioProcessor.isPreviewPlaying();
    const bool stateChange = (playing != lastPlayingState);
    lastPlayingState = playing;

    // --- Progress bar + playhead ---
    if (playing)
    {
        playbackProgress = audioProcessor.getPlaybackPosition();
        // Use raw audio seconds so MIDI roll can align to MIDI tempo
        midiRollComponent.setPlayheadSec (audioProcessor.getPlaybackCurrentSec());
    }
    else if (stateChange)
    {
        playbackProgress = 0.0;
        midiRollComponent.setPlayheadSec (-1.0); // hide on stop
    }

    // --- Refresh row play icons on state change ---
    if (stateChange)
        patternList.updateContent();

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
                                  : "Drag to DAW",
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
void TakefujiGrooveVaultAudioProcessorEditor::loadMidiRollData (const juce::File& midiFile)
{
    auto reset = [this] {
        midiRollComponent.setPattern ({}, 480, 0.0, 4, 4);
        midiRollComponent.setMidiDurationSec (0.0);
    };

    if (!midiFile.existsAsFile()) { reset(); return; }

    // BFD3 note → DRUM_PARTS index, derived from Resources/keymaps/bfd3.json's
    // note_to_part table. Every entry in that table is grouped here by its
    // "part" field so no note is silently dropped from the roll.
    // DRUM_PARTS: 0=Kick 1=Snare 2=Hi-Hat 3=Tom1 4=Tom2 5=FloorTom 6=Ride 7=CrashL 8=CrashR 9=China
    static const std::map<int,int> noteMap {
        // 0: Kick — kick (hit, alt) + kick2 (hit, bow, edge)
        { 24, 0 }, { 84, 0 }, { 36, 0 }, { 68, 0 }, { 92, 0 },
        // 1: Snare — snare (rimshot, hit, sidestick, flam, edge, bow, pedal)
        //           + snare2 (hit, bow, edge)
        { 25, 1 }, { 26, 1 }, { 27, 1 }, { 28, 1 }, { 29, 1 }, { 86, 1 }, { 88, 1 },
        { 40, 1 }, { 73, 1 }, { 94, 1 },
        // 2: Hi-Hat — hihat, all articulations
        { 18, 2 }, { 20, 2 }, { 22, 2 }, { 30, 2 }, { 32, 2 }, { 34, 2 }, { 37, 2 },
        { 39, 2 }, { 42, 2 }, { 44, 2 }, { 46, 2 }, { 49, 2 }, { 51, 2 }, { 54, 2 },
        { 56, 2 }, { 58, 2 },
        // 3: Tom 1 — tom1 (hit, bow, edge)
        { 38, 3 }, { 70, 3 }, { 93, 3 },
        // 4: Tom 2 — tom2 (hit, edge, sidestick, alt, open)
        { 41, 4 }, { 52, 4 }, { 62, 4 }, { 72, 4 }, { 75, 4 },
        // 5: Floor Tom — tom3, tom3f, tom4, tom4f, all articulations
        { 43, 5 }, { 45, 5 }, { 47, 5 }, { 50, 5 }, { 53, 5 }, { 55, 5 }, { 57, 5 },
        { 60, 5 }, { 64, 5 }, { 65, 5 }, { 67, 5 }, { 71, 5 }, { 74, 5 }, { 76, 5 },
        { 77, 5 }, { 78, 5 }, { 80, 5 }, { 81, 5 }, { 82, 5 }, { 87, 5 },
        // 6: Ride — ride (hit, bow, edge)
        { 35, 6 }, { 66, 6 }, { 91, 6 },
        // 7: Crash L — crash1 (hit, bow, edge)
        { 31, 7 }, { 61, 7 }, { 89, 7 },
        // 8: Crash R — crash2 (hit) + crash3 (hit, bow, edge)
        { 83, 8 }, { 33, 8 }, { 63, 8 }, { 90, 8 },
        // 9: China — aux (hit) + extra, all articulations
        { 95, 9 }, { 48, 9 }, { 59, 9 }, { 69, 9 }, { 79, 9 }, { 85, 9 },
    };

    juce::FileInputStream stream (midiFile);
    if (!stream.openedOk()) { reset(); return; }

    juce::MidiFile mf;
    if (!mf.readFrom (stream)) { reset(); return; }
    // Work in ticks — do NOT call convertTimestampTicksToSeconds()

    const int timeFormat = mf.getTimeFormat();
    if (timeFormat <= 0) { reset(); return; } // SMPTE not supported

    // Read time signature and tempo from meta events
    int    numerator   = 4, denominator = 4;
    double secPerQuarterNote = 0.5; // default 120 BPM
    for (int t = 0; t < mf.getNumTracks(); t++)
    {
        const auto* track = mf.getTrack (t);
        if (!track) continue;
        for (int i = 0; i < track->getNumEvents(); i++)
        {
            const auto& msg = track->getEventPointer (i)->message;
            if (msg.isTimeSignatureMetaEvent())
                msg.getTimeSignatureInfo (numerator, denominator);
            else if (msg.isTempoMetaEvent())
                secPerQuarterNote = msg.getTempoSecondsPerQuarterNote();
        }
    }

    // Total pattern length in ticks
    double maxTick = 0.0;
    for (int t = 0; t < mf.getNumTracks(); t++)
        if (const auto* track = mf.getTrack (t))
            maxTick = juce::jmax (maxTick, track->getEndTime());
    if (maxTick <= 0.0) { reset(); return; }

    // Build notes with 0.0-1.0 relative position
    std::vector<std::vector<MidiRollComponent::NoteEvent>> notes (10);
    for (int t = 0; t < mf.getNumTracks(); t++)
    {
        const auto* track = mf.getTrack (t);
        if (!track) continue;
        for (int i = 0; i < track->getNumEvents(); i++)
        {
            const auto& msg = track->getEventPointer (i)->message;
            if (!msg.isNoteOn()) continue;
            auto it = noteMap.find (msg.getNoteNumber());
            if (it == noteMap.end()) continue;
            int    partIdx = it->second;
            double relPos  = msg.getTimeStamp() / maxTick;
            notes[(size_t)partIdx].push_back ({ relPos });
        }
    }

    midiRollComponent.setPattern (notes, timeFormat, maxTick, numerator, denominator);

    // MIDI duration in seconds = (totalTicks / ppq) × secondsPerQuarterNote
    const double midiDurationSec = (maxTick / (double)timeFormat) * secPerQuarterNote;
    midiRollComponent.setMidiDurationSec (midiDurationSec);
}

void TakefujiGrooveVaultAudioProcessorEditor::saveFavourites()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("TakefujiGrooveVault");
    dir.createDirectory();
    auto file = dir.getChildFile ("favorites.json");

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& id : favouriteIds) arr.add (id);
    obj->setProperty ("favorites", arr);

    file.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

std::set<juce::String> TakefujiGrooveVaultAudioProcessorEditor::loadFavourites()
{
    auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("TakefujiGrooveVault")
                    .getChildFile ("favorites.json");

    std::set<juce::String> result;
    if (!file.existsAsFile()) return result;

    auto json = juce::JSON::parse (file.loadFileAsString());
    if (auto* arr = json["favorites"].getArray())
        for (const auto& v : *arr) result.insert (v.toString());

    return result;
}

void TakefujiGrooveVaultAudioProcessorEditor::toggleFavourite (const juce::String& filename)
{
    if (favouriteIds.count (filename))
        favouriteIds.erase (filename);
    else
        favouriteIds.insert (filename);

    saveFavourites();

    if (favoritesOnly)
        applyFilters(); // re-filter (item may disappear from list)
    else
        patternList.updateContent(); // forces refreshComponentForRow() to update star icons
}

void TakefujiGrooveVaultAudioProcessorEditor::comboBoxChanged (juce::ComboBox* cb)
{
    if (cb == &targetMapCombo) return; // keymap change doesn't need filter update
    applyFilters();
}

void TakefujiGrooveVaultAudioProcessorEditor::applyFilters()
{
    // Remember the currently-selected pattern (by filename) so we can keep it
    // selected after the list is rebuilt, instead of silently jumping to row 0.
    juce::String previouslySelectedFilename;
    {
        const int prevRow = patternList.getSelectedRow();
        if (prevRow >= 0 && prevRow < filteredPatterns.size())
            previouslySelectedFilename = filteredPatterns[prevRow]->filename;
    }

    filteredPatterns.clear();

    // source filter: map selected combo ID back to a source_id string
    juce::String sourceSel;
    {
        const int selId = sourceFilter.getSelectedId();
        const int idx   = selId - 2; // ID 1 = "All", ID 2+ = real entries
        if (idx >= 0 && idx < sourceFilterIds.size())
            sourceSel = sourceFilterIds[idx];
    }

    const auto searchText  = searchEditor.getText().toLowerCase().trim();
    const auto meterSel    = meterFilter.getText();
    const auto categorySel = categoryFilter.getText();
    const auto vibeSel     = vibeFilter.getText();
    const auto densitySel  = densityFilter.getText();
    const int  bpmMin      = bpmMinEditor.getText().getIntValue();
    int        bpmMax      = bpmMaxEditor.getText().getIntValue();
    if (bpmMax == 0) bpmMax = 9999;

    for (const auto& p : audioProcessor.getPatterns())
    {
        if (favoritesOnly && !favouriteIds.count (p.filename)) continue;
        if (searchText.isNotEmpty() && !p.display_name.toLowerCase().contains (searchText)) continue;
        if (sourceSel.isNotEmpty() && p.source_id != sourceSel) continue;
        if (meterSel != "All")
        {
            auto meterParts = juce::StringArray::fromTokens (p.meter, ",", "");
            meterParts.trim();
            if (meterSel == "Odd Meter")
            {
                // Hit if any part is NOT 4/4
                bool hasOdd = false;
                for (const auto& m : meterParts)
                    if (m != "4/4") { hasOdd = true; break; }
                if (!hasOdd) continue;
            }
            else
            {
                // Hit if the selected meter is one of the comma-separated parts
                if (!meterParts.contains (meterSel)) continue;
            }
        }
        if (categorySel != "All" && p.category != categorySel) continue;
        if (vibeSel     != "All" && !p.vibe.contains (vibeSel)) continue;
        if (densitySel  != "All" && p.density  != densitySel)  continue;
        if (p.bpm < bpmMin || p.bpm > bpmMax)                  continue;

        filteredPatterns.add (&p);
    }

    patternList.updateContent();
    patternList.repaint();
    countLabel.setText (juce::String (filteredPatterns.size()) + " patterns",
                        juce::dontSendNotification);

    juce::Logger::writeToLog ("[GrooveVault] applyFilters: " + juce::String (filteredPatterns.size())
                              + " results, currentRow=" + juce::String (patternList.getSelectedRow()));

    if (filteredPatterns.isEmpty())
    {
        // No results -- clear preview bar and disable PLAY
        selectedRowsChanged (-1);
        return;
    }

    // Try to keep the same pattern selected in the rebuilt list.
    int newRow = -1;
    if (previouslySelectedFilename.isNotEmpty())
    {
        for (int i = 0; i < filteredPatterns.size(); ++i)
        {
            if (filteredPatterns[i]->filename == previouslySelectedFilename)
            {
                newRow = i;
                break;
            }
        }
    }

    if (newRow >= 0)
    {
        // Still present -- restore selection silently, without touching the
        // MIDI roll, transport bar, or playback state.
        juce::SparseSet<int> sel;
        sel.addRange (juce::Range<int> (newRow, newRow + 1));
        patternList.setSelectedRows (sel, juce::dontSendNotification);
        patternList.scrollToEnsureRowIsOnscreen (newRow);
        patternList.repaint();
    }
    else
    {
        // Previously-selected pattern was filtered out (or nothing was
        // selected) -- auto-select first row so PLAY is immediately usable.
        // selectRow() internally calls selectedRowsChanged() on the model.
        patternList.selectRow (0, true /*don't scroll*/, true /*deselect others*/);
    }
}

void TakefujiGrooveVaultAudioProcessorEditor::populateFilterOptions()
{
    juce::StringArray sourceIds, meters, categories, vibes, densities;

    for (const auto& p : audioProcessor.getPatterns())
    {
        if (p.source_id.isNotEmpty() && !sourceIds.contains (p.source_id))
            sourceIds.add (p.source_id);
        // Split comma-separated meters (e.g. "7/4,6/4") into individual entries
        for (const auto& m : juce::StringArray::fromTokens (p.meter, ",", ""))
        {
            auto trimmed = m.trim();
            if (trimmed.isNotEmpty() && !meters.contains (trimmed))
                meters.add (trimmed);
        }
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

    // Meter filter: sorted individual meters + "Odd Meter" fixed at end
    meterFilter.clear (juce::dontSendNotification);
    meterFilter.addItem ("All", 1);
    for (int i = 0; i < meters.size(); ++i)
        meterFilter.addItem (meters[i], i + 2);
    meterFilter.addItem ("Odd Meter", meters.size() + 2);
    meterFilter.setSelectedId (1, juce::dontSendNotification);

    populate (categoryFilter, categories);
    populate (vibeFilter,     vibes);
    populate (densityFilter,  densities);
}
