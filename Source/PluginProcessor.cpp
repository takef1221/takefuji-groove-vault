/*
  ==============================================================================
    Takefuji Groove Vault -- MIDI Pattern Browser
    PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TakefujiGrooveVaultAudioProcessor::TakefujiGrooveVaultAudioProcessor()
    : AudioProcessor (BusesProperties())
{
    // Write all Logger output to a file so it is visible when running inside a DAW.
    // Log is at: %TEMP%\TakefujiGrooveVault_debug.log
    auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("TakefujiGrooveVault_debug.log");
    fileLogger = std::make_unique<juce::FileLogger> (logFile, "=== TakefujiGrooveVault ===", 0);
    juce::Logger::setCurrentLogger (fileLogger.get());

    // Register audio formats for preview (WAV, AIFF, FLAC, OGG).
    // AudioDeviceManager is intentionally NOT initialized here -- we defer it
    // to ensurePreviewDevice() called on first PLAY click.  This avoids
    // competing with Studio One for the audio device during plugin load.
    formatManager.registerBasicFormats();
    processorAlive = std::make_shared<std::atomic<bool>> (true);

    loadPatterns();
    loadKeymaps();
}

TakefujiGrooveVaultAudioProcessor::~TakefujiGrooveVaultAudioProcessor()
{
    *processorAlive = false; // signal any in-flight background thread

    // Tear down audio engine before deleting reader source
    transportSource.stop();
    audioSourcePlayer.setSource (nullptr);
    if (previewDeviceOk)
        previewDeviceManager.removeAudioCallback (&audioSourcePlayer);
    transportSource.setSource (nullptr); // waits for audio thread
    readerSource.reset();

    juce::Logger::setCurrentLogger (nullptr);
}

//==============================================================================
juce::File TakefujiGrooveVaultAudioProcessor::getResourcesDirectory() const
{
    static juce::File cachedResult;
    static bool       resolved = false;

    if (resolved)
        return cachedResult;
    resolved = true;

    auto logCand = [] (const juce::File& f) {
        juce::Logger::writeToLog ("[GrooveVault] resources candidate = " + f.getFullPathName()
                                  + (f.isDirectory() ? "  [DIR EXISTS]" : "  [not found]"));
    };

    juce::File exeFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::File appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::Logger::writeToLog ("[GrooveVault] exe path = " + exeFile.getFullPathName());
    juce::Logger::writeToLog ("[GrooveVault] app path = " + appFile.getFullPathName());

    // Mac: .app バンドル内 Contents/MacOS/ の隣 Contents/Resources/
    juce::File mac = exeFile.getParentDirectory().getSiblingFile ("Resources");
    logCand (mac);
    if (mac.getChildFile ("metadata.json").existsAsFile())
    {
        juce::Logger::writeToLog ("[GrooveVault] resources found = " + mac.getFullPathName() + "  [Mac-bundle]");
        cachedResult = mac; return cachedResult;
    }

    // A0: Standalone -- Resources/ sits next to the exe
    juce::File a0 = exeFile.getParentDirectory().getChildFile ("Resources");
    logCand (a0);
    if (a0.isDirectory())
    {
        juce::Logger::writeToLog ("[GrooveVault] resources found = " + a0.getFullPathName() + "  [A0-standalone]");
        cachedResult = a0; return cachedResult;
    }

    // A: VST3 bundle (Contents/x86_64-win/ -> Contents/Resources/)
    juce::File a = exeFile.getParentDirectory().getParentDirectory().getChildFile ("Resources");
    logCand (a);
    if (a.isDirectory())
    {
        juce::Logger::writeToLog ("[GrooveVault] resources found = " + a.getFullPathName() + "  [A-vst3]");
        cachedResult = a; return cachedResult;
    }

    // A2: appFile path (VST3 host exe -> plugin bundle)
    juce::File a2 = appFile.getParentDirectory().getParentDirectory().getChildFile ("Resources");
    logCand (a2);
    if (a2.isDirectory())
    {
        juce::Logger::writeToLog ("[GrooveVault] resources found = " + a2.getFullPathName() + "  [A2]");
        cachedResult = a2; return cachedResult;
    }

    // B: dev build -- walk up looking for Resources/ that contains metadata.json
    juce::File dir = exeFile.getParentDirectory();
    for (int i = 0; i < 12; ++i)
    {
        juce::File cand = dir.getChildFile ("Resources");
        bool hasMeta = cand.isDirectory() && cand.getChildFile ("metadata.json").existsAsFile();
        juce::Logger::writeToLog ("[GrooveVault] resources candidate = " + cand.getFullPathName()
                                  + (hasMeta ? "  [DIR+meta]" : (cand.isDirectory() ? "  [DIR no meta]" : "  [not found]")));
        if (hasMeta)
        {
            juce::Logger::writeToLog ("[GrooveVault] resources found = " + cand.getFullPathName() + "  [B-dev]");
            cachedResult = cand; return cachedResult;
        }
        juce::File up = dir.getParentDirectory();
        if (up == dir) break;
        dir = up;
    }

    cachedResult = exeFile.getParentDirectory().getChildFile ("Resources");
    juce::Logger::writeToLog ("[GrooveVault] resources found = " + cachedResult.getFullPathName() + "  [fallback]");
    return cachedResult;
}

void TakefujiGrooveVaultAudioProcessor::loadPatterns()
{
    patterns.clear();

    auto resourcesDir = getResourcesDirectory();
    auto metadataFile = resourcesDir.getChildFile ("metadata.json");

    juce::Logger::writeToLog ("[GrooveVault] loadPatterns: metadata = "
                              + metadataFile.getFullPathName()
                              + (metadataFile.existsAsFile() ? "  [exists]" : "  [NOT FOUND]"));

    if (!metadataFile.existsAsFile())
        return;

    // juce::JSON::parse(File) returns var::undefined on failure.
    // The two-argument overload is parse(String, var&) -> Result; we use the File overload.
    auto parsed = juce::JSON::parse (metadataFile);
    if (parsed.isVoid() || parsed.isUndefined())
    {
        juce::Logger::writeToLog ("[GrooveVault] JSON parse failed (empty or malformed)");
        return;
    }

    auto* arr = parsed.getArray();
    if (arr == nullptr)
    {
        juce::Logger::writeToLog ("[GrooveVault] JSON root is not an array");
        return;
    }

    for (const auto& item : *arr)
    {
        PatternInfo p;
        p.filename     = item.getProperty ("filename",     "").toString();
        p.preview      = item.getProperty ("preview",      "").toString();
        p.meter        = item.getProperty ("meter",        "4/4").toString();
        p.category     = item.getProperty ("category",     "").toString();
        p.density      = item.getProperty ("density",      "").toString();
        p.bpm          = static_cast<int> (item.getProperty ("bpm", 120));
        p.source_id    = item.getProperty ("source_id",    "").toString();
        p.source_title = item.getProperty ("source_title", "").toString();

        // display_name: prefer explicit field, else auto-generate from source_title + category
        p.display_name = item.getProperty ("display_name", "").toString();
        if (p.display_name.isEmpty())
        {
            if (p.source_title.isNotEmpty() && p.category.isNotEmpty())
                p.display_name = p.source_title + " - " + p.category;
            else if (p.source_title.isNotEmpty())
                p.display_name = p.source_title;
            else
                p.display_name = p.filename.upToLastOccurrenceOf (".", false, false);
        }

        // vibe: JSON array or legacy string
        auto vibeVar = item.getProperty ("vibe", juce::var());
        if (auto* vibeArr = vibeVar.getArray())
        {
            for (const auto& v : *vibeArr)
                p.vibe.add (v.toString());
        }
        else if (!vibeVar.isVoid() && !vibeVar.isUndefined())
        {
            auto s = vibeVar.toString();
            if (s.isNotEmpty())
                p.vibe.add (s);
        }

        if (p.filename.isNotEmpty())
        {
            juce::Logger::writeToLog ("[GrooveVault] pattern: display_name='" + p.display_name
                                      + "'  preview='" + p.preview + "'"
                                      + "  vibes=" + juce::String (p.vibe.size())
                                      + (p.preview.isEmpty() ? "  [WARNING: preview empty]" : ""));
            patterns.add (p);
        }
    }

    juce::Logger::writeToLog ("[GrooveVault] loadPatterns: loaded "
                              + juce::String (patterns.size()) + " patterns");
}

//==============================================================================
// KeyMapEntry implementation
//==============================================================================
bool KeyMapEntry::getInfoForNote (int note,
                                   juce::String& outPart,
                                   juce::String& outArtic) const
{
    int idx = srcNotes.indexOf (note);
    if (idx < 0) return false;
    outPart  = srcParts[idx];
    outArtic = srcArtics[idx];
    return true;
}

int KeyMapEntry::getNoteForPartArtic (const juce::String& part,
                                       const juce::String& artic) const
{
    // 1. Exact match: "part_artic"
    int idx = dstKeys.indexOf (part + "_" + artic);
    if (idx >= 0) return dstNotes[idx];

    // 2. Fallback: "part_hit"
    idx = dstKeys.indexOf (part + "_hit");
    if (idx >= 0) return dstNotes[idx];

    return -1; // not mappable
}

KeyMapEntry KeyMapEntry::fromJSON (const juce::var& json)
{
    KeyMapEntry entry;
    entry.name = json["name"].toString();

    // --- note_to_part section (primary format) ---
    if (auto* ntp = json["note_to_part"].getDynamicObject())
    {
        for (const auto& prop : ntp->getProperties())
        {
            entry.srcNotes.add (prop.name.toString().getIntValue());
            entry.srcParts.add  (prop.value.getProperty ("part",  "").toString());
            entry.srcArtics.add (prop.value.getProperty ("artic", "hit").toString());
        }
    }

    // --- part_to_note section (primary format) ---
    if (auto* ptn = json["part_to_note"].getDynamicObject())
    {
        for (const auto& prop : ptn->getProperties())
        {
            entry.dstKeys.add  (prop.name.toString());
            entry.dstNotes.add (static_cast<int> (prop.value));
        }
    }

    // --- Legacy flat "map" format (backward compat) ---
    if (entry.isEmpty())
    {
        if (auto* mapObj = json["map"].getDynamicObject())
        {
            for (const auto& prop : mapObj->getProperties())
            {
                juce::String key  = prop.name.toString();
                int          note = static_cast<int> (prop.value);
                entry.dstKeys.add (key);
                entry.dstNotes.add (note);
                entry.srcNotes.add (note);
                auto parts = juce::StringArray::fromTokens (key, "_", "");
                entry.srcParts.add  (parts.size() > 0 ? parts[0] : key);
                entry.srcArtics.add (parts.size() > 1 ? parts[1] : "hit");
            }
        }
    }

    return entry;
}

juce::var KeyMapEntry::toJSON() const
{
    juce::DynamicObject::Ptr ntpObj = new juce::DynamicObject();
    for (int i = 0; i < srcNotes.size(); ++i)
    {
        juce::DynamicObject::Ptr info = new juce::DynamicObject();
        info->setProperty ("part",  srcParts[i]);
        info->setProperty ("artic", srcArtics[i]);
        ntpObj->setProperty (juce::String (srcNotes[i]), juce::var (info.get()));
    }

    juce::DynamicObject::Ptr ptnObj = new juce::DynamicObject();
    for (int i = 0; i < dstKeys.size(); ++i)
        ptnObj->setProperty (dstKeys[i], dstNotes[i]);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("name",         name);
    root->setProperty ("note_to_part", juce::var (ntpObj.get()));
    root->setProperty ("part_to_note", juce::var (ptnObj.get()));
    return juce::var (root.get());
}

//==============================================================================
// Keymap loading
//==============================================================================
void TakefujiGrooveVaultAudioProcessor::loadKeymaps()
{
    keymaps.clear();

    auto keymapsDir = getResourcesDirectory().getChildFile ("keymaps");
    juce::Logger::writeToLog ("[GrooveVault] loadKeymaps: dir = " + keymapsDir.getFullPathName()
                              + (keymapsDir.isDirectory() ? "  [OK]" : "  [NOT FOUND]"));

    if (keymapsDir.isDirectory())
    {
        auto files = keymapsDir.findChildFiles (juce::File::findFiles, false, "*.json");
        juce::Logger::writeToLog ("[GrooveVault] loadKeymaps: found "
                                  + juce::String (files.size()) + " JSON file(s)");

        for (auto& f : files)
        {
            juce::Logger::writeToLog ("[GrooveVault]   file: " + f.getFileName()
                                      + "  size=" + juce::String (f.getSize()) + " bytes");

            auto jsonText = f.loadFileAsString();
            if (jsonText.isEmpty())
            {
                juce::Logger::writeToLog ("[GrooveVault]   -> EMPTY FILE, skipping");
                continue;
            }

            // Log first 80 chars so we can see if the JSON structure looks sane
            juce::Logger::writeToLog ("[GrooveVault]   preview: "
                                      + jsonText.substring (0, 80).replaceCharacters ("\r\n", "  "));

            auto parsed = juce::JSON::parse (jsonText);
            if (parsed.isVoid() || parsed.isUndefined())
            {
                juce::Logger::writeToLog ("[GrooveVault]   -> JSON PARSE FAILED");
                continue;
            }

            auto nameField = parsed["name"].toString();
            juce::Logger::writeToLog ("[GrooveVault]   name field: '" + nameField + "'"
                                      + (nameField.isEmpty() ? "  [WARNING: empty name!]" : ""));

            auto entry = KeyMapEntry::fromJSON (parsed);
            entry.builtIn = true;

            // JUCE ComboBox::addItem silently skips items with empty text.
            // Use the filename (without extension) as a fallback so the map
            // still appears in the TARGET combo even if "name" was missing.
            if (entry.name.isEmpty())
            {
                entry.name = f.getFileNameWithoutExtension().toUpperCase();
                juce::Logger::writeToLog ("[GrooveVault]   name was empty -- using filename: " + entry.name);
            }

            juce::Logger::writeToLog ("[GrooveVault]   srcNotes=" + juce::String (entry.srcNotes.size())
                                      + "  dstKeys=" + juce::String (entry.dstKeys.size())
                                      + "  name='" + entry.name + "'"
                                      + "  isEmpty=" + (entry.isEmpty() ? "YES" : "NO"));

            if (!entry.isEmpty())
            {
                keymaps.add (entry);
                juce::Logger::writeToLog ("[GrooveVault]   -> added to combo: '" + entry.name + "'");
            }
            else
            {
                juce::Logger::writeToLog ("[GrooveVault]   -> SKIPPED (isEmpty=true)");
            }
        }
    }

    // Also load custom keymaps from APPDATA
    loadCustomKeymaps();
    juce::Logger::writeToLog ("[GrooveVault] loadKeymaps: total " + juce::String (keymaps.size()) + " maps");
}

void TakefujiGrooveVaultAudioProcessor::loadCustomKeymaps()
{
    auto dir = getCustomKeymapsDir();
    if (!dir.isDirectory()) return;

    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto parsed = juce::JSON::parse (f);
        if (!parsed.isVoid() && !parsed.isUndefined())
        {
            auto entry = KeyMapEntry::fromJSON (parsed);
            entry.builtIn = false;
            if (!entry.isEmpty())
                keymaps.add (entry);
        }
    }
}

bool TakefujiGrooveVaultAudioProcessor::saveCustomKeymap (const KeyMapEntry& map,
                                                           const juce::String& name)
{
    auto dir = getCustomKeymapsDir();
    if (!dir.createDirectory()) return false;

    auto file = dir.getChildFile (name + ".json");
    auto json = map.toJSON();
    auto text = juce::JSON::toString (json, true);

    juce::FileOutputStream stream (file);
    if (!stream.openedOk()) return false;
    stream.writeText (text, false, false, nullptr);
    return true;
}

juce::File TakefujiGrooveVaultAudioProcessor::getCustomKeymapsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("TakefujiGrooveVault")
                       .getChildFile ("keymaps");
}

//==============================================================================
// MIDI note conversion
//==============================================================================
juce::File TakefujiGrooveVaultAudioProcessor::createConvertedMidiFile (
    const juce::File& sourceMidi,
    const KeyMapEntry& srcMap,
    const KeyMapEntry& dstMap)
{
    juce::Logger::writeToLog ("[GrooveVault] createConvertedMidi: " + sourceMidi.getFileName()
                              + "  " + srcMap.name + " -> " + dstMap.name);

    juce::FileInputStream inputStream (sourceMidi);
    if (!inputStream.openedOk()) return {};

    juce::MidiFile midiFile;
    if (!midiFile.readFrom (inputStream)) return {};

    juce::MidiFile converted;
    converted.setTicksPerQuarterNote (midiFile.getTimeFormat());

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        const auto* srcSeq = midiFile.getTrack (t);
        juce::MidiMessageSequence newSeq;

        for (int i = 0; i < srcSeq->getNumEvents(); ++i)
        {
            auto msg = srcSeq->getEventPointer (i)->message;

            if (msg.isNoteOn() || msg.isNoteOff())
            {
                int          srcNote = msg.getNoteNumber();
                juce::String part, artic;

                if (srcMap.getInfoForNote (srcNote, part, artic))
                {
                    int destNote = dstMap.getNoteForPartArtic (part, artic);
                    if (destNote >= 0 && destNote != srcNote)
                    {
                        double ts = msg.getTimeStamp();
                        if (msg.isNoteOn())
                            msg = juce::MidiMessage::noteOn  (msg.getChannel(), destNote, msg.getVelocity());
                        else
                            msg = juce::MidiMessage::noteOff (msg.getChannel(), destNote, msg.getVelocity());
                        msg.setTimeStamp (ts);
                    }
                }
                // Notes not in the source map pass through unchanged
            }

            newSeq.addEvent (msg);
        }

        newSeq.updateMatchedPairs();
        converted.addTrack (newSeq);
    }

    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("TGV_conv_" + juce::String (juce::Time::currentTimeMillis()) + ".mid");

    juce::FileOutputStream outputStream (tempFile);
    if (!outputStream.openedOk()) return {};
    converted.writeTo (outputStream);

    juce::Logger::writeToLog ("[GrooveVault] converted MIDI written: " + tempFile.getFullPathName());
    return tempFile;
}

//==============================================================================
const juce::String TakefujiGrooveVaultAudioProcessor::getName() const { return JucePlugin_Name; }

bool TakefujiGrooveVaultAudioProcessor::acceptsMidi()  const { return true; }
bool TakefujiGrooveVaultAudioProcessor::producesMidi() const { return true; }
bool TakefujiGrooveVaultAudioProcessor::isMidiEffect() const { return true; }
double TakefujiGrooveVaultAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int          TakefujiGrooveVaultAudioProcessor::getNumPrograms()                         { return 1; }
int          TakefujiGrooveVaultAudioProcessor::getCurrentProgram()                      { return 0; }
void         TakefujiGrooveVaultAudioProcessor::setCurrentProgram (int)                  {}
const juce::String TakefujiGrooveVaultAudioProcessor::getProgramName (int)              { return {}; }
void         TakefujiGrooveVaultAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
// Preview audio
//==============================================================================
bool TakefujiGrooveVaultAudioProcessor::ensurePreviewDevice()
{
    if (previewDeviceOk)    return true;
    if (previewDeviceTried) return false; // already failed -- don't retry
    previewDeviceTried = true;

    juce::Logger::writeToLog ("[GrooveVault] ensurePreviewDevice: initialising AudioDeviceManager ...");

    auto err = previewDeviceManager.initialise (0,      // no inputs
                                                2,      // stereo output
                                                nullptr,
                                                true);  // select default on failure
    if (err.isNotEmpty())
    {
        juce::Logger::writeToLog ("[GrooveVault] ensurePreviewDevice FAILED: " + err);
        return false;
    }

    previewDeviceManager.addAudioCallback (&audioSourcePlayer);
    audioSourcePlayer.setSource (&transportSource);
    previewDeviceOk = true;

    auto* dev = previewDeviceManager.getCurrentAudioDevice();
    juce::Logger::writeToLog ("[GrooveVault] ensurePreviewDevice OK -- device: "
                              + (dev ? dev->getName() : "none")
                              + "  sampleRate="
                              + (dev ? juce::String (dev->getCurrentSampleRate()) : "?")
                              + "  outCh="
                              + (dev ? juce::String (dev->getActiveOutputChannels().countNumberOfSetBits()) : "?"));
    return true;
}

void TakefujiGrooveVaultAudioProcessor::startPreview (const juce::File& audioFile)
{
    juce::Logger::writeToLog ("[GrooveVault] startPreview: path = " + audioFile.getFullPathName());
    juce::Logger::writeToLog ("[GrooveVault] startPreview: exists = "
                              + juce::String (audioFile.existsAsFile() ? "YES" : "NO"));

    // Always stop before swapping sources to avoid audio-thread races.
    transportSource.stop();
    transportSource.setSource (nullptr); // waits for the audio thread to clear
    readerSource.reset();

    if (!ensurePreviewDevice())
    {
        juce::Logger::writeToLog ("[GrooveVault] startPreview: aborted -- no audio device");
        return;
    }

    if (!audioFile.existsAsFile())
    {
        juce::Logger::writeToLog ("[GrooveVault] startPreview: file not found");
        return;
    }

    auto* reader = formatManager.createReaderFor (audioFile);
    if (reader == nullptr)
    {
        juce::Logger::writeToLog ("[GrooveVault] startPreview: unsupported format or read error");
        return;
    }

    juce::Logger::writeToLog ("[GrooveVault] startPreview: sampleRate="
                              + juce::String (reader->sampleRate)
                              + "  lengthInSamples="
                              + juce::String (reader->lengthInSamples));

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transportSource.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    transportSource.start();
    juce::Logger::writeToLog ("[GrooveVault] startPreview: playback started");
}

void TakefujiGrooveVaultAudioProcessor::stopPreview()
{
    juce::Logger::writeToLog ("[GrooveVault] stopPreview called");
    transportSource.stop();
}

bool TakefujiGrooveVaultAudioProcessor::isPreviewPlaying() const
{
    return transportSource.isPlaying();
}

double TakefujiGrooveVaultAudioProcessor::getPlaybackPosition() const
{
    const double length = transportSource.getLengthInSeconds();
    if (length <= 0.0) return 0.0;
    return juce::jlimit (0.0, 1.0, transportSource.getCurrentPosition() / length);
}

void TakefujiGrooveVaultAudioProcessor::startPreviewFromUrl (const juce::String& filename)
{
    if (filename.isEmpty()) return;

    juce::Logger::writeToLog ("[GrooveVault] startPreviewFromUrl: " + filename);

    // Initialise the audio device now so it is ready when audio arrives.
    // On first call this takes a moment; subsequent calls return immediately.
    ensurePreviewDevice();

    // Cache hit: same file already in memory — replay without downloading.
    if (filename == cachedPreviewFilename && previewMemoryBlock.getSize() > 0)
    {
        juce::Logger::writeToLog ("[GrooveVault] startPreviewFromUrl: cache hit, replaying from memory");
        startPreviewInMemory (previewMemoryBlock);
        if (previewReadyCallback) previewReadyCallback (false);
        return;
    }

    previewLoading = true;

    juce::String urlStr  = juce::String (kPreviewBaseUrl) + filename;
    auto         alive   = processorAlive; // shared_ptr keeps flag valid after processor destroyed

    juce::Thread::launch ([this, urlStr, filename, alive]()
    {
        juce::Logger::writeToLog ("[GrooveVault] streamThread: GET " + urlStr);

        juce::MemoryBlock block;

        auto stream = juce::URL (urlStr).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (10000));
        bool ok = (stream != nullptr);
        if (ok) { juce::MemoryOutputStream mos (block, false); mos.writeFromInputStream (*stream, -1); }

        if (!ok || block.isEmpty())
        {
            juce::Logger::writeToLog ("[GrooveVault] streamThread: offline or download failed");
            previewLoading = false;
            juce::MessageManager::callAsync ([this, alive]()
            {
                if (!alive->load()) return;
                if (previewReadyCallback) previewReadyCallback (true);
            });
            return;
        }

        juce::Logger::writeToLog ("[GrooveVault] streamThread: loaded "
                                  + juce::String (block.getSize()) + " bytes");
        previewLoading = false;

        juce::MessageManager::callAsync ([this, alive, filename, blk = std::move (block)]() mutable
        {
            if (!alive->load()) return;
            cachedPreviewFilename = filename;
            startPreviewInMemory (blk);
            if (previewReadyCallback) previewReadyCallback (false);
        });
    });
}

void TakefujiGrooveVaultAudioProcessor::startPreviewInMemory (const juce::MemoryBlock& block)
{
    // Stop and clear current playback before swapping buffer
    transportSource.stop();
    transportSource.setSource (nullptr);
    readerSource.reset();

    if (!ensurePreviewDevice()) return;

    // Store block as member so the MemoryInputStream pointer stays valid
    previewMemoryBlock = block;

    auto stream = std::make_unique<juce::MemoryInputStream> (previewMemoryBlock, false);
    auto* reader = formatManager.createReaderFor (std::move (stream));

    if (reader == nullptr)
    {
        juce::Logger::writeToLog ("[GrooveVault] startPreviewInMemory: unsupported format");
        return;
    }

    juce::Logger::writeToLog ("[GrooveVault] startPreviewInMemory: sampleRate="
                              + juce::String (reader->sampleRate)
                              + "  lengthInSamples="
                              + juce::String (reader->lengthInSamples));

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transportSource.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    transportSource.start();
    juce::Logger::writeToLog ("[GrooveVault] startPreviewInMemory: playback started");
}

//==============================================================================
void TakefujiGrooveVaultAudioProcessor::prepareToPlay (double, int) {}
void TakefujiGrooveVaultAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TakefujiGrooveVaultAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const
{
    return true; // MIDI effect: no audio buses required
}
#endif

void TakefujiGrooveVaultAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer&)
{
    buffer.clear();
}

//==============================================================================
bool TakefujiGrooveVaultAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* TakefujiGrooveVaultAudioProcessor::createEditor()
{
    return new TakefujiGrooveVaultAudioProcessorEditor (*this);
}

//==============================================================================
void TakefujiGrooveVaultAudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void TakefujiGrooveVaultAudioProcessor::setStateInformation (const void*, int)   {}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TakefujiGrooveVaultAudioProcessor();
}
