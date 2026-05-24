#include "Engine/Runtime/Time/Time.h"

#include <cmath>
#include <iostream>

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

    bool NearlyEqual(double left, double right, double tolerance = 1.0e-6)
    {
        return std::abs(left - right) <= tolerance;
    }

    bool TestResetDefaults()
    {
        bool passed = true;

        ve::Time::Reset();
        const ve::Time::TimeSnapshot snapshot = ve::Time::GetSnapshot();

        passed &= Expect(snapshot.frameIndex == 0, "Reset should clear frame index");
        passed &= Expect(NearlyEqual(snapshot.totalSeconds, 0.0), "Reset should clear total time");
        passed &= Expect(NearlyEqual(snapshot.fixedAccumulatorSeconds, 0.0), "Reset should clear fixed accumulator");
        passed &= Expect(snapshot.rawDeltaSeconds == 0.0f, "Reset should clear raw delta");
        passed &= Expect(snapshot.deltaSeconds == 0.0f, "Reset should clear clamped delta");
        passed &=
            Expect(snapshot.maxDeltaSeconds == ve::Time::DefaultMaxDeltaSeconds, "Reset should restore max delta");
        passed &= Expect(snapshot.fixedDeltaSeconds == ve::Time::DefaultFixedDeltaSeconds,
                         "Reset should restore fixed delta");
        passed &= Expect(snapshot.fixedStepCount == 0, "Reset should clear fixed step count");

        return passed;
    }

    bool TestAdvanceUpdatesFrameAndClampsDelta()
    {
        bool passed = true;

        ve::Time::Reset();
        ve::Time::Advance(0.016f);

        ve::Time::TimeSnapshot snapshot = ve::Time::GetSnapshot();
        passed &= Expect(snapshot.frameIndex == 1, "Advance should increment frame index");
        passed &= Expect(snapshot.rawDeltaSeconds == 0.016f, "Advance should preserve raw delta");
        passed &= Expect(snapshot.deltaSeconds == 0.016f, "Advance should preserve delta below clamp");
        passed &= Expect(NearlyEqual(snapshot.totalSeconds, 0.016), "Advance should accumulate total time");

        ve::Time::Advance(1.0f);
        snapshot = ve::Time::GetSnapshot();

        passed &= Expect(snapshot.frameIndex == 2, "Advance should keep incrementing frame index");
        passed &= Expect(snapshot.rawDeltaSeconds == 1.0f, "Advance should expose unclamped raw delta");
        passed &= Expect(snapshot.deltaSeconds == ve::Time::DefaultMaxDeltaSeconds, "Advance should clamp large delta");
        passed &= Expect(NearlyEqual(snapshot.totalSeconds, 0.016 + ve::Time::DefaultMaxDeltaSeconds),
                         "Advance should accumulate clamped delta");

        ve::Time::Advance(-1.0f);
        snapshot = ve::Time::GetSnapshot();

        passed &= Expect(snapshot.rawDeltaSeconds == 0.0f, "Negative raw delta should be treated as zero");
        passed &= Expect(snapshot.deltaSeconds == 0.0f, "Negative clamped delta should be zero");

        return passed;
    }

    bool TestDeltaConfigurationRejectsInvalidValues()
    {
        bool passed = true;

        ve::Time::Reset();

        passed &= Expect(ve::Time::SetMaxDeltaSeconds(0.5f), "Positive max delta should be accepted");
        passed &= Expect(ve::Time::GetMaxDeltaSeconds() == 0.5f, "Accepted max delta should be stored");
        passed &= Expect(!ve::Time::SetMaxDeltaSeconds(0.0f), "Zero max delta should be rejected");
        passed &= Expect(ve::Time::GetMaxDeltaSeconds() == 0.5f, "Rejected max delta should not change state");

        passed &= Expect(ve::Time::SetFixedDeltaSeconds(0.1f), "Positive fixed delta should be accepted");
        passed &= Expect(ve::Time::GetFixedDeltaSeconds() == 0.1f, "Accepted fixed delta should be stored");
        passed &= Expect(!ve::Time::SetFixedDeltaSeconds(-1.0f), "Negative fixed delta should be rejected");
        passed &= Expect(ve::Time::GetFixedDeltaSeconds() == 0.1f, "Rejected fixed delta should not change state");

        return passed;
    }

    bool TestFixedStepAccumulator()
    {
        bool passed = true;

        ve::Time::Reset();
        passed &= Expect(ve::Time::SetMaxDeltaSeconds(1.0f), "Large test delta should fit under max delta");
        passed &= Expect(ve::Time::SetFixedDeltaSeconds(0.1f), "Fixed delta should be configurable");

        ve::Time::Advance(0.35f);

        ve::Time::TimeSnapshot snapshot = ve::Time::GetSnapshot();

        passed &= Expect(snapshot.fixedStepCount == 3, "Accumulator should expose this frame's fixed step budget");
        passed &= Expect(ve::Time::GetFixedStepCount() == 3, "Fixed step count getter should match snapshot");
        passed &= Expect(ve::Time::HasFixedStep(), "HasFixedStep should be true when fixed steps are budgeted");
        passed &= Expect(NearlyEqual(snapshot.fixedAccumulatorSeconds, 0.05, 1.0e-5),
                         "Fixed accumulator should retain fractional remainder after budgeting steps");

        ve::Time::Advance(0.04f);
        snapshot = ve::Time::GetSnapshot();
        passed &= Expect(snapshot.fixedStepCount == 0, "Remainder below fixed delta should not budget a step");
        passed &= Expect(NearlyEqual(snapshot.fixedAccumulatorSeconds, 0.09, 1.0e-5),
                         "Fixed accumulator should carry remainder across frames");

        ve::Time::Advance(0.02f);
        snapshot = ve::Time::GetSnapshot();
        passed &= Expect(snapshot.fixedStepCount == 1, "Crossing fixed delta should budget one step");
        passed &= Expect(NearlyEqual(snapshot.fixedAccumulatorSeconds, 0.01, 1.0e-5),
                         "Budgeting a fixed step should leave only the new remainder");

        return passed;
    }

    bool TestTickUsesMonotonicClock()
    {
        bool passed = true;

        ve::Time::Reset();
        ve::Time::Tick();

        const ve::Time::TimeSnapshot snapshot = ve::Time::GetSnapshot();
        passed &= Expect(snapshot.frameIndex == 1, "Tick should advance one frame");
        passed &= Expect(snapshot.rawDeltaSeconds >= 0.0f, "Tick raw delta should be non-negative");
        passed &= Expect(snapshot.deltaSeconds >= 0.0f, "Tick clamped delta should be non-negative");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestResetDefaults();
    passed &= TestAdvanceUpdatesFrameAndClampsDelta();
    passed &= TestDeltaConfigurationRejectsInvalidValues();
    passed &= TestFixedStepAccumulator();
    passed &= TestTickUsesMonotonicClock();

    if (passed)
    {
        std::cout << "VEngineTimeTests passed" << '\n';
        return 0;
    }

    return 1;
}
