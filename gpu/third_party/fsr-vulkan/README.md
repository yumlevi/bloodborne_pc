# Portable FidelityFX Vulkan module

This directory is the API and portability boundary for native Vulkan FSR
work.  It is deliberately buildable without Q2RTX so that the resulting
implementation can be reused by other Vulkan applications.

Standalone source: <https://github.com/FireBurn/FSR-Vulkan>. The same tree is
vendored by Q2RTX for its reference integration.

Current status: the public C contract, validation layer, device capability
probe, pinned AMD 1.1.4 host scheduler, native Vulkan FSR 3 Upscaler, and
analytical Optical Flow/Frame Interpolation compute backends are implemented
and tested. The versioned source-v07 FSR4 INT8/DOT4 provider is also included.
Opaque public C APIs create, destroy, and record upscaling plus frame-generation
work without exposing an AMD type. The generated Vulkan shader tables are
checked in with manifests. Q2RTX supplies a validation-tested two-acquire,
generated-then-real reference presenter; extracting that WSI policy as a
general-purpose standalone policy library is complete. Its callback-based
two-image acquisition API also preserves the first acquired image for a safe
normal-present fallback when WSI cannot provide the second target.
`ffxVkFrameGenerationBuildPresentPlan` turns that outcome into an ordered
one- or two-slot render/present plan, including the reset-frame real-scene
guard. Generic command recording, queue submission, and presentation callbacks
remain application-owned.

## Design boundary

The application owns the Vulkan instance, device, queues, command buffers, and
external images.  The module owns FidelityFX contexts and their internal
resources.  Dispatch calls record work into an application-provided command
buffer.  Every imported image carries its current resource state; the runtime
must restore imported resources to that state before returning.

The API keeps these independently selectable pieces behind one contract:

1. FSR 3 temporal upscaling.
2. FSR 3 analytical frame interpolation, including optical flow.
3. A Q2RTX reference presenter that schedules real and generated frames and
   composes UI after interpolation; its reusable WSI policy library is
   available through its tested callback-based acquire and immutable
   generated-then-real present-plan API.
4. The source-v07 FSR4 provider using the same Vulkan resource and temporal
   input types.
5. `ffx-vulkan::rayregeneration-contract`, a provider-neutral validator for
   Ray-Regeneration-style depth, motion, material, noisy-radiance, hit-distance,
   ambient/specular-occlusion, and optional dominant-light inputs. It is deliberately not a neural
   denoiser or a claim that a signed AMD RR provider is available.
6. `ffx-vulkan::radiancecache-contract`, a provider-neutral validator for
   public Radiance Caching inference/training buffers and its two atomic
   counters. It does not generate path-tracer samples or implement neural
   inference/training.

The presenter will use an explicit API rather than impersonating a Vulkan
swapchain handle.  That makes queue ownership and synchronization visible and
avoids the Windows-only behavior in AMD's old Vulkan swapchain reference.

## Feature availability

| Feature | Reusable Vulkan status | Important boundary |
| --- | --- | --- |
| FSR3 1.1.4 / 3.1.5 temporal upscaling | Runnable native Vulkan compute | Public-source implementation; choose the versioned opaque API required by the host. |
| FSR3 3.1.6 Optical Flow / Frame Interpolation | Runnable native Vulkan compute plus WSI policy helpers | The host owns acquire, submit, present, UI composition, and pacing; a generated frame is never a replacement for a required real frame. |
| FSR4 v07 INT8/DOT4 | Runnable experimental Vulkan provider | Requires a complete externally supplied, same-preset v07 shader/model bundle; it is not AMD FSR 4.1.1. |
| Ray-Regeneration-style inputs | Runnable provider-neutral validation contract | Validates inputs/outputs only; it does not denoise, own models, or imply an AMD neural provider. |
| Radiance Caching host buffers | Runnable provider-neutral validation contract | Validates host buffer/counter ownership only; it does not emit samples, run a model, or imply an AMD neural provider. |
| Official FSR 4.1.1, ML Frame Generation, Ray Regeneration, Radiance Caching | Not provided by this project | AMD distributes these as signed DX12 providers. Do not relabel any analytical or v07 path as one of them. |

The matrix is deliberately about software integration, not a hardware promise:
hosts must check their own Vulkan feature set and their selected provider's
published support before enabling an option.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/ffx_vk_capability_probe
```

The standalone build creates `ffx-vulkan::portable`,
`ffx-vulkan::fsr3-host-1.1.4`, and the compute-only
`ffx-vulkan::fsr3-vk-backend-1.1.4` target. The backend exposes AMD's normal
Vulkan resource/pipeline/dispatch callbacks but deliberately leaves
`fpSwapChainConfigureFrameGeneration` null because the upstream implementation
of that callback is Win32-only. Set
`FFX_VK_PORTABLE_BUILD_FSR3_VK_BACKEND=OFF` to omit the backend and generated
shader tables. In that configuration the public upscaler create/record calls
remain linkable and return `FFX_VK_PORTABLE_ERROR_UNSUPPORTED`. Set
`FFX_VK_PORTABLE_BUILD_FSR3_HOST=OFF` as well to build only the public contract
and capability probe.

SDK 2.3 analytical frame generation is separately available as
`ffx-vulkan::fsr3-vk-framegeneration-3.1.6`. Its public declarations live in
`ffx_vk_fsr3_3_1_5_bridge.h` so the opaque SDK 3.1.5 upscaler and privately
prefixed 3.1.6 FI/OF source can coexist in one application. Its lifecycle is
`create -> record prepare -> record dispatch -> retire frame -> destroy`.
`RetireFrame(completedFrameId)` is required only after the application's GPU
fence signals; monotonic frame IDs permit multiple queue-ordered frames while
it releases their temporary image views safely. RGBA8 and RGBA16F colour
images are both supported and validation-smoked. This target
records compute work but intentionally does not own a swapchain or pacing
policy. `RecordDispatch` also accepts an optional sampled `R16G16_SFLOAT`
distortion field containing `UV_after - UV_before` for lens/post-process
distortion; leave it null for the provider's neutral internal field.
Its generated output must be a distinct Vulkan image from the colour, depth,
motion, and optional distortion inputs supplied for that frame. The API rejects
aliases rather than permitting a generated dispatch to overwrite a host's
temporal source or another effect's recurrent storage. The context retains its
create-time maximum-render and display extents: recreate it after a resize,
because stale extents, clipped interpolation rectangles, and non-finite camera,
motion, timing, or luminance inputs are rejected before Vulkan work is recorded.
An all-zero interpolation rectangle is the explicit whole-display shorthand;
any other rectangle must be non-empty and contained within that display extent.

The project also creates `ffx-vulkan::fsr4-v07-assets`, a dependency-free C host helper
for the source-v07 INT8 asset contract. Given a fixed model or DRS preset and
output extent, it returns the matching 1080/2160/4320 shader paths, initializer,
pass-0 weights, RCAS, and SPD names. It contains no model bytes, filesystem
policy, Vulkan dispatch backend, or claim of FSR 4.1.1 support; applications use
it to avoid accidentally mixing a graph with another model's weights.

`ffx-vulkan::fsr4-v07-vulkan` is the matching source-v07 Vulkan compute
provider. It consumes application-owned Vulkan objects and model/SPIR-V bytes,
and exposes versioned `ffxFsr4V07…` functions so it does not collide with an
AMD SDK/DLL's unversioned FFX exports. `FfxFsr4V07CreateContext` copies the
explicit `FfxInterface` installed by `ffxFsr4V07SetBackendInterface`; no
renderer global is required. It is the same target Q2RTX links.

`ffx-vulkan::framegeneration-presenter-policy` is the reusable WSI policy
layer used by Q2RTX. It does not acquire or present images for the application;
instead it codifies FIFO selection for generated→real pairs, `minImageCount +
2` requirements, pair validation, image/GPU semaphore ownership, and the
callback-based `ffxVkFrameGenerationAcquirePair` helper. The latter works with
either core or device-group image acquisition and leaves a first acquired image
available for normal presentation when the second acquire fails. Its paired
`ffxVkFrameGenerationBuildPresentPlan` supplies the only valid ordered slots
for the host to record, submit, and present, including the reset-frame real
scene guard. This keeps platform windowing and queue submission in the host
while avoiding common binary-semaphore and Mailbox/Immediate mistakes.

## Use in another Vulkan project

For all FSR3 paths, either vendor this directory (including `upstream/` and
`generated/`) and use `add_subdirectory`:

```cmake
add_subdirectory(extern/ffx-vulkan)
target_link_libraries(my_renderer PRIVATE
    ffx-vulkan::portable
    ffx-vulkan::rayregeneration-contract
    ffx-vulkan::radiancecache-contract
    ffx-vulkan::fsr3-vk-framegeneration-3.1.6
    ffx-vulkan::framegeneration-presenter-policy
    ffx-vulkan::fsr4-v07-vulkan)
```

For a host that wants the complete tested FSR3+FSR4 feature closure, link the
convenience `ffx-vulkan::effects` target instead. It is demonstrated by the
independent [full-stack contract](examples/full-stack):

```sh
cmake -S extern/ffx-vulkan/examples/full-stack -B build/ffx-full-stack
cmake --build build/ffx-full-stack
./build/ffx-full-stack/ffx_vk_full_stack_contract
```

or install the complete static-library package and link the same target from a
clean C++ consumer:

```sh
cmake -S extern/ffx-vulkan -B build/ffx-vulkan
cmake --build build/ffx-vulkan
cmake --install build/ffx-vulkan --prefix /opt/ffx-vulkan
cmake -S extern/ffx-vulkan/examples/installed-full-stack -B build/consumer \
  -DCMAKE_PREFIX_PATH=/opt/ffx-vulkan
cmake --build build/consumer
./build/consumer/ffx_vk_installed_full_stack_contract
```

The complete package contains the compiled, private SDK closures and exports
only the versioned public targets and `include/` headers; a consumer needs
Vulkan but does not need Q2RTX, `upstream/`, `generated/`, or source-tree
include paths. The smaller `examples/consumer` remains an FSR4-v07 + WSI-policy
contract for C hosts that do not need the FSR3 closure.

The application retains instance/device/queue/swapchain ownership. It records
FSR3 or FSR4 work in its own command buffer, keeps imported images alive until
its frame fence signals, and composes UI after interpolation. The FSR3 public
headers document the exact resource/layout and fence-retirement contracts.

### Ray Regeneration input contract

`ffx-vulkan::rayregeneration-contract` is a lightweight C validator intended
for a renderer's pre-provider integration check. Populate
`FfxVkRayRegenerationInputs` with sampled image metadata plus the required
motion-vector scale, jitter, camera delta, view/projection matrices, and depth
bounds, then call `ffxVkRayRegenerationValidateInputs`. The returned issue-bit
mask identifies incompatible extent, format, usage/state, alpha semantic, or
camera/dominant-light metadata before a provider is allowed to consume it.
It does not inspect GPU pixels, create a Vulkan context, record commands, or
include an AMD Ray Regeneration binary. Those are intentionally separate host
and provider responsibilities.

Before recording a provider dispatch, populate matching full-resolution
`FfxVkRayRegenerationOutputs` and call
`ffxVkRayRegenerationValidateOutputs`. It validates the R8/RGBA16F output
formats, storage usage/state, and permits documented in-place input/output
aliasing. Inputs can declare checkerboard reconstruction with a selected-signal
subset and per-signal origin bits; checkerboard inputs have a half-width active
pixel region while outputs remain full resolution. This is a concrete hand-off
contract for a provider, not a substitute provider implementation.

The contract models all seven independent RR-style signal inputs: direct and
indirect diffuse/specular radiance, dominant-light visibility, ambient
occlusion, and specular occlusion. The scalar occlusion signals are R8_UNORM
in the [0, 1] range. They are optional additions: at least one radiance or
dominant-light signal must still be selected. Q2RTX currently exports the four
radiance inputs and dominant-light visibility, not the two separate occlusion
inputs.

For the optional dominant-light signal, the direction is from the light source
toward the shaded target (matching AMD RR); an `R16_SFLOAT` value of FP16_MAX
means fully exposed. A renderer whose shadow ray instead points from the
surface to its emitter must negate that vector at this boundary. The
camera-position delta must be supplied directly by the renderer, rather than
reconstructed from view matrices. Its convention is previous position minus
current position. Motion-vector scale has three components: XY transforms
motion into UV space and Z transforms the previous-minus-current signed-linear
depth delta; all three must be nonzero.

The FSR4-v07 provider also has an explicit three-frame Vulkan lifetime:

### Radiance Caching host-buffer contract

The radiancecache-contract target validates the public Radiance Caching host
boundary before a provider sees it. Its create-info records maximum inference
and training sample capacities. For each dispatch, it validates sampled
prediction/training input buffers, writable prediction output and counter
buffers, storage usage, declared resource states, finite optional
hyperparameters, and the documented minimum eight-byte inference/training
atomic-counter buffer.

The contract deliberately does not prescribe a provider-private structured
element layout, allocate buffers, emit Q2/path-tracer samples, record commands,
or implement neural inference/training. A host must generate the five buffers
and retain them through its own GPU completion; attach a legal provider only
after it reports support on that platform.

The FSR4-v07 provider also has an explicit three-frame Vulkan lifetime:
call `ffxFsr4VkBeginFrame(&interface, frame_id)` before each provider dispatch,
then call `ffxFsr4VkRetireFrame(&interface, completed_frame_id)` only after the
host fence proves those command buffers have completed. This retires descriptor
sets, staged model uploads, and the corresponding host-visible constant-buffer
partition without assuming a particular swapchain or frames-in-flight policy.

Every FSR4 external image must also be registered with
`ffxFsr4VkSetExternalImageState` before `ffxFsr4V07Dispatch`. The FFX ABI
itself contains only a `VkImageView`, so this separate record supplies the
underlying `VkImage`, current layout/stage/access, and requested restored
layout/stage/access. The provider transitions the image to `GENERAL` for its
compute passes and restores the requested state while unregistering imports.
Inputs must have `FFX_API_RESOURCE_STATE_COMPUTE_READ`; the output must have
`FFX_API_RESOURCE_STATE_UNORDERED_ACCESS`. This is an explicit host ownership
boundary, not an assumption that all renderers use Q2RTX's layout convention.

Before pipeline creation, the v07 provider reflects each supplied SPIR-V module
and rejects descriptor set/binding/type declarations outside its documented
v07 ABI. `ffxFsr4VkValidateShaderLayout` exposes the same fail-closed check to
asset loaders. It then builds a compact, binding-sorted descriptor-set layout
for that exact module rather than allocating a permissive generic layout, and
refuses a dispatch unless every declared non-sampler descriptor is populated.
This is an ABI guard for the known v07 graph, not a claim that arbitrary future
FSR4 shaders can use the provider unchanged.

The dependency-complete FSR4-v07 subset can also be installed as a CMake
package:

```sh
cmake -S extern/ffx-vulkan -B build/ffx-vulkan
cmake --build build/ffx-vulkan --target ffx_vulkan_fsr4_v07_vulkan
cmake --install build/ffx-vulkan --prefix /opt/ffx-vulkan
cmake -S extern/ffx-vulkan/examples/consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH=/opt/ffx-vulkan
cmake --build build/consumer
```

The `examples/consumer` project is intentionally a compile/link contract, not
a renderer. It is a minimal starting point for a host that already owns Vulkan
initialization. The v07 provider does **not** include or install model weights
or shader binaries: obtain them under their applicable terms, keep a complete
same-preset bundle together, and load the names returned by
`ffxFsr4V07BuildAssetSet`. It is experimental source-v07 code, not FSR 4.1.1,
Ray Regeneration, or ML Frame Generation.

### Optional v07 asset installation

The reusable library deliberately carries no model data by default. A package
builder with a compatible, licensed source-v07 bundle can make that explicit:

```sh
cmake -S extern/ffx-vulkan -B build/ffx-vulkan \
  -DFFX_VK_PORTABLE_INSTALL_FSR4_V07_ASSETS=ON \
  -DFFX_VK_PORTABLE_FSR4_V07_ASSET_DIR=/path/to/fsr4_shaders
cmake --build build/ffx-vulkan
cmake --install build/ffx-vulkan --prefix /opt/ffx-vulkan
```

Configuration fails unless the notice plus every native/quality/balanced/
performance/ultraperf/DRS initializer, pre-weight, and manifest file exists.
The bundle is installed under `share/ffx-vulkan/fsr4-v07`; a consumer that uses
`find_package(ffx-vulkan)` receives that path as
`FFX_VK_FSR4_V07_ASSET_DIR`. The application remains responsible for passing
the matching paths returned by `ffxFsr4V07BuildAssetSet` to the provider.
The installed-package consumer test verifies all 288 required files when this
opt-in mode is enabled, including the shared RCAS and SPD modules.

The default source tree contains no source-v07 shader/model payload. Its
payload-dependent SPIR-V layout test is therefore added only when
`FFX_VK_PORTABLE_FSR4_V07_TEST_ASSET_DIR` names an external compatible bundle.
Q2RTX's superproject supplies its locally installed bundle automatically; a
standalone clone reports that the one test is skipped and still fully builds
and tests the redistributable FSR3, presenter, RR-contract, and FSR4 host/API
closure.

The tests verify the complete patched-source and generated-file hash manifests,
all 2,816 upscaler, 352 Frame Interpolation, and 56 Optical Flow valid
pass/permutation lookups, and every unique SPIR-V module with `spirv-val` when
it is installed. Separate headless tests exercise AMD's low-level backend and
the public C API. They prefer a discrete compute device, enable supported
Vulkan 1.1-1.3 features, record and submit real upscaler and two-frame
Optical-Flow/Frame-Interpolation dispatches, and require finite, nonzero,
fully overwritten RGBA16F readbacks. Both warnings and errors from
`VK_LAYER_KHRONOS_validation` fail the test.
A machine without a Vulkan 1.3 compute device reports those tests as skipped,
as does one without the required subgroup operations, storage-image formats,
or unformatted storage-image writes. The smoke procedure loader intentionally
hides the KHR-suffixed memory-requirements2 command, exercising the Vulkan 1.1
core fallback used by applications that do not enable the promoted extension.
It also enables the AMD coherent-memory extension, when present, without
enabling its optional `deviceCoherentMemory` feature. Every backend allocation
is audited and the test fails if it selects a memory type carrying
`VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD` in that configuration. Subgroup
size control and synchronization2 are physically queried but deliberately
disabled at logical-device creation. The tests require the backend to suppress
the corresponding capabilities and required-subgroup-size pipeline metadata,
and verify that disabled debug-utils and AMD buffer-marker procedures are not
even requested.

## Public upscaler and frame-generation API

`ffx_vk_portable.h` is a C11-compatible boundary. A minimal lifecycle is:

```c
FfxVkPortableDeviceInfo device_info = {
    .structSize = sizeof(device_info),
    .instance = instance,
    .physicalDevice = physical_device,
    .device = device,
    .getDeviceProcAddr = vkGetDeviceProcAddr,
    .queue = compute_queue,
    .queueFamilyIndex = compute_queue_family,
    .deviceCoherentMemoryEnabled = VK_FALSE,
    .shaderFloat16Enabled = shader_float16_was_enabled,
    .subgroupSizeControlEnabled = subgroup_size_control_was_enabled,
    .computeFullSubgroupsEnabled = compute_full_subgroups_was_enabled,
    .synchronization2Enabled = synchronization2_was_enabled,
    .bufferMarkerEnabled = amd_buffer_marker_extension_was_enabled,
    .debugUtilsEnabled = debug_utils_instance_extension_was_enabled,
    .shaderStorageBufferArrayNonUniformIndexingEnabled =
        storage_buffer_nonuniform_indexing_was_enabled,
    .accelerationStructureEnabled = acceleration_structure_was_enabled,
    .shaderStorageImageWriteWithoutFormatEnabled =
        storage_image_write_without_format_was_enabled,
};
FfxVkPortableUpscaleCreateInfo create_info = {
    .structSize = sizeof(create_info),
    .flags = FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT,
    .maxRenderSize = {1920, 1080},
    .maxOutputSize = {3840, 2160},
};
FfxVkPortableUpscaleContext *upscaler = NULL;
FfxVkPortableResult result = ffxVkPortableUpscaleContextCreate(
    &device_info, &create_info, &upscaler);

/* Begin an application command buffer and fill all dispatch fields/images. */
result = ffxVkPortableUpscaleContextRecordDispatch(upscaler, &dispatch_info);

/* Submit in record order. Wait for all referencing frames before destruction. */
result = ffxVkPortableUpscaleContextDestroy(upscaler);
```

Leave `deviceCoherentMemoryEnabled` as `VK_FALSE` unless the logical device was
created with
`VkPhysicalDeviceCoherentMemoryFeaturesAMD::deviceCoherentMemory = VK_TRUE`.
Physical extension and memory-type availability do not prove that the feature
was enabled. The portable layer excludes AMD device-coherent memory types by
default because Vulkan forbids allocating them otherwise.

The remaining `*Enabled` fields have the same rule: describe the features and
extensions actually used to create the application's instance/device, not
everything returned by physical-device queries. Leave optional fields zero
when uncertain. The backend then selects conservative shader permutations and
omits optional debug/Breadcrumbs commands. In particular,
`subgroupSizeControlEnabled` controls required-subgroup-size pipeline metadata;
`computeFullSubgroupsEnabled` records the related feature for future effects
but the current upscaler does not request full-subgroup pipeline semantics.
The public FSR3 upscaler additionally requires
`shaderStorageImageWriteWithoutFormatEnabled = VK_TRUE`; its checked
accumulate modules declare that Vulkan feature, so physical-device support
without logical-device enablement is rejected at context creation.

Creation and destruction do not submit work. The context owns the backend,
scratch storage, temporal images, and FSR3's three shared upscaler outputs. It
does not own the instance, physical/logical device, queue, command buffers, or
images supplied by the application. A non-null `allocationCallbacks` pointer
must remain valid until context destruction. It is used for the portable
wrapper's shared images; AMD's pinned backend keeps its upstream behavior and
uses Vulkan's default allocator for backend-internal objects.

Each `FfxVkPortableImage.state` describes its actual layout/access state when
the record call begins. The backend records transitions back to that state
before returning. Queue-family ownership is not transferred: imported images
must already be owned by the family of the supplied command buffer.
`UNDEFINED` is intended only for first-use/discardable image contents. Calls on
one context mutate temporal/backend state and must be CPU serialized; their
command buffers must be submitted in the same order. A successful record also
requires every external image to remain alive until the recorded GPU work
completes. The API does not wait for the GPU on destruction, so the application
must do that with its normal frame fences.

### Upscaler allocation accounting

Both reusable upscaler lifecycles expose the exact effect-owned allocation
total after context creation: `ffxVkPortableUpscaleContextGetMemoryUsage` for
the FSR3 1.1.4 portable API (set `FfxVkPortableMemoryUsage::structSize`), and
`ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage` for the SDK 2.3 / FSR3 3.1.5
bridge. Imported application images are never counted. The 3.1.5 bridge gives
each SDK resource a separate `VkDeviceMemory` allocation, so its aliasable
total is accurately zero. `FFX_VK_PORTABLE_ABI_VERSION` is 2 for this additive
portable API revision.

The FSR3.1.6 analytical frame-generation lifecycle similarly exposes
`ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage`. Its total is deliberately
consolidated: optical flow and frame interpolation share one bridge, so adding
their individual SDK queries would double-count that backend. It includes the
five lifecycle-owned shared images and excludes imported application frames.

The pinned backend retains dynamic external-image views for eight effect calls
rather than the upstream four. This is intentional: analytical frame
interpolation records a preparation workload and a generation workload for one
application frame. A host with two frames in flight therefore needs more than
four calls of retention before safely recycling an external view. This is a
generic Vulkan lifetime correction, not a Q2RTX-only policy.

## Reproducing the shader headers

The checked-in headers under
`generated/ffx-1.1.4/vk/fsr3upscaler/` are build inputs, not handwritten
source. Regenerate them into an empty staging directory with AMD's unmodified
v1.1.4 tools:

```sh
FFX_VK_FFX_SC=/path/to/v1.1.4/sdk/tools/binary_store/FidelityFX_SC.exe \
FFX_VK_GLSLANG_EXE=/path/to/v1.1.4/sdk/tools/binary_store/glslangValidator.exe \
./tools/generate_fsr3upscaler_shaders.sh /tmp/fsr3upscaler-generated
```

The script verifies both tool hashes, forces `-num-threads=1`, and checks all
200 output headers against `SHA256SUMS`. Wine is required on non-Windows hosts;
override its path with `FFX_VK_WINE`. The generated `.d` files are retained in
the staging directory for inspection but are not vendored because they contain
machine-specific absolute Wine paths.

The capability booleans are prerequisites, not claims of image quality or
official hardware support.  In particular, `fsr4Int8Prerequisites` only means
that the Vulkan integer-dot-product feature set needed by an INT8 path is
present.

## Resource and temporal conventions

- Motion vectors point from the current pixel to the corresponding previous
  frame pixel.  Multiplying the stored value by `motionVectorScale` produces a
  displacement in render pixels.
- Jitter is the exact sub-pixel offset used to render the current frame, in
  render-pixel units.
- Depth is the original device-depth signal.  Inverted and infinite depth are
  creation flags, not guesses made by the module.
- `frameId` increments exactly once per rendered frame.  It does not increment
  for an interpolated frame.
- `reset` is required after camera cuts, seeks, map loads, resolution changes,
  or any discontinuity in those inputs.
- `ffx-vulkan::temporal-lifecycle` provides the tested conservative camera-cut
  classifier used by Q2RTX: a teleport over 256 application world units, turn
  over 90 degrees, lens jump over 0.35 radians, or non-finite camera state.
  Supply normalized forward vectors and forward its result as `reset` to every
  temporal provider for that rendered frame. Its presentation-availability
  helper likewise reports either edge of a host-provided focus, visibility, or
  WSI availability change, so every temporal provider can reset before paired
  presentation resumes.
- FSR input color is linear scene color.  UI is supplied separately and
  composed after interpolation.

See [UPSTREAM.md](UPSTREAM.md) for the source/version import plan and license
provenance. [NOTICE.md](NOTICE.md) describes the third-party and model-payload
boundary, while [PUBLISHING.md](PUBLISHING.md) records the clean subtree split
and standalone verification procedure.
