# Full FSR3 + FSR4 Vulkan integration contract

This is the smallest buildable starting point for a Vulkan renderer that
vendors `extern/ffx-vulkan`. It links `ffx-vulkan::effects`, the convenience
target containing the tested FSR3 1.1.4 path, SDK-2.3 FSR3.1.5 upscaler,
FSR3.1.6 optical-flow/frame-interpolation path, source-v07 FSR4 provider, and
the reusable frame-generation presentation policy.

```sh
cmake -S extern/ffx-vulkan/examples/full-stack -B build/ffx-full-stack
cmake --build build/ffx-full-stack
./build/ffx-full-stack/ffx_vk_full_stack_contract
```

The executable does not create a Vulkan device. It verifies that the versioned
public APIs and their static libraries coexist. Replace it with the renderer's
normal device, command-buffer, image-state, submit/present, and fence code.

The application owns Vulkan and WSI. In particular, it must retain imported
images and call each provider's retire function only after the relevant GPU
fence has signalled. It can use individual `ffx-vulkan::*` targets instead of
`ffx-vulkan::effects` when it deliberately supports a smaller feature set.

The identical complete target is also available from an installed package; see
[`../installed-full-stack`](../installed-full-stack). That route is useful when
the host must not depend on this source tree.

FSR4 here is the experimental source-v07 INT8/DOT4 provider. It is not
official FSR 4.1.1, Ray Regeneration, or ML Frame Generation. Shader/model
assets are intentionally excluded from the project and must be supplied under
their applicable terms as one matching asset set.
