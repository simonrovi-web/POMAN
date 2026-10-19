// Portable standalone app: replaces JUCE's default StandaloneFilterApp so settings (audio device,
// plugin state) live next to Abyssal.exe instead of %APPDATA%. Falls back to %APPDATA% when the
// executable's folder is read-only (e.g. Program Files).

#include <juce_core/juce_core.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PortablePaths.h"

namespace
{
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = JucePlugin_Name;
    options.filenameSuffix = ".settings";
    options.folderName = "";
    return options;
}

juce::File settingsFile()
{
    const auto portableDir = abyssal::portableRoot();
    if (portableDir.hasWriteAccess())
        return portableDir.getChildFile (juce::String (JucePlugin_Name) + ".settings");

    return settingsOptions().getDefaultFile();
}
} // namespace

class AbyssalStandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        settings = std::make_unique<juce::PropertiesFile> (settingsFile(), settingsOptions());

        auto holder = std::make_unique<juce::StandalonePluginHolder> (settings.get(), false);
        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            std::move (holder));
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        if (settings != nullptr)
            settings->saveIfNeeded();
        settings = nullptr;
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
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new AbyssalStandaloneApp();
}

#endif
