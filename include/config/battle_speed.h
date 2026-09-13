#ifndef GUARD_CONFIG_BATTLE_SPEED_H
#define GUARD_CONFIG_BATTLE_SPEED_H

// Fast Send-Out System
// Halves the hardcoded delays in the Pokemon send-out sequence.
// Set every value back to its listed vanilla number to restore original pacing.

// --- Stage 1: Poke Ball throw and open ---
#define B_SENDOUT_PLAYER_START_DELAY    15   // vanilla 31
#define B_SENDOUT_PLAYER_TRAINER_SLIDE  25   // vanilla 50
#define B_SENDOUT_OPPONENT_TRAINER_SLIDE 18  // vanilla 35
#define B_SENDOUT_PLAYER_ARC_FRAMES     13   // vanilla 25
#define B_SENDOUT_OPPONENT_HOLD          7   // vanilla 15 (compared with >)
#define B_SENDOUT_DOUBLES_STAGGER       12   // vanilla 24 (compared with >)
#define B_SENDOUT_BALL_OPEN_FRAMES       3   // vanilla 5, per anim frame (x2 frames)
#define B_SENDOUT_BALL_FADE_FRAMES       7   // vanilla 14

// --- Stage 2: Mon emerge scale-up (the "grows out of the ball" affine) ---
// Emerge starts at scale 0x28 and must land exactly on 0x100, so the per-frame
// step is derived instead of hardcoded. 0xD8 must divide evenly by the frame count.
// Safe values: 2, 3, 4, 6, 8, 12, 18, 24.
#define B_EMERGE_FRAMES                  6   // vanilla 12
#define B_EMERGE_SCALE_STEP  ((0x100 - 0x28) / B_EMERGE_FRAMES)
// The mon also hops down from y2 = 0x1000 during the emerge; scale the step so the
// hop still lands near zero when the emerge finishes.
#define B_EMERGE_HOP_STEP    ((288 * 12) / B_EMERGE_FRAMES)   // vanilla 288

// --- Stage 3: Per-species intro animation ---
#define B_MON_INTRO_ANIM_MAX_FRAMES     30   // 0 = uncapped (vanilla)
#define B_MON_INTRO_ANIM_DELAY_DIV       2   // 1 = vanilla per-species start delay
// Hard ceiling on the per-species front-pic frame animation, which the intro
// blocks on. The slowest vanilla tables run 150+ frames (see Aron).
#define B_FRONT_PIC_ANIM_MAX_FRAMES     45   // 0 = uncapped (vanilla)

// --- Stage 4: Post-send-out waits ---
#define B_HEALTHBOX_SLIDE_SPEED         10   // vanilla 5 (px/frame)
#define B_HEALTHBOX_SLIDE_DELAY         10   // vanilla 20 (2nd player mon in doubles)
#define B_INTRO_END_DELAY                1   // vanilla 3
#define B_PARTY_SUMMARY_DELAY           46   // vanilla 92
#define B_WAIT_FOR_FULL_CRY          FALSE   // TRUE = vanilla (block until cry ends)

// Fast Move / Skill Animation System
// Shortens the waits around and between move animations. The move animation
// artwork itself is untouched.

// Divides every `delay N` in data/battle_anim_scripts.s. 1 = vanilla.
#define B_ANIM_DELAY_DIV                 2
// Frames Cmd_end waits for a trailing sound effect before cutting it off.
#define B_ANIM_SFX_WAIT_FRAMES          45   // vanilla 90
// Frames the HP bar takes to drain, regardless of how much damage was dealt.
// Vanilla behaviour is 1 HP per frame, i.e. proportional to the damage number.
#define B_HP_BAR_DRAIN_FRAMES           24
// Same idea for the EXP bar after a battle.
#define B_EXP_BAR_SPEEDUP                2   // 1 = vanilla

#endif // GUARD_CONFIG_BATTLE_SPEED_H
