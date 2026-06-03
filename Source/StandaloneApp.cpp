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
    constexpr const char* kAppVersion    = "1.0.0";
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
// Update check
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

static void checkForUpdates()
{
    juce::Thread::launch ([]()
    {
        auto stream = juce::URL (kGithubApi)
            .createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (10000)
                    .withNumRedirectsToFollow (3)
                    .withExtraHeaders ("User-Agent: TakefujiGrooveVault/1.0.0\r\n"));

        if (stream == nullptr) return;
        auto parsed = juce::JSON::parse (stream->readEntireStreamAsString());
        if (parsed.isVoid() || parsed.isUndefined()) return;

        auto tagName = parsed["tag_name"].toString();
        auto body    = parsed["body"].toString();
        if (tagName.isEmpty() || !isNewerVersion (tagName, kAppVersion)) return;

        juce::MessageManager::callAsync ([tagName, body]()
        {
            auto* aw = new juce::AlertWindow (
                "Update Available: " + tagName,
                body.substring (0, 800),
                juce::MessageBoxIconType::InfoIcon);
            aw->addButton ("Get Update", 1);
            aw->addButton ("Later",      0);
            aw->enterModalState (true,
                juce::ModalCallbackFunction::create ([] (int result)
                {
                    if (result == 1)
                        juce::URL (kGumroadUrl).launchInDefaultBrowser();
                }),
                true /* deleteWhenDismissed */);
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
