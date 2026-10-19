// Portable standalone app: replaces JUCE's default StandaloneFilterApp so settings (audio device,
// plugin state) live next to POMAN.exe instead of %APPDATA%. Falls back to %APPDATA% when the
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
    const auto portableDir = poman::portableRoot();
    if (portableDir.hasWriteAccess())
        return portableDir.getChildFile (juce::String (JucePlugin_Name) + ".settings");

    return settingsOptions().getDefaultFile();
}

// Smallest buffer the device offers that is still >= 64 samples (below that most USB interfaces crackle).
void useSmallestStableBuffer (juce::AudioDeviceManager& deviceManager)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    int chosen = device->getDefaultBufferSize();
    for (auto size : device->getAvailableBufferSizes())
        if (size >= 64 && size < chosen)
            chosen = size;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = chosen;
    deviceManager.setAudioDeviceSetup (setup, true);
}

bool trySelectDevice (juce::AudioDeviceManager& deviceManager, const juce::String& typeName, const juce::String& deviceName)
{
    deviceManager.setCurrentAudioDeviceType (typeName, true);

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = deviceName;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    setup.bufferSize = 0;

    if (deviceManager.setAudioDeviceSetup (setup, true).isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
        return false;

    useSmallestStableBuffer (deviceManager);
    return true;
}

// First run only: prefer a hardware ASIO driver, then WASAPI low-latency mode. Once the user picks a
// device in Audio/MIDI Settings it is saved as "audioSetup" and this is never applied again.
void preferLowLatencyDevice (juce::AudioDeviceManager& deviceManager)
{
    const auto previousType = deviceManager.getCurrentAudioDeviceType();
    const auto previousSetup = deviceManager.getAudioDeviceSetup();

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO")
            continue;

        type->scanForDevices();
        juce::StringArray candidates;
        for (const auto& name : type->getDeviceNames())
        {
            // Software/aggregator drivers add their own buffering; real interfaces go first.
            const bool software = name.containsIgnoreCase ("FL Studio") || name.containsIgnoreCase ("ASIO4ALL")
                               || name.containsIgnoreCase ("Generic") || name.containsIgnoreCase ("Voicemeeter");
            if (! software)
                candidates.add (name);
        }

        for (const auto& name : candidates)
            if (trySelectDevice (deviceManager, "ASIO", name))
                return;
    }

    if (trySelectDevice (deviceManager, "Windows Audio (Low Latency Mode)", {}))
        return;

    deviceManager.setCurrentAudioDeviceType (previousType, true);
    deviceManager.setAudioDeviceSetup (previousSetup, true);
}
} // namespace

class PomanStandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String& commandLine) override
    {
        settings = std::make_unique<juce::PropertiesFile> (settingsFile(), settingsOptions());
        const bool firstRun = ! settings->containsKey ("audioSetup");

        // A guitar amp needs to hear its input: unmute by default (JUCE mutes to avoid mic feedback).
        if (! settings->containsKey ("shouldMuteInput"))
            settings->setValue ("shouldMuteInput", false);

        auto holder = std::make_unique<juce::StandalonePluginHolder> (settings.get(), false);

        if (firstRun)
            preferLowLatencyDevice (holder->deviceManager);

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            std::move (holder));
        mainWindow->setVisible (true);

        // Dev aid: POMAN.exe --snapshot=out.png renders the editor to a PNG and quits.
        const auto snapshotPath = commandLine.fromFirstOccurrenceOf ("--snapshot=", false, false).unquoted();
        if (snapshotPath.isNotEmpty())
        {
            juce::Timer::callAfterDelay (1500, [this, snapshotPath]
            {
                if (auto* content = mainWindow->getContentComponent())
                {
                    juce::File target (snapshotPath);
                    target.deleteFile();
                    juce::FileOutputStream out (target);
                    if (out.openedOk())
                        juce::PNGImageFormat().writeImageToStream (content->createComponentSnapshot (content->getLocalBounds()), out);
                }
                systemRequestedQuit();
            });
        }
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
    return new PomanStandaloneApp();
}

#endif
