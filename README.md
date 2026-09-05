# 🌟 Pokémon Modern Emerald

[![Build & Release](https://github.com/Mickosis/Modern_Emerald/actions/workflows/build_and_release.yml/badge.svg)](https://github.com/Mickosis/Modern_Emerald/actions)
[![Release](https://img.shields.io/github/v/release/Mickosis/Modern_Emerald?style=for-the-badge&logo=github)](https://github.com/Mickosis/Modern_Emerald/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=for-the-badge)](LICENSE)

**Pokémon Modern Emerald** is an enhanced edition of *Pokémon Emerald*, built on the baseline of [Pokémon Emerald Legacy](https://github.com/cRz-Shadows/Pokemon_Emerald_Legacy). It delivers a modern, vibrant Hoenn adventure with essential **Quality of Life upgrades**, **Follower Pokémon**, **Custom Surfing Sprites**, and **Overworld Wild Encounters**—while strictly preserving 100% authentic Generation 3 battle mechanics, Pokémon stats, learnsets, and game balance.

---

## 🎮 Key Features

### 🎒 Quality of Life & Field Mechanics
- **Modern EXP. Share (Gen VII+ Formula)**: The EXP. ALL Key Item (obtained from Mr. Stone at Devon Corp) enables a **modern scaled EXP formula** with rubber-band level scaling. Active battlers receive **100%** EXP, benched party members receive **50%**, and fainted Pokémon receive **0 EXP and 0 EVs**. Underleveled Pokémon gain exponentially more EXP while overleveled Pokémon gain near-zero, keeping your team naturally balanced. Toggle On/Off via the Key Items pocket or register to `SELECT`.
- **HM Field Move Freedom**: Clear obstacles without HMs taking up your Pokémon's combat moveslots—having the Gym Badge and HM in your bag is all you need!
- **Overworld Speed Up (Audio Decoupled)**: Accelerate overworld traversal without distorted, high-pitched audio! Tap **L** in the overworld to cycle between **1x (Standard)**, **2x (Fast)**, and **4x (Turbo)** speeds. While moving at high speeds, hold **R** to temporarily drop back to 1x normal speed for precise alignment with doors and NPCs. Route music, Pokémon cries, and sound effects remain at 100% authentic pitch and tempo.
- **Persistent Flash**: Cave illumination from Flash stays lit across map transitions and room exits.
- **Starter Learnset Symmetry & Sceptile Dragon Buffs**: Treecko, Grovyle, and Sceptile receive early learnset pacing aligned symmetrically with Torchic and Mudkip (*Quick Attack* at Lv. 6, *Vine Whip* at Lv. 9). Grovyle and Sceptile are also **Grass/Dragon** dual-types gaining key Dragon STAB moves (*Dragon Breath* upon evolving at Lv. 16, *Dragon Claw*) and *SolarBeam*, preserving authentic battle balance while establishing early-game starter parity.

---

### 🐾 Overworld Follower Pokémon
- **All 386 Pokémon + Shinies**: Every Pokémon from Generations 1–3 follows directly behind you in the overworld!
- **Interactive Dialogue & Emotions**: Turn around and press **A** to interact with your partner. They react with unique emotes, sounds, and text dialogues based on friendship, status conditions, weather, map environment, and type effectiveness.

---

### 🏄 Custom Surfing Sprites
- Over **79 Pokémon species** across Generations 1–3 render with unique, dedicated surfing overworld sprites and normal/shiny palettes, seamlessly falling back to the classic surf blob for other species.

---

### 🌿 Overworld Wild Encounters (OWEs)
Wild Pokémon visibly roam across the tall grass, caves, and waters of Hoenn!
- **Dynamic Behaviors**: Wild Pokémon react to your presence—charging aggressively, fleeing timidly, or curiously observing you.
- **Overworld Shinies**: Shiny Pokémon appear with authentic shiny overworld colors, sparkle upon spawning, and remain persistent without despawning on the map.

---

## 🕹️ Download & How to Patch

1. Download the latest **`Modern_Emerald.bps`** patch from the [Releases Page](https://github.com/Mickosis/Modern_Emerald/releases/latest).
2. Open [Marc Robledo's ROM Patcher JS](https://marcrobledo.com/RomPatcher.js/).
3. Select your clean **Pokémon Emerald (USA, Europe)** ROM as the Source.
4. Select **`Modern_Emerald.bps`** as the Patch file.
5. Click **Apply patch** to generate your `.gba` file!

---

## 🛠️ Building from Source

```bash
# Clone the repository
git clone https://github.com/Mickosis/Modern_Emerald.git
cd Modern_Emerald

# Compile tools & ROM (Linux / macOS)
make -f make_tools.mk -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Generate .bps patch
python3 tools/make_bps.py vanilla_emerald.gba pokeemerald.gba Modern_Emerald.bps
```

---

## 🙏 Credits & Acknowledgements

- **Original Emerald Legacy Team**: [TheSmithPlays](https://www.youtube.com/@smithplayspokemon), [cRz-Shadows](https://github.com/cRz-Shadows), Weebra, Aerogod, Disq, Isona, ZuperZACH, Karlos, Regi.
- **Overworld Wild Encounters (OWE) & Overworld Speed Up**: [HashtagMarky](https://github.com/HashtagMarky) (Overworld Speedup, Team Aqua's Asset Repo) & [Bivurnum](https://github.com/bivurnum) (OWE PR #8434 / pokeemerald-expansion).
- **Follower Pokémon & Custom Overworld Sprites**: [Exclsior](https://github.com/Exclsior) (Follower Pokémon engine, dynamic surfing sprites, speedup controls), Voloved, Ghoulslash, ExpoSeed, Lunos, Mkol103, FieryMewtwo, TheXaman, LOuroboros, Jaizu, Buffel Saft, AkimotoBubble, Scyrous.
- **Pret Community**: [pokeemerald](https://github.com/pret/pokeemerald) disassembly project and tooling.
