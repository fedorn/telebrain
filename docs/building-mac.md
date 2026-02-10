## Build instructions for macOS

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

**Note about disk space:** The full build process will require approximately **55 GB** of free space. This includes:
- **~35 GB** for libraries (when building for both x64 and arm64 architectures)
- **~20 GB** for the compiled Telebrain app (in the `out` folder)

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Clone source code and prepare libraries

Go to ***BuildPath*** and run

    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    brew install git automake cmake wget pkg-config gnu-tar ninja nasm meson

    sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

    git clone --recursive https://github.com/fedorn/telebrain.git
    ./telebrain/Telegram/build/prepare/mac.sh

### Building the project

Go to ***BuildPath*/telebrain/Telegram** and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    ./configure.sh -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH

Then launch Xcode, open ***BuildPath*/telebrain/out/Telegram.xcodeproj** and build for Debug / Release.

### Reducing app size (strip and re-sign)

If you build from Xcode only (and do not run `build/build.sh`), the app binary is not stripped and can be much larger than the official Telegram Desktop (~430 MB). To reduce size: strip the executables, then re-sign the app (stripping invalidates the code signature, so the app will not launch until you re-sign).

From ***BuildPath*/telebrain/out/Release**:

```bash
strip Telebrain.app/Contents/MacOS/Telebrain
codesign --force --deep --sign - Telebrain.app --entitlements ../../Telegram/Telegram/Telegram.entitlements
```

`--sign -` is ad-hoc signing (suitable for local use). For distribution, use your Developer ID certificate instead.


[api_credentials]: api_credentials.md
