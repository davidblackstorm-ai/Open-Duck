#include "LocalAssist.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace allocationFault
{
std::atomic<bool> armed{false};
std::thread::id callingThread;
}
// Test-owned allocator fault: exactly one sufficiently large allocation on the
// calling thread. Other threads/allocations use malloc normally; no RAM pressure.
void* operator new(std::size_t size)
{
    if (size >= 160000 && allocationFault::armed.load() && std::this_thread::get_id() == allocationFault::callingThread)
    {
        allocationFault::armed = false; throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

// Deliberately throwing MOCK evaluator. This tests exception containment and
// six-slot scheduling, not model quality or real DSP preservation scores.
// Each failure type runs in a separate test-owned child process. The terminate
// handler exits 86, preventing a crash dialog or a hang on the pre-fix source.
namespace
{
using namespace odx::ai;
constexpr int sr = 8000, samples = sr * 5;
int passed = 0, failed = 0;
void check(bool ok, const char* label)
{
    std::cout << (ok ? "PASS " : "FAIL ") << label << '\n'; ok ? ++passed : ++failed;
}
bool wait(Controller& controller, int mode)
{
    for (int i = 0; i < 2000; ++i)
    {
        const auto state = controller.status(mode).state;
        if (state != State::capturing && state != State::analyzing) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
struct Audio
{
    std::vector<float> main = std::vector<float>((size_t)samples), side = main;
    Audio()
    {
        for (int i = 0; i < samples; ++i)
        {
            const double time = i / (double)sr, local = std::fmod(time, .5);
            main[(size_t)i] = .2f * (float)std::sin(2 * juce::MathConstants<double>::pi * 73 * time);
            side[(size_t)i] = .6f * (float)(std::sin(2 * juce::MathConstants<double>::pi * 80 * local) * std::exp(-local / .035));
        }
    }
    void push(Controller& controller) const { controller.push(main.data(), main.data(), side.data(), side.data(), samples, 0, 120); }
};
int child(const std::string& kind)
{
    std::set_terminate([] { std::cerr << "PRE-FIX UNCAUGHT WORKER EXCEPTION: terminate\n"; std::_Exit(86); });
    Controller controller; controller.prepare(sr); Audio audio;
    Context context; context.sampleRate = sr; context.trigger = 1; context.bpm = 120; context.revision = 31;
    std::atomic<bool> throwing{false}, entered{false}, release{false};
    controller.setEvaluator([&](int mode, const Candidate& candidate, const CaptureView&, const Context& frozen,
                               Metrics& metric, const std::atomic<bool>&)
    {
        if (mode == 0 && throwing.load())
        {
            entered = true;
            if (kind == "cancel") while (!release.load()) std::this_thread::yield();
            if (kind == "bad_alloc") throw std::bad_alloc();
            if (kind == "unknown") throw 31;
            throw std::runtime_error("injected evaluator exception");
        }
        metric.finite = true; metric.preservationMeasured = true;
        metric.protectedBandMask = 15; metric.protectedFraction = .5; metric.recoveryRegions = 3;
        metric.objective = candidate.controlCount > frozen.baseline.controlCount ? .5 : 1;
        return true;
    });
    check(controller.available(), "The actual matching trained-model header is present");
    if (!controller.available()) return 2;
    check(controller.start(5, 5, context), "Independent sector fixture starts"); audio.push(controller);
    Proposal retained; check(wait(controller, 5) && controller.proposal(5, retained) && retained.targetImproved,
                             "Independent sector fixture retains a Ready target");
    if (failed) return 2;
    if (kind == "start_alloc")
    {
        bool escaped = false, started = false;
        allocationFault::callingThread = std::this_thread::get_id(); allocationFault::armed = true;
        try { started = controller.start(0, 5, context); }
        catch (const std::bad_alloc&) { escaped = true; }
        const bool injected = !allocationFault::armed.load(); allocationFault::armed = false;
        check(injected, "Exactly one calling-thread capture allocation was fault-injected without real memory pressure");
        check(!escaped && !started && controller.status(0).state == State::error,
              "Capture allocation failure returns false and publishes Error without escaping to the UI caller");
        Proposal rejected;
        check(!controller.proposal(0, rejected) && !controller.requestStrength(0, .5f),
              "Failed capture start has no applicable proposal or cached Strength target");
        check(controller.proposal(5, retained) && retained.targetImproved && retained.contextRevision == context.revision,
              "Capture-start rollback preserves the other sector's Ready target");
        check(controller.start(1, 5, context), "A subsequent capture starts after the injected allocation failure");
        audio.push(controller);
        check(wait(controller, 1) && controller.proposal(1, retained) && retained.targetImproved,
              "The controller and worker recover after capture-start resource failure");
        std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
        return failed ? 1 : 0;
    }
    if (kind == "strength")
    {
        check(controller.start(0, 5, context), "Strength fixture starts"); audio.push(controller);
        check(wait(controller, 0) && controller.proposal(0, retained) && retained.targetImproved,
              "Strength fixture has a verified full target before fault injection");
        throwing = true; check(controller.requestStrength(0, .5f), "Strength fault queues one worker render");
    }
    else
    {
        throwing = true; check(controller.start(0, 5, context), "Throwing evaluator capture starts"); audio.push(controller);
    }
    if (kind == "cancel")
    {
        for (int i = 0; i < 2000 && !entered.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(entered.load(), "Cancellation fault reaches evaluator"); controller.cancel(0); release = true;
    }
    const bool completed = wait(controller, 0);
    const auto status = controller.status(0);
    check(completed && entered.load() && status.state == (kind == "cancel" ? State::cancelled : State::error),
          "Worker exception is contained in the affected slot and Cancel keeps precedence");
    Proposal rejected;
    check(!controller.proposal(0, rejected) && !controller.requestStrength(0, .25f),
          "Failed worker cannot expose or reuse an applicable proposal or cached Strength target");
    check(controller.proposal(5, retained) && retained.targetImproved && retained.contextRevision == context.revision,
          "Exception cleanup preserves another sector's Ready proposal and capture");
    if (kind != "cancel") check(!status.message.empty(), "Contained failure provides an explicit diagnostic");
    throwing = false; bool started = false;
    for (int i = 0; i < 1000 && !started; ++i)
    {
        started = controller.start(1, 5, context);
        if (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(started, "The same controller accepts a new job after failure cleanup");
    if (started) audio.push(controller);
    check(started && wait(controller, 1) && controller.proposal(1, retained) && retained.targetImproved,
          "The same worker completes a subsequent job after the injected exception");
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
}
int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    if (argc == 3 && std::string(argv[1]) == "--child") return child(argv[2]);
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    for (const char* kind : {"standard", "bad_alloc", "unknown", "strength", "cancel", "start_alloc"})
    {
        juce::ChildProcess process;
        const bool launched = process.start(juce::StringArray{exe, "--child", kind});
        const bool finished = launched && process.waitForProcessToFinish(20000);
        if (launched && !finished) process.kill(); // Only this test's explicitly created child.
        std::cout << "CHILD " << kind << '\n' << process.readAllProcessOutput();
        const auto exit = launched && finished ? process.getExitCode() : 999;
        std::cout << "CHILD-EXIT " << kind << ' ' << exit << '\n';
        check(finished && exit == 0, "Injected exception child survives, cleans up and recovers");
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
