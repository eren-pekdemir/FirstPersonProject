# First Person Shooter Showcase

A small first person shooter showcase built with **Unreal Engine 5.7** and **C++**.
Fast, Apex-style movement (sprint, slide, slide jump, mantle, vault) combined with data-driven gunplay:
a pistol, an assault rifle and a pump shotgun with magazines, reloads, ADS, recoil, real bullet spread,
damage, hit markers and layered weapon sounds. The level contains a traversal course and a shooting range
with target dummies.

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

### Gunplay

- **Inventory** – up to three weapons; `1` `2` `3` or the mouse wheel switch between them. Picking up a
  weapon with a full inventory swaps it with the held one.
- **Ammo and reloading** – magazine + reserve ammo, tactical and (longer) empty reloads with a timed
  sound timeline (mag out, mag in, charge), per-shell reloads for the shotgun that the trigger can
  interrupt, automatic reload when the magazine runs dry.
- **Aim down sights** – per-weapon FOV zoom, slower movement, tighter spread, softer recoil and a
  look sensitivity that scales with the zoom.
- **Bullet spread** – the crosshair gap is the real deviation cone; it grows with shots, movement and
  jumping. Shotguns fire multiple pellets.
- **Recoil feel** – pattern recoil on the view, partial recovery after the burst, plus a procedural
  camera kick and FOV punch that spring back.
- **Damage and feedback** – point damage with headshots, physics impulses, hit marker (white / yellow
  headshot / red kill), hit sounds and floating damage numbers.
- **Sounds** – randomised gunshots with tails, reload foley, equip / aim foley, dry fire, shotgun pump.

### Movement

- **Sprint** forward only; aiming or holding the trigger cancels it.
- **Slide** – crouch while sprinting. Speed boost (with cooldown), slopes accelerate it, jumping out of a
  slide keeps the momentum. Landing while holding crouch slides straight away.
- **Mantle / vault** – jump into a ledge to climb it (up to ~2.3 m); low thin obstacles are vaulted
  without losing speed. Ledges are also grabbed automatically when pushing into them in the air.
- **Jump assists** – coyote time and jump buffering.
- **Camera feel** – sprint / slide FOV, strafe and slide roll, landing and mantle dips, subtle head bob,
  footsteps and landing sounds.

## Controls

| Action           | Key             |
| ---------------- | --------------- |
| Move             | `W` `A` `S` `D` |
| Look             | Mouse           |
| Sprint           | `Left Shift`    |
| Jump / mantle / vault | `Space`    |
| Crouch / slide   | `Left Ctrl` or `C` |
| Fire             | `Left Mouse`    |
| Aim down sights  | `Right Mouse`   |
| Reload           | `R`             |
| Switch weapon    | `1` `2` `3` / mouse wheel |
| Pick up / swap   | `E`             |

## Code Overview

All gameplay code lives in `Source/FirstPersonProject`.

| Class | Responsibility |
| ----- | -------------- |
| `APlayerCharacter` (`Player/`) | Input, interaction. Movement (sprint, slide, jump assists, footsteps) lives in `PlayerCharacterMovement.cpp`, inventory / firing / ADS / reload in `PlayerCharacterWeapons.cpp`. |
| `UTraversalComponent` (`Player/`) | Ledge detection and procedural mantle / vault moves. |
| `UCameraFeelComponent` (`Player/`) | FOV, roll, recoil kick, dips and head bob on the camera. |
| `AGunBase` (`Gun/`) | Fire rate, ammo, reloading, pellets, damage, recoil, spread, slide kick and all firing FX. Handles its own held / holstered / world state. |
| `UWeaponDataAsset` (`Data/`) | All tuning values for a weapon type (firing, damage, ammo, handling, sounds, feedback, recoil, spread). |
| `ADamageTarget` (`Gameplay/`) | Shooting range dummy with a headshot zone, hit flash, knock-down / respawn and optional strafing. |
| `AShooterHUD` (`UI/`) | Canvas HUD: ammo, weapon slots, reload bar, hit markers, damage numbers. |
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
| `Content/Game/Guns` | Pistol, rifle and shotgun Blueprints, weapon data assets and firing FX. |
| `Content/Game/Character/Montages` | Reload, equip and rifle fire montages. |
| `Content/FreeWeaponSFX`, `Content/FootstepsMiniPack` | Weapon and footstep sounds from Fab. |
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
- Unreal Engine First Person template, mannequins, weapon meshes and animations, Level Prototyping assets – Epic Games
- Free Weapon Sound Effects – SoundLab1 (Fab)
- Footsteps Mini Sound Pack (Fab)
