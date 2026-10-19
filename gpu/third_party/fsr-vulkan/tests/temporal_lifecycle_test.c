/* SPDX-License-Identifier: MIT */

#include "ffx_vk_temporal_lifecycle.h"

#include <math.h>
#include <stdio.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static FfxVkTemporalCameraState camera_at(float x, float y, float z)
{
    FfxVkTemporalCameraState camera = {
        .position = { x, y, z },
        .forward = { 0.0f, 1.0f, 0.0f },
        .verticalFovRadians = 1.0f,
    };
    return camera;
}

int main(void)
{
    const FfxVkTemporalCameraState previous = camera_at(0.0f, 0.0f, 0.0f);
    FfxVkTemporalCameraState current = camera_at(32.0f, 0.0f, 0.0f);

    CHECK(!ffxVkTemporalCameraCutDetected(&current, &previous));

    current = camera_at(FFX_VK_TEMPORAL_CAMERA_CUT_DISTANCE_UNITS, 0.0f, 0.0f);
    CHECK(!ffxVkTemporalCameraCutDetected(&current, &previous));
    current = camera_at(FFX_VK_TEMPORAL_CAMERA_CUT_DISTANCE_UNITS + 0.1f, 0.0f, 0.0f);
    CHECK(ffxVkTemporalCameraCutDetected(&current, &previous));

    current = previous;
    current.forward[0] = 1.0f;
    current.forward[1] = 0.0f;
    CHECK(!ffxVkTemporalCameraCutDetected(&current, &previous));
    current.forward[0] = 0.0f;
    current.forward[1] = -1.0f;
    CHECK(ffxVkTemporalCameraCutDetected(&current, &previous));

    current = previous;
    current.verticalFovRadians += FFX_VK_TEMPORAL_CAMERA_CUT_FOV_DELTA_RADIANS;
    CHECK(!ffxVkTemporalCameraCutDetected(&current, &previous));
    current.verticalFovRadians += 0.01f;
    CHECK(ffxVkTemporalCameraCutDetected(&current, &previous));

    current = previous;
    current.position[0] = NAN;
    CHECK(ffxVkTemporalCameraCutDetected(&current, &previous));
    CHECK(ffxVkTemporalCameraCutDetected(NULL, &previous));

    CHECK(!ffxVkTemporalPresentationAvailabilityChanged(true, true));
    CHECK(!ffxVkTemporalPresentationAvailabilityChanged(false, false));
    CHECK(ffxVkTemporalPresentationAvailabilityChanged(true, false));
    CHECK(ffxVkTemporalPresentationAvailabilityChanged(false, true));
    return 0;
}
