#!/usr/bin/env python3
"""Create the minimal non-destructive Vulkan annotation overlay for SDK 2.3 FI/OF."""

# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

from __future__ import annotations

import pathlib
import sys


def replace_exact(source: str, old: str, new: str, expected: int) -> str:
    count = source.count(old)
    if count != expected:
        raise RuntimeError(f"expected {expected} occurrences of {old!r}, found {count}")
    return source.replace(old, new)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} FRAMEGEN_INCLUDE OUTPUT_OVERLAY", file=sys.stderr)
        return 2
    include = pathlib.Path(sys.argv[1])
    output_root = pathlib.Path(sys.argv[2])
    output = output_root / "opticalflow"
    source_path = include / "opticalflow" / "ffx_opticalflow_callbacks_hlsl.h"
    try:
        source = source_path.read_text(encoding="utf-8")

        # The overlay is intentionally shallow (only this changed header), so
        # preserve the SDK include through the generator's existing core path
        # instead of the original five-directory relative route.
        source = replace_exact(
            source,
            '#include "../../../../../api/internal/gpu/ffx_core.h"',
            '#include "ffx_core.h"',
            1,
        )

        # The SDK host creates these as R8_UINT. DX12 permits the uint HLSL
        # declaration directly; SPIR-V needs its actual typed image format.
        source = replace_exact(
            source,
            "RWTexture2D<FfxUInt32>                   rw_optical_flow_input",
            '[[vk::image_format("r8ui")]] RWTexture2D<FfxUInt32> rw_optical_flow_input',
            1,
        )
        source = replace_exact(
            source,
            "globallycoherent RWTexture2D<FfxUInt32>  rw_optical_flow_input_level_",
            '[[vk::image_format("r8ui")]] globallycoherent RWTexture2D<FfxUInt32> rw_optical_flow_input_level_',
            6,
        )

        # The SDK host creates both OF pyramids and the shared result as
        # R16G16_SINT, while HLSL's int2 otherwise infers Rg32i in SPIR-V.
        source = replace_exact(
            source,
            "RWTexture2D<FfxInt32x2>                   rw_optical_flow ",
            '[[vk::image_format("rg16i")]] RWTexture2D<FfxInt32x2> rw_optical_flow ',
            1,
        )
        source = replace_exact(
            source,
            "RWTexture2D<FfxInt32x2>                   rw_optical_flow_next_level",
            '[[vk::image_format("rg16i")]] RWTexture2D<FfxInt32x2> rw_optical_flow_next_level',
            1,
        )

        output.mkdir(parents=True, exist_ok=False)
        (output / source_path.name).write_text(source, encoding="utf-8")

        fi_source_path = include / "frameinterpolation" / "ffx_frameinterpolation_callbacks_hlsl.h"
        fi_source = fi_source_path.read_text(encoding="utf-8")
        fi_source = replace_exact(
            fi_source,
            '#include "../../../../../api/internal/gpu/ffx_core.h"',
            '#include "ffx_core.h"',
            1,
        )
        # The public FI host creates this shared target as R16G16_FLOAT.
        # HLSL float2 otherwise infers Rg32f, which Vulkan rejects.
        fi_source = replace_exact(
            fi_source,
            "RWTexture2D<FfxFloat32x2> rw_dilated_motion_vectors",
            '[[vk::image_format("rg16f")]] RWTexture2D<FfxFloat32x2> rw_dilated_motion_vectors',
            1,
        )
        # These resources have compact Vulkan allocations.  HLSL defaults to
        # 32-bit storage-image formats, which is valid for DX12 but makes the
        # generated SPIR-V write an incompatible image type on Vulkan.
        fi_source = replace_exact(
            fi_source,
            "RWTexture2D<FfxFloat32x2> rw_disocclusion_mask",
            '[[vk::image_format("rg8")]] RWTexture2D<FfxFloat32x2> rw_disocclusion_mask',
            1,
        )
        fi_source = replace_exact(
            fi_source,
            "RWTexture2D<FfxFloat32>   rw_inpainting_mask",
            '[[vk::image_format("r8")]] RWTexture2D<FfxFloat32> rw_inpainting_mask',
            1,
        )
        fi_source = replace_exact(
            fi_source,
            "RWTexture2D<FfxFloat32x3> rw_output          FFX_DECLARE_UAV(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT);",
            '[[vk::image_format("unknown")]] RWTexture2D<FfxFloat32x4> rw_output FFX_DECLARE_UAV(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT);',
            1,
        )
        fi_source = replace_exact(
            fi_source,
            "return FfxFloat32x4(rw_output[iPxPos], rw_inpainting_mask[iPxPos]);",
            "return FfxFloat32x4(rw_output[iPxPos].rgb, rw_inpainting_mask[iPxPos]);",
            1,
        )
        fi_source = replace_exact(
            fi_source,
            "rw_output[iPxPos] = val.rgb;",
            "rw_output[iPxPos] = FfxFloat32x4(val.rgb, 0.0f);",
            1,
        )
        fi_source = replace_exact(
            fi_source,
            "#elif defined(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT)\n    RWTexture2D<FfxFloat32x4> rw_output FFX_DECLARE_UAV(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT);",
            '#elif defined(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT)\n    [[vk::image_format("unknown")]] RWTexture2D<FfxFloat32x4> rw_output FFX_DECLARE_UAV(FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT);',
            1,
        )
        for mip in tuple(range(5)) + tuple(range(6, 13)):
            spacing = "   " if mip < 10 else "  "
            fi_source = replace_exact(
                fi_source,
                f"RWTexture2D<FfxFloat32x4>                   rw_inpainting_pyramid{mip}{spacing}FFX_DECLARE_UAV(",
                f'[[vk::image_format("rgba16f")]] RWTexture2D<FfxFloat32x4> rw_inpainting_pyramid{mip} FFX_DECLARE_UAV(',
                1,
            )
        fi_source = replace_exact(
            fi_source,
            "globallycoherent RWTexture2D<FfxFloat32x4>  rw_inpainting_pyramid5",
            '[[vk::image_format("rgba16f")]] globallycoherent RWTexture2D<FfxFloat32x4> rw_inpainting_pyramid5',
            1,
        )
        fi_output = output_root / "frameinterpolation"
        fi_output.mkdir(parents=True, exist_ok=False)
        (fi_output / fi_source_path.name).write_text(fi_source, encoding="utf-8")
        fi_resources = fi_source_path.with_name("ffx_frameinterpolation_resources.h")
        (fi_output / fi_resources.name).write_text(fi_resources.read_text(encoding="utf-8"),
                                                    encoding="utf-8")
    except (OSError, RuntimeError) as error:
        print(f"FSR3.1.6 Vulkan overlay error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
