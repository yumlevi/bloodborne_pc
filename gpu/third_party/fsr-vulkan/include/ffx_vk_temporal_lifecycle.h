/*
 * Copyright (C) 2026 Q2RTX contributors.
 * SPDX-License-Identifier: MIT
 *
 * Small provider-neutral temporal-lifecycle helpers.  These do not own Vulkan
 * objects; an application forwards the resulting reset decision to whichever
 * upscaler, denoiser, optical-flow, or frame-generation provider it uses.
 */

#ifndef FFX_VK_TEMPORAL_LIFECYCLE_H
#define FFX_VK_TEMPORAL_LIFECYCLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Q2's ordinary maximum camera motion is only a few world units per rendered
 * frame.  The values are deliberately conservative so normal motion remains
 * motion-vector reprojected while discontinuities discard all temporal state.
 * The caller's forward vectors must be normalized. */
#define FFX_VK_TEMPORAL_CAMERA_CUT_DISTANCE_UNITS 256.0f
#define FFX_VK_TEMPORAL_CAMERA_CUT_FORWARD_DOT_MIN 0.0f
#define FFX_VK_TEMPORAL_CAMERA_CUT_FOV_DELTA_RADIANS 0.35f

typedef struct FfxVkTemporalCameraState {
    float position[3];
    float forward[3];
    float verticalFovRadians;
} FfxVkTemporalCameraState;

/* Returns true for a single-frame teleport, turn greater than 90 degrees,
 * lens jump, or non-finite camera state. Invalid input is deliberately a
 * reset rather than a chance to feed corrupt history to a temporal provider. */
bool ffxVkTemporalCameraCutDetected(const FfxVkTemporalCameraState *current,
                                    const FfxVkTemporalCameraState *previous);

/* Returns true exactly when presentation availability changes. A host maps
 * window focus/visibility, swapchain suspension, or another compositor/WSI
 * availability signal to these booleans and forwards a reset to every temporal
 * provider on either edge. It intentionally has no platform or Vulkan types. */
bool ffxVkTemporalPresentationAvailabilityChanged(bool previousAvailable,
                                                  bool currentAvailable);

#ifdef __cplusplus
}
#endif

#endif /* FFX_VK_TEMPORAL_LIFECYCLE_H */
