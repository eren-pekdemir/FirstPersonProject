# First Person Shooter Showcase

A small first person shooter showcase built with **Unreal Engine 5.7** and **C++**.
The player can walk up to a pistol or a rifle, pick it up with **E** and fire it with weapon-specific
recoil, crosshair spread, muzzle FX, tracers and impact effects. Picking up a weapon while holding
another one swaps them in place.

## Features

- **Data-driven weapons** – every weapon is a child Blueprint of `AGunBase` plus a `UWeaponDataAsset`.
  Fire rate, fire mode, range, recoil pattern, spread, FX, sounds and animations are all configured in data.
- **Hitscan firing** from the center of the screen with single and automatic fire modes and a
  fire rate that is enforced between trigger presses.
- **Pattern-based recoil** – each shot pushes the view along a per-weapon recoil pattern; the pattern
  resets after the trigger is released.
- **Dynamic crosshair** – spread grows per shot and with movement speed, then recovers over time.
- **Weapon feedback** – slide kick-back, muzzle flash, Niagara tracers, impact actors, camera shake and
  a delayed muzzle smoke puff after the last shot.
- **Look-at pickup** – a trace from the camera detects weapons in front of the player and shows an
  interaction prompt.
- **Animation layers** – each weapon links its own anim layer (`ALI_Weapon`) to the character's
  Animation Blueprint, with left hand IK driven by a socket on the weapon mesh.
- **Sprint / fire interplay** – holding the trigger cancels the sprint, and the weapon only starts
  firing once the character has slowed down to walking speed.

## Controls

| Action           | Key             |
| ---------------- | --------------- |
| Move             | `W` `A` `S` `D` |
| Look             | Mouse           |
| Sprint           | `Left Shift`    |
| Fire             | `Left Mouse`    |
| Pick up / swap   | `E`             |

## Code Overview

All gameplay code lives in `Source/FirstPersonProject`.

| Class | Responsibility |
| ----- | -------------- |
| `APlayerCharacter` (`Player/`) | Movement, sprinting, interaction trace, equipping / swapping weapons, forwarding fire input. |
| `AGunBase` (`Gun/`) | Fire rate, hitscan trace, recoil, spread, slide kick and all firing FX. Handles its own held / world state. |
| `UWeaponDataAsset` (`Data/`) | All tuning values for a weapon type. |
| `UInteractPromptWidget` (`UI/`) | Interaction prompt shown while looking at a weapon. |
| `AFirstPersonProjectPlayerController` | Adds the input mapping contexts and the camera manager. |
| `AFirstPersonProjectCameraManager` | Clamps the view pitch. |

### Adding a new weapon

1. Create a `UWeaponDataAsset` and fill in the firing, recoil, crosshair, FX and animation values.
2. Create a Blueprint child of `AGunBase`, assign the meshes and the data asset.
3. Make sure the weapon mesh has a `MuzzleSocket` and a `LeftHandSocket` (used by the left hand IK).
4. Place it in the level – it can be picked up right away.

## Content Layout

| Folder | Contents |
| ------ | -------- |
| `Content/Game/Character` | Player Blueprint, Animation Blueprints, weapon anim layers and blend spaces. |
| `Content/Game/Guns` | Pistol and rifle Blueprints, weapon data assets and firing FX. |
| `Content/Game/Widget` | Crosshair and interaction prompt widgets. |
| `Content/FirstPerson` | Showcase level and game mode. |

## Getting Started

**Requirements:** Unreal Engine 5.7, Visual Studio 2022 (or JetBrains Rider) with the
"Game development with C++" workload.

1. Clone the repository.
2. Right click `FirstPersonProject.uproject` → **Generate Visual Studio project files**.
3. Open the solution, build the `FirstPersonProjectEditor` target (Development Editor, Win64) and run,
   or simply open the `.uproject` and let the editor build the module.
4. The showcase level `Content/FirstPerson/Lvl_FirstPerson` opens by default – press **Play**.

## Credits

Third party assets used in this project belong to their respective authors:

- Paragon: Lt. Belica character – Epic Games
- Lyra Starter Game animations – Epic Games
- Weapon models from Fab (`GUN_Pistol_Orange`, `SS2_V5_Assault_Rifle`)
- Impact and muzzle effects from the Dynamic FPS pack
- Quixel Megascans material presets
- Unreal Engine First Person template, mannequins and Level Prototyping assets – Epic Games
