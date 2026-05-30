/*
  ==============================================================================
    Takefuji Groove Vault -- Custom Standalone Application
    StandaloneApp.cpp

    Provides a custom JUCEApplication entry point so that:
      - No Audio/MIDI settings dialog is shown at startup
        (JUCE loads saved settings or uses the system default automatically)
      - The title bar shows "Takefuji Groove Vault  v1.0.0"
      - The Options button remains available for on-demand device configuration

    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 is added to the Standalone project's
    preprocessor defines so that juce_audio_plugin_client_Standalone.cpp calls
    juce_CreateApplication() defined here instead of its own StandaloneFilterApp.
  ==============================================================================
*/

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    constexpr const char* kAppTitle   = "Takefuji Groove Vault";
    constexpr const char* kAppVersion = "1.0.0";
}

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
        // Build StandalonePluginHolder from saved (or default) device settings.
        // No startup dialog -- user opens Audio/MIDI Settings via Options button.
        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(),
            false  // appProperties owns the PropertySet, not the holder
        );

        const juce::String windowTitle =
            juce::String (kAppTitle) + "  v" + kAppVersion;

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            windowTitle,
            juce::Colour (0xff1a1a2e),   // matches the plugin's dark background
            std::move (holder)
        );

        mainWindow->setVisible (true);
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
    juce::ApplicationProperties                   appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

//==============================================================================
// Entry point called by JUCE's WinMain when
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new TGVStandaloneApp();
}
