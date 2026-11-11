<!--
Copyright 2013-2019 High Fidelity, Inc.
Copyright 2020-2021 Vircadia contributors
Copyright 2020-2026 Overte e.V.
SPDX-License-Identifier: Apache-2.0
-->

# Build macOS

*Last Updated on 2026-11-10*

Please read the [general build guide](BUILD.md) for information on dependencies required for all platforms.
This will include the necessary environment variables to customize your build. Only macOS specific instructions are found in this document.

## Prerequisites

**Xcode** needs to be installed from the App Store, which requires an Apple account. Select it for the command line tools:
```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
```

### CMake, NPM and Conan

[Homebrew](https://brew.sh/) is an excellent package manager for macOS. It makes the installation of some Overte dependencies very simple.

```bash
brew install cmake npm conan
```

**Note:** You can also download alternative CMake versions from [Github](https://github.com/Kitware/CMake/releases) if needed.

### Qt 6
#### system (default)
Overte uses Homebrew's Qt 6 unless told otherwise (Conan option `qt_source=system`). Install the modules it needs:
```bash
brew install qtbase qtdeclarative qtsvg qtwebchannel qtwebsockets qtmultimedia qt5compat qtimageformats qttools qtwebengine
```
Homebrew's Qt is a framework build; CMake finds it through Homebrew's prefix, no `PATH` changes are needed.

#### source
Conan can also build Qt from source (`-o 'Overte/*:qt_source=source'` on the `conan install` command below; this is what CI does). That needs an extra Python module:
```bash
pip3 install --user --break-system-packages html5lib
```
<!-- Qt Conan source package: Check for Python html5lib before attempting to build.
https://github.com/conan-io/conan-center-index/issues/27285
https://github.com/conan-io/conan-center-index/pull/29181 -->

### Prepare Conan

Add the Overte remote to Conan:
```bash
conan remote add overte https://artifactory.overte.org/artifactory/api/conan/overte -f
```

## Compiling

Install the dependencies with Conan:
```bash
cd overte
conan install . -s build_type=Release -b missing -pr:a="tools/conan-profiles/macos" -of build
```

Configure and build with Ninja (`-G Ninja` is required, see Troubleshooting):
```bash
cmake --preset conan-release -G Ninja -DOVERTE_RENDERING_BACKEND=Vulkan
cmake --build --preset conan-release --target Overte
```
The Interface target is called `Overte` on macOS. Leave out `--target` to build everything (server, tools, tests).

### Vulkan

On macOS the Vulkan backend runs on Metal through [MoltenVK](https://github.com/KhronosGroup/MoltenVK), which comes from Conan and is
linked into the application; no Vulkan SDK is needed. Qt's Vulkan support is pointed at that same MoltenVK at run time, so only one
copy is ever loaded.

### Running and installing

The build tree's `interface/Overte.app` runs as is, but uses Qt and the Conan libraries from where they were built.
To get a self-contained, relocatable `Overte.app`, install the `client` component:
```bash
rm -rf <prefix>/Overte.app
cmake --install build --prefix <prefix> --component client
```
This runs `macdeployqt`, which copies Qt (including QtWebEngine), the Conan libraries, MoltenVK and Overte's plugins into the bundle and
rewrites their references. It skips libraries that are already in the destination, so always remove an old `Overte.app` first.
Installing everything (no `--component`) also deploys the server components and needs a full build.

A bundle made from Homebrew's Qt runs on the macOS versions Homebrew built that Qt for, usually only the installed one,
whatever `os.version` the Conan profile sets.

Run the bundle from a terminal with `<prefix>/Overte.app/Contents/MacOS/Overte`. To keep macOS privacy permissions (microphone, ...)
across rebuilds, sign the bundle with a stable identity, e.g. `codesign --force --deep --sign <identity> <prefix>/Overte.app`.

To package the installation, you can run `cmake --build --preset conan-release --target package` afterwards.

### Xcode

Xcode projects (`-G Xcode`) are not supported: Xcode sandboxes user scripts by putting all environment variables into the command,
which then exceeds macOS's maximum command length. Use Ninja, and open the source directory in an IDE that understands CMake presets
(see [BUILD_IDE.md](BUILD_IDE.md)).

## Troubleshooting
- `error: unable to spawn process '/bin/sh' (Argument list too long)`
  You are using the Xcode generator. *Use Ninja instead, by appending `-G Ninja` to the CMake configuration step.*
- Try updating CMake via `brew upgrade cmake`. Apple breaks things often and fast, so CMake requires updates pretty often.
