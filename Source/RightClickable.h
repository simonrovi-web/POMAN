#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Adds a right-click (popup-menu gesture) callback to a JUCE control without the control also
// reacting to that click: a rotary would otherwise start dragging, a button would toggle.
template <typename Base>
class RightClickable : public Base
{
public:
    using Base::Base;

    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick)
        {
            onRightClick();
            return;
        }
        Base::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onRightClick))
            Base::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onRightClick))
            Base::mouseUp (e);
    }
};

using MidiSlider = RightClickable<juce::Slider>;
using MidiToggle = RightClickable<juce::ToggleButton>;
using MidiTextButton = RightClickable<juce::TextButton>;
