# ISP Image Viewer

A lightweight cross-platform desktop image browser and synchronized comparison tool. The JPG/PNG
MVP has been completed and accepted. The project now moves into release-candidate stabilization;
existing RAW/YUV code is retained but is not the active product-expansion direction.

## Documentation

- [Documentation index](docs/README.md)
- [User manual](docs/user-manual.md)
- [Current features and roadmap](docs/feature-status-and-roadmap.md)
- [Architecture](docs/architecture.md)
- [Development log](docs/development-log.md)
- [Development handoff](docs/development-handoff.md)
- [Test checklist](docs/test-checklist.md)
- [Next-work TODO](docs/todo.md)

## Implemented features

- Explorer-style directory tree, history navigation, current-folder search, file grid, and resizable
  preview pane; the multi-folder workspace uses independent clickable breadcrumbs
- Asynchronous JPEG/JPG/PNG scanning, decoding, and thumbnails
- GPU-backed image canvas using Qt RHI
- Fit, 100%, cursor-centered zoom, pan, pixel coordinates, and RGBA inspection
- Full-screen browsing and synchronized 2–4 image comparison
- FastStone-style four-edge full-screen overlays, plus a context menu for file-manager reveal and
  pinned in-canvas EXIF or histogram inspection
- Immersive comparison window with a single top toolbar, per-pane file/EXIF/histogram/pixel
  overlays, vertical/horizontal split for two images, and press-and-hold B-over-A inspection
- Independent 2×2 multi-folder workspace; select 2–4 images within or across panes and launch the
  normal comparison view
- Byte-bounded memory caching and persistent thumbnail caching
- Incremental directory monitoring with a folder-first thumbnail grid, always-visible filenames,
  and selectable natural-name/date/size/type sorting
- Drag images or folders out through native file URLs to Finder/Explorer; external drops copy into
  the current directory without changing navigation
- System clipboard Copy/Cut/Paste for images and folders, with background transfer, no-overwrite
  conflict naming, atomic same-volume move, and copy-before-delete cross-volume move
- Explorer-style file/folder context menus, safe single-item rename, Properties,
  Finder/Explorer reveal, and system Trash integration
- Trash is available from the thumbnail context menu and window-level platform shortcuts (macOS
  Command+Delete/Backspace, Windows Delete), with a confirmation dialog whose warning can be
  disabled by the user; text editors retain their normal delete behavior
- Parameterized NV12/NV21/I420/P010 and Bayer RAW10/12/16 decoding with GPU Plane display,
  including little- and big-endian P010
- A docked RAW/YUV parameter panel that updates the current preview and thumbnail after a short
  debounce; headerless data is deliberately interpreted as a single frame
- Original-value RAW/YUV pixel inspection and per-file sidecar parameters
- Lossless RAW/YUV display orientation at 0/90/180/270 degrees with immutable source-plane probes
- RAW white balance, 3×3 color correction matrix, and display gamma preview
- Dockable asynchronous display RGB/luma histogram with bounded sampling and stale-result rejection
- Normalized ROI drag selection remains available internally for histogram region analysis, but is
  intentionally absent from the main toolbar
- Selectable source-plane Y/U/V or Bayer R/Gr/Gb/B histogram and ROI statistics that preserve
  original bit depth, packing, stride, byte order, and display orientation mapping
- Optional LibRaw-backed DNG/camera RAW thumbnails and full previews with typed camera, lens,
  exposure, aperture, ISO, focal-length, capture-time, and sensor-size metadata
- Optional Exiv2-backed EXIF/IPTC/XMP metadata for JPEG/PNG, including source Orientation,
  descriptive fields, typed camera data, and privacy-preserving GPS-presence reporting
- Optional LittleCMS conversion of embedded RGB ICC profiles in JPEG/PNG to a fixed sRGB display
  buffer, with profile fingerprint, transform provenance, bounded memory, and Alpha preservation
- Dockable Properties panel shared by encoded, headerless RAW/YUV, and camera RAW frames

An optional directory may be passed on the command line:

```sh
ISPImageViewer /path/to/images
```

## Requirements

- CMake 3.25+
- C++20 compiler
- Qt 6.7+ with Widgets, ShaderTools, and private Gui headers
- Optional LibRaw 0.21+ for DNG and camera RAW support
- Optional Exiv2 0.28+ for JPEG/PNG EXIF, IPTC, and XMP metadata
- Optional LittleCMS 2.x for embedded RGB ICC conversion

Qt 6.9.x is the validated development version.

Camera RAW support is enabled automatically when a usable LibRaw installation is found. Builds
without LibRaw remain supported and simply omit camera RAW files from the browser. With vcpkg,
enable the manifest feature `camera-raw`; alternatively install LibRaw through the platform package
manager. `ISPVIEW_ENABLE_LIBRAW=OFF` explicitly disables the adapter. CMake performs a native macOS
runtime probe so a stale dynamic library with missing transitive dependencies cannot break the app.

JPEG/PNG metadata support is enabled automatically when Exiv2 is found and can be disabled with
`ISPVIEW_ENABLE_EXIV2=OFF`. The non-default vcpkg feature is `metadata-exiv2`. Exiv2's current
vcpkg package is GPL-2.0-or-later, so distributors must review that license before enabling and
shipping this optional feature. Metadata failures never replace a successful pixel decode, and GPS
coordinates are intentionally neither retained nor displayed.

Embedded ICC conversion is enabled automatically when LittleCMS is found and can be disabled with
`ISPVIEW_ENABLE_LCMS2=OFF`; the non-default vcpkg feature is `color-management`. The current slice
normalizes encoded JPEG/PNG pixels to sRGB before GPU upload. It does not yet transform sRGB into a
specific monitor profile or provide an HDR swapchain, so it must not be described as complete
end-to-end display calibration.

TIFF, WebP, OpenEXR, HEIC/HEIF, AVIF, JPEG XL, PSD, SVG, PDF, and GIF are intentionally outside the
current lightweight MVP. They are not listed by the browser or accepted by the encoded decoder.

## Build on macOS

Recommended wrapper:

```sh
./build_macos.sh debug --test
./build_macos.sh release
```

The wrapper configures and builds the matching CMake preset. Use `./build_macos.sh --help` for
`--clean`, `--rhi`, and `-j N`.

Equivalent raw CMake commands:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

The application bundle is generated under `build/macos-preset-debug/src`.

Repeatable CPU decode and native GPU upload benchmarks are built by the Release presets:

```sh
cmake --build --preset macos-release
./build/macos-preset-release/tools/ispview_raw_benchmark --48mp
./build/macos-preset-release/tools/ispview_histogram_benchmark --48mp
./build/macos-preset-release/tools/ispview_color_benchmark --48mp
./build/macos-preset-release/tools/ispview_gpu_benchmark --48mp
./build/macos-preset-release/tools/ispview_gpu_benchmark --bayer --48mp
./build/macos-preset-release/tools/ispview_gpu_benchmark --pipeline --48mp
./build/macos-preset-release/tools/ispview_ui_benchmark --48mp
./build/macos-preset-release/tools/ispview_ui_benchmark --encoded-directory test_images
./build/macos-preset-release/tools/ispview_ui_benchmark --raw-directory test_images \
  --candidate-raw16 6236x4178:14:RGGB --orientation 180
./build/macos-preset-release/tools/ispview_sample_check --allow-incomplete test_images
./build/macos-preset-release/tools/ispview_gpu_benchmark --sample-directory test_images
./build/macos-preset-release/tools/ispview_sample_check --allow-incomplete \
  --candidate-raw16 6236x4178:14:RGGB --orientation 180 test_images
./build/macos-preset-release/tools/ispview_gpu_benchmark --sample-directory test_images \
  --candidate-raw16 6236x4178:14:RGGB --orientation 180
```

The GPU benchmark requires a native display session and fails if RAW/YUV rendering falls back to CPU RGBA.
The UI benchmark additionally opens the real `MainWindow`, drives sequential thumbnail navigation,
and measures the first visible image and Full plane submissions. After timing, it sends a native
mouse ROI drag and requires normalized ROI state, a visible overlay, and converged region statistics;
it then switches to source-plane mode and requires the YUV ROI statistics to converge. Placing these
checks after timing prevents them from warming navigation prefetch. Its `--encoded-directory` mode
drives sequential thumbnail navigation using real JPEG/PNG files. Its `--raw-directory` mode copies
RAW samples into a temporary directory, writes temporary sidecars from `--candidate-raw16`, and
requires Preview, Bayer GPU Full, logical source size, bounded fallback, and status-bar pixel probe.
It does not modify the source samples or persistent user settings. It also fails on CPU fallback.
`ispview_sample_check` validates local real-world samples through the production decoder registry;
it reports unsupported formats and unconfigured headerless files separately from decoder failures.
The optional `--candidate-raw16 WIDTHxHEIGHT:VALID_BITS:CFA` interpretation is process-local and
does not write sidecars or settings; add `--msb-aligned`, `--big-endian`, or
`--orientation 0|90|180|270` when required. Orientation is applied only to the process-local RAW
candidate. It changes display geometry and rendering while probes continue to read the mapped
source pixel from the immutable Plane.
Local `test_images` data is intentionally ignored by Git.

## Build on Windows

Use the same Visual Studio 2022 x64 presets as CI:

```pwsh
.\build_windows.ps1 -Mode debug -Test
.\build_windows.ps1 -Mode release
```

The wrapper configures and builds the matching CMake preset. Use `.\build_windows.ps1 -Help` for
`-Clean`, `-Rhi`, and `-Jobs N`.

Equivalent raw CMake commands:

```pwsh
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The multi-configuration build writes executables below `build/windows-debug`, with Debug tools
and tests in their respective `Debug` subdirectories. The Release preset enables benchmark tools:

```pwsh
cmake --preset windows-release
cmake --build --preset windows-release
```

To turn the platform RHI test into an acceptance gate that must not silently skip when no native
surface is available, run the platform preset:

```sh
ctest --preset macos-rhi-acceptance
# On Windows:
ctest --preset windows-rhi-acceptance
```

The Windows CI uses these presets and requires the Direct3D 11 path at three levels: framebuffer
Golden tests, the 4K Loader/prefetch/GPU pipeline benchmark, and real 4K `MainWindow` frame
navigation. The benchmark timings are diagnostic only; functional fallback, missing native
surfaces, or incomplete frame submission fail the steps. A green ordinary CTest run alone is not
sufficient native-GPU evidence.

## Architecture

See [docs/architecture.md](docs/architecture.md).

For the current implementation status, validation evidence, known issues, and exact handoff point,
read [docs/development-handoff.md](docs/development-handoff.md) before continuing development.
