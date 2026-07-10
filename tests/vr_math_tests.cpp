#include "vr_math.h"
#include "vr_locomotion.h"
#include "vr_physical_interactions.h"

#include <cstdlib>
#include <iostream>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace hytalevr;
    bool success = true;

    const Matrix4 identity = identity_matrix();
    success &= check(nearly_equal(multiply(identity, identity), identity),
                     "identity multiplication failed");

    const float origin[9]{
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    success &= check(nearly_equal(relative_view_rotation(origin, origin), identity),
                     "recentered pose must produce identity");

    constexpr float half_sqrt_three = 0.8660254037844386f;
    const float pitched_recenter_pose[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, half_sqrt_three, -0.5f, 1.6f,
        0.0f, 0.5f, half_sqrt_three, 0.0f,
    };
    float standing_origin[12]{};
    make_yaw_only_tracking_origin(pitched_recenter_pose, standing_origin);
    success &= check(std::fabs(standing_origin[1]) < 0.0001f &&
                         std::fabs(standing_origin[5] - 1.0f) < 0.0001f &&
                         std::fabs(standing_origin[9]) < 0.0001f,
                     "recenter origin must keep SteamVR's physical up axis");
    const Matrix4 standing_pitch_view =
        relative_view_pose(standing_origin, pitched_recenter_pose);
    success &= check(!nearly_equal(standing_pitch_view, identity) &&
                         std::fabs(standing_pitch_view.value[6] + 0.5f) < 0.0001f,
                     "recenter must not absorb headset pitch into the floor");

    const float yaw_positive_90[9]{
         0.0f, 0.0f, 1.0f,
         0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
    };
    Matrix4 expected_inverse_yaw = identity;
    expected_inverse_yaw.value[0] = 0.0f;
    expected_inverse_yaw.value[2] = 1.0f;
    expected_inverse_yaw.value[8] = -1.0f;
    expected_inverse_yaw.value[10] = 0.0f;
    success &= check(nearly_equal(relative_view_rotation(origin, yaw_positive_90),
                                  expected_inverse_yaw),
                     "head pose must be inverted when converted to a view rotation");

    const float origin_pose[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.6f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    const float moved_pose[12]{
        1.0f, 0.0f, 0.0f, 0.25f,
        0.0f, 1.0f, 0.0f, 1.7f,
        0.0f, 0.0f, 1.0f, -0.50f,
    };
    Matrix4 expected_translation = identity;
    expected_translation.value[12] = -0.25f;
    expected_translation.value[13] = -0.10f;
    expected_translation.value[14] = 0.50f;
    success &= check(nearly_equal(relative_view_pose(origin_pose, moved_pose),
                                  expected_translation),
                     "head translation must be inverted into camera view space");

    float extracted_position[3]{};
    float extracted_forward[3]{};
    view_pose(expected_translation, extracted_position, extracted_forward);
    success &= check(std::fabs(extracted_position[0] - 0.25f) < 0.0001f &&
                         std::fabs(extracted_position[1] - 0.10f) < 0.0001f &&
                         std::fabs(extracted_position[2] + 0.50f) < 0.0001f,
                     "view translation must recover the tracked world offset");
    success &= check(std::fabs(extracted_forward[0]) < 0.0001f &&
                         std::fabs(extracted_forward[1]) < 0.0001f &&
                         std::fabs(extracted_forward[2] + 1.0f) < 0.0001f,
                     "view forward must use the camera negative Z axis");

    Matrix4 pitched_view = identity;
    pitched_view.value[5] = 0.8f;
    pitched_view.value[6] = -0.6f;
    pitched_view.value[9] = 0.6f;
    pitched_view.value[10] = 0.8f;
    const Matrix4 level_view = horizontal_view(pitched_view);
    float level_position[3]{};
    float level_forward[3]{};
    view_pose(level_view, level_position, level_forward);
    success &= check(std::fabs(level_forward[1]) < 0.0001f &&
                         std::fabs(level_view.value[1]) < 0.0001f &&
                         std::fabs(level_view.value[5] - 1.0f) < 0.0001f &&
                         std::fabs(level_view.value[9]) < 0.0001f,
                     "native mouse pitch and roll must not tilt the VR horizon");

    const float yaw_pose[12]{
         0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 1.6f,
        -1.0f, 0.0f, 0.0f, 0.0f,
    };
    success &= check(nearly_equal(relative_view_pose(origin_pose, yaw_pose),
                                  expected_inverse_yaw),
                     "full 6DoF pose must preserve the proven rotation convention");

    Matrix4 expected_scaled_translation = identity;
    expected_scaled_translation.value[12] = 0.50f;
    expected_scaled_translation.value[13] = -0.30f;
    expected_scaled_translation.value[14] = -1.00f;
    success &= check(nearly_equal(relative_view_pose(origin_pose, moved_pose, 2.0f,
                                                     3.0f, true),
                                  expected_scaled_translation),
                     "translation gain and horizontal inversion must be explicit");

    PhysicalMotionHistory motion_history{};
    motion_history.add(0.0f, 1.50f, 0.0f, 0);
    motion_history.add(0.3f, 1.56f, 0.0f, 100);
    motion_history.add(0.6f, 1.62f, 0.0f, 200);
    success &= check(std::fabs(motion_history.average_speed(200, 330) -
                               3.0594f) < 0.001f,
                     "physical swing history must average segment speeds");
    success &= check(std::fabs(motion_history.net_vertical_movement(200, 250) -
                               0.12f) < 0.0001f,
                     "physical jump history must preserve vertical displacement");
    success &= check(physical_jump_requested(1.62f, 1.50f, 0.12f) &&
                         !physical_jump_requested(1.54f, 1.50f, 0.12f),
                     "physical jump must use the configured height and movement thresholds");
    success &= check(physical_sneak_requested(1.09f, 1.50f) &&
                         !physical_sneak_requested(1.11f, 1.50f),
                     "physical sneak must use the 0.4 meter threshold");
    success &= check(physical_swing_requested(2.51f) &&
                         !physical_swing_requested(2.49f),
                     "physical swing must use the 2.5 meter per second threshold");

    DigitalStickState stick{};
    stick = digital_stick(0.40f, 0.70f, stick, 0.35f);
    success &= check(stick.right && stick.forward && !stick.left && !stick.backward,
                     "quest stick must generate diagonal movement");
    stick = digital_stick(0.25f, 0.25f, stick, 0.35f);
    success &= check(stick.right && stick.forward,
                     "stick hysteresis must avoid key chatter");
    stick = digital_stick(0.10f, 0.10f, stick, 0.35f);
    success &= check(!stick.right && !stick.forward,
                     "stick keys must release below hysteresis");

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
