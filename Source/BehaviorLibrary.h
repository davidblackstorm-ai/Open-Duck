#pragma once
#include <JuceHeader.h>
#include <array>

namespace odx
{
struct CurvePoint
{
    float x = 0, y = 0;
    // Type belongs to the segment beginning at this point. Old two-field aggregates remain valid.
    int segment = 0;
    float curvature = 0;
    bool reversed = false;
};
struct CurveSnapshot
{
    std::array<CurvePoint, 256> points{};
    int count = 0, shape = 0, grid = 0, presetId = 0;
    float phase = 0, length = 1, tension = 0, amount = 100, attack = 10, release = 150;
    float focus = 160, riseSlope = 1;
    bool custom = false, linear = false, edit = false, freeDraw = false, band = false, legacy = false;
    bool modern3 = true;
    bool template4 = false;
    float position4 = 50, duration4 = 0;
    // Separate curve-edit coordinates. MIX/PHASE retain their published laws.
    float moveCurve4 = 0, slope4 = 0;
    // Legacy snapshots omitted MIX depth and therefore retain full processing.
    float duckMix4 = 100;
    int profileModel4 = 0;
    bool mirrorH4 = false, mirrorV4 = false;
};
struct BehaviorPreset
{
    int id = 0, category = 0, shape = 0, grid = 0;
    float phase = 0, length = 1, tension = 0, amount = 100, mix = 80, attack = 10, release = 150;
    juce::String name;
    int mode = -1;
    float character = 0, low = 1, mid = 1, high = 1;
};
inline const std::array<const char *, 7> &categoryNames()
{
    static const std::array<const char *, 7> names{{"Smooth Pump", "Punch & Kick", "Bass & Sub",
                                                    "Gates & Chops", "Reverse & Swell", "Groove & Rhythm",
                                                    "Creative Motion"}};
    return names;
}
inline const std::array<const char *, 16> &shapeNames()
{
    static const std::array<const char *, 16> names{
        {"Soft Duck", "Fast Rise", "Late Rise", "S Rise", "Short Hold", "Long Hold", "Hard Gate",
         "Linear Rise", "Sub Smooth", "Sub Long", "Short Trim", "Soft Trim", "Reverse Duck", "Reverse Soft",
         "Double Pump", "Triple Pump"}};
    return names;
}
inline const std::array<BehaviorPreset, 700> &factoryPresets()
{
    static const auto presets = []
    {
        std::array<BehaviorPreset, 700> result{};
        constexpr int shapes[7][5] = {{0, 1, 2, 3, 7},   {1, 4, 6, 10, 11},   {8, 9, 3, 0, 5},
                                      {6, 4, 5, 7, 2},   {12, 13, 10, 11, 7}, {14, 15, 0, 4, 12},
                                      {13, 14, 15, 9, 6}};
        constexpr int grids[7] = {0, 1, 0, 2, 5, 3, 1};
        constexpr float phases[5] = {-20, -10, 0, 10, 20}, lengths[4] = {.45f, .65f, .85f, 1};
        constexpr float amounts[4] = {65, 78, 90, 100}, attacks[5] = {2, 5, 10, 20, 35};
        const char *timings[5] = {"Early 20", "Early 10", "On Beat", "Late 10", "Late 20"};
        const char *lengthNames[4] = {"Tight", "Short", "Open", "Full"};
        for (int c = 0; c < 7; ++c)
            for (int s = 0; s < 5; ++s)
                for (int p = 0; p < 5; ++p)
                    for (int l = 0; l < 4; ++l)
                    {
                        const int id = c * 100 + s * 20 + p * 4 + l;
                        auto &out = result[(size_t)id];
                        out.id = id;
                        out.category = c;
                        out.shape = shapes[c][s];
                        out.grid = grids[c];
                        out.phase = phases[p];
                        out.length = lengths[l];
                        out.tension = (float)(-60 + c * 20);
                        out.amount = amounts[l];
                        out.attack = attacks[s];
                        out.release = std::round(80 + lengths[l] * 420 + (float)c * 30);
                        out.name = juce::String(shapeNames()[(size_t)out.shape]) + " / " + timings[p] +
                                   " / " + lengthNames[l];
                    }
        auto &init = result[0];
        init.name = "INIT / Soft Duck";
        init.shape = 0;
        init.phase = 0;
        init.length = 1;
        init.tension = 0;
        init.amount = 100;
        init.attack = 10;
        init.release = 150;
        init.grid = 0;
        return result;
    }();
    return presets;
}
inline float clamp(float x)
{
    return juce::jlimit(0.0f, 1.0f, x);
}
inline float smooth(float x)
{
    x = clamp(x);
    return x * x * (3 - 2 * x);
}
inline float factoryShape(float t, int type)
{
    t = clamp(t);
    switch (type)
    {
    case 0:
        return t < .14f ? .1f * (1 - t / .14f) : smooth((t - .14f) / .43f);
    case 1:
        return smooth((t - .05f) / .18f);
    case 2:
        return smooth((t - .52f) / .32f);
    case 3:
        return smooth((t - .09f) / .8f);
    case 4:
        return smooth((t - .22f) / .18f);
    case 5:
        return smooth((t - .64f) / .2f);
    case 6:
        return smooth((t - .45f) / .025f);
    case 7:
        return clamp((t - .15f) / .65f);
    case 8:
        return std::pow(smooth(t / .8f), 1.8f);
    case 9:
        return std::pow(smooth((t - .1f) / .87f), 2.6f);
    case 10:
        return 1 - smooth((t - .12f) / .25f);
    case 11:
        return 1 - smooth((t - .18f) / .6f);
    case 12:
        return 1 - smooth((t - .2f) / .5f);
    case 13:
        return std::pow(juce::jmax(0.0f, std::sin(juce::MathConstants<float>::pi * t)), .8f);
    case 14:
        return smooth((std::fmod(t * 2, 1.0f) - .12f) / .6f);
    case 15:
        return smooth((std::fmod(t * 3, 1.0f) - .12f) / .6f);
    default:
        return smooth(t);
    }
}
} // namespace odx
