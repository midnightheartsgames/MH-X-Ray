# MH-X-Ray
MH X-Ray open source x64 for Stalker SoC

Based on X-Ray Engine v1.0007rc1, the version used in S.T.A.L.K.E.R.: Shadow of Chernobyl (fork of https://github.com/xrModder/X-Ray).

## How to build
- install Visual Studio 2022 Community or newer with the v143 C++ toolset;
- open X-Ray.sln;
- select the configuration: Release (recommended) or Mixed (optimized, with debug checks);
- select the platform: x64 or Win32;
- select "Build - Build Solution" from the menu.

x64 binaries are written to "Output\Binaries_x64", Win32 binaries to "Output\Binaries".

## How to launch
- x64: copy "Output\Binaries_x64" to the game folder as "binaries_x64", copy the contents of the "Game" folder to the game folder and run Launch_x64.cmd;
- Win32: copy "Output\Binaries" to the game folder as "binaries", copy the contents of the "Game" folder to the game folder and run Launch.cmd.

The game needs the Microsoft Visual C++ 2015-2022 Redistributable for the chosen platform and the DirectX End-User Runtime (d3dx9_43.dll).
Saved games load on both platforms.

## Roadmap
The x64 port and multiplayer removal plan: Docs/x64-port-plan.md.
