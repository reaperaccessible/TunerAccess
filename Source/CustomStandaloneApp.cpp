// Custom Standalone wrapper for TunerAccess.
//
// Replaces JUCE's stock "Options" button menu (which opens the inaccessible
// AudioDeviceSelectorComponent dialog) with our own AccessibleSettingsPanel.
//
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 (set in CMakeLists.txt).

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"          // for TunerAccessAudioProcessorEditor::openAudioSettings()

namespace juce
{

//==============================================================================
// Subclass that overrides Button::Listener::buttonClicked so the Options
// button opens our accessible panel directly (no popup menu, no JUCE dialog).
//==============================================================================
class TunerAccessStandaloneWindow : public StandaloneFilterWindow
{
public:
    TunerAccessStandaloneWindow (const String& title,
                                  Colour backgroundColour,
                                  std::unique_ptr<StandalonePluginHolder> holder)
        : StandaloneFilterWindow (title, backgroundColour, std::move (holder))
    {
        // Relabel the JUCE stock "Options" button to "Audio Settings" and
        // make it screen-reader friendly. Our buttonClicked() override below
        // intercepts the click and opens AccessibleSettingsPanel — the SAME
        // panel as the F10 shortcut. No JUCE stock dialog reachable anywhere.
        Component::SafePointer<TunerAccessStandaloneWindow> safeThis (this);
        MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr) return;
            for (int i = 0; i < safeThis->getNumChildComponents(); ++i)
            {
                if (auto* btn = dynamic_cast<TextButton*> (safeThis->getChildComponent (i)))
                {
                    if (btn->getButtonText() == "Options")
                    {
                        btn->setButtonText ("Audio Settings");
                        btn->setTitle ("Audio Settings");
                        btn->setName ("Audio Settings");
                        btn->setDescription ("Open the accessible audio settings panel. Same as pressing F10.");
                    }
                }
            }
        });
    }

private:
    // Single source of truth: route the click through the editor's
    // openAudioSettings() so the title-bar button and the F10 shortcut both
    // invoke the EXACT same code path — same dialog, same focus save, same
    // accessibility configuration. No risk of divergence.
    void buttonClicked (Button*) override
    {
        if (pluginHolder == nullptr || pluginHolder->processor == nullptr)
            return;

        if (auto* ed = pluginHolder->processor->getActiveEditor())
            if (auto* tunerEd = dynamic_cast<TunerAccessAudioProcessorEditor*> (ed))
                tunerEd->openAudioSettings();
    }
};

//==============================================================================
class TunerAccessStandaloneApp final : public JUCEApplication
{
public:
    TunerAccessStandaloneApp()
    {
        PropertiesFile::Options o;
        o.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        o.filenameSuffix      = ".settings";
        o.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName          = "~/.config";
       #else
        o.folderName          = "";
       #endif
        appProperties.setStorageParameters (o);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig (channels, numElementsInArray (channels));
       #else
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<StandalonePluginHolder> (appProperties.getUserSettings(),
                                                         false,
                                                         String{},
                                                         nullptr,
                                                         channelConfig,
                                                         false);
    }

    TunerAccessStandaloneWindow* createWindow()
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;
            return nullptr;
        }

        return new TunerAccessStandaloneWindow (
            getApplicationName(),
            LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
            createPluginHolder());
    }

    void initialise (const String&) override
    {
        mainWindow.reset (createWindow());
        if (mainWindow != nullptr)
        {
            // JUCE defaults shouldMuteInput=true to prevent feedback loops.
            // A tuner only ANALYSES the input (nothing routed back to output),
            // so muting just kills the signal we need. Force unmute.
            if (mainWindow->pluginHolder != nullptr)
                mainWindow->pluginHolder->getMuteInputValue().setValue (false);

            mainWindow->setVisible (true);
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    ApplicationProperties appProperties;
    std::unique_ptr<TunerAccessStandaloneWindow> mainWindow;
};

} // namespace juce

//==============================================================================
// Hook required by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new juce::TunerAccessStandaloneApp();
}
