/*
 * Copyright (C) 2026 Q2RTX contributors.
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_temporal_lifecycle.h"

#include <float.h>
#include <math.h>

static bool finite_camera_state(const FfxVkTemporalCameraState *camera)
{
    return camera && isfinite(camera->position[0]) &&
        isfinite(camera->position[1]) && isfinite(camera->position[2]) &&
        isfinite(camera->forward[0]) && isfinite(camera->forward[1]) &&
        isfinite(camera->forward[2]) && isfinite(camera->verticalFovRadians);
}

bool ffxVkTemporalCameraCutDetected(const FfxVkTemporalCameraState *current,
                                    const FfxVkTemporalCameraState *previous)
{
    if (!finite_camera_state(current) || !finite_camera_state(previous))
        return true;

    const float dx = current->position[0] - previous->position[0];
    const float dy = current->position[1] - previous->position[1];
    const float dz = current->position[2] - previous->position[2];
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    const float forward_dot = current->forward[0] * previous->forward[0] +
        current->forward[1] * previous->forward[1] +
        current->forward[2] * previous->forward[2];
    const float fov_delta = fabsf(current->verticalFovRadians -
        previous->verticalFovRadians);
    /* Adding a nominal 0.35f lens jump to a typical ~1 radian FOV can round
     * one ULP above the threshold. Preserve the public "over 0.35" contract
     * rather than turning that representation detail into a false cut. */
    const float fov_epsilon = 8.0f * FLT_EPSILON * fmaxf(1.0f,
        fmaxf(fabsf(current->verticalFovRadians),
            fabsf(previous->verticalFovRadians)));

    return distance_squared > FFX_VK_TEMPORAL_CAMERA_CUT_DISTANCE_UNITS *
            FFX_VK_TEMPORAL_CAMERA_CUT_DISTANCE_UNITS ||
        forward_dot < FFX_VK_TEMPORAL_CAMERA_CUT_FORWARD_DOT_MIN ||
        fov_delta > FFX_VK_TEMPORAL_CAMERA_CUT_FOV_DELTA_RADIANS + fov_epsilon;
}

bool ffxVkTemporalPresentationAvailabilityChanged(bool previousAvailable,
                                                  bool currentAvailable)
{
    return previousAvailable != currentAvailable;
}
