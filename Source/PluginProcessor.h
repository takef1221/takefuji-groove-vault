/*
  ==============================================================================
    Takefuji Groove Vault -- MIDI Pattern Browser
    PluginProcessor.h
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

struct PatternInfo
{
    juce::String      display_name;  // from metadata; auto-generated if absent
    juce::String      filename;      // .mid file in Resources/midi/
    juce::String      preview;       // preview audio file in Resources/preview/
    juce::String      meter;
    juce::String      category;
    juce::StringArray vibe;          // JSON array of vibe tags
    juce::String      density;
    int               bpm = 120;
    juce::String      source_id;
    juce::String      source_title;
};

//==============================================================================
// Drum instrument keymap entry.
//
// JSON format (note_to_part + part_to_note):
//   note_to_part: { "24": { "part":"kick", "artic":"hit" }, ... }
//   part_to_note: { "kick_hit": 36, ... }
//
// Conversion direction:
//   source note  ->  note_to_part  ->  {part, artic}
//   {part, artic} -> part_to_note  ->  dest note
//   (falls back to part_hit if exact artic is absent in dest map)
//==============================================================================
struct KeyMapEntry
{
    juce::String name;
    bool builtIn = false;

    // note_to_part: source note -> part / artic
    juce::Array<int>  srcNotes;
    juce::StringArray srcParts;
    juce::StringArray srcArtics;

    // part_to_note: "part_artic" -> dest note
    juce::StringArray dstKeys;
    juce::Array<int>  dstNotes;

    // Reverse-look up part+artic from a MIDI note number.
    bool getInfoForNote (int note,
                         juce::String& outPart,
                         juce::String& outArtic) const;

    // Forward-look up dest note from part+artic.
    // Falls back to part_hit if the exact articulation has no mapping.
    int getNoteForPartArtic (const juce::String& part,
                              const juce::String& artic) const;

    static KeyMapEntry fromJSON (const juce::var& json);
    juce::var           toJSON()  const;

    bool isEmpty() const { return srcNotes.isEmpty() && dstKeys.isEmpty(); }
};

//==============================================================================
class TakefujiGrooveVaultAudioProcessor : public juce::AudioProcessor
{
public:
    TakefujiGrooveVaultAudioProcessor();
    ~TakefujiGrooveVaultAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------- Pattern library ----------
    const juce::Array<PatternInfo>& getPatterns() const noexcept { return patterns; }
    juce::File getResourcesDirectory() const;
    void loadPatterns();

    // ---------- Keymap ----------
    void loadKeymaps();
    const juce::Array<KeyMapEntry>& getKeymaps() const noexcept { return keymaps; }

    // Save a custom keymap to %APPDATA%\TakefujiGrooveVault\keymaps\<name>.json
    bool saveCustomKeymap (const KeyMapEntry& map, const juce::String& name);
    // Load all custom keymaps from APPDATA and append to keymaps array
    void loadCustomKeymaps();
    // APPDATA directory for custom keymaps
    static juce::File getCustomKeymapsDir();

    // Create a converted MIDI file in the system temp directory.
    // Returns an empty File on failure.
    juce::File createConvertedMidiFile (const juce::File& sourceMidi,
                                        const KeyMapEntry& srcMap,
                                        const KeyMapEntry& dstMap);

    // ---------- Preview audio (local) ----------
    void startPreview (const juce::File& audioFile);
    void stopPreview();
    bool   isPreviewPlaying()    const;
    double getPlaybackPosition() const;

    // ---------- Preview audio (URL download with temp-file cache) ----------
    static constexpr const char* kPreviewBaseUrl =
        "https://github.com/takef1221/takefuji-groove-vault/releases/download/preview-assets/";

    using PreviewReadyCallback = std::function<void(bool offline)>;
    void setPreviewReadyCallback (PreviewReadyCallback cb) { previewReadyCallback = std::move (cb); }
    void startPreviewFromUrl (const juce::String& filename);
    bool isPreviewLoading()  const noexcept { return previewLoading.load(); }

private:
    juce::Array<PatternInfo> patterns;
    juce::Array<KeyMapEntry> keymaps;

    // Debug file logger
    std::unique_ptr<juce::FileLogger> fileLogger;

    // URL in-memory streaming
    PreviewReadyCallback            previewReadyCallback;
    std::atomic<bool>               previewLoading { false };
    juce::MemoryBlock               previewMemoryBlock;
    std::shared_ptr<std::atomic<bool>> processorAlive;
    void startPreviewInMemory (const juce::MemoryBlock& block);

    // Preview audio engine (lazy-initialized on first PLAY click)
    bool ensurePreviewDevice();
    juce::AudioFormatManager                    formatManager;
    juce::AudioDeviceManager                    previewDeviceManager;
    juce::AudioSourcePlayer                     audioSourcePlayer;
    juce::AudioTransportSource                  transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    bool previewDeviceOk    = false;
    bool previewDeviceTried = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakefujiGrooveVaultAudioProcessor)
};
