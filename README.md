# IndyPort: Indiana Jones and the Emperor's Tomb on PC

An unofficial native PC port of the original Xbox version of *Indiana Jones and the Emperor's Tomb* (USA), made by
statically recompiling the game's code with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp). It comes
with **IndyPort**, a launcher for choosing your copy of the game and setting up graphics and controls.

> [!NOTE]
> No game files are included. The port runs from your own legally acquired copy of the game: a disc image (`.iso`)
> or the extracted files of the Xbox (USA) release.

## Getting started

1. Download the release and unzip it anywhere.
2. Run **IndyPort.exe**.
3. On the **Game** page, choose your disc image (`.iso`, a whole disc dump or an xiso) or the folder of the
   extracted game (the one with `default.xbe`). Nothing is copied; the port reads the game where it is.
4. Press **Play**. The first start compiles the game's code for your PC; this only happens once.

Without a choice in IndyPort, the port also finds the game in an `assets` folder beside it.

## Features

### Display and performance
- Fullscreen, borderless or windowed, at any resolution.
- Render scale from 50% to 200% (supersampling above 100%).
- Frame rate limit of 30, 60, 120, 144, your monitor's refresh rate or unlimited. The game keeps its speed at any
  frame rate.
- An FPS counter (F9 in the game toggles it).

### Picture
- **Sharp text and HUD**: the game's fonts, menus and HUD are upscaled on your PC with smooth outlines, so they stay
  crisp at 4K.
- **Modern Xbox button icons** in the game's prompts and menus (LB/RB for White/Black, LT, RT, LS, RS), drawn for
  high resolutions. The original icons are one click away.
- **Sharpening** (contrast adaptive) and up to 16x anisotropic filtering for the game's low resolution textures.
- **Draw distance** up to 400% of each level's own.
- **Sharper movies**: the game's 640 x 480 videos are scaled with a sharper filter.
- Texture packs: images in `textures\replacement` replace the game's textures; the game's textures can be dumped
  to make them.

### Controls
- Xbox, PlayStation and Switch controllers, with hot plugging and rumble.
- Remap every button of the game's controller in IndyPort, with a reset to defaults.
- Keyboard: move W A S D, camera I J K L, d-pad arrow keys, A Space or Enter, B Q or Backspace, X E, Y R,
  Start Enter, Back Tab, triggers Left Shift / Left Ctrl, Black / White C / Z, stick clicks V / B.

### Saves
- Saves live on the port's Xbox hard disk (`hdd\E\UDATA` beside the exe). IndyPort lists them and backs them up.

## System requirements
- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 12

## Building

1. Clone this repository with its submodules, and apply `sdk-patches\*.patch` to `sdk`.
2. Put your extracted game files in the `assets` folder: `default.xbe` with the game's folders beside it.
3. Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and
   **C++ CMake tools for Windows** (`build.bat` offers to add the last two).
4. Run `build.bat`. The first build recompiles the game and takes a while. `IndyET.exe` and `IndyPort.exe` are
   written to `out\build\release`; `release.bat` makes the zip.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482, the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis), [toml++](https://github.com/marzer/tomlplusplus),
  [Dear ImGui](https://github.com/ocornut/imgui) and [SDL](https://github.com/libsdl-org/SDL).
- The launcher's jungle temple painting is royalty-free art; the modern button icons are drawn by
  `tools\make_button_icons.ps1` from shapes and text.

## Legal

Indiana Jones is a trademark of Lucasfilm Ltd. This project is not affiliated with or endorsed by Lucasfilm,
LucasArts, Disney or Microsoft. It contains no game code or assets; you need your own copy of the game.
