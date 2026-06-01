#pragma once
#include <JuceHeader.h>

//==============================================================================
// AccessibleComboDropdown — An accessible combo box replacement for screen readers.
//
// Standard JUCE ComboBox applies changes immediately on Up/Down, which is wrong
// for settings. This component provides the standard Windows pattern:
//   - Tab to focus: NVDA announces current value
//   - Alt+Down: opens a dropdown list
//   - Up/Down: browse items WITHOUT applying
//   - Enter: confirm selection and apply
//   - Escape: cancel and close without changing
//==============================================================================
class AccessibleComboDropdown : public juce::Component
{
public:
    AccessibleComboDropdown();
    ~AccessibleComboDropdown() override;

    void clear();
    void addItem (const juce::String& text, int itemId);
    int getNumItems() const { return (int) items.size(); }

    void setSelectedId (int itemId, juce::NotificationType notification = juce::dontSendNotification);
    int getSelectedId() const { return currentId; }
    int getSelectedItemIndex() const;
    juce::String getText() const { return currentText; }

    std::function<void()> onConfirm;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void focusGained (FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Item { juce::String text; int id; };

    std::vector<Item> items;
    int currentId = 0;
    juce::String currentText;

    bool dropdownOpen = false;
    int browseIndex = -1;

    class DropdownList : public juce::ListBox, public juce::ListBoxModel
    {
    public:
        AccessibleComboDropdown* owner = nullptr;
        DropdownList();
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
        juce::String getNameForRow (int row) override;
        bool keyPressed (const juce::KeyPress& key) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    };

    DropdownList dropdownList;

    void openDropdown();
    void closeDropdown (bool confirm);
    void announceItem (int index);

public:
    static void screenReaderAnnounce (const juce::String& text);
    static void screenReaderAnnounceNow (const juce::String& text);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleComboDropdown)
};
