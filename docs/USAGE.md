# dots-kyunkyun-nya 1.0.2

Windows x64 After Effects effect. Apply it to a layer through **Effect > Stylize > dots-kyunkyun-nya**. It replaces the layer image with circular dots and transparent gaps. No panel, extension, generator, or separate application is installed.

## Install

1. Close After Effects and replace the existing `dots-kyunkyun-nya.aex` in `Support Files\Plug-ins\Effects`. Keep only one installed copy. Windows may request administrator approval.
2. Reopen After Effects. About should show version 1.0.2.
3. Apply the effect to the image or noise layer.
4. For the fast path, select **Mercury GPU Acceleration (CUDA)** in Project Settings.

The file is self-contained. No companion DLLs or CUDA Toolkit installation is required. CUDA acceleration is built for NVIDIA Turing, Ampere, Ada, Hopper, and Blackwell; this build was tested on an RTX 3080 with driver 581.95. Unsupported GPU devices fall back to the CPU. Windows ARM and macOS are not included.

## Controls

| Group | Control | Behavior |
|---|---|---|
| Source | Input | Luma, Alpha, Luma + Alpha, or Solid |
| Source | Response | Opacity for LED dots; Size for halftone; Size + Opacity for both |
| Source | Repeat Edges | Off: transparent beyond the source. On: repeat the outermost source pixels outward for stylization |
| Dots | Size | Maximum circle diameter in full-resolution square-pixel units |
| Dots | Gap | Space between maximum-size dots; grid pitch is Size + Gap |
| Dots | Offset X / Y | Move the grid in full-resolution units |
| Dots | Antialiasing | 0 for hard edges, 1 for standard coverage, up to 4 for softer edges |
| Output | Color | Dot color |
| Output | Opacity | Overall opacity multiplier |
| Output | Alpha Black Point | Levels-style input black handle: values at or below this become transparent |
| Output | Alpha White Point | Levels-style input white handle: values at or above this become fully opaque |

Defaults: Luma + Alpha, Opacity response, Repeat Edges off, Size 8, Gap 4, white, 100% opacity, Alpha Black Point 0%, Alpha White Point 100%, Antialiasing 0 (off). Existing instances retain their saved antialiasing value.

Input is sampled bilinearly at each dot center. With Repeat Edges off, out-of-bounds sample taps are transparent; the last row or column does not extend toward the composition boundary. With it on, the old edge continuation is restored. Luma uses Rec.709 weights on unpremultiplied input RGB; fully transparent samples contribute zero. Luma + Alpha multiplies luma by alpha. HDR signal values are limited to 0–1.

Dot RGB stays at the selected color independently of alpha. Input modulation, opacity and antialiasing change alpha only; raising alpha with a later Levels adjustment does not reveal gray RGB baked into translucent dots. Completely absent pixels remain zero. This is intentionally unassociated RGB output, replacing the previous premultiplied output behavior at the user's request.

Alpha input handles remap values between black and white to 0–100%. For example, Black 20%, White 80% maps incoming 20% to 0%, 50% to 50%, and 80% to 100%. This happens before overall opacity and edge coverage so sharpening alpha retains smooth circle boundaries. Crossing the handles makes them meet at the white point to form a threshold. These replace the old output-opacity clamps; their saved parameter IDs are retained, but old clamp values now act as input levels.

Size response uses the square root of the signal so circle area tracks brightness; its opacity is controlled by Output settings. Output stays within the original layer bounds. With Repeat Edges off, transparent unused output margins are excluded from the requested render buffer when the source bounds allow it.

Native, keyframeable AE controls. CPU support: 8, 16, and 32 bits per channel. GPU support: AE's floating-point CUDA worlds. Multi-frame rendering flag enabled; render settings are immutable per frame.

## Validation and performance

The compiled `.aex` was loaded and exercised through its exported effect entry point with an SDK host harness. It passed 36 edge-continuation regressions, Repeat Edges off/on tests, fixed-RGB checks through 1–100% opacity, 36 CPU/GPU image comparisons, 24 integer-depth comparisons, ROI consistency, sparse output bounds, stride/padding, empty-input, alpha input-level, stable parameter-ID and cleanup checks. The new source-edge regression failed against the old binary before the fix.

On the RTX 3080, 3000 × 3000 noise with antialiasing off measured **0.241–0.244 ms median GPU time**, with the slowest of 450 timed frames at **0.300 ms**. These warmed, GPU-resident timings exclude initialization, noise generation, host transfers, and the rest of the composition. A separate pageable upload + effect + download measurement was **48.84 ms**.

With antialiasing off in both versions, eight-worker CPU median times improved from 6.58 / 7.57 / 7.14 ms to **3.33 / 5.46 / 5.23 ms** for 8 / 16 / 32 bits per channel. The hard-edge path draws horizontal spans, reuses circle geometry, and samples once per dot. Per-worker scratch storage removes the old dense-grid fallback: an 8-bit Size 1.6 / Gap 0.4 case improved from 101.65 to 37.12 ms. Dense settings can still exceed 10 ms on CPU.

This revision requests only the source region needed by the output when Repeat Edges is off, avoids requesting hidden RGB, and skips source/render work for zero size or opacity. It also fixes invalid output rectangles when the source lies completely outside the layer. Repeat Edges retains full-source sampling for continuity.

**The user's approximately 20 ms AE timing has not been remeasured; 10 ms in the actual composition is not verified.** The effect's About dialog now reports the last render backend (CPU or CUDA) and buffer dimensions, across all instances, to help identify the actual render path.

**This revised build has not been installed or tested in the user's running AE composition.** Installation is left to the user as requested. See `Validation.txt` for recorded results.

## Build

The source is in source/. Build products go into build/ and dist/. Requires Adobe After Effects SDK 25.6 headers/utilities, Visual Studio 2022 C++ tools with Windows SDK, PowerShell 7, and CUDA Toolkit 13.2.

Run `source\build.ps1 -SdkExamples "path\to\SDK\Examples"`. Optional arguments: `-CudaPath`, `-BuildDir`, and `-OutputDir`. Build files go into the selected build directory. Adobe SDK files are not redistributed.

Run `source\verify.ps1 -SdkExamples "path\to\SDK\Examples"` to rebuild and run the integration harness. An NVIDIA GPU and driver are required for verification. The harness writes a noise-output TGA into the build directory.
