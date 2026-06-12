# Third-party notices

uhdrtool statically links the following open-source components. They are fetched
from upstream at build time (not committed to this repo); their license texts are
available at the upstream URLs and in the fetched source under `build/_deps/`.

## libultrahdr
- Upstream: https://github.com/google/libultrahdr
- Pinned commit: 13a058f452d846e43d4691f6885eeeaa8b0ea8d0 (v1.4.0-18-g13a058f)
- License: Apache-2.0 (with MIT-licensed portions)
- Fetched by CMake `FetchContent` — keep the pinned commit here in sync with
  `CMakeLists.txt`.

## libjpeg-turbo
- Built from source as a libultrahdr dependency (UHDR_BUILD_DEPS), version 3.1.0
- License: IJG / BSD-style / zlib
- Cloned by libultrahdr's own build (`ExternalProject_Add`, pinned to tag 3.1.0).
