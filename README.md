# Illuminated Core Framework - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `LICENSE`.

One plugin for every Illuminated mod. Each mod's own download carries its data and a small manifest; the part of this plugin a
manifest names runs only while that mod is installed.

## Where it stands
The kernel only. It loads, reads the manifests and its own settings file, finds the lighting the game draws with once at data
load (Community Shaders, ENB or Vanilla), shows its menu section and answers DevBench. It carries the hook table every part will
share (no hook is written while no part asks for one), the dimmer's keys, the light composer and the object-light library's
data reader (all tested off the game), and its testing build a test part that tries the engine's own light dimmer on the game's
casting light and on one light of its own, with a slider, through DevBench. No mod's
part is built in yet: Illuminated - Spell Lighting NG, RELight - Spell Lighting NG, Illuminated - Creature Lighting NG,
Illuminated - Soul Gem Hub and Dynamic Wards each still ship a plugin of its own, and each moves in here in turn. Nothing here is
released.

## What it does
- When the game starts (SKSE's post-load message, before any hook of a part is written) it reads every
  `SKSE\Plugins\IlluminatedCore\Modules\<part>.json` (at most 64 KB each): the part it names, the
  mod's data version and the core versions that data runs on.
- A part stays off, and is named in ONE message box at the main menu, on the menu's Problems found page and in
  `IlluminatedCore.log`, when its file is broken, when this core does not have the part, when the data is older than this core
  runs or needs a newer core, when one mod is installed twice, or when two mods that light the same spells are both installed.
- A switch per part on the menu's Installed page, kept in `SKSE\Plugins\IlluminatedCore\Settings.ini`; a change takes hold from
  the next game start.
- With no mod installed it runs nothing: no hook, no event sink, no task.

## A manifest
```json
{
  "format": 1,
  "module": "illuminated",
  "title": "Illuminated - Spell Lighting NG",
  "dataVersion": "4.0",
  "needsCore": "1.0.0",
  "needsCoreMax": "1.99.99",
  "needs": ["record-slots"],
  "notice": "Illuminated - Spell Lighting NG 4.0 needs Illuminated Core Framework 1.0.0 or newer."
}
```
`needsCoreMax`, `needs` and `notice` are optional. `notice` is the line shown when the installed core is too old for the data:
plain text, one line, at most 200 characters.

## Requires
SKSE and Address Library for SKSE Plugins; SKSE Menu Framework for the menu. Skyrim VR: SKSE VR and the VR version of Address
Library, with the VR build (a build on the wrong game stays off and says so in a box). The VR build is compiled, never played by
the author.

## Settings
SKSE Menu Framework > Illuminated Core Framework, saved to `Data\SKSE\Plugins\IlluminatedCore\Settings.ini`.

## Building
`build.bat` (git, xmake 3 and the Visual Studio C++ tools): CommonLib at the pinned commit, the DLL, then the off-game tests
(`tests/testcore.cpp`: the game-free readers in `src/*Text.h` and the check sets the shared headers carry). For Skyrim VR:
run `build.bat` once first (it fetches CommonLib into `lib\commonlibsse-ng` at the pinned commit), then
`git -C lib\commonlibsse-ng submodule update --init extern/openvr`, then `build.bat` with `GLOW_VR=1` set (a VR-only build).

## Credits
A manifest's version range in both directions, the data's own message line and the refusal of a part named by two manifests
follow Mini's Micro-Systems Framework by minizbot2012 (GPL-3.0): the method was learned from its source, none of its code is used.

## Third-party files
`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).

`src/DevBenchAPI.h` and `src/DevBenchAPI.cpp` are devbench's own interface files, MIT licensed by
devbench's author (see `src/DevBenchAPI.LICENSE.txt`) and copied unchanged. devbench is not required.
