## Password-protected portable identity

This fork adds startup password protection for the Moonlight client private key.

Moonlight normally stores its client certificate and private key in the settings file. This fork keeps the client certificate visible, but encrypts the client private key with a password before writing it to `Moonlight.ini`.

This is mainly useful for portable builds. If someone copies the whole portable Moonlight folder, they should not be able to use the already-paired client identity without knowing the password.

### What is protected

The protected data is the Moonlight client private key.

The password does not encrypt the whole configuration file. Host records and other settings may still exist in `Moonlight.ini`, but the paired client identity cannot be used unless the private key is decrypted successfully.

The encrypted private key is stored with fields like:

```ini
key_protection=password-v1
key_kdf=pbkdf2-sha256
key_iter=800000
key_salt=...
key_nonce=...
key_ciphertext=...
```

The old plaintext key field is removed:

```ini
key=...
```

### First run

On first launch, Moonlight asks for a password.

If no existing encrypted private key is found, the password entered on this first launch becomes the password for the new client identity.

Moonlight then generates a new client certificate and private key, encrypts the private key with that password, and writes the protected identity to `Moonlight.ini`.

Important: remember the first password. There is no recovery if it is forgotten.

### Normal launch

On later launches, Moonlight asks for the password before loading the main UI.

If the password is correct, the encrypted private key is decrypted and Moonlight starts normally.

If the password is wrong or the dialog is cancelled, Moonlight exits immediately.

### Forgotten password

If the password is forgotten, the old encrypted private key cannot be recovered.

To reset the client identity, delete or rename the portable settings directory, for example:

```bat
cd /d C:\test\MoonlightJimothyPassword
ren "Moonlight Game Streaming Project" "Moonlight Game Streaming Project.forgot-password"
Moonlight.exe
```

After reset, Moonlight will generate a new certificate and private key. All hosts must be paired again because the host sees this as a new Moonlight client.

### Pairing note

The password does not come from the host and pairing does not normally change the client private key.

The flow is:

```text
Moonlight generates client certificate + private key
Moonlight encrypts the private key with the startup password
During pairing, the host records/trusts the client certificate
On later connections, Moonlight uses the private key to prove it is the same paired client
```

If `Moonlight.ini` is deleted, a new client certificate/private key is generated. The old host pairing will no longer match and the host must be paired again.

### Identity self-check

This fork also checks that the certificate stored in settings matches the decrypted private key.

If they do not match, Moonlight stops with:

```text
Identity certificate and private key do not match
```

If they match, the log contains:

```text
Identity certificate and private key match
Identity certificate SHA256: ...
```

The SHA256 value is only the certificate fingerprint. The private key is not printed.

### Files changed for future upstream merges

When applying this patch to a newer official Moonlight Qt release, check these files:

```text
app/backend/protectedkeystore.h
app/backend/protectedkeystore.cpp
app/backend/identitymanager.cpp
app/app.pro
app/main.cpp
```

Required changes:

```text
1. Add ProtectedKeyStore class
   - PBKDF2-HMAC-SHA256
   - AES-256-GCM
   - salt / nonce / tag
   - key_protection=password-v1
   - key_kdf=pbkdf2-sha256
   - key_iter=800000

2. app.pro
   - add widgets to QT modules
   - add backend/protectedkeystore.cpp to SOURCES
   - add backend/protectedkeystore.h to HEADERS

3. main.cpp
   - use QApplication instead of QGuiApplication
   - this is needed for QInputDialog password prompt

4. identitymanager.cpp
   - ask for the password at startup
   - decrypt encrypted private key if it exists
   - migrate old plaintext key to encrypted storage
   - remove the old plaintext key field
   - create new credentials if no identity exists
   - verify certificate/private key readability
   - verify certificate/private key match
   - log certificate SHA256 fingerprint
```

### Expected log messages

First run with no existing identity:

```text
No existing credentials found
Wrote new protected identity credentials to settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Normal run with correct password:

```text
Loaded protected private key from settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Wrong password or cancelled dialog:

```text
Moonlight exits before loading the main UI
```

### Security scope

This is not full configuration encryption.

It protects the Moonlight client private key so that copied portable folders cannot directly reuse an already-paired client identity without the password.

It does not protect against malware already running as the same user while Moonlight is open, and it does not hide every value in `Moonlight.ini`.


# Moonlight PC

[Moonlight PC](https://moonlight-stream.org) is an open source PC client for NVIDIA GameStream and [Sunshine](https://github.com/LizardByte/Sunshine).

Moonlight also has mobile versions for [Android](https://github.com/moonlight-stream/moonlight-android) and [iOS](https://github.com/moonlight-stream/moonlight-ios).

You can follow development on our [Discord server](https://moonlight-stream.org/discord) and help translate Moonlight into your language on [Weblate](https://hosted.weblate.org/projects/moonlight/moonlight-qt/).

 [![Build](https://img.shields.io/github/actions/workflow/status/moonlight-stream/moonlight-qt/build.yml?branch=master)](https://github.com/moonlight-stream/moonlight-qt/actions/workflows/build.yml?query=branch%3Amaster)
 [![Downloads](https://img.shields.io/github/downloads/moonlight-stream/moonlight-qt/total)](https://github.com/moonlight-stream/moonlight-qt/releases)
 [![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-qt/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-qt/)

## Features
 - Hardware accelerated video decoding on Windows, Mac, and Linux
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - YUV 4:4:4 support (Sunshine only)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host
 
## Downloads
- [Windows, macOS, and Steam Link](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Snap (for Ubuntu-based Linux distros)](https://snapcraft.io/moonlight)
- [Flatpak (for other Linux distros)](https://flathub.org/apps/details/com.moonlight_stream.Moonlight)
- [AppImage](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Raspberry Pi 4 and 5](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Raspberry-Pi-4)
- [Generic ARM 32-bit and 64-bit Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-ARM%E2%80%90based-Single-Board-Computers) (not for Raspberry Pi)
- [Experimental RISC-V Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-RISC%E2%80%90V-Single-Board-Computers)
- [NVIDIA Jetson and Nintendo Switch (Ubuntu L4T)](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Linux4Tegra-(L4T)-Ubuntu)

### Nightly Builds
- [Downloads](https://nightly.link/moonlight-stream/moonlight-qt/workflows/build/master)

#### Special Thanks

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

Hosting for Moonlight's Debian and L4T package repositories is graciously provided for free by [Cloudsmith](https://cloudsmith.com).

## Building

### Windows Build Requirements
* Qt 6.7 SDK or later (earlier versions may work but are not officially supported)
* [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community edition is fine)
* Select **MSVC** option during Qt installation. MinGW is not supported.
* [7-Zip](https://www.7-zip.org/) (only if building installers for non-development PCs)
* Graphics Tools (only if running debug builds)
  * Install "Graphics Tools" in the Optional Features page of the Windows Settings app.
  * Alternatively, run `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0` and reboot.

### macOS Build Requirements
* Qt 6.7 SDK or later (earlier versions may work but are not officially supported)
* Xcode 14 or later (earlier versions may work but are not officially supported)
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Linux/Unix Build Requirements
* Qt 6 is recommended, but Qt 5.12 or later is also supported (replace `qmake6` with `qmake` when using Qt 5).
* GCC or Clang
* FFmpeg 4.0 or later
* Install the required packages:
  * Debian/Ubuntu:
    * Base Requirements: `libegl1-mesa-dev libgl1-mesa-dev libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libavcodec-dev libavformat-dev libswscale-dev libva-dev libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev`
    * Qt 6 (Recommended): `qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick`
    * Qt 5: `qtbase5-dev qt5-qmake qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-window2 qml-module-qtquick2 qtwayland5`
  * RedHat/Fedora (RPM Fusion repo required):
    * Base Requirements: `openssl-devel SDL2-devel SDL2_ttf-devel ffmpeg-devel libva-devel libvdpau-devel opus-devel pulseaudio-libs-devel alsa-lib-devel libdrm-devel`
    * Qt 6 (Recommended): `qt6-qtsvg-devel qt6-qtdeclarative-devel`
    * Qt 5: `qt5-qtsvg-devel qt5-qtquickcontrols2-devel`
* Building the Vulkan renderer requires a `libplacebo-dev`/`libplacebo-devel` version of at least v7.349.0 and FFmpeg 6.1 or later.

### Steam Link Build Requirements
* [Steam Link SDK](https://github.com/ValveSoftware/steamlink-sdk) cloned on your build system
* STEAMLINK_SDK_PATH environment variable set to the Steam Link SDK path

**Steam Link Hardware Limitations**  
Moonlight builds for Steam Link are subject to hardware limitations of the Steam Link device:
* Maximum resolution: **1080p (1920x1080)**
* Maximum framerate: **60 FPS**
* Maximum video bitrate: **40 Mbps**
* **HDR streaming is not supported** on the original hardware

### Docker containers
If you want to use Docker for building, look at [this repo](https://github.com/cgutman/moonlight-packaging) containing canonical containers
for different architectures, which handle building deps and extra linking for you.

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS, but you will need to use `brew install qt --with-debug` to be able to create debug builds of Moonlight.
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.12 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Download submodules and dependencies
    * Run `git submodule update --init --recursive` from within `moonlight-qt/`.
    * On Windows and macOS, you must also run `setup-deps.ps1` (Windows) or `setup-deps.py` (macOS).
    * Perform these steps each time you pull new changes from the Git repository.
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For Windows builds, use `scripts\build-arch.bat` and `scripts\generate-bundle.bat`. Execute these scripts from the root of the repository within a Qt command prompt. Ensure  7-Zip binary directory is on your `%PATH%`.
        * For macOS builds, use `scripts/generate-dmg.sh`. Execute this script from the root of the repository and ensure Qt's `bin` folder is in your `$PATH`.
        * For Steam Link builds, run `scripts/build-steamlink-app.sh` from the root of the repository.
    * To build from the command line for development use on macOS or Linux, run `qmake6 moonlight-qt.pro` then `make debug` or `make release`.
        * The final binary will be placed in `app/moonlight`.
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

## Contribute
1. Fork us
2. Write code
3. Send Pull Requests

Check out our [website](https://moonlight-stream.org) for project links and information.


