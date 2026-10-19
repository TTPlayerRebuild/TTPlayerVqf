# Fixed dependencies and reproducible build

The wrapper is MIT licensed. The statically linked FFmpeg components are LGPL 2.1 or later. Complete license texts and copyright credits are retained in `LICENSES/`; no license resource is embedded in the DLL.

| Dependency | Version | Source SHA-256 |
|---|---|---|
| FFmpeg | 9.0.2 | `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e` |
| GNU make (build tool only) | 4.4.1 | `dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3` |
| VC-LTL | 5.3.1 | `7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad` |
| YY-Thunks | 1.2.2 | `518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc` |

Sources: [FFmpeg official release archive](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz), [GNU make official source](https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz), [VC-LTL](https://github.com/Chuyu-Team/VC-LTL5/releases/tag/v5.3.1), [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks/releases/tag/v1.2.2).

Build sources are downloaded and digest-checked by `cmake/build_ffmpeg.py` and CMake FetchContent; they are not vendored. The normal cache is this project's `build/ffmpeg`, not the private test tree. GNU make is bootstrapped from source with MSVC and is never linked into or distributed with the plugin. Git for Windows (Bash), Python 3.12 or newer, CMake 3.24 or newer, and MSVC Win32 build tools are required. Python 3.14 and VS 2026 were used for local verification.

The FFmpeg profile disables autodetection, network protocols, programs, filters, devices, external libraries, assembly, swscale and swresample. GPL, nonfree, and version3 components remain disabled; generated `config.h` records `LGPL version 2.1 or later`. Only the selected container and decoders are enabled. The standalone source, pinned dependency locations and complete build recipes allow rebuilding/relinking against a modified LGPL backend; binary redistributors must retain the applicable license and source/relink provisions.

## Local FFmpeg modification

`libavcodec/twinvq.c`: when the build defines `TTP_TWINVQ_KEEP_LEADING_FRAMES`, expose the first two synthesis frames instead of discarding them in FFmpeg, and zero the initial current/previous overlap histories. The zeroing is required because upstream never exposes those unprimed frames; without it, the legacy direct-Read path can emit stale heap contents. This macro is enabled only for the VQF profile. Original TTPlayer performs the discard in Reader.SetCallback (`801D1AD7`), rather than in the low-level decoder. The wrapper restores that sequencing. Other codec profiles have unchanged TwinVQ behavior and do not compile this decoder.

The configure output and build identity record are preserved in the build directory. Script/configuration changes force a clean backend rebuild. Windows native GNU make requires normalized drive-letter paths and disabled localized MSVC include-dependency scanning; these changes affect build files, not codec algorithms.


RM profile additionally patches `libavformat/rmdec.c` behind `TTP_RM_MP3_ADU` to recognize the original `audio/X-MP3-draft` MIME before the zero-length codec-header return. It restores MP3 ADU/audio classification and millisecond timestamps. This patch is inactive in every other profile. Enabled RM decoders are cook, atrac3, sipr, ra_144, ra_288, aac, ac3, eac3 (shared MSVC declarations) and mp3adu.

`-SourcePackage` adds a separate corresponding-source ZIP: complete FFmpeg source including compatibility changes, wrapper source, official GNU make source archive, full licenses and scripts. It excludes private tests, samples, build products and caches. The source bundle automatically selects `third_party/ffmpeg`; its complete source digest participates in the build identity so modifications trigger recompilation. Runtime ZIP contents remain DLL + SHA256SUMS only.
