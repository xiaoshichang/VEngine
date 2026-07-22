#include "Engine/Runtime/Time/Time.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }

    bool ExpectNear(ve::Float64 actual, ve::Float64 expected, ve::Float64 tolerance, const char* message)
    {
        return Expect(std::abs(actual - expected) <= tolerance, message);
    }

    bool TestPausedTimeDoesNotAdvance()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        time.Advance(0.1f);
        const ve::TimeSnapshot snapshot = time.GetSnapshot();
        passed &= Expect(time.IsPaused(), "TimeSystem should report paused");
        passed &= Expect(snapshot.frameIndex == 1, "Paused frames should still increment frameIndex");
        passed &= Expect(snapshot.deltaSeconds == 0.0f, "Paused frames should expose zero delta");
        passed &= Expect(snapshot.fixedStepCount == 0, "Paused frames should not budget fixed steps");
        passed &= Expect(snapshot.totalSeconds == 0.0, "Paused frames should not accumulate simulation time");
        return passed;
    }

    bool TestStepAdvancesExactlyOnce()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        passed &= Expect(time.RequestStep(), "Paused TimeSystem should accept a default Step");
        time.Advance(0.25f);
        const ve::TimeSnapshot stepped = time.GetSnapshot();
        passed &= ExpectNear(stepped.deltaSeconds, ve::DefaultFixedDeltaSeconds, 1.0e-6, "Step should use the default fixed delta");
        passed &= ExpectNear(stepped.totalSeconds, ve::DefaultFixedDeltaSeconds, 1.0e-6, "Step should advance total time once");
        passed &= Expect(stepped.fixedStepCount == 1, "Default Step should budget one fixed update");
        passed &= Expect(time.IsPaused(), "Step should keep TimeSystem paused");

        time.Advance(0.25f);
        const ve::TimeSnapshot pausedAgain = time.GetSnapshot();
        passed &= Expect(pausedAgain.deltaSeconds == 0.0f, "Step request should be consumed once");
        passed &= ExpectNear(pausedAgain.totalSeconds, stepped.totalSeconds, 1.0e-6, "Following paused frame should not advance total time");
        return passed;
    }

    bool TestResumeClearsPendingStep()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        passed &= Expect(time.RequestStep(), "Paused TimeSystem should accept Step before resume");
        time.SetPaused(false);
        time.Advance(0.02f);
        const ve::TimeSnapshot snapshot = time.GetSnapshot();
        passed &= Expect(!time.IsPaused(), "TimeSystem should resume");
        passed &= ExpectNear(snapshot.deltaSeconds, 0.02, 1.0e-6, "Resume should use the new frame delta instead of pending Step");
        passed &= ExpectNear(snapshot.totalSeconds, 0.02, 1.0e-6, "Resume should not add a cleared Step");
        return passed;
    }

    bool TestInvalidStepRequestsAreRejected()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        passed &= Expect(!time.RequestStep(), "Running TimeSystem should reject Step");
        time.SetPaused(true);
        passed &= Expect(!time.RequestStep(0.0f), "Zero Step should be rejected");
        passed &= Expect(!time.RequestStep(-0.01f), "Negative Step should be rejected");
        passed &= Expect(!time.RequestStep(std::numeric_limits<ve::Float32>::quiet_NaN()), "Non-finite Step should be rejected");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestPausedTimeDoesNotAdvance();
    passed &= TestStepAdvancesExactlyOnce();
    passed &= TestResumeClearsPendingStep();
    passed &= TestInvalidStepRequestsAreRejected();
    if (passed)
    {
        std::cout << "VEngineTimeTests passed\n";
        return 0;
    }
    return 1;
}
