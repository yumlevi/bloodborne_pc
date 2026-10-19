# Publishing the standalone repository

This directory is self-contained and can be published independently of
Q2RTX. The intended release artifact is the Git subtree rooted here; do not
copy Q2RTX's `baseq2/fsr4_shaders` directory, DLLs, screenshots, build trees,
or cached model payload into the new repository.

Before publishing, run the clean-tree check from this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Without a separately supplied source-v07 asset bundle, the FSR4 layout test is
intentionally absent and the redistributable suite contains 35 tests. Supply
`-DFFX_VK_PORTABLE_FSR4_V07_TEST_ASSET_DIR=/path/to/compatible/assets` only
when that bundle is lawful to use; the exhaustive layout test then raises the
count to 36.

From the Q2RTX checkout, create a review branch for the new repository:

```sh
git subtree split --prefix=extern/ffx-vulkan -b ffx-vulkan-release
git log --oneline ffx-vulkan-release
```

Push that branch to the intended empty GitHub repository only after reviewing
the resulting file list and `NOTICE.md`/`UPSTREAM.md`. The first standalone
tag should identify the source commit it was split from. The nested GitHub
Actions workflow becomes the repository-root workflow after the split.
