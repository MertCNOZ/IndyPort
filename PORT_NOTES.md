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
- Found by hand so far: D3DDevice_SetIndices, D3DDevice_DrawVerticesUP, D3DDevice_Swap, D3D_SetFence and the
  data D3D_pDevice, D3D_DirtyFlags, D3D_IndexData, D3D_Streams (evidence in config/game.toml comments).

## 5120 device layout
**5120 is its own layout**, see `xdk5120/layout.draft.toml`: the front of CDevice mostly matches 4831, a few
fields are 4 bytes lower (Time 0x2C, PixelShader 0x36C, VertexShader 0x37C, IndexBuffer 0x388), and the tail is
0xC00 shorter (SwapCount 0x2010 vs 0x2C10). 18 values proven, the rest TODO.

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
