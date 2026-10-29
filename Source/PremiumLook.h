#pragma once
#include <JuceHeader.h>
#include <functional>

namespace premium
{
inline juce::Colour cyan() { return juce::Colour(0xff00ddff); }
inline juce::Colour modeColour(int i)
{
    static constexpr juce::uint32 colours[] = {0xff00ddff, 0xffc255ff, 0xff00e7a3,
                                               0xffff37d4, 0xffffad37, 0xff428dff};
    return juce::Colour(colours[juce::jlimit(0, 5, i)]);
}
inline juce::Font font(float height, bool bold = true)
{
    return juce::Font(juce::FontOptions("Bahnschrift SemiCondensed", height,
                                       bold ? juce::Font::bold : juce::Font::plain));
}
inline void panel(juce::Graphics &g, juce::Rectangle<float> r, float radius, bool metal = false)
{
    juce::ColourGradient fill(metal ? juce::Colour(0xff354551) : juce::Colour(0xff102630),
                               r.getTopLeft(), juce::Colour(0xff030c14), r.getBottomRight(), false);
    fill.addColour(0.30, metal ? juce::Colour(0xff182630) : juce::Colour(0xff07151f));
    fill.addColour(0.80, metal ? juce::Colour(0xff293b48) : juce::Colour(0xff0d202c));
    g.setGradientFill(fill);
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colour(0xff020508));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 2.0f);
    g.setColour(juce::Colour(0xff738b99));
    g.drawRoundedRectangle(r.reduced(2), juce::jmax(1.0f, radius - 2), 1);
    g.setColour(juce::Colour(0xffcce8f3).withAlpha(0.17f));
    g.drawRoundedRectangle(r.reduced(3.5f), juce::jmax(1.0f, radius - 3), 0.7f);
    g.setColour(juce::Colour(0xff03101b));
    g.drawRoundedRectangle(r.reduced(5), juce::jmax(1.0f, radius - 4), 1);
}
inline void lcd(juce::Graphics &g, juce::Rectangle<float> r, float radius = 5)
{
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff102b3a), r.getTopLeft(),
                                           juce::Colour(0xff02070d), r.getBottomRight(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colour(0xff8bb2c5));
    g.drawRoundedRectangle(r.reduced(0.6f), radius, 1.1f);
    g.setColour(juce::Colour(0xff000207));
    g.drawRoundedRectangle(r.reduced(2), juce::jmax(1.0f, radius - 1), 1.1f);
    g.setColour(juce::Colour(0xffaeebff).withAlpha(0.10f));
    g.drawHorizontalLine(juce::roundToInt(r.getBottom() - 2), r.getX() + radius, r.getRight() - radius);
}
inline void led(juce::Graphics &g, juce::Point<float> centre, float radius, juce::Colour c, bool on)
{
    if (on)
    {
        for (int i = 3; i > 0; --i)
        {
            auto diameter = radius * 2 + i * 2.2f;
            g.setColour(c.withAlpha(0.035f * (4 - i)));
            g.fillEllipse(centre.x - diameter / 2, centre.y - diameter / 2, diameter, diameter);
        }
    }
    g.setColour(on ? c : juce::Colour(0xff263942));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    if (on)
    {
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(centre.x - radius * .35f, centre.y - radius * .65f, radius * .7f, radius * .7f);
    }
}
}

class PremiumLook final : public juce::LookAndFeel_V4
{
  public:
    float scale = 1.0f;
    std::function<float(float, int)> shapeSample;
    PremiumLook()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff071722));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe1f4fc));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff07141e));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffdceef8));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff16465d));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd4edf8));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff04101a));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff4d7187));
        setColour(juce::Slider::thumbColourId, premium::cyan());
        setColour(juce::Slider::trackColourId, premium::cyan().withAlpha(.65f));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff132b3a));
    }
    void drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1);
        const auto type = b.getProperties()["premiumType"].toString();
        if (type == "mode")
        {
            const int mode = (int)b.getProperties()["modeIndex"];
            const auto c = premium::modeColour(mode);
            premium::panel(g, r, 10 * scale);
            g.setColour(c.withAlpha(b.getToggleState() ? .94f : .25f));
            g.drawRoundedRectangle(r.reduced(3 * scale), 8 * scale, b.getToggleState() ? 1.8f * scale : .8f);
            auto strip = r.withTrimmedLeft(12 * scale).withTrimmedRight(12 * scale)
                             .withHeight(2.3f * scale).withY(r.getBottom() - 8 * scale);
            g.setColour(c.withAlpha(b.getToggleState() ? .12f : .035f));
            g.fillRoundedRectangle(strip.expanded(2 * scale), 3 * scale);
            g.setColour(c.withAlpha(b.getToggleState() ? 1.0f : .55f));
            g.fillRoundedRectangle(strip, scale);
            auto icon = r.reduced(25 * scale, 44 * scale).translated(0, -3 * scale);
            drawModeIcon(g, icon, mode, c);
            return;
        }
        const bool active = b.getToggleState();
        premium::lcd(g, r, 5 * scale);
        if (over || down || active)
        {
            g.setColour(premium::cyan().withAlpha(active ? .15f : down ? .2f : .07f));
            g.fillRoundedRectangle(r.reduced(2), 4 * scale);
            if (active)
            {
                g.setColour(premium::cyan());
                g.drawRoundedRectangle(r.reduced(1.5f), 4 * scale, 1.5f * scale);
            }
        }
        if (type == "shape" && shapeSample)
        {
            const int index = (int)b.getProperties()["shapeIndex"];
            auto area = r.reduced(6 * scale, 4 * scale);
            juce::Path path;
            for (int i = 0; i <= 64; ++i)
            {
                const float t = i / 64.0f, x = area.getX() + t * area.getWidth();
                const float y = area.getBottom() - shapeSample(t, index) * area.getHeight();
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
            g.setColour(active ? premium::cyan() : juce::Colour(0xffb5cfdc));
            g.strokePath(path, juce::PathStrokeType(juce::jmax(1.0f, 1.4f * scale)));
        }
    }
    void drawButtonText(juce::Graphics &g, juce::TextButton &b, bool, bool) override
    {
        const auto type = b.getProperties()["premiumType"].toString();
        if (type == "shape") return;
        auto r = b.getLocalBounds().toFloat().reduced(6 * scale, 2 * scale);
        if (type == "mode")
        {
            g.setColour(premium::modeColour((int)b.getProperties()["modeIndex"]));
            g.setFont(premium::font(juce::jmax(10.0f, 17.0f * scale)));
            g.drawFittedText(b.getButtonText(), r.withTrimmedTop(r.getHeight() - 36 * scale)
                                                 .withTrimmedBottom(7 * scale).toNearestInt(),
                            juce::Justification::centred, 1, .85f);
            return;
        }
        g.setColour(b.isEnabled() ? b.getToggleState() ? juce::Colour(0xffb9f7ff) : juce::Colour(0xffe3f2fa)
                                  : juce::Colour(0xff516574));
        g.setFont(premium::font(juce::jmax(10.0f, (type == "small" ? 12.0f : 17.0f) * scale)));
        g.drawFittedText(b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1, .85f);
    }
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        if (b.getName() == "hostSync")
        {
            premium::led(g, {r.getX() + 13 * scale, r.getCentreY()}, 3.5f * scale, premium::cyan(), b.getToggleState());
            g.setColour(b.getToggleState() ? juce::Colour(0xffe0f5ff) : juce::Colour(0xff8aa5b7));
            g.setFont(premium::font(juce::jmax(10.0f, 16 * scale)));
            g.drawText("HOST SYNC", r.withTrimmedLeft(28 * scale), juce::Justification::centredLeft);
            return;
        }
        if (b.getProperties().contains("engineIndex"))
        {
            auto c = premium::modeColour((int)b.getProperties()["engineIndex"]);
            premium::led(g, {r.getX() + 8 * scale, r.getCentreY()}, 3.4f * scale, c, b.getToggleState());
            g.setColour(b.getToggleState() ? juce::Colour(0xffd8effa) : juce::Colour(0xffb2c6d1));
            g.setFont(premium::font(juce::jmax(10.0f, 14.0f * scale)));
            g.drawText(b.getToggleState() ? "ON" : "OFF", r.withTrimmedLeft(17 * scale), juce::Justification::centredLeft);
            return;
        }
        premium::lcd(g, r.reduced(1), 3 * scale);
        g.setColour(b.getToggleState() ? premium::cyan() : juce::Colour(0xff91adbf));
        g.setFont(premium::font(juce::jmax(10.0f, 13 * scale)));
        g.drawFittedText(b.getButtonText(), r.reduced(2).toNearestInt(), juce::Justification::centred, 1);
    }
    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float position,
                          float start, float end, juce::Slider &) override
    {
        const float d = (float)juce::jmin(width, height) - 19 * scale;
        if (d < 5) return;
        const auto centre = juce::Point<float>((float)x + width / 2.0f, (float)y + height / 2.0f);
        auto ellipse = juce::Rectangle<float>(d, d).withCentre(centre);
        g.setColour(juce::Colour(0xff000408)); g.fillEllipse(ellipse.expanded(5 * scale));
        for (int tick = 0; tick <= 40; ++tick)
        {
            const float a = start + (end - start) * tick / 40;
            auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
            g.setColour(juce::Colour(0xff83c0dd).withAlpha(tick / 40.0f <= position ? .7f : .25f));
            g.drawLine({centre + dir * (d / 2 + 4 * scale), centre + dir * (d / 2 + 8 * scale)}, juce::jmax(.7f, scale));
        }
        juce::Path arc; arc.addCentredArc(centre.x, centre.y, d / 2, d / 2, 0, start, end, true);
        g.setColour(juce::Colour(0xff153644)); g.strokePath(arc, juce::PathStrokeType(5 * scale));
        juce::Path active; active.addCentredArc(centre.x, centre.y, d / 2, d / 2, 0, start, start + position * (end - start), true);
        g.setColour(premium::cyan().withAlpha(.12f)); g.strokePath(active, juce::PathStrokeType(10 * scale));
        g.setColour(premium::cyan()); g.strokePath(active, juce::PathStrokeType(3.3f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto face = ellipse.reduced(9 * scale);
        g.setColour(juce::Colour(0xffb3c2cc)); g.fillEllipse(face.expanded(2 * scale));
        g.setColour(juce::Colour(0xff17212a)); g.fillEllipse(face.expanded(scale));
        const auto fc = face.getCentre(); const float radius = face.getWidth() / 2;
        for (int sector = 0; sector < 128; ++sector)
        {
            const float a = sector * juce::MathConstants<float>::twoPi / 128;
            const float b = (sector + 1.2f) * juce::MathConstants<float>::twoPi / 128;
            float lum = .57f + .25f * std::cos(a * 2 + .4f) + .10f * std::cos(a * 6 - .2f);
            juce::Path wedge; wedge.startNewSubPath(fc); wedge.lineTo(fc.x + std::sin(a) * radius, fc.y - std::cos(a) * radius);
            wedge.lineTo(fc.x + std::sin(b) * radius, fc.y - std::cos(b) * radius); wedge.closeSubPath();
            g.setColour(juce::Colour::fromFloatRGBA(lum, lum * 1.015f, juce::jmin(1.0f, lum * 1.035f), 1)); g.fillPath(wedge);
        }
        g.setColour(juce::Colour(0xffe2eaf0).withAlpha(.65f)); g.drawEllipse(face.reduced(scale), .9f * scale);
        g.setColour(juce::Colour(0xff07111b).withAlpha(.65f)); g.drawEllipse(face.reduced(3 * scale), .7f * scale);
        const float a = start + position * (end - start);
        const auto direction = juce::Point<float>(std::sin(a), -std::cos(a));
        g.setColour(juce::Colour(0xff13202c)); g.drawLine({centre + direction * (radius * .65f), centre + direction * (radius * .9f)}, 3.5f * scale);
        g.setColour(juce::Colours::white); g.drawLine({centre + direction * (radius * .67f), centre + direction * (radius * .89f)}, 1.8f * scale);
    }
    void drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &) override
    {
        premium::lcd(g, {0, 0, (float)w, (float)h}, 5 * scale);
        juce::Path arrow; const float cx = w - 17 * scale, cy = h / 2.0f;
        arrow.startNewSubPath(cx - 5 * scale, cy - 2 * scale); arrow.lineTo(cx, cy + 3 * scale); arrow.lineTo(cx + 5 * scale, cy - 2 * scale);
        g.setColour(premium::cyan()); g.strokePath(arrow, juce::PathStrokeType(2 * scale));
    }
    juce::Font getComboBoxFont(juce::ComboBox &) override { return premium::font(juce::jmax(11.0f, 20 * scale)); }
    juce::Font getPopupMenuFont() override { return premium::font(juce::jmax(13.0f, 18 * scale)); }
    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        label.setBounds(juce::roundToInt(10 * scale), 1, box.getWidth() - juce::roundToInt(40 * scale), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
  private:
    void drawModeIcon(juce::Graphics &g, juce::Rectangle<float> r, int mode, juce::Colour c)
    {
        if (r.getWidth() < 2 || r.getHeight() < 2) return;
        g.setColour(c);
        if (mode == 1 || mode == 2 || mode == 4)
        {
            if (mode == 2)
                for (int row = 0; row < 3; ++row) g.drawRect(r.withHeight(r.getHeight() / 4).withY(r.getY() + row * r.getHeight() * .36f), 2.4f * scale);
            else
                for (int i = 0; i < 5; ++i)
                {
                    auto col = r.withWidth(r.getWidth() / 7).withX(r.getX() + i * r.getWidth() / 5);
                    const float amount = mode == 1 ? .80f : .35f + (i % 3) * .24f;
                    g.drawRect(col.withTop(col.getBottom() - r.getHeight() * amount), 2.0f * scale);
                }
            return;
        }
        for (int line = 0; line < (mode == 3 ? 2 : 1); ++line)
        {
            juce::Path path;
            for (int k = 0; k <= 64; ++k)
            {
                const float t = k / 64.0f;
                const float v = mode == 5 ? std::sin(t * 6.28f * 3) * std::exp(-std::abs(t - .5f) * 8)
                                : std::sin(t * 6.28f) * (mode == 0 ? std::exp(-std::abs(t - .5f) * 3) : .7f);
                const float yy = r.getCentreY() - v * r.getHeight() * .43f + (line ? 7 * scale : 0);
                if (k == 0) path.startNewSubPath(r.getX(), yy); else path.lineTo(r.getX() + t * r.getWidth(), yy);
            }
            g.strokePath(path, juce::PathStrokeType(3 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
};
