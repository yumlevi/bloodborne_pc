# Third-party notices and distribution boundary

`LICENSE.txt` applies to the original portability layer, public headers,
examples, tests, and documentation in this project.

The `upstream/ffx-1.1.4/` and `upstream/ffx-2.3.0/` trees contain the pinned
FidelityFX SDK source subsets used to build FSR3 upscaling, Optical Flow, and
Frame Interpolation. Retain the copyright and MIT notices embedded in those
files. The complete SDK 1.1.4 MIT license is preserved at
`upstream/ffx-1.1.4/sdk/LICENSE.txt`; `UPSTREAM.md` records the exact SDK
revisions, source hashes, and the per-file license rule used for the SDK 2.3
subset.

This project does not include AMD's signed FSR4.1.1, Ray Regeneration, or ML
Frame Generation DLLs, nor any extracted code or model payload from them.
It also does not include source-v07 FSR4 shader binaries, model initializers,
or weights. A distributor may opt into installing a separate compatible v07
asset bundle only through the documented CMake option and remains responsible
for its provenance and terms.

Do not describe the experimental source-v07 provider as AMD FSR 4.1.1, Ray
Regeneration, or ML Frame Generation.
