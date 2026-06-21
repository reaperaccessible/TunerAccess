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
    juce::AccessibilityHandler::postAnnouncement (text,
        juce::AccessibilityHandler::AnnouncementPriority::medium);
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
    juce::AccessibilityHandler::postAnnouncement (text,
        juce::AccessibilityHandler::AnnouncementPriority::high);
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
           #if ! JUCE_WINDOWS
            pendingIndex = i;
           #endif
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
   #if ! JUCE_WINDOWS
    // macOS / VoiceOver: in-place adjustable value — no dropdown window, no table.
    // Arrows browse + announce (without applying); Enter commits and applies.
    // VoiceOver's VO+Up/Down routes through the value interface's setValue() ->
    // browseTo(), and VO+Space routes through the press action -> commitPending().
    const auto kc = key.getKeyCode();
    if (kc == juce::KeyPress::downKey)   { browseTo (pendingIndex + 1);             return true; }
    if (kc == juce::KeyPress::upKey)     { browseTo (pendingIndex - 1);             return true; }
    if (kc == juce::KeyPress::homeKey)   { browseTo (0);                            return true; }
    if (kc == juce::KeyPress::endKey)    { browseTo ((int) items.size() - 1);       return true; }
    if (kc == juce::KeyPress::returnKey) { commitPending();                         return true; }
    return false;
   #else
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
   #endif
}

void AccessibleComboDropdown::focusGained (FocusChangeType)
{
   #if JUCE_WINDOWS
    screenReaderAnnounce (getName() + ", " + (currentText.isEmpty() ? "no selection" : currentText)
                          + ". Press Alt+Down to open list.");
   #else
    screenReaderAnnounce (getName() + ", " + (currentText.isEmpty() ? "no selection" : currentText)
                          + ". Use up and down arrows to change, Enter to apply.");
   #endif
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> AccessibleComboDropdown::createAccessibilityHandler()
{
   #if JUCE_WINDOWS
    // Windows / NVDA: explicit comboBox role + read-only ValueInterface so braille
    // shows the current selection persistently on focus. Without this, the closed
    // combo defaulted to role=unspecified with NO ValueInterface, matching the
    // silent-on-focus condition documented in nvda_silent_roles_on_focus.md and
    // braille_display_implementation.md. The dropdown opens via Alt+Down (ListBox).
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
   #else
    // macOS / VoiceOver: an in-place adjustable combo box (role comboBox, NOT slider —
    // the user does not want a slider) with a writable, RANGED value interface. The
    // in-place "adjustable" behaviour (VO+Up/Down cycles items, setValue -> browseTo) is
    // ROLE-INDEPENDENT in JUCE: it is gated only on a non-read-only value interface with a
    // valid range (juce_Accessibility_mac.mm), so comboBox keeps the combo announcement
    // AND the arrow-adjust UX. Each item's TEXT is read — no popup window, no list/table.
    // We must derive from the BASE AccessibilityValueInterface: the convenience subclasses
    // make getRange()/getCurrentValueAsString() final, which would block either
    // adjustability or text speech. VO+Space (press action) commits/applies.
    struct ComboValue : public juce::AccessibilityValueInterface
    {
        explicit ComboValue (AccessibleComboDropdown& o) : owner (o) {}

        bool   isReadOnly()      const override { return false; }
        double getCurrentValue() const override { return (double) juce::jmax (0, owner.getBrowseIndex()); }
        juce::String getCurrentValueAsString() const override
        {
            auto t = owner.getBrowseText();
            return t.isEmpty() ? juce::String ("no selection") : t;
        }
        void setValue (double newValue) override            { owner.browseTo (juce::roundToInt (newValue)); }
        void setValueAsString (const juce::String&) override {}
        AccessibleValueRange getRange() const override
        {
            const int n = owner.getNumItems();
            if (n <= 1) return {};                                        // invalid -> not adjustable
            return AccessibleValueRange ({ 0.0, (double) (n - 1) }, 1.0); // valid -> VO+Up/Down adjusts
        }

        AccessibleComboDropdown& owner;
    };

    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::comboBox,
        juce::AccessibilityActions{}.addAction (juce::AccessibilityActionType::press,
                                                [this] { commitPending(); }),
        juce::AccessibilityHandler::Interfaces {
            std::make_unique<ComboValue> (*this)
        });
   #endif
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

#if ! JUCE_WINDOWS
juce::String AccessibleComboDropdown::getBrowseText() const
{
    if (pendingIndex >= 0 && pendingIndex < (int) items.size())
        return items[(size_t) pendingIndex].text;
    return currentText;
}

void AccessibleComboDropdown::browseTo (int index)
{
    if (items.empty())
        return;

    const int clamped = juce::jlimit (0, (int) items.size() - 1, index);
    if (clamped == pendingIndex)
        return;

    // Move the browse cursor and announce the candidate WITHOUT applying it, so
    // audio settings don't re-initialise on every arrow step.
    pendingIndex = clamped;
    repaint();

    const auto& it = items[(size_t) pendingIndex];
    screenReaderAnnounce (it.text + ", " + juce::String (pendingIndex + 1)
                          + " of " + juce::String ((int) items.size()));

    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void AccessibleComboDropdown::commitPending()
{
    if (pendingIndex < 0 || pendingIndex >= (int) items.size())
        return;

    const auto& it = items[(size_t) pendingIndex];
    const bool changed = (it.id != currentId);

    currentId   = it.id;
    currentText = it.text;
    repaint();

    screenReaderAnnounceNow (currentText + " selected");

    if (changed && onConfirm)
        onConfirm();

    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}
#endif

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
