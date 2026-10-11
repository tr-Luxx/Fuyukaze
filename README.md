# Fuyukaze

An open-world samurai action vertical slice for Unreal Engine 5.8, in the spirit of *Ghost of Yōtei*:
a lone swordswoman, an autumn island under a snow-capped volcano, three occupied camps to free and a
duel at a mountain shrine.

It is a prototype, not a finished game. The world, props and weapons are generated in C++ and the
characters are Unreal's stock mannequins and animations, so there is no bespoke art or story
content, and every sound is synthesised in code.

## Play

```powershell
.\Play.ps1            # fullscreen
.\Play.ps1 -Windowed  # 1920x1080 window
```

Or open `Fuyukaze.uproject` in the editor and press Play.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Move / look | WASD / mouse | Left / right stick |
| Sprint | Left Shift (hold) | Click left stick |
| Jump | Space | A |
| Quick cut (chains into a combo) | Left mouse | X |
| Heavy cut (hold to charge) | Right mouse | Y |
| Guard, or parry if timed just before the blow | Q or middle mouse | LB |
| Dodge | Left Alt or C | B |
| Lock on | Tab | Click right stick |
| Stance: Stone / Wind / Moon | 1 / 2 / 3 | D-pad left / up / right |
| Spend resolve to heal | F | D-pad down |
| Guiding wind (points to the objective) | G | RB |
| Cinema mode (black and white) | K | View |
| Pause (then Enter / Y quits to desktop) | Esc | Menu |
| Show or hide controls | F1 | |

## How it plays

- **Goal.** Free the three camps, then defeat Raiko at the Shrine of the First Snow. The guiding
  wind always blows towards the next objective.
- **Reading attacks.** Enemies ring you and take turns. A white glint means the blow can be guarded
  or parried; a red glint means it cannot be blocked and must be dodged.
- **Posture.** Guarded hits, heavy cuts and parries fill an enemy's posture bar. When it fills they
  stagger and take extra damage.
- **Stances.** Stone breaks swordsmen, Wind breaks spearmen, Moon breaks brutes. The matching stance
  roughly doubles posture damage.
- **Resolve.** Earned from parries and kills, spent to heal. Freeing a camp restores health and
  resolve and becomes your respawn point.
- **Hit chain.** Landing hits within three seconds of each other builds a chain. The hit sound
  climbs in pitch with it, every fifth hit pays out a little resolve, and taking a hit resets it.

Every landed blow answers back: a crescent in the colour of your stance, sparks, a white flash and
a few frames of freeze on the target, a camera kick, controller rumble and a damage number. Kills
add slow motion and a burst of leaves; the last enemy of a fight falls in a longer slow motion.

## Layout

| Path | What it is |
| --- | --- |
| `Source/Fuyukaze/Game/FuyuTerrain.*` | The island as maths: height, biomes, sites and trails |
| `Source/Fuyukaze/Game/FuyuWorld.*` | Builds terrain, sky, forests, grass, camps and wind at BeginPlay |
| `Source/Fuyukaze/Game/FuyuProps.*`, `FuyuMeshBuilder.*` | Trees, gates, tents and weapons modelled in code |
| `Source/Fuyukaze/Game/FuyuSamurai.*` | The player: guard, parry, dodge, lock-on, stances, resolve |
| `Source/Fuyukaze/Game/FuyuEnemy.*` | Enemy state machine and the four enemy types |
| `Source/Fuyukaze/Game/FuyuGameMode.*` | Camps, objectives, the duel, the tour and the self-play bot |
| `Source/Fuyukaze/Game/FuyuHUD.*` | Canvas HUD, damage numbers and the hit-chain counter |
| `Source/Fuyukaze/Game/FuyuAudio.*` | Synthesised sound effects and wind |
| `Source/Fuyukaze/Variant_Combat/` | Epic's combat template, which the samurai and enemies extend |
| `Tools/build_content.py` | Generates the materials, blueprints and level |

## Build

```powershell
.\Build.ps1                 # compile the C++ module (close the Fuyukaze editor first)
.\Tools\BuildContent.ps1    # regenerate materials, blueprints and the level
.\Tools\Tour.ps1            # headless run that saves screenshots to Saved\Tour
.\Tools\Tour.ps1 -Bot       # same, with the samurai fighting on her own and logging the result
.\Tools\Tour.ps1 -Bot -BotTime 240   # four minutes of self-play, photographing hits and kills
```

Two machine-specific notes:

- Unreal's build tool wants the .NET Framework SDK. If it is not installed, `Build.ps1` points the
  build tool at a placeholder in `Build/AutoSDK` so the check passes. Installing ".NET Framework 4.8
  SDK" from the Visual Studio Installer removes the need for it, and is required to compile from
  inside the editor.
- If another Unreal Editor is open with Live Coding on, add `-IgnoreLiveCoding` to `Build.ps1`.

## Scanned assets

The realistic trees, grass, ground and lanterns come from Fab and are not in this repository (they are
licensed per account and add about 3.7 GB). Without them the game falls back to its code-built stylised
trees, grass and lanterns. To get them:

1. On fab.com, add these free listings to your library: Megaplants Ginkgo, Norway Maple Forest and
   Norway Spruce; Megascans Wild Grass, Dry Grass, Forest Floor, Rocky Forest Floor and Japanese Stone Lantern.
2. Open the project in the editor, open the Fab window and choose Add to Project on each.
3. Close the editor and run `.\Tools\BuildContent.ps1`. This rebuilds the terrain material and flags the
   Megascans materials for instancing. Run it on a fresh clone too, since the committed terrain material
   expects the scanned textures.

## Known limits

- Characters are untextured mannequins using the template's unarmed attack animations with a sword
  attached, so swings do not look like swordplay.
- Sound effects are simple synthesised tones and noise; there is no music or voice.
- The island is generated when the level starts, so it is empty in the editor viewport until you
  press Play.
- Lighting uses the sky light and screen-space effects rather than Lumen, because meshes built at
  runtime have no distance fields.
- No save system, no mount, no stealth, no ranged weapons.
