# Bellwood
_An Animal Crossing PC Port_

Lead Developers:
* [Lywx](https://www.github.com/kiritodv)

## Discord

Official Discord: https://discord.gg/harbourmasters

If you're having any trouble after reading through this `README`, feel free ask for help in the Support text channels. Please keep in mind that we do not condone piracy.

# Quick Start

Bellwood does not include any copyrighted assets. You are required to provide a supported copy of the game.

### 1. Verify your disc dump
The supported version is **Animal Crossing (USA)**, game ID `GAFE01`, revision 0. Dolphin shows both on the Info tab of the game's Properties.

The Japanese (`GAFJ01`), European (`GAFP01`) and Australian (`GAFU01`) versions are not supported yet.

### 2. Verify your disc is in a supported format
Bellwood reads `.iso`, `.gcm`, `.ciso` and `.gcz` disc images, or a folder extracted from the disc (containing `sys/` and `files/`). If your dump is an `.rvz` or `.wia`, convert it to `.iso` with Dolphin: right-click the game and choose **Convert File...**.

### 3. Get Bellwood
There are no releases yet. Build Bellwood yourself by following the [building instructions](#building).

### 4. Generating the O2R from the disc and Play!
* Run `bellwood` (`bellwood.exe` on Windows).
* On the first launch Bellwood offers to generate `ac.o2r` from your disc. It uses a supported disc placed next to the executable, or asks you to choose a disc image or an extracted disc folder.
* Extraction runs once; the game starts when it finishes.

You can also pass the disc on the command line: `bellwood /path/to/disc.iso`.

# Configuration

### Default keyboard configuration
| GameCube | A | B | X | Y | Z | L | R | Start | Control stick | C-stick | D-Pad |
| - | - | - | - | - | - | - | - | - | - | - | - |
| Keyboard | X | C | | | Z | E | R | Space | WASD | Arrow keys | TFGH |

X and Y have no keyboard keys by default. You can bind them in **Settings → Controls**.

### Other shortcuts
| Keys | Action |
| - | - |
| Esc or F1 | Toggle menu |
| Ctrl+R (Cmd+R on macOS) | Reset |
| F11 | Fullscreen |
| Tab | Toggle Alternate assets |

### Graphics Backends
Currently, there are four rendering APIs supported: DirectX 11 (Windows), OpenGL (all platforms), Metal (macOS), and Vulkan (only offered when your graphics driver supports it). You can change which API to use in the `Settings` menu, which requires a restart. If you're having an issue with crashing, you can change the API in the `bellwood.cfg.json` file by finding the `"Backend": {` block and changing the `Id` value; the game fills in the matching `Name` on the next launch. The IDs are `1` for `DirectX 11`, `2` for `OpenGL`, `3` for `Metal`, and `4` for `Vulkan`. `DirectX 11` is the default on Windows and `Metal` is the default on macOS.

### Widescreen
The game fills the whole window at any aspect ratio. **Settings → Graphics → HUD Aspect Ratio** keeps the HUD at the window edges or inside a 4:3, 16:9 or 21:9 area.

# Saves
Saves are stored in the `save` folder next to the executable.

To bring over a town from a GameCube memory card, use **Settings → General → Import GameCube Save...** on the title screen, or drop the file onto the window. Bellwood accepts `.gci` and `.gcs` saves and raw memory card images (`.raw`, `.gcp`). Passports, NES games and their saves, and gift letters on the card are imported with the town. Your previous save is backed up in `save/backup` first.

# Game Boy Advance and e-Reader
The PC has no GBA link, so the features that used one work with files instead. Each can be turned off in **Settings → Game Boy Advance**, which also has buttons to open the folders.

* **Island:** Kapp'n takes you to the island without a GBA.
* **e-Reader cards:** put card dumps (`.raw` or `.bin`) in the `ecards` folder. When the game asks for a card, choose one from the list, browse for it, or drop it onto the window.
* **Designs:** the Able Sisters' "Send to GBA" writes your designs as PNG files to the `designs` folder, and "Load from GBA" reads them back.
* **NES games:** memory card NES games (`.gci`) placed in the `famicom` folder show up in the Disk System.

# Custom Assets
Custom assets are packed in `.o2r` or `.otr` files. To use custom assets, place them in the `mods` folder.

If you're interested in creating and/or packing your own custom asset `.o2r`/`.otr` files, check out the following tools:
* [**retro - OTR and O2R generator**](https://github.com/HarbourMasters64/retro)

# Development
### Building

Requires:
* CMake 3.24 or newer
* A C++20 compiler: Visual Studio 2022 on Windows, Clang (Xcode) on macOS, Clang or GCC on Linux
* Git
* SDL2, plus GLEW on macOS (`brew install sdl2 glew`) and the OpenGL development files on Linux. On Windows the libraries are installed automatically through vcpkg.

1. Clone this repository with its submodules (Torch and libultraship):

   ```sh
   git clone --recursive <repository URL>
   ```

   If you cloned without `--recursive`, run `git submodule update --init --recursive`.

2. Configure and build:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

   The build writes `bellwood` and its `bellwood.o2r` to `build`.

3. Run `build/bellwood` and select your disc on the first launch, as in the [Quick Start](#4-generating-the-o2r-from-the-disc-and-play).

   To generate `ac.o2r` at build time instead, point the build at your disc and build the `ExtractAssets` target:

   ```sh
   cmake -S . -B build -DAC_DISC_PATH=/path/to/disc.iso
   cmake --build build --target ExtractAssets
   ```

### Android, iOS and Web

**Android** needs the Android SDK with NDK `30.0.15729638`, CMake `3.30.3` and JDK 17 or newer. Gradle drives the native build:

```sh
android/gradlew -p android assembleRelease
```

The APK is written to `android/app/build/outputs/apk/release/`. On first launch it asks for your disc image and generates `ac.o2r` on the device. There are no on-screen controls yet, so Android and iOS need a Bluetooth or USB controller.

**iOS** needs Xcode. Add `-DIOS_SIGNING=OFF` for an unsigned build to sideload:

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_TOOLCHAIN_FILE=cmake/ios.bellwood.toolchain.cmake -DPLATFORM=OS64
cmake --build build-ios --config Release --target Bellwood
```

Copy your disc image into **Files → On My iPhone → Bellwood** and start the app. GameCube saves go in the `import` folder there.

**Web** needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```sh
embuilder build zlib
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target Bellwood
python3 -m http.server -d build-web 8080
```

Open `http://localhost:8080/bellwood.html` and pick your disc image. It is read in the browser, and everything the game writes is kept in the site's storage.

### Xbox (Developer Mode)

Builds an x64 UWP app package for a retail Xbox in [Developer Mode](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation). Series X|S is the tested target; Xbox One has a much smaller memory budget and is untested.

Needs Visual Studio 2022 with both the **Universal Windows Platform development** workload and its C++ component, which are separate from the desktop C++ workload:

```sh
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vs_installer.exe" modify ^
  --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" ^
  --add Microsoft.VisualStudio.Workload.Universal ^
  --add Microsoft.VisualStudio.ComponentGroup.UWP.VC --passive --norestart
```

The package has to be signed with a certificate whose subject matches the manifest's `Publisher` (`CN=Lywx` by default). Create one once — it stays in your certificate store, so nothing secret lands on disk:

```powershell
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=Lywx" -KeyUsage DigitalSignature `
  -FriendlyName "Bellwood Xbox Dev Signing" -CertStoreLocation "Cert:\CurrentUser\My" `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") `
  -NotAfter (Get-Date).AddYears(3)
$cert.Thumbprint
```

Build with **Ninja and clang-cl**, from a shell where `vcvars64.bat` has been run — Ninja needs it to resolve MSVC's `link.exe` and `rc.exe` rather than any same-named tools earlier on `PATH`:

```sh
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-uwp -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/uwp.bellwood.toolchain.cmake \
  -DUWP_PACKAGE_CERT_THUMBPRINT=<thumbprint from above>
cmake --build build-uwp --target bellwood_uwp_package
```

That produces a signed `build-uwp/Bellwood.msix`.

Two notes on why this target is built differently from every other one:

- **clang-cl is required, not optional.** The decompiled structs only lay out correctly under clang-cl; MSVC gives `sizeof(Private_c) == 0x2398` against the required `0x2440`, which the `static_assert` in `m_private.h` catches. The desktop Windows build has the same requirement and meets it with `-T ClangCL`.
- **The package is assembled by hand.** MSBuild ships no ClangCL platform toolset for the UWP application type (only `v143`), so there is no way to have MSBuild both compile this code and build the appx. Instead CMake links the executable with `/APPCONTAINER` against the store CRT, then packs it with `MakeAppx` and signs it with `SignTool`. The `bellwood_uwp_package` target does all of that.

For CI, where there is no certificate store to read from, use `-DUWP_PACKAGE_CERT=<path to .pfx>` (plus `-DUWP_PACKAGE_CERT_PASSWORD=` if it has one) instead. A different publisher needs `-DUWP_PACKAGE_PUBLISHER="CN=..."` to match the certificate.

The console also has to trust the certificate before it will accept the package. Export the public half (no private key) and install it into **Local Machine → Trusted People** on the console via the Device Portal, or on the PC if you deploy from Visual Studio:

```powershell
Export-Certificate -Cert "Cert:\CurrentUser\My\<thumbprint>" -FilePath bellwood-public.cer
```

Deploy with the helper script, which uploads through the console's Device Portal and waits for the install. Pass the certificate on the first deploy so the console adds it to Trusted People:

```powershell
tools\deploy-xbox.ps1 -Console 192.168.100.66 -Package build-uwp\Bellwood.msix -Certificate bellwood-public.cer
```

Repeat deploys install over the top, which is an update and keeps the app's LocalState — the game archive, saves and mods all survive. Pass `-Clean` to uninstall first instead, which **deletes** all of it.

Dropping the `.msix` on the Device Portal's **Add** button in a browser does the same thing by hand. Visual Studio's remote deploy is not available for this target, because the package is not built by MSBuild.

The console has no file picker, so the game archive is uploaded rather than chosen. In the Device Portal's file explorer, put `ac.o2r` in the app's **LocalState** folder; the same folder holds `Bellwood.cfg.json`, `save/`, `mods/` and `logs/`. Dropping a disc image (`.iso`, `.gcm`, `.ciso`, `.gcz`) there instead works too — the app finds it on the next launch and extracts on-console, which is slow but needs no PC.

Two things differ from the desktop build by necessity:

- **DirectX 11 only.** There is no OpenGL or Vulkan driver on the console, and the renderer presents to a `CoreWindow` swapchain instead of an `HWND`.
- **No scripting or mod DLLs.** The app container forbids runtime code generation and `LoadLibrary`, so the TCC-based scripting runtime is compiled out, as it is for the other cross-platform targets.

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>

# Special Thanks:

* [ACreTeam](https://github.com/ACreTeam/ac-decomp) : for the Animal Crossing decompilation this port is built on.
* [Kenix3](https://github.com/Kenix3) : for helping with the Engine development that were brought into other ports.
* [HM64 Team](https://github.com/harbourMasters) : for creating Libultraship and Torch, and helping with various issues.

# AI Disclosure

Bellwood accepts pull requests that use AI, within policy guidelines. For more information please see the HarbourMasters [AI Policy](https://github.com/HarbourMasters/code-of-conduct/blob/main/AI_POLICY.md).
