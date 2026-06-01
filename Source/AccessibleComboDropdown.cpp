#include "AccessibleComboDropdown.h"

#ifdef _WIN32
 #include <Windows.h>
 #include <Shlobj.h>

 #ifndef NotificationKind_ActionCompleted
  #define NotificationKind_ActionCompleted 4
 #endif
 #ifndef NotificationProcessing_ImportantAll
  #define NotificationProcessing_ImportantAll 2
 #endif

static void comboUiaRaiseNotification (const juce::String& message)
{
    using RaiseNotifFunc = HRESULT (WINAPI*) (void*, int, int, BSTR, BSTR);
    static auto* uiaModule = LoadLibraryW (L"UIAutomationCore.dll");
    static auto raiseNotif = uiaModule
        ? reinterpret_cast<RaiseNotifFunc> (GetProcAddress (uiaModule, "UiaRaiseNotificationEvent"))
        : nullptr;
    if (! raiseNotif) return;

    auto& desktop = juce::Desktop::getInstance();
    if (desktop.getNumComponents() == 0) return;
    auto* topComp = desktop.getComponent (0);
    if (! topComp) return;
    auto* handler = topComp->getAccessibilityHandler();
    if (! handler) return;
    auto* native = handler->getNativeImplementation();
    if (! native) return;

    auto bstr = SysAllocString (message.toWideCharPointer());
    raiseNotif (reinterpret_cast<void*> (native),
                NotificationKind_ActionCompleted,
                NotificationProcessing_ImportantAll,
                bstr, bstr);
    SysFreeString (bstr);
}

struct NvdaComboApi
{
    using SpeakFunc     = unsigned long (__stdcall*) (const wchar_t*);
    using CancelFunc    = unsigned long (__stdcall*) (void);
    using TestFunc      = unsigned long (__stdcall*) (void);
    using SpeakSsmlFunc = unsigned long (__stdcall*) (const wchar_t*, int, int, unsigned char);

    HMODULE       module         = nullptr;
    SpeakFunc     speakText      = nullptr;
    CancelFunc    cancelSpeech   = nullptr;
    TestFunc      testIfRunning  = nullptr;
    SpeakSsmlFunc speakSsml      = nullptr;

    NvdaComboApi()
    {
        wchar_t appDataPath[MAX_PATH];
        if (SHGetFolderPathW (nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath) == S_OK)
        {
            juce::String dllPath = juce::String (appDataPath) + "\\TunerAccess\\nvdaControllerClient.dll";
            module = LoadLibraryW (dllPath.toWideCharPointer());
        }
        if (module == nullptr)
            module = LoadLibraryW (L"nvdaControllerClient.dll");

        if (module != nullptr)
        {
            speakText      = reinterpret_cast<SpeakFunc>     (GetProcAddress (module, "nvdaController_speakText"));
            cancelSpeech   = reinterpret_cast<CancelFunc>    (GetProcAddress (module, "nvdaController_cancelSpeech"));
            testIfRunning  = reinterpret_cast<TestFunc>      (GetProcAddress (module, "nvdaController_testIfRunning"));
            speakSsml      = reinterpret_cast<SpeakSsmlFunc> (GetProcAddress (module, "nvdaController_speakSsml"));
        }
    }

    bool isRunning() const { return testIfRunning != nullptr && testIfRunning() == 0; }
};

static NvdaComboApi& getNvdaCombo()
{
    static NvdaComboApi instance;
    return instance;
}
#endif

class ComboReadOnlyTextValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit ComboReadOnlyTextValue (std::function<juce::String()> getter)
        : getTextFn (std::move (getter)) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override
    {
        return getTextFn ? getTextFn() : juce::String{};
    }
    void setValueAsString (const juce::String&) override {}
private:
    std::function<juce::String()> getTextFn;
};

class ComboListBoxTableInterface : public juce::AccessibilityTableInterface
{
public:
    explicit ComboListBoxTableInterface (juce::ListBox& lb) : listBox (lb) {}
    int getNumRows() const override
    {
        return listBox.getListBoxModel() ? listBox.getListBoxModel()->getNumRows() : 0;
    }
    int getNumColumns() const override { return 1; }
    const juce::AccessibilityHandler* getHeaderHandler() const override { return nullptr; }
    const juce::AccessibilityHandler* getRowHandler (int row) const override
    {
        if (auto* comp = listBox.getComponentForRowNumber (row))
            return comp->getAccessibilityHandler();
        return nullptr;
    }
    const juce::AccessibilityHandler* getCellHandler (int, int) const override { return nullptr; }
    juce::Optional<Span> getRowSpan (const juce::AccessibilityHandler& handler) const override
    {
        auto rowNum = listBox.getRowNumberOfComponent (&handler.getComponent());
        return rowNum != -1 ? juce::makeOptional (Span { rowNum, 1 }) : juce::nullopt;
    }
    juce::Optional<Span> getColumnSpan (const juce::AccessibilityHandler&) const override
    {
        return Span { 0, 1 };
    }
    void showCell (const juce::AccessibilityHandler& h) const override
    {
        if (auto row = getRowSpan (h))
            listBox.scrollToEnsureRowIsOnscreen (row->begin);
    }
private:
    juce::ListBox& listBox;
};

#ifdef _WIN32
static juce::String comboWrapSsml (const juce::String& plainText)
{
    auto escaped = plainText
        .replace ("&", "&amp;")
        .replace ("<", "&lt;")
        .replace (">", "&gt;");
    return "<speak>" + escaped + "</speak>";
}

static constexpr int kComboSpeechNormal = 0;
static constexpr int kComboSpeechNow    = 2;
static constexpr int kComboSymbolUnchanged = -1;

static void comboNvdaSpeak (NvdaComboApi& nvda, const juce::String& text, int priority)
{
    if (nvda.speakSsml != nullptr)
    {
        auto ssml = comboWrapSsml (text);
        nvda.speakSsml (ssml.toWideCharPointer(), kComboSymbolUnchanged, priority, 1);
    }
    else
    {
        if (priority == kComboSpeechNow && nvda.cancelSpeech != nullptr)
            nvda.cancelSpeech();
        if (nvda.speakText != nullptr)
            nvda.speakText (text.toWideCharPointer());
    }
}
#endif

void AccessibleComboDropdown::screenReaderAnnounce (const juce::String& text)
{
#ifdef _WIN32
    auto& nvda = getNvdaCombo();
    if (nvda.isRunning())
        comboNvdaSpeak (nvda, text, kComboSpeechNormal);
    else
        comboUiaRaiseNotification (text);
#else
    juce::ignoreUnused (text);
#endif
}

void AccessibleComboDropdown::screenReaderAnnounceNow (const juce::String& text)
{
#ifdef _WIN32
    auto& nvda = getNvdaCombo();
    if (nvda.isRunning())
        comboNvdaSpeak (nvda, text, kComboSpeechNow);
    else
        comboUiaRaiseNotification (text);
#else
    juce::ignoreUnused (text);
#endif
}

//==============================================================================
AccessibleComboDropdown::AccessibleComboDropdown()
{
    setWantsKeyboardFocus (true);
    setAccessible (true);

    dropdownList.owner = this;
    dropdownList.setModel (&dropdownList);
    dropdownList.setRowHeight (24);
    dropdownList.setWantsKeyboardFocus (true);
    dropdownList.setAccessible (true);
    dropdownList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xFF2A2A3E));
    dropdownList.setColour (juce::ListBox::outlineColourId, juce::Colours::grey);
    addChildComponent (dropdownList);
}

AccessibleComboDropdown::~AccessibleComboDropdown() = default;

void AccessibleComboDropdown::clear()
{
    items.clear();
    currentId = 0;
    currentText = {};
    browseIndex = -1;
    dropdownOpen = false;
    dropdownList.setVisible (false);
    repaint();
}

void AccessibleComboDropdown::addItem (const juce::String& text, int itemId)
{
    items.push_back ({ text, itemId });
}

void AccessibleComboDropdown::setSelectedId (int itemId, juce::NotificationType)
{
    for (int i = 0; i < (int) items.size(); ++i)
    {
        if (items[(size_t) i].id == itemId)
        {
            currentId = itemId;
            currentText = items[(size_t) i].text;
            repaint();
            return;
        }
    }
}

int AccessibleComboDropdown::getSelectedItemIndex() const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].id == currentId)
            return i;
    return -1;
}

void AccessibleComboDropdown::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (juce::Colour (0xFF2A2A3E));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (hasKeyboardFocus (false) ? juce::Colour (0xFF3366CC) : juce::Colours::grey);
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.setColour (juce::Colours::white);
    g.setFont (14.0f);

    juce::String displayText = currentText.isEmpty() ? "(none)" : currentText;
    g.drawText (displayText, bounds.reduced (8, 0), juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::lightgrey);
    auto arrowArea = bounds.removeFromRight (24).reduced (6);
    g.drawText (dropdownOpen ? "^" : "v", arrowArea, juce::Justification::centred);
}

void AccessibleComboDropdown::resized()
{
    if (dropdownOpen)
    {
        int listHeight = juce::jmin ((int) items.size() * 24, 300);
        dropdownList.setBounds (0, getHeight(), getWidth(), listHeight);
    }
}

bool AccessibleComboDropdown::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::downKey && key.getModifiers().isAltDown())
    {
        if (! dropdownOpen)
            openDropdown();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::upKey && key.getModifiers().isAltDown())
    {
        if (dropdownOpen)
            closeDropdown (false);
        return true;
    }

    if (! dropdownOpen && key.getKeyCode() == juce::KeyPress::returnKey)
    {
        screenReaderAnnounce (currentText.isEmpty() ? "No selection" : currentText);
        return true;
    }

    return false;
}

void AccessibleComboDropdown::focusGained (FocusChangeType)
{
    screenReaderAnnounce (getName() + ", " + (currentText.isEmpty() ? "no selection" : currentText)
                          + ". Press Alt+Down to open list.");
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> AccessibleComboDropdown::createAccessibilityHandler()
{
    // Explicit comboBox role + ValueInterface so braille shows the current selection
    // persistently on focus. Without this, the closed combo defaulted to
    // role=unspecified with NO ValueInterface, matching the silent-on-focus condition
    // documented in nvda_silent_roles_on_focus.md and braille_display_implementation.md.
    struct ClosedComboValue : public juce::AccessibilityTextValueInterface
    {
        explicit ClosedComboValue (std::function<juce::String()> g) : getter (std::move (g)) {}
        bool isReadOnly() const override                  { return true; }
        juce::String getCurrentValueAsString() const override { return getter ? getter() : juce::String(); }
        void setValueAsString (const juce::String&) override {}
        std::function<juce::String()> getter;
    };

    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::comboBox,
        juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces {
            std::make_unique<ClosedComboValue>([this]()
            {
                return currentText.isEmpty() ? juce::String ("no selection") : currentText;
            })
        });
}

void AccessibleComboDropdown::openDropdown()
{
    if (items.empty()) return;

    dropdownOpen = true;
    browseIndex = getSelectedItemIndex();
    if (browseIndex < 0) browseIndex = 0;

    int listHeight = juce::jmin ((int) items.size() * 24, 300);

    auto* parent = getParentComponent();
    if (parent != nullptr)
    {
        int spaceBelow = parent->getHeight() - (getY() + getHeight());
        if (spaceBelow < listHeight)
            dropdownList.setBounds (getX(), getY() - listHeight, getWidth(), listHeight);
        else
            dropdownList.setBounds (getX(), getY() + getHeight(), getWidth(), listHeight);

        parent->addAndMakeVisible (dropdownList);
        dropdownList.toFront (false);
    }
    else
    {
        dropdownList.setBounds (0, getHeight(), getWidth(), listHeight);
        dropdownList.setVisible (true);
    }

    dropdownList.updateContent();
    dropdownList.selectRow (browseIndex);
    dropdownList.grabKeyboardFocus();
    announceItem (browseIndex);
    repaint();
}

void AccessibleComboDropdown::closeDropdown (bool confirm)
{
    if (! dropdownOpen) return;

    if (confirm && browseIndex >= 0 && browseIndex < (int) items.size())
    {
        currentId = items[(size_t) browseIndex].id;
        currentText = items[(size_t) browseIndex].text;
        screenReaderAnnounceNow (currentText + " selected");
        if (onConfirm)
            onConfirm();
    }
    else
    {
        screenReaderAnnounce ("Cancelled");
    }

    dropdownOpen = false;
    dropdownList.setVisible (false);

    if (dropdownList.getParentComponent() != this)
    {
        if (auto* parent = dropdownList.getParentComponent())
            parent->removeChildComponent (&dropdownList);
    }

    grabKeyboardFocus();
    repaint();
}

void AccessibleComboDropdown::announceItem (int index)
{
    if (index >= 0 && index < (int) items.size())
    {
        if (auto* h = dropdownList.getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }
}

//==============================================================================
AccessibleComboDropdown::DropdownList::DropdownList()
    : juce::ListBox ("Dropdown List", nullptr) {}

int AccessibleComboDropdown::DropdownList::getNumRows()
{
    return owner ? (int) owner->items.size() : 0;
}

void AccessibleComboDropdown::DropdownList::paintListBoxItem (int row, juce::Graphics& g,
                                                               int width, int height, bool selected)
{
    if (owner == nullptr || row < 0 || row >= (int) owner->items.size()) return;
    if (selected) g.fillAll (juce::Colour (0xFF3366CC));
    g.setColour (selected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont (14.0f);
    g.drawText (owner->items[(size_t) row].text, 8, 0, width - 16, height,
                juce::Justification::centredLeft, true);
}

juce::String AccessibleComboDropdown::DropdownList::getNameForRow (int row)
{
    if (owner == nullptr || row < 0 || row >= (int) owner->items.size()) return {};
    return owner->items[(size_t) row].text;
}

bool AccessibleComboDropdown::DropdownList::keyPressed (const juce::KeyPress& key)
{
    if (owner == nullptr) return juce::ListBox::keyPressed (key);

    auto kc = key.getKeyCode();

    if (kc == juce::KeyPress::returnKey)
    {
        owner->browseIndex = getSelectedRow();
        owner->closeDropdown (true);
        return true;
    }
    if (kc == juce::KeyPress::escapeKey)
    {
        owner->closeDropdown (false);
        return true;
    }
    if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey)
    {
        int numRows = getNumRows();
        int cur = getSelectedRow();
        int next = (kc == juce::KeyPress::downKey)
            ? juce::jmin (cur + 1, numRows - 1)
            : juce::jmax (cur - 1, 0);

        if (next != cur)
        {
            selectRow (next);
            owner->browseIndex = next;
            owner->announceItem (next);
        }
        return true;
    }
    if (kc == juce::KeyPress::homeKey)
    {
        selectRow (0);
        owner->browseIndex = 0;
        owner->announceItem (0);
        return true;
    }
    if (kc == juce::KeyPress::endKey)
    {
        int last = getNumRows() - 1;
        if (last >= 0)
        {
            selectRow (last);
            owner->browseIndex = last;
            owner->announceItem (last);
        }
        return true;
    }
    if (kc == juce::KeyPress::tabKey)
    {
        owner->closeDropdown (false);
        return false;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler>
AccessibleComboDropdown::DropdownList::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this,
        juce::AccessibilityRole::list,
        juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces {
            std::make_unique<ComboReadOnlyTextValue> ([this]()
            {
                int row = getSelectedRow();
                if (owner != nullptr && row >= 0 && row < static_cast<int> (owner->items.size()))
                    return juce::String (row + 1) + " of "
                         + juce::String (static_cast<int> (owner->items.size()))
                         + ". " + owner->items[static_cast<size_t> (row)].text;
                return juce::String{};
            }),
            nullptr,
            std::make_unique<ComboListBoxTableInterface> (*this),
            nullptr
        });
}
