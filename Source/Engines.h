#pragma once
#include <JuceHeader.h>
// Causal 1024-point periodic-Hann WOLA, hop 256. Fixed 1024-sample latency.
// The sum of four squared windows is 1.5; no magnitude-only reconstruction.
class SpectralEngine
{
  public:
    struct FocusMask
    {
        float depth = 0, cutoff = 160;
        bool sidechainWeighted = false;
    };
    static constexpr int size = 1024, hop = 256, ringSize = 4096;
    SpectralEngine() : fft(10)
    {
        for (int i = 0; i < size; ++i)
            window[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / size);
        reset();
    }
    void reset()
    {
        for (auto &a : input)
            a.fill(0);
        for (auto &a : output)
            a.fill(0);
        side.fill(0);
        sideRight.fill(0);
        sideDifferences = 0;
        cursor = 0;
    }
    void process(const float *x, float sc, float *y, int channels, float depth, float low, float high,
                 double rate, const std::array<FocusMask, 6> *focused = nullptr, bool fullDepth = false,
                 const std::array<float,3>* bandGains = nullptr, float crossLow = 160, float crossHigh = 2500,
                 const float* stereoKey = nullptr)
    {
        const auto slot = (size_t)(cursor % ringSize);
        for (int c = 0; c < channels; ++c)
        {
            y[c] = output[(size_t)c][slot];
            output[(size_t)c][slot] = 0;
            input[(size_t)c][(size_t)(cursor % size)] = x[c];
        }
        const auto sideSlot = (size_t)(cursor % size);
        if (side[sideSlot] != sideRight[sideSlot]) --sideDifferences;
        side[sideSlot] = stereoKey != nullptr ? stereoKey[0] : sc;
        sideRight[sideSlot] = stereoKey != nullptr ? stereoKey[1] : sc;
        if (side[sideSlot] != sideRight[sideSlot]) ++sideDifferences;
        if ((cursor + 1) % hop == 0)
        {
            sideFFT.fill(0);
            for (int k = 0; k < size; ++k)
                sideFFT[(size_t)k] = side[(size_t)((cursor + 1 + k) % size)] * window[(size_t)k];
            fft.performRealOnlyForwardTransform(sideFFT.data(), true);
            // Combining stereo samples by their instantaneous maximum creates
            // intermodulation in the detector. Combine spectral power instead.
            // Identical channels retain the exact mono FFT and normalization.
            if (sideDifferences != 0)
            {
                sideRightFFT.fill(0);
                for (int k = 0; k < size; ++k)
                    sideRightFFT[(size_t)k] = sideRight[(size_t)((cursor + 1 + k) % size)] * window[(size_t)k];
                fft.performRealOnlyForwardTransform(sideRightFFT.data(), true);
            }
            float maxSide = 0;
            for (int b = 0; b <= size / 2; ++b)
            {
                const float left = std::hypot(sideFFT[(size_t)(b * 2)], sideFFT[(size_t)(b * 2 + 1)]);
                const float right = sideDifferences != 0
                    ? std::hypot(sideRightFFT[(size_t)(b * 2)], sideRightFFT[(size_t)(b * 2 + 1)]) : left;
                sideMagnitude[(size_t)b] = sideDifferences == 0 ? left
                    : std::hypot(left, right) * 0.7071067811865475f;
                maxSide = juce::jmax(maxSide, sideMagnitude[(size_t)b]);
            }
            for (int c = 0; c < channels; ++c)
            {
                work.fill(0);
                for (int k = 0; k < size; ++k)
                    work[(size_t)k] = input[(size_t)c][(size_t)((cursor + 1 + k) % size)] * window[(size_t)k];
                fft.performRealOnlyForwardTransform(work.data(), true);
                for (int b = 0; b <= size / 2; ++b)
                {
                    float hz = (float)(b * rate / size);
                    float edge =
                        juce::jmin(1.0f, juce::jmax(0.0f, (hz - low) / juce::jmax(20.0f, low * 0.25f))) *
                        juce::jmin(1.0f, juce::jmax(0.0f, (high - hz) / juce::jmax(50.0f, high * 0.1f)));
                    float mask =
                        maxSide > 1e-7f
                            ? std::sqrt(sideMagnitude[(size_t)b] / maxSide)
                            : 1;
                    const float depthLimit = fullDepth ? 1.0f : .95f;
                    float gain = 1 - juce::jlimit(0.0f, depthLimit, depth) * edge * mask;
                    if(bandGains){
                        auto transition=[&](float cutoff){const float width=juce::jmax(float(rate/size),cutoff*.08f);
                            float u=juce::jlimit(0.f,1.f,(hz-cutoff+width)/(2*width));return u*u*(3-2*u);};
                        const float a=transition(crossLow),b=transition(crossHigh);
                        const float curve=(*bandGains)[0]*(1-a)+(*bandGains)[1]*(a-b)+(*bandGains)[2]*b;
                        gain=juce::jmin(gain,1-(1-juce::jlimit(0.f,1.f,curve))*edge*mask);
                    }
                    if (focused != nullptr)
                        for (const auto &focus : *focused)
                            if (focus.depth > 0)
                            {
                                // Frequency resolution is rate/1024; use a smooth two-bin edge.
                                const float width = juce::jmax((float)(rate / size), focus.cutoff * .08f);
                                float u =
                                    juce::jlimit(0.0f, 1.0f, (hz - (focus.cutoff - width)) / (2 * width));
                                u = u * u * (3 - 2 * u);
                                gain = juce::jmin(gain, 1 - juce::jlimit(0.0f, depthLimit, focus.depth) * (1 - u) *
                                                                (focus.sidechainWeighted ? mask : 1));
                            }
                    work[(size_t)(b * 2)] *= gain;
                    work[(size_t)(b * 2 + 1)] *= gain;
                }
                fft.performRealOnlyInverseTransform(work.data());
                for (int k = 0; k < size; ++k)
                    output[(size_t)c][(size_t)((cursor + 1 + k) % ringSize)] +=
                        work[(size_t)k] * window[(size_t)k] * (2.0f / 3.0f);
            }
        }
        ++cursor;
    }

  private:
    juce::dsp::FFT fft;
    uint64_t cursor = 0;
    std::array<float, size> window{}, side{}, sideRight{};
    std::array<float, size * 2> work{}, sideFFT{}, sideRightFFT{};
    std::array<float, size / 2 + 1> sideMagnitude{};
    int sideDifferences = 0;
    std::array<std::array<float, size>, 2> input{};
    std::array<std::array<float, ringSize>, 2> output{};
};

// Complementary linear-phase windowed-sinc FIR crossover. All three bands sum
// exactly to a 128-sample delay, including when the cutoff is automated.
class BandEngine
{
  public:
    static constexpr int taps = 257, delay = 128;
    void setSlope(int orderChoice) {
        orderChoice=juce::jlimit(-1,2,orderChoice);
        if(orderChoice!=slopeChoice){slopeChoice=orderChoice;lastLow=lastHigh=-1;}
    }
    void prepare(double sr)
    {
        rate = sr;
        for (auto &a : history)
            a.fill(0);
        cursor = 0;
        lastLow = lastHigh = -1;
        first = true;
        transition = 0;
        setCutoffs(180, 2500);
    }
    void setCutoffs(float low, float high)
    {
        if (low == lastLow && high == lastHigh)
            return;
        lastLow = low;
        lastHigh = high;
        design(low, target1);
        design(high, target2);
        if (first)
        {
            lp1 = target1;
            lp2 = target2;
            first = false;
        }
        else
        {
            transition = juce::jmax(1, (int)(rate * 0.02));
            for (int k = 0; k < taps; ++k)
            {
                delta1[(size_t)k] = (target1[(size_t)k] - lp1[(size_t)k]) / transition;
                delta2[(size_t)k] = (target2[(size_t)k] - lp2[(size_t)k]) / transition;
            }
        }
    }
    float process(int ch, float x, const std::array<float, 3> &gains, float &unattenuated)
    {
        auto &data = history[(size_t)ch];
        data[(size_t)cursor] = x;
        if(slopeChoice>=0&&gains[0]==gains[1]&&gains[1]==gains[2]){
            unattenuated=data[(size_t)((cursor+taps-delay)%taps)];
            return unattenuated*gains[0];
        }
        float lo = 0, belowHigh = 0;
        for (int k = 0; k < taps; ++k)
        {
            float sample = data[(size_t)((cursor + taps - k) % taps)];
            lo += sample * lp1[(size_t)k];
            belowHigh += sample * lp2[(size_t)k];
        }
        unattenuated = data[(size_t)((cursor + taps - delay) % taps)];
        float result = lo * gains[0] + (belowHigh - lo) * gains[1] + (unattenuated - belowHigh) * gains[2];
        return result;
    }
    void advance()
    {
        cursor = (cursor + 1) % taps;
        if (transition > 0)
        {
            for (int k = 0; k < taps; ++k)
            {
                lp1[(size_t)k] += delta1[(size_t)k];
                lp2[(size_t)k] += delta2[(size_t)k];
            }
            if (--transition == 0)
            {
                lp1 = target1;
                lp2 = target2;
            }
        }
    }

  private:
    void design(float hz, std::array<float, taps> &coeff)
    {
        if(slopeChoice>=0){
            // Original FIR stays untouched for old states. Revision 3 samples a Butterworth
            // magnitude of order 2/4/8 and imposes linear phase with the same 128-sample delay.
            constexpr int bins=512;
            using Cosines=std::array<std::array<float,bins+1>,taps>;
            // Prepared on first revision-3 prepareToPlay, never lazily from an audio callback.
            static const auto cosines=[=](){auto table=std::make_unique<Cosines>();
                for(int k=0;k<taps;++k)for(int b=0;b<=bins;++b)
                    (*table)[(size_t)k][(size_t)b]=float(std::cos(juce::MathConstants<double>::pi*(k-delay)*b/bins));
                return table;}();
            std::array<double,bins+1> response{};
            const int order=2<<slopeChoice;
            for(int b=0;b<=bins;++b){const double ratio=(rate*.5*b/bins)/juce::jmax(1.f,hz);
                response[(size_t)b]=1/std::sqrt(1+std::pow(ratio,order*2));}
            double sum=0;
            for(int k=0;k<taps;++k){const int n=k-delay;double value=response[0]+response[bins]*std::cos(juce::MathConstants<double>::pi*n);
                for(int b=1;b<bins;++b)value+=2*response[(size_t)b]*(*cosines)[(size_t)k][(size_t)b];
                const double window=.42-.5*std::cos(2*juce::MathConstants<double>::pi*k/(taps-1))+.08*std::cos(4*juce::MathConstants<double>::pi*k/(taps-1));
                coeff[(size_t)k]=float(value/(bins*2)*window);sum+=coeff[(size_t)k];}
            if(std::abs(sum)>1e-12)for(auto&c:coeff)c=float(c/sum);
            return;
        }
        float sum = 0;
        double fc = hz / rate;
        for (int k = 0; k < taps; ++k)
        {
            int n = k - delay;
            double sinc = n == 0 ? 2 * fc
                                 : std::sin(juce::MathConstants<double>::twoPi * fc * n) /
                                       (juce::MathConstants<double>::pi * n);
            double w = 0.42 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * k / (taps - 1)) +
                       0.08 * std::cos(2 * juce::MathConstants<double>::twoPi * k / (taps - 1));
            coeff[(size_t)k] = (float)(sinc * w);
            sum += coeff[(size_t)k];
        }
        for (auto &c : coeff)
            c /= sum;
    }
    double rate = 48000;
    float lastLow = -1, lastHigh = -1;
    int cursor = 0, transition = 0;
    bool first = true;
    int slopeChoice = -1;
    std::array<float, taps> lp1{}, lp2{}, target1{}, target2{}, delta1{}, delta2{};
    std::array<std::array<float, taps>, 2> history{};
};
