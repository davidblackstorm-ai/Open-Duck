#pragma once
#include <JuceHeader.h>

namespace odx::ui4 {
// Relative vertical input avoids the diagonal acceleration of XY dragging.
// The slider value remains the sole source for the arc, pointer and readout.
class DenseRotarySlider final : public juce::Slider {
public:
    static constexpr double fullRangePixels = 1200.0;
    DenseRotarySlider() {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setMouseDragSensitivity(int(fullRangePixels));
        setVelocityBasedMode(false);
        setVelocityModeParameters(1.0, 1, 0.0, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragging = isEnabled() && e.mods.isLeftButtonDown() && !e.mods.isPopupMenu();
        previousY = e.position.y;
        juce::Slider::mouseDown(e); // Owns the host/Undo begin gesture.
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging || !isEnabled()) return;
        const double delta = double(previousY) - double(e.position.y);
        previousY = e.position.y;
        if (delta == 0.0) return;
        const double precision = e.mods.isShiftDown() ? 0.1 : 1.0;
        const double at = valueToProportionOfLength(getValue());
        const double next = juce::jlimit(0.0, 1.0, at + delta * precision / fullRangePixels);
        setValue(proportionOfLengthToValue(next), juce::sendNotificationSync);
        if(isShowing())e.source.enableUnboundedMouseMovement(true, false);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        dragging = false;
        juce::Slider::mouseUp(e); // Ends exactly the same host/Undo gesture.
    }

    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override {
        // Shift may change precision mid-gesture without restoring/warping
        // the unbounded pointer or resetting its relative origin.
        if(!dragging)juce::Slider::modifierKeysChanged(modifiers);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        auto adjusted = wheel;
        const float precision = e.mods.isShiftDown() ? 0.01f : 0.1f;
        adjusted.deltaX *= precision;
        adjusted.deltaY *= precision;
        juce::Slider::mouseWheelMove(e, adjusted);
    }

private:
    bool dragging = false;
    float previousY = 0;
};
}
