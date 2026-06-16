/*
  ==============================================================================
    Takefuji Groove Vault -- Custom Standalone Application
    StandaloneApp.cpp

    Startup sequence:
      1. License check  (Gumroad API, async dialog)
      2. Main window
      3. Background update check  (GitHub API)
  ==============================================================================
*/

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>


namespace
{
    constexpr const char* kAppTitle      = "Takefuji Groove Vault";
    constexpr const char* kAppVersion    = "1.1.0";
    constexpr const char* kGumroadUrl    = "https://takefujidrums.gumroad.com/l/GrooveVaultPro";
    constexpr const char* kGumroadApi    = "https://api.gumroad.com/v2/licenses/verify";
    constexpr const char* kGumroadProdId = "fTvKOXCmH89fkLFNGo4LIw==";
    constexpr const char* kGithubApi     =
        "https://api.github.com/repos/takef1221/takefuji-groove-vault/releases/latest";
}

//==============================================================================
// License file helpers
//==============================================================================

static juce::File getLicenseFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TakefujiGrooveVault/license.json");
}

static bool hasSavedLicense()
{
    auto f = getLicenseFile();
    if (!f.existsAsFile()) return false;
    auto parsed = juce::JSON::parse (f);
    return static_cast<bool> (parsed.getProperty ("verified", false));
}

static void saveLicense (const juce::String& key)
{
    auto f = getLicenseFile();
    f.getParentDirectory().createDirectory();
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("license_key", key);
    obj->setProperty ("verified",    true);
    f.replaceWithText (juce::JSON::toString (juce::var (obj.get()), true));
}

// Called from background thread — blocks on network.
static bool verifyLicenseOnline (const juce::String& key)
{
    auto stream = juce::URL (kGumroadApi)
        .withParameter ("product_id", kGumroadProdId)
        .withParameter ("license_key", key)
        .createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withConnectionTimeoutMs (15000));

    if (stream == nullptr) return false;
    auto parsed = juce::JSON::parse (stream->readEntireStreamAsString());
    if (parsed.isVoid() || parsed.isUndefined()) return false;
    return static_cast<bool> (parsed["success"]);
}

//==============================================================================
// Update check — version comparison
//==============================================================================

static bool isNewerVersion (const juce::String& latest, const juce::String& current)
{
    auto parse = [] (const juce::String& v) -> std::tuple<int,int,int>
    {
        auto s = v.trimCharactersAtStart ("v");
        auto p = juce::StringArray::fromTokens (s, ".", "");
        return { p.size() > 0 ? p[0].getIntValue() : 0,
                 p.size() > 1 ? p[1].getIntValue() : 0,
                 p.size() > 2 ? p[2].getIntValue() : 0 };
    };
    return parse (latest) > parse (current);
}

//==============================================================================
// Update check — settings helpers (skipped-version persistence)
//==============================================================================

static juce::File getSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TakefujiGrooveVault/settings.json");
}

static juce::String getSkippedVersion()
{
    auto f = getSettingsFile();
    if (!f.existsAsFile()) return {};
    return juce::JSON::parse (f)["skippedVersion"].toString();
}

static void saveSkippedVersion (const juce::String& version)
{
    auto f = getSettingsFile();
    f.getParentDirectory().createDirectory();

    juce::var existing;
    if (f.existsAsFile()) existing = juce::JSON::parse (f);

    auto* raw = existing.getDynamicObject();
    juce::DynamicObject::Ptr obj = raw ? raw : new juce::DynamicObject();
    obj->setProperty ("skippedVersion", version);
    f.replaceWithText (juce::JSON::toString (juce::var (obj.get()), true));
}

//==============================================================================
// Update check — installer download (ThreadWithProgressWindow, async via launchThread)
//
// JUCE_MODAL_LOOPS_PERMITTED defaults to 0 on desktop, so runThread() is
// unavailable. We use launchThread() + threadComplete() instead.
//==============================================================================

class InstallerDownloadJob : public juce::ThreadWithProgressWindow
{
public:
    // onDone(true, file) on success; onDone(false, {}) on failure/cancel.
    // The job self-deletes inside threadComplete().
    InstallerDownloadJob (const juce::String& url,
                          std::function<void (bool, juce::File)> onDone)
        : juce::ThreadWithProgressWindow ("Downloading Update", true, true),
          downloadUrl (url), callback (std::move (onDone)) {}

    void run() override
    {
        setProgress (-1.0);
        setStatusMessage ("Connecting...");

#if JUCE_WINDOWS
        destFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("TGVSetup_update.exe");
#elif JUCE_MAC
        destFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("TGVUpdate_mac.zip");
#endif
        destFile.deleteFile();

        auto stream = juce::URL (downloadUrl)
            .createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (30000)
                    .withNumRedirectsToFollow (5)
                    .withExtraHeaders ("User-Agent: TakefujiGrooveVault/1.1.0\r\n"));

        if (stream == nullptr || threadShouldExit()) { failed = true; return; }

        setStatusMessage ("Downloading...");
        const juce::int64 total = stream->getTotalLength();

        juce::FileOutputStream out (destFile);
        if (!out.openedOk()) { failed = true; return; }

        constexpr int kBuf = 65536;
        juce::HeapBlock<char> buf (kBuf);
        juce::int64 received = 0;

        while (!stream->isExhausted() && !threadShouldExit())
        {
            int n = stream->read (buf, kBuf);
            if (n <= 0) break;
            out.write (buf, (size_t) n);
            received += n;
            if (total > 0)
                setProgress ((double) received / (double) total);
        }

        out.flush();

        if (threadShouldExit()) { destFile.deleteFile(); failed = true; return; }
        failed = (destFile.getSize() == 0);
    }

    // Called on the message thread when the thread finishes or cancel is pressed.
    void threadComplete (bool userPressedCancel) override
    {
        bool ok = !userPressedCancel && !failed && destFile.getSize() > 0;
        auto file = ok ? destFile : juce::File{};
        if (callback) callback (ok, file);
        delete this;
    }

private:
    juce::String downloadUrl;
    juce::File   destFile;
    bool         failed = false;
    std::function<void (bool, juce::File)> callback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstallerDownloadJob)
};

//==============================================================================
// Update check — silent install then quit, or Gumroad fallback
//==============================================================================

static void launchInstaller (const juce::File& file)
{
#if JUCE_WINDOWS
    // ShellExecuteW internally; NSIS UAC manifest triggers elevation automatically.
    if (juce::Process::openDocument (file.getFullPathName(), "/S"))
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
        return;
    }
    juce::URL (kGumroadUrl).launchInDefaultBrowser();
#elif JUCE_MAC
    auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                      .getParentDirectory();
    auto destApp = appDir.getChildFile ("TakefujiGrooveVault.app");

    juce::String cmd;
    cmd << "unzip -o " << file.getFullPathName().quoted()
        << " -d /tmp/TGVUpdate/ && "
        << "cp -R /tmp/TGVUpdate/TakefujiGrooveVault.app "
        << destApp.getFullPathName().quoted() << " && "
        << "open " << destApp.getFullPathName().quoted();

    if (system (cmd.toRawUTF8()) == 0)
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
        return;
    }
    juce::URL (kGumroadUrl).launchInDefaultBrowser();
#endif
}

//==============================================================================
// Update check — background fetch + dialog flow
//==============================================================================

static void checkForUpdates()
{
    juce::Thread::launch ([]()
    {
        auto stream = juce::URL (kGithubApi)
            .createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (10000)
                    .withNumRedirectsToFollow (3)
                    .withExtraHeaders ("User-Agent: TakefujiGrooveVault/1.1.0\r\n"));

        if (stream == nullptr) return;
        auto parsed = juce::JSON::parse (stream->readEntireStreamAsString());
        if (parsed.isVoid() || parsed.isUndefined()) return;

        auto tagName = parsed["tag_name"].toString();
        auto body    = parsed["body"].toString();
        if (tagName.isEmpty() || !isNewerVersion (tagName, kAppVersion)) return;
        if (tagName == getSkippedVersion()) return;

        // Find the Windows installer asset
        juce::String installerUrl;
        if (auto* assets = parsed["assets"].getArray())
        {
            for (const auto& asset : *assets)
            {
                auto name = asset["name"].toString();
#if JUCE_WINDOWS
                if (name.containsIgnoreCase ("Setup") && name.endsWithIgnoreCase (".exe"))
#elif JUCE_MAC
                if (name.endsWithIgnoreCase (".zip") && name.containsIgnoreCase ("mac"))
#endif
                {
                    installerUrl = asset["browser_download_url"].toString();
                    break;
                }
            }
        }

        juce::MessageManager::callAsync ([tagName, body, installerUrl]()
        {
            auto* aw = new juce::AlertWindow (
                "Update Available: " + tagName,
                body.substring (0, 800),
                juce::MessageBoxIconType::InfoIcon);
            aw->addButton ("Update Now",        1);
            aw->addButton ("Later",             0);
            aw->addButton ("Skip This Version", 2);
            aw->enterModalState (true,
                juce::ModalCallbackFunction::create ([tagName, installerUrl] (int result)
                {
                    if (result == 2) { saveSkippedVersion (tagName); return; }
                    if (result != 1) return; // "Later" — do nothing

                    if (installerUrl.isEmpty())
                    {
                        juce::URL (kGumroadUrl).launchInDefaultBrowser();
                        return;
                    }

                    // launchThread is used (not runThread) because
                    // JUCE_MODAL_LOOPS_PERMITTED defaults to 0 on desktop.
                    // InstallerDownloadJob self-deletes inside threadComplete().
                    (new InstallerDownloadJob (installerUrl, [] (bool ok, juce::File file)
                    {
                        if (!ok || !file.existsAsFile())
                        {
                            juce::URL (kGumroadUrl).launchInDefaultBrowser();
                            return;
                        }

                        // Confirm install
                        auto* confirmAw = new juce::AlertWindow (
                            "Ready to Install",
                            "The update has been downloaded.\n"
                            "The app will close and the installer will run silently.",
                            juce::MessageBoxIconType::QuestionIcon);
                        confirmAw->addButton ("Install", 1);
                        confirmAw->addButton ("Cancel",  0);
                        confirmAw->enterModalState (true,
                            juce::ModalCallbackFunction::create ([file] (int r)
                            {
                                if (r == 1) launchInstaller (file);
                            }),
                            true);
                    }))->launchThread();
                }),
                true);
        });
    });
}

//==============================================================================
// License dialog — JUCE 8 async-compatible DialogWindow subclass
//==============================================================================

class LicenseDialog : public juce::DialogWindow
{
public:
    // callback(true)=activated  callback(false)=cancelled/closed
    explicit LicenseDialog (std::function<void (bool)> callback);

    void closeButtonPressed() override;
    void activationSucceeded (const juce::String& key);

private:
    std::function<void (bool)> onComplete;

    void dismiss (bool activated)
    {
        setVisible (false);
        if (onComplete) onComplete (activated);
        delete this;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseDialog)
};

// Content component (defined after LicenseDialog for access to activationSucceeded)
class LicenseContent : public juce::Component
{
public:
    explicit LicenseContent (LicenseDialog& dlg) : owner (dlg)
    {
        instructLabel.setText (
            "Enter your Takefuji Groove Vault license key to activate:",
            juce::dontSendNotification);
        instructLabel.setFont   (juce::Font (juce::FontOptions (13.f)));
        instructLabel.setColour (juce::Label::textColourId, juce::Colour (0xff777777));
        addAndMakeVisible (instructLabel);

        keyEditor.setTextToShowWhenEmpty ("XXXX-XXXX-XXXX-XXXX", juce::Colours::grey);
        keyEditor.setFont (juce::Font (juce::FontOptions (15.f)));
        keyEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xffFFFFFF));
        keyEditor.setColour (juce::TextEditor::textColourId,       juce::Colour (0xff333333));
        keyEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xffBBBBBB));
        keyEditor.onReturnKey = [this] { onActivate(); };
        addAndMakeVisible (keyEditor);

        errorLabel.setColour (juce::Label::textColourId, juce::Colours::red);
        errorLabel.setFont   (juce::Font (juce::FontOptions (12.f)));
        addAndMakeVisible    (errorLabel);

        activateBtn.setButtonText ("Activate");
        activateBtn.onClick = [this] { onActivate(); };
        addAndMakeVisible (activateBtn);

        purchaseBtn.setButtonText ("Purchase");
        purchaseBtn.onClick = []
        {
            juce::URL (kGumroadUrl).launchInDefaultBrowser();
        };
        addAndMakeVisible (purchaseBtn);

        setSize (460, 170);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (20, 16);
        instructLabel.setBounds (a.removeFromTop (22));
        a.removeFromTop (8);
        keyEditor.setBounds (a.removeFromTop (32));
        a.removeFromTop (6);
        errorLabel.setBounds (a.removeFromTop (18));
        a.removeFromTop (10);
        auto row = a.removeFromTop (34);
        activateBtn.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (10);
        purchaseBtn.setBounds (row.removeFromLeft (110));
    }

private:
    void onActivate()
    {
        auto key = keyEditor.getText().trim();
        if (key.isEmpty()) return;

        activateBtn.setEnabled    (false);
        activateBtn.setButtonText ("Verifying...");
        errorLabel.setText ({}, juce::dontSendNotification);

        juce::Component::SafePointer<LicenseContent> safe (this);

        juce::Thread::launch ([safe, key]()
        {
            bool ok = verifyLicenseOnline (key);
            juce::MessageManager::callAsync ([safe, key, ok]()
            {
                if (safe == nullptr) return;   // dialog was closed during request
                if (ok)
                {
                    safe->owner.activationSucceeded (key);
                }
                else
                {
                    safe->errorLabel.setText (
                        "Invalid license key. Please check and try again.",
                        juce::dontSendNotification);
                    safe->activateBtn.setEnabled    (true);
                    safe->activateBtn.setButtonText ("Activate");
                }
            });
        });
    }

    LicenseDialog&   owner;
    juce::Label      instructLabel, errorLabel;
    juce::TextEditor keyEditor;
    juce::TextButton activateBtn, purchaseBtn;
};

// --- LicenseDialog out-of-line definitions ---

LicenseDialog::LicenseDialog (std::function<void (bool)> callback)
    : juce::DialogWindow ("Activate Takefuji Groove Vault",
                          juce::Colour (0xffF5F5F5),
                          true,   // escapeKeyTriggersCloseButton
                          false), // addToDesktop: we do it manually below
      onComplete (std::move (callback))
{
    // Set native title bar BEFORE addToDesktop to avoid peer recreation
    setUsingNativeTitleBar (true);
    setResizable (false, false);
    setContentOwned (new LicenseContent (*this), true);

    // Explicitly add to desktop, then make visible, then centre + bring to front
    addToDesktop (getDesktopWindowStyleFlags());
    setVisible (true);
    centreWithSize (460, 200);
    toFront (true);
}

void LicenseDialog::closeButtonPressed()
{
    dismiss (false);
}

void LicenseDialog::activationSucceeded (const juce::String& key)
{
    saveLicense (key);
    dismiss (true);
}

//==============================================================================
// Application
//==============================================================================

class TGVStandaloneApp : public juce::JUCEApplication
{
public:
    TGVStandaloneApp()
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = kAppTitle;
        opts.filenameSuffix      = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.folderName          = "";
        appProperties.setStorageParameters (opts);
    }

    const juce::String getApplicationName()    override { return kAppTitle; }
    const juce::String getApplicationVersion() override { return kAppVersion; }
    bool moreThanOneInstanceAllowed()          override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    //--------------------------------------------------------------------------
    void initialise (const juce::String&) override
    {
        // デバッグ用ファイルログ
        {
            auto licFile = getLicenseFile();
            bool saved   = hasSavedLicense();
            juce::String log;
            log << "initialise() called\n"
                << "hasSavedLicense = " << (saved ? "true" : "false") << "\n"
                << "licenseFile path = " << licFile.getFullPathName() << "\n"
                << "licenseFile exists = " << (licFile.existsAsFile() ? "true" : "false") << "\n";

            juce::File("/tmp/tgv_debug.txt").replaceWithText(log);
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                .getChildFile("tgv_debug.txt").replaceWithText(log);
        }
        juce::Logger::writeToLog ("DEBUG: hasSavedLicense = " + juce::String (hasSavedLicense() ? "true" : "false"));

        if (hasSavedLicense())
        {
            launchMainWindow();
            return;
        }

        // Defer dialog creation to the next event-loop tick so the JUCE
        // desktop and peer infrastructure is fully initialised before we
        // create a top-level window.
        juce::MessageManager::callAsync ([this]()
        {
            juce::Logger::writeToLog ("DEBUG: showing LicenseDialog");
            new LicenseDialog ([this] (bool activated)
            {
                if (!activated) { quit(); return; }
                launchMainWindow();
            });
        });
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    void launchMainWindow()
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(), false);

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            juce::String (kAppTitle) + "  v" + kAppVersion,
            juce::Colour (0xffF5F5F5),
            std::move (holder));

        mainWindow->setVisible (true);
        checkForUpdates();
    }

    juce::ApplicationProperties                   appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

//==============================================================================
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new TGVStandaloneApp();
}
