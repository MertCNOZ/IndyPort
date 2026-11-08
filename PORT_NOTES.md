# Port notes: Indiana Jones and the Emperor's Tomb (Xbox, USA)

## Game image
- Redump USA disc, XISO made with extract-xiso (MD5 of the Redump ISO: e2f50e5ea83e9a247d593d4ec12ca0da)
- Title ID 0x4C410007, entry 0x47DE05
- **XDK 5120** for all libraries (XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL, WMVDEC, LIBCMT)
- No XNET section, so the networking symbols do not apply. Movies use WMV (WMVDEC), not Bink.

## Tools in this repo
- `tools/xbe_info.py <xbe|iso>`: title, sections, XDK library versions
- `tools/xiso_extract.py <iso> assets`: extracts the disc into `assets/`
- `tools/vsenv.bat <cmd>`: runs a command with the VS 2022 Clang/CMake/Ninja toolchain

## XDK symbols (2026-11-08)
XboxXBERecomp has signatures for 4831 and 5849 only. Running `xbe_symbols` with each set renamed as 5120:

| Signatures | Found |
|---|---|
| 4831 | 78 |
| 5849 | 58 |
| Merged (no conflicts) | 89 |

`config/game.toml` holds the merged result. 131 names are still 0, ignoring networking and Bink. The core ones
(Direct3D_CreateDevice, D3DDevice_Swap/Clear/DrawVertices, DirectSoundCreateBuffer, the D3D_* data) must be found
by hand in Ghidra. Not all of the 131 are necessarily linked into this game.

## Finding symbols by hand (2026-11-08)
- `tools/ghidra_scripts/ExportLibs.java` dumps the library sections (decompiled C + asm) and the call graph of
  all 11,690 functions to `out/ghidra/export/`. Run it from a normal terminal:
  `.\tools\ghidra.bat headless "D:\Game Setups\Indy\ghidra" IndyET -process IndyET_xbe.exe -noanalysis -scriptPath "D:\Game Setups\Indy\tools\ghidra_scripts" -postScript ExportLibs.java "D:\Game Setups\Indy\out\ghidra\export"`
- `tools/find_in_export.py` searches that export by asm patterns (NV2A methods, struct offsets, constants).
- `tools/align_symbols.py` guesses addresses from a reference game's symbol order (007 NightFire, 4831):
  81% right in a leave-one-out test, so its guesses are only leads to verify.
- Found by hand so far (evidence in config/game.toml comments): Direct3D_CreateDevice, D3DDevice_Swap, Clear,
  End, SetIndices, DrawVerticesUP, Get{RenderTarget,DepthStencilSurface,BackBuffer}2, SetScreenSpaceOffset,
  SetGammaRamp, BlockUntilVerticalBlank, SetTextureStageStateNotInline, D3D_SetFence and the data D3D_pDevice,
  D3D_DirtyFlags, D3D_IndexData, D3D_Streams, D3D_DeferredTextureState.
- DirectSound and input: CDirectSoundStream GetInfo/GetStatus/Process/Flush (read from DSound_StreamVtable),
  IDirectSoundBuffer_SetAllParameters, IDirectSound EnableHeadphones/SetDistanceFactor/SetDopplerFactor/
  SetRolloffFactor, XInputOpen, XInitDevices. XGetDevices is 0x5194C8, XGetDeviceChanges 0x5194EA.
- The DSOUND section only holds what the game uses (the linker dropped the rest), so most DirectSound names
  still at 0 are simply not in this game: the wrapper rows at 0x4B07C0-0x4B08BD and 0x4B0EF3-0x4B0F63 and the
  listener rows at 0x4B17AD-0x4B18ED are all named now. 0x4B07E7 is IDirectSound_GetTime (KeQuerySystemTime,
  kernel ordinal 128), which the SDK does not replace.
- Status: 109 symbols found, 139 at 0. Of those, ~45 are networking/Bink (not in this game), 43 are D3DDevice
  (mostly render states, lights, push buffers; not all are necessarily linked), ~40 DirectSound (likely not
  linked, see above), 7 CRT math. Data still 0: D3D_FvfShader, D3D_RenderState, XPP_MemoryUnitType.
- CRT math: only crt_acos, crt_asin and crt_pow call the CRT dispatch helpers 0x4863C5/0x4863DC, and all three
  are found. The game uses sin, cos, tan, atan, sqrt, log and log10 as compiler intrinsics (inline FSIN, FCOS,
  ...; `tools/ghidra_scripts/FindCrtMath.java` lists them), so those crt_* stay 0.
- XInputGetCapabilities: the 4831 signatures matched 0x51E3A9 by constants (0.60), but that function takes one
  argument and has no callers; the game does not link it, so it is 0 again. The other constant matches were
  checked by argument count and NV2A method (SelectVertexShader 0x1EA0, StencilEnable 0x32C, LineWidth 0x380).
- Leads: 0x49EE80 CDevice_KickOff?, 0x49F340 MakeSpace?, 0x49D260 D3DResource_AddRef? (the surface getters call it).

## 5120 device layout
**5120 is its own layout**, see `xdk5120/layout.draft.toml`: the front of CDevice mostly matches 4831, a few
fields are 4 bytes lower (Time 0x2C, PixelShader 0x36C, VertexShader 0x37C, IndexBuffer 0x388), and everything
from RenderTarget on, the miniport block included, sits 0xC00 lower (miniport 0x17C0, SwapCount 0x2010, size
0x2030). ZScale moved too (0x50C; +0x510 now holds the surface pitch). **All 45 values are proven**, so the
draft can become `sdk/xdk/5120/layout.toml`; DSOUND and XPP match 4831.

## First build and boot (2026-11-08)
- `xdk5120/layout.draft.toml` is copied to `sdk/xdk/5120/layout.toml` (inside the sdk submodule, so it is not in
  this repo's commits yet; it needs a fork or an upstream PR). The file must start its values with `[layout]`.
- `tools\vsenv.bat cmake --build out\build\release` builds in a few minutes. On first start the exe compiles the
  game code from the player's own default.xbe (about 5 minutes), then reuses it.
- Fixed on the way: XInputGetCapabilities false match, missing `[layout]` header, D3D_RenderState (0x4A93D0) and
  D3D_FvfShader (0x4AB878). 5120 numbers render states like 4831/5659/5849 (proven by four setters).
- Third launch: D3D12 comes up on the GPU, the DirectSound effects image loads, the intro logos play with sound,
  then a black screen before the main menu.
- Black screen cause: an access violation writing guest 0x84000000 (the end of the 64 MB) from
  D3DDevice_DrawIndexedVerticesUP 0x49CC30, called by the game at 0x320DB1. The runtime replaces that function, but
  it is in no signature list, so xbe_symbols never wrote it; it is added to [symbols] by hand now. Comparing the
  runtime's HLE_* names with the config found no other gap that this game links (SetBackBufferScale and
  IDirectSound_CreateSoundStream are not in it).
- Also named on the way: the D3D wait and push buffer functions CDevice_KickOff, D3D_BlockOnTime,
  D3D_BlockOnResource, D3D_BlockOnNonSurfaceResource, D3D_KickOffAndWaitForIdle, D3D_MakeRequestedSpace and
  D3DDevice_MakeSpace (their lengths match the 4831 signatures).
- `tools/ghidra_scripts/DumpAt.java <out> <addr>...` dumps the functions holding given addresses (crash stacks).
- Fourth launch: no crash; it stops on the Loading screen.
- Loading screen "hang" is the game waiting for a button: FUN_000776b0 sets the loader callback 0x773F0 through
  the loading object's vtable 0x5FF7C0 (slot 0x190 = 0x31A020 stores it at 0x64A248), then slot 0x18C (0x31AAA0)
  loops `while (callback() && slot_1A0()) Sleep(15)`. The callback polls the pad manager at [0x6675E8]
  (0x77F60 polls 4 ports of 0x3C-byte states, 0x77FD0 returns the active port's) and waits for a press and release
  of state bits 0x1/0x1000. With no controller the runtime reports no ports, so it waits forever.
- Fix: `src/common/keyboard.cpp` makes the keyboard port 0's controller through GameInfo::pad (W A S D left
  stick, I J K L right stick, arrows d-pad, Space/Enter A, Q/Backspace B, E X, R Y, Enter start, Tab back,
  Left Shift / Left Ctrl triggers, C / Z black / white, V / B thumbs).
- Debugging hangs: F3 in the game window logs every guest thread's guest stack (`tools/send_f3.ps1`,
  `tools/hang_probe.ps1` do it from a script). The recompiled code keeps guest return addresses on the guest
  stack, not the host one, so host minidumps (`tools/dump_process.ps1`, `tools/stack_scan.py`) show little.
  sdk/runtime/app/main.cpp DumpThreads scans 512 words instead of 32 (local sdk change).
- `tools/ghidra_scripts/FindRefs.java` lists code and data references to values (globals, vtable pointers).
- 0x47AE95 is Sleep (one argument), not WaitForSingleObject; 0x329FC0 is a Sleep-based lock.
- Main menu reached (keyboard Enter on the Loading screen). The keyboard now always reports port 0 connected, so
  the game sees a controller even when its window opens behind another one.
- Second hang: the render lock 0x329FC0/0x32A0A0 (object 0x6D64C4) is a plain count (`while (count) Sleep; count++;
  if (count != 1) count--`), safe on the single CPU Xbox but racy with guest threads in parallel; a lost update
  left the count at 1 with no owner. Fix: `kernel::g_guest_affinity` (new in sdk/runtime/kernel, opt-in, applied in
  Spawn) and `src/common/cpu.cpp`, which pins every guest thread to one CPU from GameInfo::init. Log line
  `cpu: game threads on CPU mask 2`. With it the texture dumper (also caught in that deadlock) works.
- Only LOGR lines reach release logs; LOGI (thread starts, "controller ports now") are filtered out.
- Fonts: 512x512 63a2aab7d65f6005 and 256x512 60a1460f61441655 (DXT5, glyphs in alpha) decode correctly
  (`tools/png_alpha.py` shows the alpha), so the garbled menu text comes from how its quads are drawn.

## Playable (2026-11-08)
- With guest threads on one core the first level (the jungle, with the stone head) loads and plays; the tip box
  text renders correctly. Starting a new game made the game relaunch its own default.xbe (log: "Relaunched").
- Graphics bugs seen:
  - Sky: large white translucent sheets. Frame 131130: draws 0009-0045 (blend srcalpha/one) and 0046-0082
    (srcalpha/invsrcalpha), vertex program B01C9821 (vs_661671090BACCE2F.hlsl, a lit reflective program using
    c[97..104] and point lights c[170..182]), texture 82B11000 = clouds (decodes fine). Suspect the vertex
    program's outputs (alpha/fade or positions), not the texture.
  - Menu: text and lens flare skewed into rows at the top left live; in the 640x480 capture (130925) the font
    draws 0147-0166 (vs B01C8BA1, prim 5/6, fonts 830E8000 512x512 / 830D2000 256x512) leave nothing visible.
- Capture workflow: `$env:XBE_CAPTURE_DIR = '...'`, F3 in game; `tools/frame_summary.py <frame.txt>` groups draws by
  state, `tools/bmp2png.py` converts the BMPs (final.bmp, tex_*.bmp, --alpha), textures dump to
  out/build/release/textures/dumps with `dump_textures = true` in settings.toml.

## Open questions
- The old PC release shares the codebase and can help name game functions (camera, frame limiter, menus).

## Ghidra
- Ghidra 12.2 in `D:\Tools\ghidra_12.2_PUBLIC`, using Temurin JDK 25.0.5 in `D:\Tools\jdk-25.0.5+8`
  (set in Ghidra's `support\launch.properties`; the system Java 1.8 is untouched). Ghidra 12.x needs JDK 25+.
- `tools\ghidra.bat` opens the GUI; `tools\ghidra.bat headless <args>` runs analyzeHeadless.
- Project: `ghidra\IndyET.gpr` (gitignored), program `IndyET_xbe.exe` made by
  `python sdk/tools/symbols/xbe2pe.py config/game.toml out/ghidra/IndyET_xbe.exe`. Auto-analysis: 172 s.
- Headless runs from Claude's sandboxed shell fail with a Felix "data file must be inside the data dir" error;
  run Ghidra from a normal terminal.

## Next steps
1. Optionally REA (`npx rea-agents setup`) so the agent can query the Ghidra project directly.
2. Re-export the PE after each config change and re-import it in Ghidra.
3. Fill in the missing XDK functions and data, then write `xdk/5120/layout.toml`.
4. `xbe_symbols --sign` to produce 5120 signatures (worth sending upstream).
5. First full build and boot.
