/*
    Convo — a convolution audio effect plugin
    Copyright (C) 2026 mvzn

    This program is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option) any
    later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License along
    with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <JuceHeader.h>

/**
    Convo's visual theme: a black / dark gunmetal structure with a phthalo-green
    accent, an analogous mint+teal family for highlights, and a single
    complementary copper used sparingly (the bypass LED) so green = active and
    copper = bypassed read instantly.
*/
namespace ConvoColours
{
    inline const juce::Colour bg          { 0xff0e1316 }; // window background (near-black gunmetal)
    inline const juce::Colour panel       { 0xff161c20 }; // drop zone / panels
    inline const juce::Colour panelRaised { 0xff1e262b }; // buttons, raised surfaces
    inline const juce::Colour knobBody    { 0xff222c31 }; // rotary body
    inline const juce::Colour border      { 0xff2c373d }; // edges / outlines
    inline const juce::Colour gunmetalHi  { 0xff3a474e }; // subtle highlight gunmetal

    inline const juce::Colour accent      { 0xff0e6b52 }; // phthalo green — primary accent
    inline const juce::Colour accentDeep  { 0xff073a2d }; // pressed / shadow phthalo
    inline const juce::Colour arcTrack    { 0xff0b2a22 }; // unfilled rotary arc
    inline const juce::Colour mint         { 0xff36c9a0 }; // bright highlight / indicator / waveform
    inline const juce::Colour teal         { 0xff14a6a0 }; // analogous secondary
    inline const juce::Colour copper       { 0xffc2703d }; // complementary warm accent (bypass)
    inline const juce::Colour clip         { 0xffe5484d }; // meter clip

    inline const juce::Colour text        { 0xffd7e0dc }; // primary text (faint green tint)
    inline const juce::Colour textDim     { 0xff8a9a94 }; // dim text
    inline const juce::Colour label       { 0xff9fb0aa }; // knob labels
}

class ConvoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ConvoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoLookAndFeel)
};
