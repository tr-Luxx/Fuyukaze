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
| Pass the time to nightfall, or to morning | T | |
| Horse: whistle for one, mount it, or dismount | E | |
| Ultimate, Winter Wind (needs a full resolve meter) | R | LT |
| Challenge a player to a duel, accept, or yield | V | RT |
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
- **Winter Wind.** With all three resolve filled, the ultimate spends the lot: the samurai gathers, blinks through up to
  six enemies within 15 m leaving trails of light, holds, and then cuts them all at once for heavy damage. She cannot be
  hurt while it plays, and its kills do not earn resolve back.
- **Day and night.** The sun crosses the sky in ten minutes and the night lasts six. After dusk the moon rises,
  the stars come out and the Milky Way stands over the mountain. The clock is beside the compass, and T passes
  the time to full dark or to the next morning. In multiplayer everyone shares the host's clock.
- **Wild horses.** Three herds gallop circuits over the open meadows, pull up to rest for a while, and run again.
  Walk up to one and press E to take it for your own.
- **Riding.** E whistles and a horse gallops to you; E beside it mounts, and E again gets down. It trots, and
  gallops while Shift is held, at about twice your sprint. You cannot fight from the saddle, and a blow that
  reaches you puts you back on your feet. The horse waits where you leave it.
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
| `Source/Fuyukaze/Game/FuyuSky.cpp` | Sun, moon and clock, and the painted panorama of stars and Milky Way |
| `Source/Fuyukaze/Variant_Combat/` | Epic's combat template, which the samurai and enemies extend |
| `Tools/build_content.py` | Generates the materials, blueprints and level |
| `Source/Fuyukaze/Game/FuyuHorses.cpp` | The herds: their circuits, and when they run and rest |
| `Source/Fuyukaze/Game/FuyuRide.cpp`, `FuyuRiderAnim.*` | Whistling, mounting and riding, and the seated pose the rider is put in |
| `Tools/convert_katana.py` | Converts the katana model from a .glb into the mesh file and textures the game loads |
| `Tools/convert_horse.py` | Converts the horse model and tags its legs, neck and tail for the gallop its material plays |

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

## Multiplayer and duels

Up to a few players can share the island over a local network.

- **Host:** double-click `Host Game.bat`. It prints this PC's IP addresses, then starts the game.
- **Join:** double-click `Join Game.bat` and type the host's IP address (or press Enter to join a host on the same PC).
- **Duels:** stand near another player and press **V** to challenge them. They press **V** to accept. Players can only
  hurt each other during a duel. The first to run out of health yields and is left standing; **V** during a duel gives up.

The host decides health, guards, hits and duels; enemies think on the host only. Hit-stop and slow motion are
single-player only, because slowing one machine's clock would put it out of step with the others.

Every player needs their own copy of the project built against Unreal Engine 5.8 (there is no packaged build yet).
Playing over the internet needs UDP port 7777 forwarded to the host, or a virtual LAN.

`Tools\NetTest.ps1` runs a host and a joining player headless with bots on both sides and prints what happened.
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

## Credits

The katana is "Katana" by nejenabinupa (https://sketchfab.com/3d-models/katana-61e49a14ec9043a7bd3ba22fb7878508),
licensed [CC-BY-NC-4.0](http://creativecommons.org/licenses/by-nc/4.0/): it must be credited and may not be
used commercially. `Tools\convert_katana.py` took the drawn sword out of the model's scene, re-posed it for the
hand, and wrote `Content/Fuyukaze/Models/Katana.fuyumesh`; `Tools\BuildContent.ps1 -Only katana` imported its
textures and built its materials. Without those files the game uses a simple katana built in code.

The horse is "Low poly Horse Brown Colour 3d Model Free" by iRahulRajput
(https://sketchfab.com/3d-models/low-poly-horse-brown-colour-3d-model-free-9073d1dfc345495a88ef84eed4d2a9e5),
licensed [CC-BY-4.0](http://creativecommons.org/licenses/by/4.0/). The model is a standing horse with no skeleton
or animation, so its gallop is made here: `Tools\convert_horse.py` marks which vertices are legs, neck and tail, and
the horse material (`Tools\BuildContent.ps1 -Only horse`) swings them about the joints. Without the converted files
the island has no horses.

## Known limits

- Characters are untextured mannequins using the template's unarmed attack animations with a sword
  attached, so swings do not look like swordplay.
- Sound effects are simple synthesised tones and noise; there is no music or voice.
- The island is generated when the level starts, so it is empty in the editor viewport until you
  press Play.
- Lighting uses the sky light and screen-space effects rather than Lumen, because meshes built at
  runtime have no distance fields.
- The rider holds one fixed pose made in code; there is no riding animation, no mounted combat, and the horse
  has no collision of its own beyond the rider's.
- No save system, no stealth, no ranged weapons.
