# Lightroom UltraHDR

A Lightroom Classic plugin that merges an SDR rendition and an HDR rendition of
the same photo into a single **UltraHDR** (ISO 21496-1) gain-map JPEG — a file
that shows full HDR on capable displays and falls back cleanly to the SDR image
everywhere else.

The plugin bundles a small native encoder (`uhdrtool`) built on
[libultrahdr](https://github.com/google/libultrahdr); there is no ffmpeg or
external dependency at runtime.

> Status: early. This README is a skeleton — fuller docs and examples to come.

## For users (install and use)

You do **not** need to compile anything. Grab the prebuilt `.lrplugin` bundle
from the [Releases](../../releases) page, unzip it, and add it in Lightroom via
**File ▸ Plug-in Manager ▸ Add**.

Then:

1. Take your photo and create an HDR edit (HDR mode on) and an SDR edit of it.
2. Select both in the Library grid.
3. **File ▸ Plug-in Extras ▸ Merge SDR + HDR to UltraHDR…**
4. Pick an output location and run.

The plugin figures out which selected photo is the HDR one and which is the SDR
one automatically, exports both renditions, and writes the merged UltraHDR JPEG.

_Export settings and a full walkthrough with examples: TODO._

## For developers (build from source)

Requires CMake, a C++17 compiler (MSVC on Windows), `git`, and
[NASM](https://www.nasm.us/) (libjpeg-turbo's SIMD needs it). libultrahdr is
fetched from upstream at configure time (pinned to an exact commit) and built from
source together with its libjpeg-turbo dependency — nothing third-party is
committed to this repo.

```bash
cmake -B build
cmake --build build --config Release --target uhdrtool
```

That produces `build/Release/uhdrtool.exe` and copies it into the plugin bundle
at `lua/lightroom-hdr.lrplugin/bin/win/` automatically.

`uhdrtool` on its own:

```
uhdrtool --hdr input.tif --sdr input.jpg --out result.jpg
```

`--hdr` is a 32-bit float TIFF (Lightroom HDR export, "Maximize Compatibility"
**off**); `--sdr` is a standard JPEG of the same image.

## Layout

| Path              | What                                                        |
| ----------------- | ----------------------------------------------------------- |
| `src/`            | `uhdrtool` C++ source (TIFF reader, gain-map encoder)       |
| `lua/`            | the `.lrplugin` Lightroom plugin                            |
| `licenses/`       | third-party attribution (`NOTICE.md`)                       |
| `.github/`        | CI: build, package the bundle, publish on tag               |

libultrahdr (and its libjpeg-turbo dependency) are fetched at build time, not
committed — see `CMakeLists.txt`.

## License

[Apache-2.0](LICENSE). Bundled third-party components are listed in
[`licenses/NOTICE.md`](licenses/NOTICE.md).
