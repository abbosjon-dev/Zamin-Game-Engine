#include "TestAssertions.h"
#include <Zamin/Gameplay/GameplayRuntime.h>
#include <limits>
using namespace Zamin::Gameplay;
namespace {
    MotorInput Ground()
    {
        MotorInput i;
        i.Grounded = true;
        i.Delta = .1f;
        return i;
    }
}
TEST(GameplayMotorAcceleratesAndDecelerates)
{
    MotorSettings settings;
    MotorState state;
    auto input = Ground();
    input.Move = { 1, 0 };
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(out.Horizontal.x, 3, 1e-6f);
    input.HorizontalVelocity = out.Horizontal;
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(out.Horizontal.x, 5, 1e-6f);
    input.HorizontalVelocity = out.Horizontal;
    input.Move = {};
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(out.Horizontal.x, 1.5f, 1e-6f);
}
TEST(GameplayMotorAnalogAndDiagonalSpeedsAreBounded)
{
    MotorSettings settings;
    settings.Acceleration = 1000;
    MotorState state;
    auto input = Ground();
    MotorOutput out;
    std::string error;
    input.Move = { 1, 1 };
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(glm::length(out.Horizontal), 5, 1e-5f);
    input.Move = { .25f, 0 };
    input.HorizontalVelocity = {};
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(out.Horizontal.x, 1.25f, 1e-6f);
    input.Move = { 1, 0 };
    input.Sprint = true;
    CHECK(StepMotor(settings, state, input, out, error));
    CHECK_NEAR(out.Horizontal.x, 8, 1e-6f);
}
TEST(GameplayMotorAccelerationIsFramePartitionInvariant)
{
    MotorSettings s;
    MotorState a, b;
    auto i = Ground();
    i.Move = { 1, 0 };
    MotorOutput x, y;
    std::string error;
    CHECK(StepMotor(s, a, i, x, error));
    i.Delta = .01f;
    for (int n = 0; n < 10; ++n)
    {
        i.HorizontalVelocity = y.Horizontal;
        CHECK(StepMotor(s, b, i, y, error));
    }
    CHECK_NEAR(x.Horizontal.x, y.Horizontal.x, 1e-5f);
}
TEST(GameplayMotorAirControlPreservesMomentum)
{
    MotorSettings s;
    s.AirControl = 0;
    MotorState state;
    auto i = Ground();
    i.Grounded = false;
    i.Move = { -1, 0 };
    i.HorizontalVelocity = { 4, 0 };
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Horizontal.x == 4);
}
TEST(GameplayMotorCoyoteJumpWorksWithinGrace)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    i.Grounded = false;
    i.Delta = .05f;
    i.JumpPressed = true;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Jumped && out.Vertical == 8 && state.Grace == 0);
}
TEST(GameplayMotorExpiredCoyoteDoesNotJump)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    i.Grounded = false;
    i.Delta = .2f;
    i.JumpPressed = true;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(!out.Jumped);
}
TEST(GameplayMotorBuffersJumpUntilLanding)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    i.Grounded = false;
    i.JumpPressed = true;
    i.VerticalVelocity = -1;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(!out.Jumped && state.Buffered > 0);
    i.Grounded = true;
    i.JumpPressed = false;
    i.Delta = .05f;
    i.VerticalVelocity = 0;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Jumped && state.Buffered == 0);
}
TEST(GameplayMotorExpiredBufferDoesNotReplay)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    i.Grounded = false;
    i.JumpPressed = true;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    i.Grounded = true;
    i.JumpPressed = false;
    i.Delta = .2f;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(!out.Jumped);
}
TEST(GameplayMotorJumpCutOnlyAppliesOnceToOwnedJump)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    i.JumpPressed = true;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Jumped);
    i.Grounded = false;
    i.JumpPressed = false;
    i.VerticalVelocity = 8;
    i.JumpHeld = false;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Vertical == 4);
    i.VerticalVelocity = 4;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Vertical == 4);
    state = {};
    i.VerticalVelocity = 8;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Vertical == 8);
}
TEST(GameplayMotorDoesNotDoubleJumpFromStaleGround)
{
    MotorSettings s;
    MotorState state;
    auto i = Ground();
    i.JumpPressed = true;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Jumped);
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(!out.Jumped);
}
TEST(GameplayMotorDisableCancelsBufferedInput)
{
    MotorSettings s;
    MotorState state;
    state.Buffered = .1f;
    state.Grace = .1f;
    auto i = Ground();
    i.Enabled = false;
    i.HorizontalVelocity = { 3, 2 };
    i.VerticalVelocity = -4;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(out.Horizontal == glm::vec2(0) && out.Vertical == -4);
    CHECK(state.Buffered == 0 && state.Grace == 0);
}
TEST(GameplayMotorZeroDeltaAndInvalidInputAreTransactional)
{
    MotorSettings s;
    MotorState state;
    state.Buffered = .1f;
    auto i = Ground();
    i.Delta = 0;
    i.HorizontalVelocity = { 3, 2 };
    i.JumpPressed = true;
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(state.Buffered == .1f && out.Horizontal == i.HorizontalVelocity && !out.Jumped);
    i.Delta = -1;
    CHECK(!StepMotor(s, state, i, out, error));
    CHECK(state.Buffered == .1f);
    i.Delta = .1f;
    s.Speed = std::numeric_limits<float>::infinity();
    CHECK(!StepMotor(s, state, i, out, error));
    CHECK(state.Buffered == .1f);
}
TEST(GameplayMotorHitchAccelerationIsBoundedAndTimersExpire)
{
    MotorSettings s;
    MotorState state;
    state.Buffered = .12f;
    auto i = Ground();
    i.Grounded = false;
    i.Delta = 20;
    i.Move = { 1, 0 };
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK(glm::length(out.Horizontal) <= 5);
    CHECK(state.Buffered == 0 && !out.Jumped);
}
TEST(GameplayMotorHugeFiniteInputKeepsDirectionAndFiniteSpeed)
{
    MotorSettings s;
    s.Acceleration = 1000;
    MotorState state;
    auto i = Ground();
    i.Move = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    MotorOutput out;
    std::string error;
    CHECK(StepMotor(s, state, i, out, error));
    CHECK_NEAR(glm::length(out.Horizontal), 5, 1e-5f);
    CHECK(out.Horizontal.x == out.Horizontal.y);
}
TEST(GameplayCameraInitializesAndDampsWithoutFrameDependence)
{
    CameraState state;
    CameraInput i;
    i.Anchor = { 2, 3, 0 };
    i.Offset = { 0, 0, 5 };
    i.Delta = .1f;
    glm::vec3 out;
    std::string error;
    CHECK(StepCamera(state, i, out, error));
    CHECK(out == glm::vec3(2, 3, 5));
    CameraState a = state, b = state;
    i.Current = { 0, 0, 5 };
    i.Anchor = { 1, 0, 0 };
    i.Damping = .5f;
    i.SnapDistance = 0;
    CHECK(StepCamera(a, i, out, error));
    auto full = out;
    i.Delta = .01f;
    for (int n = 0; n < 10; ++n)
    {
        CHECK(StepCamera(b, i, out, error));
        i.Current = out;
    }
    CHECK_NEAR(out.x, full.x, 1e-5f);
}
TEST(GameplayCameraSnapHandlesTeleport)
{
    CameraState state;
    state.Initialized = true;
    CameraInput i;
    i.Current = {};
    i.Anchor = { 100, 0, 0 };
    i.Delta = .01f;
    glm::vec3 p;
    std::string error;
    CHECK(StepCamera(state, i, p, error));
    CHECK(p == i.Anchor + i.Offset);
}
TEST(GameplayCameraOrbitClampsPitchAndWrapsYaw)
{
    CameraState state;
    CameraInput i;
    i.Orbit = true;
    i.Look = { 100000, 100000 };
    i.Delta = .1f;
    glm::vec3 p;
    std::string error;
    CHECK(StepCamera(state, i, p, error));
    CHECK(std::abs(state.Pitch) <= 1.35f && std::abs(state.Yaw) <= 3.142f);
    CHECK(std::isfinite(p.x) && std::isfinite(p.z));
}
TEST(GameplayCameraZeroDeltaPreservesPoseAndInvalidIsTransactional)
{
    CameraState state;
    state.Initialized = true;
    CameraInput i;
    i.Current = { 1, 2, 3 };
    i.Anchor = { 2, 3, 4 };
    i.SnapDistance = 0;
    i.Delta = 0;
    i.Orbit = true;
    i.Look = { 100, 100 };
    glm::vec3 p;
    std::string error;
    CHECK(StepCamera(state, i, p, error));
    CHECK(p == i.Current && state.Yaw == 0 && state.Pitch == 0);
    i.Damping = -1;
    CHECK(!StepCamera(state, i, p, error));
    CHECK(state.Yaw == 0);
}
TEST(GameplayCheckpointRoundTripsFullActorPrecision)
{
    Checkpoint original{ "Test scene", 0xfedcba9876543210ull, { 1, 2, 3 }, { .1f, .2f, .3f } }, copy;
    std::string error;
    CHECK(DecodeCheckpoint(EncodeCheckpoint(original), copy, error));
    CHECK(copy.Actor == original.Actor && copy.Scene == original.Scene);
    CHECK(copy.Position == original.Position && copy.Rotation == original.Rotation);
}
TEST(GameplayCheckpointRejectsMalformedWithoutReplacingLastGood)
{
    Checkpoint point{ "Safe", 42, { 1, 2, 3 }, {} };
    std::string error;
    for (const auto& text : { std::string("{"), std::string(4097, 'x'), std::string(R"({"Format":"Zamin.GameplayCheckpoint","Version":2})") })
    {
        CHECK(!DecodeCheckpoint(text, point, error));
        CHECK(point.Actor == 42 && point.Position == glm::vec3(1, 2, 3));
    }
}
TEST(GameplayCheckpointRejectsWrongVectorAndActorTypes)
{
    using V = Zamin::Visual::Value;
    auto data = V::parse(EncodeCheckpoint({ "Safe", 42, { 1, 2, 3 }, {} }));
    Checkpoint point;
    std::string error;
    for (const V& id : std::vector<V>{ 42, -1, "-1", "18446744073709551616", "1x" })
    {
        auto copy = data;
        copy["Actor"] = id;
        CHECK(!DecodeCheckpoint(copy.dump(), point, error));
    }
    for (const V& pos : std::vector<V>{ V::array({ 1, 2 }), V::array({ "x", 2, 3 }), V::array({ 1.e30, 2, 3 }) })
    {
        auto copy = data;
        copy["Position"] = pos;
        CHECK(!DecodeCheckpoint(copy.dump(), point, error));
    }
}
TEST(GameplayCheckpointRejectsDeepPayloadAndInvalidIdentity)
{
    using V = Zamin::Visual::Value;
    auto data = V::parse(EncodeCheckpoint({ "Safe", 42, { 1, 2, 3 }, {} }));
    V nested = 1;
    for (int i = 0; i < 12; ++i) nested = V::array({ nested });
    data["Extra"] = nested;
    Checkpoint point;
    std::string error;
    CHECK(!DecodeCheckpoint(data.dump(), point, error));
    data.erase("Extra");
    data["Scene"] = "";
    CHECK(!DecodeCheckpoint(data.dump(), point, error));
    data["Scene"] = "Safe";
    data["Actor"] = "0";
    CHECK(!DecodeCheckpoint(data.dump(), point, error));
}
