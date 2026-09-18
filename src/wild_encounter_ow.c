#include "global.h"
#include "wild_encounter_ow.h"
#include "battle_setup.h"
#include "battle_main.h"
#include "battle_pike.h"
#include "battle_pyramid.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "follower_helper.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "random.h"
#include "roamer.h"
#include "script.h"
#include "script_movement.h"
#include "sprite.h"
#include "sound.h"
#include "task.h"
#include "trainer_hill.h"
#include "wild_encounter.h"
#include "constants/battle_frontier.h"
#include "constants/event_objects.h"
#include "constants/field_effects.h"
#include "constants/layouts.h"
#include "constants/items.h"
#include "constants/map_types.h"
#include "constants/trainer_types.h"
#include "constants/songs.h"
#include "constants/vars.h"
#include "constants/wild_encounter.h"

#define sOverworldEncounterLevel        trainerRange_berryTreeId
#define sOverworldEncounterAge          playerCopyableMovement
#define sOverworldEncounterCategory     warpArrowSpriteId
#define OWE_MAX_ROAMERS                 UINT8_MAX - 3

#define OWE_FLAG_BIT                    (1 << 7)
#define OWE_SAVED_MOVEMENT_STATE_FLAG   OWE_FLAG_BIT
#define OWE_NO_DESPAWN_FLAG             OWE_FLAG_BIT

#define OWE_SPAWN_DISTANCE_LAND         1   // A spawn cannot happen within this many tiles of the player position.
#define OWE_SPAWN_DISTANCE_WATER        3   // A spawn cannot happen within this many tiles of the player position (while surfing).
#define OWE_SPAWN_WIDTH_TOTAL           15  // Width of the on-screen spawn area in tiles.
#define OWE_SPAWN_HEIGHT_TOTAL          9   // Height of the on-screen spawn area in tiles.
#define OWE_SPAWN_WIDTH_RADIUS          ((OWE_SPAWN_WIDTH_TOTAL - 1) / 2)     // Distance from center to left/right edge (not including center).
#define OWE_SPAWN_HEIGHT_RADIUS         ((OWE_SPAWN_HEIGHT_TOTAL - 1) / 2)    // Distance from center to top/bottom edge (not including center).

#define OWE_SPAWN_TIME_REPLACEMENT      90  // The number of frames before an existing spawn will be replaced with a new one (requires WE_OWE_SPAWN_REPLACEMENT).
#define OWE_SPAWN_TIME_LURE             0
#define OWE_SPAWN_TIME_MINIMUM          10  // The minimum value the spawn wait time can be reset to. Prevents spawn attempts every frame.
#define OWE_SPAWN_TIME_PER_ACTIVE       15  // The number of frames that will be added to the countdown per currently active spawn.

#define OWE_DEFAULT_CHASE_RANGE         5
#define OWE_RESTORED_MOVEMENT_FUNC_ID   10

#define OWE_NO_ENCOUNTER_SET            0xFF
#define OWE_INVALID_SPAWN_SLOT          0xFF

#ifndef ROAMER_COUNT
#define ROAMER_COUNT 1
#endif

#ifndef assertf
#define assertf(cond, ...) if (!(cond))
#endif

#ifndef DN_FLAG_SEARCHING
#define DN_FLAG_SEARCHING 0
#endif

#ifndef OW_FLAG_NO_ENCOUNTER
#define OW_FLAG_NO_ENCOUNTER 0
#endif

#ifndef LURE_STEP_COUNT
#define LURE_STEP_COUNT 0
#endif

#define REPEL_STEP_COUNT VarGet(VAR_REPEL_STEP_COUNT)

#define FNPC_ENABLE_NPC_FOLLOWERS FALSE
#define PlayerHasFollowerNPC() FALSE
#define GetFollowerNPCObjectId() 0

static const u8 sStandardDirections[4] = {DIR_SOUTH, DIR_NORTH, DIR_WEST, DIR_EAST};
static EWRAM_DATA u32 sBattleOWEObjectEventId = OBJECT_EVENTS_COUNT;

#define GetObjectEventIdByLocalId(localId) GetObjectEventIdByLocalIdAndMap((localId), gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup)

#define READ_OTID_FROM_SAVE T1_READ_32(gSaveBlock2Ptr->playerTrainerId)

static inline bool32 ComputePlayerShinyOdds(u32 personality, u32 otId)
{
    return GET_SHINY_VALUE(otId, personality) < SHINY_ODDS;
}

enum __attribute__((packed)) CategoryOWE
{
    OWE_CATEGORY_ROAMER = 0,
    OWE_CATEGORY_MASS_OUTBREAK = ROAMER_COUNT,
    OWE_CATEGORY_FEEBAS,
    OWE_CATEGORY_WILD,
    OWE_CATEGORY_UNDEFINED
};

struct InfoOWE
{
    u16 speciesId;
    enum CategoryOWE category;
    u8 localId;
    u8 level;
    bool8 isShiny;
    bool8 isFemale;
    bool8 noDespawn;
};

#if WE_OW_ENCOUNTERS == TRUE && ROAMER_COUNT > OWE_MAX_ROAMERS
#error "ROAMER_COUNT needs to be less than OWE_MAX_ROAMERS due to it being stored in the u8 field warpArrowSpriteId"
#endif

#if OWE_SPAWN_DISTANCE_LAND >= OWE_SPAWN_WIDTH_RADIUS || OWE_SPAWN_DISTANCE_WATER >= OWE_SPAWN_WIDTH_RADIUS
#error "OWE_SPAWN_DISTANCE_LAND and OWE_SPAWN_DISTANCE_WATER must both be less than OWE_SPAWN_WIDTH_RADIUS."
#endif

static inline u32 GetLocalIdByOWESpawnSlot(u32 spawnSlot)
{
    return LOCALID_OW_ENCOUNTER_END - spawnSlot;
}

static inline u32 GetSpawnSlotByOWELocalId(u32 localId)
{
    return LOCALID_OW_ENCOUNTER_END - localId;
}

static inline u32 GetOWECategory(const struct ObjectEvent *owe)
{
    return owe->sOverworldEncounterCategory & ~OWE_SAVED_MOVEMENT_STATE_FLAG;
}

static inline bool32 HasSavedOWEMovementState(const struct ObjectEvent *owe)
{
    return owe->sOverworldEncounterCategory & OWE_SAVED_MOVEMENT_STATE_FLAG;
}

void SetSavedOWEMovementState(struct ObjectEvent *owe)
{
    owe->sOverworldEncounterCategory |= OWE_SAVED_MOVEMENT_STATE_FLAG;
}

void ClearSavedOWEMovementState(struct ObjectEvent *owe)
{
    owe->sOverworldEncounterCategory &= ~OWE_SAVED_MOVEMENT_STATE_FLAG;
}

static inline bool32 HasOWENoDespawnFlag(const struct ObjectEvent *owe)
{
    return owe->sOverworldEncounterLevel & OWE_NO_DESPAWN_FLAG;
}

static inline bool32 ShouldSpawnWaterOWE(void)
{
    return TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING | PLAYER_AVATAR_FLAG_UNDERWATER);
}

// Helper function for IsOverworldWildEncounter and GetOverworldWildEncounterType
static inline bool32 IsObjectActiveOWE(struct ObjectEvent *owe)
{
    return (owe->active && owe->trainerType == TRAINER_TYPE_OW_WILD_ENCOUNTER);
}

static bool32 CreateEnemyPartyOWE(struct InfoOWE *info, s32 x, s32 y);
static bool32 OWE_DoesOWERoamerExist(void);
static bool32 StartWildBattleWithOWE_CheckRoamer(enum CategoryOWE category);
static bool32 StartWildBattleWithOWE_CheckMassOutbreak(enum CategoryOWE category, u16 speciesId);
static bool32 CheckCurrentWildMonHeaderForOWE(bool32 shouldSpawnWaterMons);
static u32 GetOldestActiveOWESlot(bool32 forceRemove);
static u32 GetNextOWESpawnSlot(void);
static u32 GetSpeciesByOWESpawnSlot(u32 spawnSlot);
static bool32 TrySelectTileForOWE(s32* outX, s32* outY);
static void SetSpeciesInfoForOWE(struct InfoOWE *info, u32 x, u32 y);
static u32 GetGraphicsIdForOWE(const struct InfoOWE *info);
static bool32 CheckCanLoadOWE(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y);
static bool32 CheckCanLoadOWE_Palette(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y);
static bool32 CheckCanLoadOWE_Tiles(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y);
static void SortOWEAges(void);
static bool32 ShouldDespawnGeneratedForNewOWE(struct ObjectEvent *owe);
static void SetNewOWESpawnCountdown(void);
static void DoOWESpawnAnim(struct ObjectEvent *owe);
static void DoOWEDespawnAnim(struct ObjectEvent *owe);
static enum SpawnDespawnTypeOWE GetOWESpawnDespawnAnimType(u32 metatileBehavior);
static void PlayOWECry(struct ObjectEvent *owe);
static struct ObjectEvent *GetRandomOWEObjectEvent(void);
static bool32 OWE_ShouldPlayOWEFleeSound(struct ObjectEvent *owe);
static bool32 CheckRestrictedOWEMovementAtCoords(struct ObjectEvent *owe, s32 xNew, s32 yNew, u8 newDirection, u8 collisionDirection);
static bool32 CheckRestrictedOWEMovementMetatile(s32 xCurrent, s32 yCurrent, s32 xNew, s32 yNew);
static bool32 CheckRestrictedOWEMovementMap(struct ObjectEvent *owe, s32 xNew, s32 yNew);
static bool32 CanOWEReachPlayer(struct ObjectEvent *owe);
static bool32 IsOWENextToObject(struct ObjectEvent *owe, struct ObjectEvent *object);
static u8 CheckOWEPathToPlayerFromCollision(struct ObjectEvent *owe, u8 newDirection);
static void Task_OWEApproachForBattle(u8 taskId);
static bool32 CheckValidOWESpecies(u16 speciesId);

static EWRAM_DATA u8 sOWESpawnCountdown = 0;

struct AgeSort
{
    u8 slot:4;
    u8 age:4;
};

const struct FieldEffectInfoOWE gOverworldWildEncounterFieldEffectInfo[] =
{
    [OWE_SPAWN_ANIM_GRASS] =
    {
        .xOffset = 0,
        .yOffset = 8,
        .visual = FLDEFFOBJ_JUMP_TALL_GRASS,
    },

    [OWE_SPAWN_ANIM_LONG_GRASS] =
    {
        .xOffset = 0,
        .yOffset = 0,
        .visual = FLDEFFOBJ_JUMP_LONG_GRASS,
    },

    [OWE_SPAWN_ANIM_WATER] =
    {
        .xOffset = 0,
        .yOffset = 8,
        .visual = FLDEFFOBJ_JUMP_BIG_SPLASH,
    },

    [OWE_SPAWN_ANIM_UNDERWATER] =
    {
        .xOffset = 0,
        .yOffset = 0,
        .visual = FLDEFFOBJ_BUBBLES,
    },

    [OWE_SPAWN_ANIM_CAVE] =
    {
        .xOffset = 0,
        .yOffset = 12,
        .visual = FLDEFFOBJ_GROUND_IMPACT_DUST,
    },

    [OWE_SPAWN_ANIM_SHINY] =
    {
        .xOffset = 0,
        .yOffset = 0,
        .visual = FLDEFFOBJ_SHINY_SPARKLE,
    },
};

void UpdateOverworldWildEncounter(void)
{
    bool32 shouldSpawnWaterMons;
    struct ObjectEvent *player;
    u32 spawnSlot;
    s32 x, y;
    struct InfoOWE infoOWE;
    struct ObjectEventTemplate objectEventTemplate;
    u32 objectEventId;
    struct ObjectEvent *owe;

    // Check if possible to spawn.
    shouldSpawnWaterMons = ShouldSpawnWaterOWE();
    
    if (ArePlayerFieldControlsLocked() || !CheckCurrentWildMonHeaderForOWE(shouldSpawnWaterMons))
        return;

    if (!WE_OW_ENCOUNTERS
     || (WE_OWE_FLAG_DISABLED != 0 && FlagGet(WE_OWE_FLAG_DISABLED))
     || (gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS && !WE_OWE_BATTLE_PIKE)
     || (gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR && !WE_OWE_BATTLE_PYRAMID)
     || InTrainerHillChallenge())
    {
        if (sOWESpawnCountdown != OWE_NO_ENCOUNTER_SET)
        {
            DespawnAllOverworldWildEncounters(OWE_GENERATED, 0);
            sOWESpawnCountdown = OWE_NO_ENCOUNTER_SET;
        }
        return;
    }
    else if (sOWESpawnCountdown == OWE_NO_ENCOUNTER_SET)
    {
        SetMinimumOWESpawnTimer();
    }

    if (sOWESpawnCountdown)
    {
        sOWESpawnCountdown--;
        return;
    }
    
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    // Don't spawn if player is mid step.
    if (player->currentCoords.x != player->previousCoords.x || player->currentCoords.y != player->previousCoords.y)
        return;

    // Check for a valid tile.
    spawnSlot = GetNextOWESpawnSlot();
    if (spawnSlot == OWE_INVALID_SPAWN_SLOT
     || (shouldSpawnWaterMons && AreLegendariesInSootopolisPreventingEncounters())
     || !TrySelectTileForOWE(&x, &y))
    {
        SetMinimumOWESpawnTimer();
        return;
    }

    // Check for a valid Pokemon.
    memset(&infoOWE, 0, sizeof(infoOWE));

    infoOWE.localId = GetLocalIdByOWESpawnSlot(spawnSlot);
    infoOWE.category = OWE_CATEGORY_UNDEFINED;
    SetSpeciesInfoForOWE(&infoOWE, x, y);

    if (infoOWE.speciesId == SPECIES_NONE
     || (WE_OWE_SPECIAL_ONLY && infoOWE.category >= OWE_CATEGORY_WILD)
     || !IsWildLevelAllowedByRepel(infoOWE.level)
     || !IsAbilityAllowingEncounter(infoOWE.level)
     || !CheckCanLoadOWE(infoOWE.speciesId, infoOWE.isFemale, infoOWE.isShiny, x, y))
    {
        SetMinimumOWESpawnTimer();
        return;
    }

    // Spawn the Pokemon.
    // Zero the whole template first. Only some of its fields are assigned below, and
    // PackGraphicsId reads `script` and `trainerRange_berryTreeId` to derive the form
    // bits of graphicsId - leaving those as stack garbage corrupts the species.
    memset(&objectEventTemplate, 0, sizeof(objectEventTemplate));

    objectEventTemplate.localId = infoOWE.localId;
    objectEventTemplate.graphicsId = GetGraphicsIdForOWE(&infoOWE);
    objectEventTemplate.x = x - MAP_OFFSET;
    objectEventTemplate.y = y - MAP_OFFSET;
    objectEventTemplate.elevation = MapGridGetElevationAt(x, y);
    objectEventTemplate.movementType = OWE_GetMovementTypeFromSpecies(infoOWE.speciesId);
    objectEventTemplate.trainerType = TRAINER_TYPE_OW_WILD_ENCOUNTER;

    objectEventId = GetObjectEventIdByLocalId(infoOWE.localId);
    if (objectEventId < OBJECT_EVENTS_COUNT)
    {
        owe = &gObjectEvents[objectEventId];
        if (ShouldDespawnGeneratedForNewOWE(owe))
            RemoveObjectEvent(owe);
    }
    objectEventId = SpawnSpecialObjectEvent(&objectEventTemplate);

    assertf(objectEventId < OBJECT_EVENTS_COUNT, "could not spawn generated overworld encounter. too many object events exist")
    {
        SetMinimumOWESpawnTimer();
        return;
    }

    owe = &gObjectEvents[objectEventId];
    owe->disableCoveringGroundEffects = TRUE;
    owe->shiny = infoOWE.isShiny;
    // The sprite was created before this flag was known, so it still holds the
    // non-shiny palette. Reload it now or a shiny spawns in normal colours.
    ObjectEventRefreshShinyPalette(owe);
    owe->sOverworldEncounterLevel = infoOWE.noDespawn ? (infoOWE.level | OWE_NO_DESPAWN_FLAG) : infoOWE.level;
    owe->sOverworldEncounterCategory = infoOWE.category;

    ObjectEventTurn(owe, sStandardDirections[Random() & 3]);
    SetNewOWESpawnCountdown();
}

bool32 IsOverworldWildEncounter(struct ObjectEvent *owe, enum TypeOWE oweType)
{
    if (!IsObjectActiveOWE(owe))
        return FALSE;

    switch (oweType)
    {
    default:
    case OWE_ANY:
        return TRUE;
    
    case OWE_GENERATED:
        return IS_LOCALID_GENERATED_OWE(owe->localId);

    case OWE_MANUAL:
        return !IS_LOCALID_GENERATED_OWE(owe->localId);
    }
}

static enum TypeOWE GetOverworldWildEncounterType(struct ObjectEvent *owe)
{
    if (!IsObjectActiveOWE(owe))
        return OWE_NONE;

    if (IS_LOCALID_GENERATED_OWE(owe->localId))
        return OWE_GENERATED;

    return OWE_MANUAL;
}

void StartWildBattleWithOWE(struct ScriptContext *ctx)
{
    u32 localId;
    u32 objEventId;
    struct ObjectEvent *owe;
    enum CategoryOWE category;
    u16 speciesId;
    bool32 shiny;
    u32 level;
    u32 personality;

    localId = VarGet(ScriptReadHalfword(ctx));
    objEventId = GetObjectEventIdByLocalId(localId);

    // Bounds-check before dereferencing. GetObjectEventIdByLocalId returns
    // OBJECT_EVENTS_COUNT when the object is gone, and gObjectEvents[OBJECT_EVENTS_COUNT]
    // reads past the array into neighbouring EWRAM.
    assertf(objEventId < OBJECT_EVENTS_COUNT, "cannot start overworld wild encounter")
    {
        UnlockPlayerFieldControls();
        UnfreezeObjectEvents();
        ScriptContext_Enable();
        return;
    }

    owe = &gObjectEvents[objEventId];
    category = GetOWECategory(owe);

    assertf(IsOverworldWildEncounter(owe, OWE_ANY), "cannot start overworld wild encounter")
    {
        UnlockPlayerFieldControls();
        UnfreezeObjectEvents();
        ScriptContext_Enable();
        return;
    }

    if (category < ROAMER_COUNT && StartWildBattleWithOWE_CheckRoamer(category))
        return;

    speciesId = OW_SPECIES(owe);
    shiny = OW_SHINY(owe) ? TRUE : FALSE;
    level = owe->sOverworldEncounterLevel & ~OWE_NO_DESPAWN_FLAG;

    // Never hand an out-of-range species to CreateWildMon; it indexes gSpeciesInfo
    // unchecked, which produces a "?????" battle rather than anything recoverable.
    assertf(CheckValidOWESpecies(speciesId), "overworld wild encounter has invalid species")
    {
        UnlockPlayerFieldControls();
        UnfreezeObjectEvents();
        ScriptContext_Enable();
        return;
    }

    assertf(level >= MIN_LEVEL && level <= MAX_LEVEL, "overworld wild encounter does not have valid level")
    {
        level = MIN_LEVEL;
    }

    ZeroEnemyPartyMons();
    if (shiny)
        FlagSet(FLAG_SHINY_CREATION);

    CreateWildMon(speciesId, level);
    
    if (StartWildBattleWithOWE_CheckMassOutbreak(category, speciesId))
        return;

    sBattleOWEObjectEventId = objEventId;
    BattleSetup_StartWildBattle();
}

void SetOverworldObjectSpecies(struct ScriptContext *ctx)
{
    u32 varId;
    u32 localId;
    u32 objectEventId;
    struct ObjectEvent *object;
    u16 speciesId;

    varId = ScriptReadHalfword(ctx);
    localId = VarGet(ScriptReadHalfword(ctx));
    objectEventId = GetObjectEventIdByLocalId(localId);
    speciesId = SPECIES_NONE;

    // GetObjectEventIdByLocalId returns OBJECT_EVENTS_COUNT when the object is gone;
    // indexing with that reads past gObjectEvents.
    assertf(objectEventId < OBJECT_EVENTS_COUNT, "species was not found for specified object")
    {
        VarSet(varId, SPECIES_NONE);
        return;
    }

    object = &gObjectEvents[objectEventId];

    switch (object->graphicsId)
    {
    case OBJ_EVENT_GFX_RAYQUAZA_STILL:
    case OBJ_EVENT_GFX_RAYQUAZA:
        speciesId = SPECIES_RAYQUAZA;
        break;

    case OBJ_EVENT_GFX_UNUSED_NATU_DOLL:
        speciesId = SPECIES_NATU;
        break;

    case OBJ_EVENT_GFX_UNUSED_SQUIRTLE_DOLL:
        speciesId = SPECIES_SQUIRTLE;
        break;

    case OBJ_EVENT_GFX_UNUSED_WOOPER_DOLL:
        speciesId = SPECIES_WOOPER;
        break;

    case OBJ_EVENT_GFX_UNUSED_PIKACHU_DOLL:
    case OBJ_EVENT_GFX_PIKACHU_DOLL:
    case OBJ_EVENT_GFX_PIKACHU:
        speciesId = SPECIES_PIKACHU;
        break;

    case OBJ_EVENT_GFX_UNUSED_MAGNEMITE_DOLL:
        speciesId = SPECIES_MAGNEMITE;
        break;

    case OBJ_EVENT_GFX_UNUSED_PORYGON2_DOLL:
        speciesId = SPECIES_PORYGON2;
        break;

    case OBJ_EVENT_GFX_VIGOROTH_CARRYING_BOX:
    case OBJ_EVENT_GFX_VIGOROTH_FACING_AWAY:
        speciesId = SPECIES_VIGOROTH;
        break;

    case OBJ_EVENT_GFX_ZIGZAGOON_1:
    case OBJ_EVENT_GFX_ZIGZAGOON_2:
        speciesId = SPECIES_ZIGZAGOON;
        break;

    case OBJ_EVENT_GFX_PICHU_DOLL:
        speciesId = SPECIES_PICHU;
        break;

    case OBJ_EVENT_GFX_MARILL_DOLL:
        speciesId = SPECIES_MARILL;
        break;

    case OBJ_EVENT_GFX_TOGEPI_DOLL:
        speciesId = SPECIES_TOGEPI;
        break;

    case OBJ_EVENT_GFX_CYNDAQUIL_DOLL:
        speciesId = SPECIES_CYNDAQUIL;
        break;

    case OBJ_EVENT_GFX_CHIKORITA_DOLL:
        speciesId = SPECIES_CHIKORITA;
        break;

    case OBJ_EVENT_GFX_TOTODILE_DOLL:
        speciesId = SPECIES_TOTODILE;
        break;

    case OBJ_EVENT_GFX_JIGGLYPUFF_DOLL:
        speciesId = SPECIES_JIGGLYPUFF;
        break;

    case OBJ_EVENT_GFX_MEOWTH_DOLL:
        speciesId = SPECIES_MEOWTH;
        break;

    case OBJ_EVENT_GFX_CLEFAIRY_DOLL:
        speciesId = SPECIES_CLEFAIRY;
        break;

    case OBJ_EVENT_GFX_DITTO_DOLL:
        speciesId = SPECIES_DITTO;
        break;

    case OBJ_EVENT_GFX_SMOOCHUM_DOLL:
        speciesId = SPECIES_SMOOCHUM;
        break;

    case OBJ_EVENT_GFX_TREECKO_DOLL:
        speciesId = SPECIES_TREECKO;
        break;

    case OBJ_EVENT_GFX_TORCHIC_DOLL:
        speciesId = SPECIES_TORCHIC;
        break;

    case OBJ_EVENT_GFX_MUDKIP_DOLL:
        speciesId = SPECIES_MUDKIP;
        break;

    case OBJ_EVENT_GFX_DUSKULL_DOLL:
        speciesId = SPECIES_DUSKULL;
        break;

    case OBJ_EVENT_GFX_WYNAUT_DOLL:
        speciesId = SPECIES_WYNAUT;
        break;

    case OBJ_EVENT_GFX_BALTOY_DOLL:
        speciesId = SPECIES_BALTOY;
        break;

    case OBJ_EVENT_GFX_KECLEON_DOLL:
    case OBJ_EVENT_GFX_KECLEON:
    case OBJ_EVENT_GFX_KECLEON_BRIDGE_SHADOW:
        speciesId = SPECIES_KECLEON;
        break;

    case OBJ_EVENT_GFX_AZURILL_DOLL:
    case OBJ_EVENT_GFX_AZURILL:
        speciesId = SPECIES_AZURILL;
        break;

    case OBJ_EVENT_GFX_SKITTY_DOLL:
    case OBJ_EVENT_GFX_SKITTY:
        speciesId = SPECIES_SKITTY;
        break;

    case OBJ_EVENT_GFX_SWABLU_DOLL:
        speciesId = SPECIES_SWABLU;
        break;

    case OBJ_EVENT_GFX_GULPIN_DOLL:
        speciesId = SPECIES_GULPIN;
        break;

    case OBJ_EVENT_GFX_LOTAD_DOLL:
        speciesId = SPECIES_LOTAD;
        break;

    case OBJ_EVENT_GFX_SEEDOT_DOLL:
        speciesId = SPECIES_SEEDOT;
        break;

    case OBJ_EVENT_GFX_BIG_SNORLAX_DOLL:
        speciesId = SPECIES_SNORLAX;
        break;

    case OBJ_EVENT_GFX_BIG_RHYDON_DOLL:
        speciesId = SPECIES_RHYDON;
        break;

    case OBJ_EVENT_GFX_BIG_LAPRAS_DOLL:
        speciesId = SPECIES_LAPRAS;
        break;

    case OBJ_EVENT_GFX_BIG_VENUSAUR_DOLL:
        speciesId = SPECIES_VENUSAUR;
        break;

    case OBJ_EVENT_GFX_BIG_CHARIZARD_DOLL:
        speciesId = SPECIES_CHARIZARD;
        break;

    case OBJ_EVENT_GFX_BIG_BLASTOISE_DOLL:
        speciesId = SPECIES_BLASTOISE;
        break;

    case OBJ_EVENT_GFX_BIG_WAILMER_DOLL:
        speciesId = SPECIES_WAILMER;
        break;

    case OBJ_EVENT_GFX_BIG_REGIROCK_DOLL:
    case OBJ_EVENT_GFX_REGIROCK:
        speciesId = SPECIES_REGIROCK;
        break;

    case OBJ_EVENT_GFX_BIG_REGICE_DOLL:
    case OBJ_EVENT_GFX_REGICE:
        speciesId = SPECIES_REGICE;
        break;

    case OBJ_EVENT_GFX_BIG_REGISTEEL_DOLL:
    case OBJ_EVENT_GFX_REGISTEEL:
        speciesId = SPECIES_REGISTEEL;
        break;

    case OBJ_EVENT_GFX_LATIAS:
        speciesId = SPECIES_LATIAS;
        break;

    case OBJ_EVENT_GFX_LATIOS:
        speciesId = SPECIES_LATIOS;
        break;

    case OBJ_EVENT_GFX_KYOGRE_FRONT:
    case OBJ_EVENT_GFX_KYOGRE_ASLEEP:
    case OBJ_EVENT_GFX_KYOGRE_SIDE:
        speciesId = SPECIES_KYOGRE;
        break;

    case OBJ_EVENT_GFX_GROUDON_FRONT:
    case OBJ_EVENT_GFX_GROUDON_ASLEEP:
    case OBJ_EVENT_GFX_GROUDON_SIDE:
        speciesId = SPECIES_GROUDON;
        break;

    case OBJ_EVENT_GFX_AZUMARILL:
        speciesId = SPECIES_AZUMARILL;
        break;

    case OBJ_EVENT_GFX_WINGULL:
        speciesId = SPECIES_WINGULL;
        break;

    case OBJ_EVENT_GFX_POOCHYENA:
        speciesId = SPECIES_POOCHYENA;
        break;

    case OBJ_EVENT_GFX_KIRLIA:
        speciesId = SPECIES_KIRLIA;
        break;

    case OBJ_EVENT_GFX_DUSCLOPS:
        speciesId = SPECIES_DUSCLOPS;
        break;

    case OBJ_EVENT_GFX_SUDOWOODO:
        speciesId = SPECIES_SUDOWOODO;
        break;

    case OBJ_EVENT_GFX_MEW:
        speciesId = SPECIES_MEW;
        break;

    case OBJ_EVENT_GFX_DEOXYS:
        speciesId = SPECIES_DEOXYS;
        break;

    case OBJ_EVENT_GFX_LUGIA:
        speciesId = SPECIES_LUGIA;
        break;

    case OBJ_EVENT_GFX_HOOH:
        speciesId = SPECIES_HO_OH;
        break;

    case OBJ_EVENT_GFX_CELEBI:
        speciesId = SPECIES_CELEBI;
        break;

    case OBJ_EVENT_GFX_SUICUNE:
        speciesId = SPECIES_SUICUNE;
        break;

    case OBJ_EVENT_GFX_ENTEI:
        speciesId = SPECIES_ENTEI;
        break;

    case OBJ_EVENT_GFX_RAIKOU:
        speciesId = SPECIES_RAIKOU;
        break;

    case OBJ_EVENT_GFX_ARTICUNO:
        speciesId = SPECIES_ARTICUNO;
        break;

    case OBJ_EVENT_GFX_ZAPDOS:
        speciesId = SPECIES_ZAPDOS;
        break;

    case OBJ_EVENT_GFX_MOLTRES:
        speciesId = SPECIES_MOLTRES;
        break;

    case OBJ_EVENT_GFX_MEWTWO:
        speciesId = SPECIES_MEWTWO;
        break;

    case OBJ_EVENT_GFX_WHISMUR:
        speciesId = SPECIES_WHISMUR;
        break;

    case OBJ_EVENT_GFX_EEVEE:
        speciesId = SPECIES_EEVEE;
        break;

    case OBJ_EVENT_GFX_VAPOREON:
        speciesId = SPECIES_VAPOREON;
        break;

    case OBJ_EVENT_GFX_FLAREON:
        speciesId = SPECIES_FLAREON;
        break;

    case OBJ_EVENT_GFX_JOLTEON:
        speciesId = SPECIES_JOLTEON;
        break;

    case OBJ_EVENT_GFX_UMBREON:
        speciesId = SPECIES_UMBREON;
        break;

    case OBJ_EVENT_GFX_ESPEON:
        speciesId = SPECIES_ESPEON;
        break;

    default:
        if (IS_OW_MON_OBJ(object))
            speciesId = OW_SPECIES(object);
        break;
    }

    // assertf is `if (!(cond))`, so with a trailing semicolon this checked nothing and an
    // out-of-range species could reach the var (and from there playmoncry).
    assertf(CheckValidOWESpecies(speciesId), "species was not found for specified object")
    {
        speciesId = SPECIES_NONE;
    }

    VarSet(varId, speciesId);
}

static bool32 CreateEnemyPartyOWE(struct InfoOWE *info, s32 x, s32 y)
{
    const struct WildPokemonInfo *wildMonInfo;
    u32 headerId;
    u32 metatileBehavior;
    u8 wildArea;

    headerId = GetCurrentMapWildMonHeaderId();
    metatileBehavior = MapGridGetMetatileBehaviorAt(x, y);

    if (headerId == HEADER_NONE)
    {
        return FALSE;
    }

    if (MetatileBehavior_IsWaterWildEncounter(metatileBehavior))
    {
        wildArea = WILD_AREA_WATER;
        wildMonInfo = gWildMonHeaders[headerId].waterMonsInfo;
    }
    else
    {
        wildArea = WILD_AREA_LAND;
        wildMonInfo = gWildMonHeaders[headerId].landMonsInfo;
    }

    if (wildMonInfo == NULL)
        return FALSE;

    if (info->category == OWE_CATEGORY_UNDEFINED)
    {
        if (TryStartRoamerEncounter() && !OWE_DoesOWERoamerExist())
        {
            info->category = 0;
            return TRUE;
        }
        else if (WE_OWE_FEEBAS_SPOTS && MetatileBehavior_IsWaterWildEncounter(metatileBehavior) && CheckFeebasAtCoords(x, y))
        {
            CreateWildMon(gWildFeebas.species, ChooseWildMonLevel(&gWildFeebas));
            info->category = OWE_CATEGORY_FEEBAS;
            if (WE_OWE_PREVENT_FEEBAS_DESPAWN)
                info->noDespawn = TRUE;

            return TRUE;
        }
        else if (DoMassOutbreakEncounterTest() && MetatileBehavior_IsLandWildEncounter(metatileBehavior))
        {
            SetUpMassOutbreakEncounter(0);
            info->category = OWE_CATEGORY_MASS_OUTBREAK;
            return TRUE;
        }
        else
        {
            return TryGenerateWildMon(wildMonInfo, wildArea, 0);
        }
    }

    return TryGenerateWildMon(wildMonInfo, wildArea, 0);
}

static bool32 OWE_DoesOWERoamerExist(void)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *owe = &gObjectEvents[i];
        if (IsOverworldWildEncounter(owe, OWE_ANY) && GetOWECategory(owe) < ROAMER_COUNT)
            return TRUE;
    }

    return FALSE;
}

static bool32 StartWildBattleWithOWE_CheckRoamer(enum CategoryOWE category)
{
    if (category < ROAMER_COUNT
     && IsRoamerAt(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum))
    {
        CreateRoamerMonInstance();
        BattleSetup_StartRoamerBattle();
        return TRUE;
    }

    return FALSE;
}

static bool32 StartWildBattleWithOWE_CheckMassOutbreak(enum CategoryOWE category, u16 speciesId)
{
    if (category == OWE_CATEGORY_MASS_OUTBREAK
     && gSaveBlock1Ptr->outbreakPokemonSpecies == speciesId)
    {
        ZeroEnemyPartyMons();
        SetUpMassOutbreakEncounter(0);
        BattleSetup_StartWildBattle();
        return TRUE;
    }

    return FALSE;
}

void SetInstantOWESpawnTimer(void)
{
    if (!WE_OW_ENCOUNTERS)
        return;

    sOWESpawnCountdown = 0;
}

void SetMinimumOWESpawnTimer(void)
{
    if (!WE_OW_ENCOUNTERS)
        return;

    sOWESpawnCountdown = OWE_SPAWN_TIME_MINIMUM;
}

void TryTriggerOverworldWildEncounter(struct ObjectEvent *obstacle, struct ObjectEvent *collider)
{
    bool32 playerFollowerIsColliderOWE;
    bool32 playerFollowerIsObstacleOWE;
    struct ObjectEvent *wildMon;
    enum CategoryOWE category;

    if (WE_OWE_NO_REPEL_DEXNAV_COLLISION && REPEL_STEP_COUNT)
        return;

    playerFollowerIsColliderOWE = ((collider->isPlayer || collider->localId == OBJ_EVENT_ID_FOLLOWER)
                                  && IsOverworldWildEncounter(obstacle, OWE_ANY));
    playerFollowerIsObstacleOWE = ((obstacle->isPlayer || obstacle->localId == OBJ_EVENT_ID_FOLLOWER)
                                  && IsOverworldWildEncounter(collider, OWE_ANY));

    if (!playerFollowerIsColliderOWE && !playerFollowerIsObstacleOWE)
        return;

    wildMon = playerFollowerIsColliderOWE ? obstacle : collider;
    category = GetOWECategory(wildMon);
    if (category < ROAMER_COUNT
     && !IsRoamerAt(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum))
    {
        RemoveObjectEvent(wildMon);
        return;
    }

    gSpecialVar_LastTalked = wildMon->localId;
    gSelectedObjectEvent = GetObjectEventIdByLocalId(wildMon->localId);

    // Stop the bobbing animation.
    if (wildMon->movementActionId >= MOVEMENT_ACTION_WALK_IN_PLACE_NORMAL_DOWN && wildMon->movementActionId <= MOVEMENT_ACTION_WALK_IN_PLACE_NORMAL_RIGHT)
        ClearObjectEventMovement(wildMon, &gSprites[wildMon->spriteId]);

    ScriptContext_SetupScript(InteractWithOverworldWildEncounter);
}

const u8 *GetOverworlWildEncounterScript(u32 objectEventId)
{
    const u8 *script = GetObjectEventScriptPointerByObjectEventId(objectEventId);
    if (script)
        return script;
    
    return InteractWithOverworldWildEncounter;
}

static bool32 CheckCurrentWildMonHeaderForOWE(bool32 shouldSpawnWaterMons)
{
    u32 headerId = GetCurrentMapWildMonHeaderId();

    if (headerId == HEADER_NONE)
        return FALSE;

    if (shouldSpawnWaterMons)
        return gWildMonHeaders[headerId].waterMonsInfo != NULL;

    return gWildMonHeaders[headerId].landMonsInfo != NULL;
}

// An encounter the player is currently interacting with must never be despawned out from
// under the running script. LoadSpritePalette calls RemoveOldestGeneratedOWE re-entrantly
// when it runs out of palette slots, and forceRemove ignores the no-despawn flag, so
// without this the emote or follower recall in the middle of the encounter script could
// delete the very mon being talked to. RemoveObjectEvent then zeroes graphicsId, which
// reaches CreateWildMon as an out-of-range species.
static bool32 IsOWEInteractionLocked(struct ObjectEvent *owe)
{
    if (sBattleOWEObjectEventId < OBJECT_EVENTS_COUNT && owe == &gObjectEvents[sBattleOWEObjectEventId])
        return TRUE;

    // Only while a script actually holds the player. gSpecialVar_LastTalked keeps its value
    // after the encounter ends and must not pin the slot permanently.
    if (ArePlayerFieldControlsLocked() && owe->localId == gSpecialVar_LastTalked)
        return TRUE;

    return FALSE;
}

static u32 GetOldestActiveOWESlot(bool32 forceRemove)
{
    struct ObjectEvent *slotMon, *oldest = NULL;
    u32 spawnSlot;
    u32 i;
    u8 objEventId;

    for (spawnSlot = 0; spawnSlot < OWE_SPAWNS_MAX; spawnSlot++)
    {
        objEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(spawnSlot));
        if (objEventId >= OBJECT_EVENTS_COUNT)
            continue;

        slotMon = &gObjectEvents[objEventId];
        if (IsOverworldWildEncounter(slotMon, OWE_GENERATED) && OW_SPECIES(slotMon) != SPECIES_NONE && (!HasOWENoDespawnFlag(slotMon) || forceRemove == TRUE)
         && !IsOWEInteractionLocked(slotMon))
        {
            oldest = slotMon;
            break;
        }
    }

    if (oldest == NULL)
        return OWE_INVALID_SPAWN_SLOT;

    for (i = spawnSlot + 1; i < OWE_SPAWNS_MAX; i++)
    {
        objEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(i));
        if (objEventId >= OBJECT_EVENTS_COUNT)
            continue;

        slotMon = &gObjectEvents[objEventId];
        if (IsOverworldWildEncounter(slotMon, OWE_GENERATED) && OW_SPECIES(slotMon) != SPECIES_NONE && (!HasOWENoDespawnFlag(slotMon) || forceRemove == TRUE)
         && !IsOWEInteractionLocked(slotMon))
        {
            if (slotMon->sOverworldEncounterAge > oldest->sOverworldEncounterAge)
                oldest = slotMon;
        }
    }

    return GetSpawnSlotByOWELocalId(oldest->localId);
}

static u32 GetNextOWESpawnSlot(void)
{
    u32 spawnSlot;

    // All mon slots are in use
    if (GetNumberOfActiveOWEs(OWE_GENERATED) >= OWE_SPAWNS_MAX)
    {
        if (WE_OWE_SPAWN_REPLACEMENT)
        {
            // Cycle through so we remove the oldest mon first
            return GetOldestActiveOWESlot(FALSE); 
        }
        return OWE_INVALID_SPAWN_SLOT;
    }
    for (spawnSlot = 0; spawnSlot < OWE_SPAWNS_MAX; spawnSlot++)
    {
        if (GetSpeciesByOWESpawnSlot(spawnSlot) == SPECIES_NONE)
            break;
    }

    return spawnSlot;
}

static u32 GetSpeciesByOWESpawnSlot(u32 spawnSlot)
{
    u32 objEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(spawnSlot));
    struct ObjectEvent *owe = &gObjectEvents[objEventId];

    if (objEventId >= OBJECT_EVENTS_COUNT)
        return SPECIES_NONE;

    return OW_SPECIES(owe);
}

static bool32 TrySelectTileForOWE(s32* outX, s32* outY)
{
    u32 elevation;
    u32 tileBehavior;
    s16 playerX, playerY;
    s16 x, y;
    u32 closeDistance;
    bool32 isEncounterTile = FALSE;

    // Spawn further away when surfing
    if (ShouldSpawnWaterOWE())
        closeDistance = OWE_SPAWN_DISTANCE_WATER;
    else
        closeDistance = OWE_SPAWN_DISTANCE_LAND;

    // Select a random tile in [-OWE_SPAWN_WIDTH_RADIUS, -OWE_SPAWN_HEIGHT_RADIUS] [OWE_SPAWN_WIDTH_RADIUS, OWE_SPAWN_HEIGHT_RADIUS]
    // range while excluding tiles that are less than closeDistance away from the player.
    x = (s16)(Random() % (OWE_SPAWN_WIDTH_TOTAL - 2 * closeDistance) ) - (OWE_SPAWN_WIDTH_RADIUS - closeDistance);
    if (x < 0)
        x -= closeDistance;
    else
        x += closeDistance;

    y = (s16)(Random() % (OWE_SPAWN_HEIGHT_TOTAL - 2 * closeDistance) ) - (OWE_SPAWN_HEIGHT_RADIUS - closeDistance);
    if (y < 0)
        y -= closeDistance;
    else
        y += closeDistance;
    
    PlayerGetDestCoords(&playerX, &playerY);
    x += playerX;
    y += playerY;

    elevation = MapGridGetElevationAt(x, y);

    if (!AreCoordsInsidePlayerMap(x, y))
        return FALSE;

    tileBehavior = MapGridGetMetatileBehaviorAt(x, y);
    if (ShouldSpawnWaterOWE() && MetatileBehavior_IsWaterWildEncounter(tileBehavior))
        isEncounterTile = TRUE;

    if (!ShouldSpawnWaterOWE() && MetatileBehavior_IsLandWildEncounter(tileBehavior))
        isEncounterTile = TRUE;

    if (isEncounterTile && !MapGridGetCollisionAt(x, y))
    {
        *outX = x;
        *outY = y;

        if (GetObjectEventIdByPosition(x, y, 0) == OBJECT_EVENTS_COUNT)
            return TRUE;
    }

    return FALSE;
}

static void SetSpeciesInfoForOWE(struct InfoOWE *info, u32 x, u32 y)
{
    u32 personality;

    if (!CreateEnemyPartyOWE(info, x, y))
    {
        ZeroEnemyPartyMons();
        info->speciesId = SPECIES_NONE;
        return;
    }
 
    info->speciesId = GetMonData(&gEnemyParty[0], MON_DATA_SPECIES);
    info->level = GetMonData(&gEnemyParty[0], MON_DATA_LEVEL);
    personality = GetMonData(&gEnemyParty[0], MON_DATA_PERSONALITY);

    info->isShiny = ComputePlayerShinyOdds(personality, READ_OTID_FROM_SAVE);
    if (GetGenderFromSpeciesAndPersonality(info->speciesId, personality) == MON_FEMALE)
        info->isFemale = TRUE;
    else
        info->isFemale = FALSE;

    if (WE_OWE_PREVENT_SHINY_DESPAWN && info->isShiny)
        info->noDespawn = TRUE;

    if (info->category == OWE_CATEGORY_UNDEFINED)
        info->category = OWE_CATEGORY_WILD;

    ZeroEnemyPartyMons();
}

static u32 GetGraphicsIdForOWE(const struct InfoOWE *info)
{
    // assertf is `if (!(cond))`, so writing it with a trailing semicolon compiled to
    // `if (!cond);` and checked nothing. It needs a real failure block.
    assertf(CheckValidOWESpecies(info->speciesId), "invalid generated overworld encounter")
    {
        return OBJ_EVENT_GFX_MON_BASE;
    }

    return OBJ_EVENT_GFX_MON_BASE + info->speciesId;
}

static bool32 CheckCanLoadOWE(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y)
{
    assertf(CheckCanLoadOWE_Palette(speciesId, isFemale, isShiny, x, y), "could not load palette")
    {
        return FALSE;
    }

    assertf(CheckCanLoadOWE_Tiles(speciesId, isFemale, isShiny, x, y), "could not load sprite tiles")
    {
        return FALSE;
    }

    return TRUE;
}

static bool32 CheckCanLoadOWE_Palette(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y)
{
    u32 numFreePalSlots = CountFreePaletteSlots();
    // GetGraphicsIdForOWE does not carry SPECIES_OVERWORLD_SHINY_TAG and owe->shiny is
    // only set after the spawn, so the sprite is always created with the normal palette.
    // That is the tag to check for here.
    u32 tag = speciesId + SPECIES_OVERWORLD_TAG;

    // Deliberately no extra reservation for shinies. Failing this check discards the
    // encounter outright, so charging a shiny an extra slot silently filtered shinies out
    // on routes with palette pressure. A shiny that cannot fit its own palette now falls
    // back to rendering in normal colours (LoadSpeciesPaletteWithFallback) and still holds
    // only one slot, so it costs no more than any other encounter.

    if (numFreePalSlots == 1)
    {
        u32 metatileBehavior = MapGridGetMetatileBehaviorAt(x, y);
        struct SpritePalette palette = GetOWESpawnDespawnAnimFldEffPalette(GetOWESpawnDespawnAnimType(metatileBehavior));
        if (IndexOfSpritePaletteTag(tag) == 0xFF && IndexOfSpritePaletteTag(palette.tag) == 0xFF)
            return FALSE;
    }
    else if (numFreePalSlots == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static u32 GetNumberOfSpawnAnimTiles(s32 x, s32 y)
{
    u32 metatileBehavior = MapGridGetMetatileBehaviorAt(x, y);
    enum SpawnDespawnTypeOWE spawnAnimType = GetOWESpawnDespawnAnimType(metatileBehavior);
    u32 visual = gOverworldWildEncounterFieldEffectInfo[spawnAnimType].visual;

    return gFieldEffectObjectTemplatePointers[visual]->images->size / TILE_SIZE_4BPP;
}

static bool32 CheckCanLoadOWE_Tiles(u16 speciesId, bool32 isFemale, bool32 isShiny, s32 x, s32 y)
{
    u32 graphicsId = OBJ_EVENT_GFX_MON_BASE + speciesId;
    const struct ObjectEventGraphicsInfo *graphicsInfo = GetObjectEventGraphicsInfo(graphicsId);
    u32 tileCount = graphicsInfo->size / TILE_SIZE_4BPP;
    tileCount += GetNumberOfSpawnAnimTiles(x, y);
    if (!CanAllocSpriteTiles(tileCount))
        return FALSE;
    
    return TRUE;
}

static void SortOWEAges(void)
{
    struct ObjectEvent *slotMon;
    struct AgeSort array[OWE_SPAWNS_MAX];
    struct AgeSort current;
    u32 numActive = GetNumberOfActiveOWEs(OWE_GENERATED);
    u32 count = 0;
    s32 i, j;
    u8 objEventId;

    if (OWE_SPAWNS_MAX <= 1)
        return;

    for (i = 0; i < OWE_SPAWNS_MAX; i++)
    {
        objEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(i));
        if (objEventId >= OBJECT_EVENTS_COUNT)
            continue;

        slotMon = &gObjectEvents[objEventId];
        if (IsOverworldWildEncounter(slotMon, OWE_GENERATED) && OW_SPECIES(slotMon) != SPECIES_NONE)
        {
            array[count].slot = i;
            array[count].age = slotMon->sOverworldEncounterAge;
            count++;
        }
        if (count == numActive)
            break;
    }

    for (i = 1; i < count; i++)
    {
        current = array[i];
        j = i - 1;

        while (j >= 0 && array[j].age < current.age)
        {
            array[j + 1] = array[j];
            j--;
        }

        array[j + 1] = current;
    }
    
    for (i = 0; i < count; i++)
    {
        objEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(array[i].slot));
        if (objEventId < OBJECT_EVENTS_COUNT)
        {
            slotMon = &gObjectEvents[objEventId];
            slotMon->sOverworldEncounterAge = count - i;
        }
    }
}

void OnOverworldWildEncounterSpawn(struct ObjectEvent *owe)
{
    enum TypeOWE type = GetOverworldWildEncounterType(owe);
    if (type == OWE_NONE)
        return;

    if (type == OWE_MANUAL)
        owe->sOverworldEncounterCategory = OWE_CATEGORY_WILD;
    
    if (type == OWE_GENERATED)
        SortOWEAges();

    DoOWESpawnAnim(owe);
}

void OnOverworldWildEncounterDespawn(struct ObjectEvent *owe)
{
    enum TypeOWE type = GetOverworldWildEncounterType(owe);
    if (type == OWE_NONE)
        return;

    if (owe->sOverworldEncounterCategory < ROAMER_COUNT)
        RoamerMove();

    owe->sOverworldEncounterLevel = 0;
    owe->sOverworldEncounterAge = 0;
    owe->sOverworldEncounterCategory = 0;
    
    DoOWEDespawnAnim(owe);
}

bool32 IsOWEDespawnExempt(struct ObjectEvent *owe)
{
    if (!IsOverworldWildEncounter(owe, OWE_ANY))
        return FALSE;

    if (HasOWENoDespawnFlag(owe) && AreCoordsInsidePlayerMap(owe->currentCoords.x, owe->currentCoords.y))
        return TRUE;

    return FALSE;
}

bool32 DespawnOWEDueToNPCCollision(struct ObjectEvent *obstacle, struct ObjectEvent *activeObject)
{
    if (activeObject->isPlayer)
        return FALSE;

    if (IsOverworldWildEncounter(activeObject, OWE_ANY))
        return FALSE;
    
    if (!IsOverworldWildEncounter(obstacle, OWE_GENERATED))
        return FALSE;

    RemoveObjectEvent(obstacle);
    return TRUE;
}

void DespawnAllOverworldWildEncounters(enum TypeOWE oweType, u32 flags)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; ++i)
    {
        struct ObjectEvent *owe = &gObjectEvents[i];

        if (!owe->active)
            continue;

        if (!IsOverworldWildEncounter(owe, oweType))
            continue;

        if (flags & WILD_CHECK_REPEL)
        {
            if (!REPEL_STEP_COUNT)
                continue;

            if (HasOWENoDespawnFlag(owe))
                continue;

            if (IsWildLevelAllowedByRepel(owe->sOverworldEncounterLevel & ~OWE_NO_DESPAWN_FLAG))
                continue;
        }

        RemoveObjectEvent(owe);
    }
}

bool32 TryAndDespawnOldestGeneratedOWE_ToFreeObject(u8 *objectEventId)
{
    if (!WE_OW_ENCOUNTERS)
        return FALSE;
    
    *objectEventId = RemoveOldestGeneratedOWE();
    if (*objectEventId == OBJECT_EVENTS_COUNT)
        return TRUE;
    
    return FALSE;
}

void DespawnOWEOnBattleStart(void)
{
    struct ObjectEvent *owe;
    u8 localId = gSpecialVar_LastTalked;
    u8 objEventId;

    if (sBattleOWEObjectEventId < OBJECT_EVENTS_COUNT)
    {
        owe = &gObjectEvents[sBattleOWEObjectEventId];
        if (IsOverworldWildEncounter(owe, OWE_ANY))
            RemoveObjectEvent(owe);
        sBattleOWEObjectEventId = OBJECT_EVENTS_COUNT;
        SetNewOWESpawnCountdown();
        gSpecialVar_LastTalked = 0;
        return;
    }

    if (localId == 0)
        return;

    objEventId = GetObjectEventIdByLocalId(localId);
    if (objEventId >= OBJECT_EVENTS_COUNT)
        return;

    owe = &gObjectEvents[objEventId];
    if (!IsOverworldWildEncounter(owe, OWE_ANY))
        return;

    RemoveObjectEvent(owe);
    SetNewOWESpawnCountdown();
    gSpecialVar_LastTalked = 0;
}

void TryDespawnOWEsCrossingMapConnection(void)
{
    if (!WE_OWE_DESPAWN_ON_ENTER_TOWN)
        return;

    if (gMapHeader.mapType != MAP_TYPE_CITY && gMapHeader.mapType != MAP_TYPE_TOWN)
        return;

    if (GetNumberOfActiveOWEs(OWE_GENERATED) == 0)
        return;

    DespawnAllOverworldWildEncounters(OWE_GENERATED, 0);
}

u32 RemoveOldestGeneratedOWE(void)
{
    u32 oldestSlot;
    u32 objectEventId;

    oldestSlot = GetOldestActiveOWESlot(TRUE);
    if (oldestSlot == OWE_INVALID_SPAWN_SLOT)
        return OBJECT_EVENTS_COUNT;

    objectEventId = GetObjectEventIdByLocalId(GetLocalIdByOWESpawnSlot(oldestSlot));
    if (objectEventId < OBJECT_EVENTS_COUNT)
        RemoveObjectEvent(&gObjectEvents[objectEventId]);
    return objectEventId;
}

static bool32 ShouldDespawnGeneratedForNewOWE(struct ObjectEvent *owe)
{
    if (!IsOverworldWildEncounter(owe, OWE_GENERATED))
        return FALSE;

    return WE_OWE_SPAWN_REPLACEMENT && GetNumberOfActiveOWEs(OWE_GENERATED) >= OWE_SPAWNS_MAX;
}

static void SetNewOWESpawnCountdown(void)
{
    u32 numActive = GetNumberOfActiveOWEs(OWE_GENERATED);

    if (WE_OWE_SPAWN_REPLACEMENT && numActive >= OWE_SPAWNS_MAX)
        sOWESpawnCountdown = OWE_SPAWN_TIME_REPLACEMENT;
    else
        sOWESpawnCountdown = OWE_SPAWN_TIME_MINIMUM + (OWE_SPAWN_TIME_PER_ACTIVE * numActive);
}

static void DoOWESpawnAnim(struct ObjectEvent *owe)
{
    bool32 isShiny;
    enum SpawnDespawnTypeOWE spawnAnimType;
    u32 metatileBehavior;

    isShiny = OW_SHINY(owe) ? TRUE : FALSE;

    if (WE_OWE_SHINY_SPARKLE && isShiny)
    {
        PlaySE(SE_SHINY);
        spawnAnimType = OWE_SPAWN_ANIM_SHINY;
    }
    else
    {
        PlayOWECry(owe);
        metatileBehavior = MapGridGetMetatileBehaviorAt(owe->currentCoords.x, owe->currentCoords.y);
        spawnAnimType = GetOWESpawnDespawnAnimType(metatileBehavior);
    }

    MovementAction_OverworldEncounterSpawn(spawnAnimType, owe);
}

static void DoOWEDespawnAnim(struct ObjectEvent *owe)
{
    u32 metatileBehavior;
    enum SpawnDespawnTypeOWE spawnAnimType;

    metatileBehavior = MapGridGetMetatileBehaviorAt(owe->currentCoords.x, owe->currentCoords.y);
    spawnAnimType = GetOWESpawnDespawnAnimType(metatileBehavior);
    MovementAction_OverworldEncounterSpawn(spawnAnimType, owe);
    if (OWE_ShouldPlayOWEFleeSound(owe))
        PlaySE(SE_FLEE);
}

static enum SpawnDespawnTypeOWE GetOWESpawnDespawnAnimType(u32 metatileBehavior)
{
    if (MetatileBehavior_IsPokeGrass(metatileBehavior) || MetatileBehavior_IsAshGrass(metatileBehavior))
        return OWE_SPAWN_ANIM_GRASS;
    else if (MetatileBehavior_IsLongGrass(metatileBehavior))
        return OWE_SPAWN_ANIM_LONG_GRASS;
    else if (MetatileBehavior_IsSurfableFishableWater(metatileBehavior) && gMapHeader.mapType != MAP_TYPE_UNDERWATER)
        return OWE_SPAWN_ANIM_WATER;
    else if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_UNDERWATER))
        return OWE_SPAWN_ANIM_UNDERWATER;
    else
        return OWE_SPAWN_ANIM_CAVE;
}

static void PlayOWECry(struct ObjectEvent *owe)
{
    struct ObjectEvent *player;
    u16 speciesId;
    s32 distanceX;
    s32 distanceY;
    u32 distanceMax;
    u32 distance;
    u32 volume;
    s32 pan;

    if (!IsOverworldWildEncounter(owe, OWE_ANY))
        return;
    
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    speciesId = OW_SPECIES(owe);
    if (speciesId == SPECIES_NONE || speciesId >= NUM_SPECIES)
        return;

    distanceX = owe->currentCoords.x - player->currentCoords.x;
    distanceY = owe->currentCoords.y - player->currentCoords.y;
    distanceMax = OWE_SPAWN_WIDTH_RADIUS + OWE_SPAWN_HEIGHT_RADIUS;

    if (distanceX > OWE_SPAWN_WIDTH_RADIUS)
        distanceX = OWE_SPAWN_WIDTH_RADIUS;
    else if (distanceX < -OWE_SPAWN_WIDTH_RADIUS)
        distanceX = -OWE_SPAWN_WIDTH_RADIUS;

    distanceY = abs(distanceY);
    if (distanceY > OWE_SPAWN_HEIGHT_RADIUS)
        distanceY = OWE_SPAWN_HEIGHT_RADIUS;

    distance = abs(distanceX) + distanceY;
    if (distance > distanceMax)
        distance = distanceMax;

    volume = 80 - (distance * 30) / distanceMax;
    pan = (distanceX * 60) / OWE_SPAWN_WIDTH_RADIUS;
    
    PlayCry_NormalNoDucking(speciesId, pan, volume, CRY_PRIORITY_AMBIENT);
}

static struct ObjectEvent *GetRandomOWEObjectEvent(void)
{
    u32 counter = 0;
    struct ObjectEvent *owe;
    u32 tmpArray[OBJECT_EVENTS_COUNT];
    u32 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        owe = &gObjectEvents[i];
        if (IsOverworldWildEncounter(owe, OWE_ANY))
        {
            tmpArray[counter] = i;
            counter++;
        }
    }
    if (counter > 0)
        return &gObjectEvents[tmpArray[(Random() % counter)]];
        
    return NULL;
}

static bool32 OWE_ShouldPlayOWEFleeSound(struct ObjectEvent *owe)
{
    if (!IsOverworldWildEncounter(owe, OWE_ANY) || OW_SPECIES(owe) == SPECIES_NONE)
        return FALSE;

    if (!AreCoordsInsidePlayerMap(owe->currentCoords.x, owe->currentCoords.y))
        return FALSE;

    if (ShouldDespawnGeneratedForNewOWE(owe))
        return FALSE;

    if (owe->offScreen)
        return FALSE;

    return WE_OWE_DESPAWN_SOUND;
}

#define sTypeFuncId data[1] // Same as in src/event_object_movement.c
#define sJumpTimer  sprite->data[7] // Same as in src/event_object_movement.c
void RestoreSavedOWEBehaviorState(struct ObjectEvent *owe, struct Sprite *sprite)
{
    if (IsOverworldWildEncounter(owe, OWE_ANY) && HasSavedOWEMovementState(owe))
    {
        sprite->sTypeFuncId = OWE_RESTORED_MOVEMENT_FUNC_ID;
        if (owe->movementType == MOVEMENT_TYPE_APPROACH_PLAYER_OWE)
            sJumpTimer = RandomUniform(RNG_NONE, OWE_APPROACH_JUMP_TIMER_MIN, OWE_APPROACH_JUMP_TIMER_MAX);
    }
}
#undef sTypeFuncId
#undef sJumpTimer

// Returns TRUE if movement is restricted.
bool32 CheckRestrictedOWEMovement(struct ObjectEvent *owe, u8 direction)
{
    s32 xCurrent;
    s32 yCurrent;
    s32 xNew;
    s32 yNew;

    if (GetCollisionInDirection(owe, direction))
        return TRUE;

    if (WE_OWE_UNRESTRICT_SIGHT
     && owe->movementType != MOVEMENT_TYPE_WANDER_AROUND_OWE
     && CanAwareOWESeePlayer(owe))
        return FALSE;

    xCurrent = owe->currentCoords.x;
    yCurrent = owe->currentCoords.y;
    xNew = xCurrent + gDirectionToVectors[direction].x;
    yNew = yCurrent + gDirectionToVectors[direction].y;

    if (CheckRestrictedOWEMovementMetatile(xCurrent, yCurrent, xNew, yNew))
        return TRUE;
    
    if (CheckRestrictedOWEMovementMap(owe, xNew, yNew))
        return TRUE;

    return FALSE;
}

static bool32 CheckRestrictedOWEMovementAtCoords(struct ObjectEvent *owe, s32 xNew, s32 yNew, u8 newDirection, u8 collisionDirection)
{
    if (CheckRestrictedOWEMovementMetatile(owe->currentCoords.x, owe->currentCoords.y, xNew, yNew))
        return FALSE;

    if (CheckRestrictedOWEMovementMap(owe, xNew, yNew))
        return FALSE;

    if (GetCollisionAtCoords(owe, xNew, yNew, collisionDirection))
        return FALSE;

    return TRUE;
}

static bool32 CheckRestrictedOWEMovementMetatile(s32 xCurrent, s32 yCurrent, s32 xNew, s32 yNew)
{
    u32 metatileBehaviourCurrent;
    u32 metatileBehaviourNew;

    if (!WE_OWE_RESTRICT_METATILE)
        return FALSE;

    metatileBehaviourCurrent = MapGridGetMetatileBehaviorAt(xCurrent, yCurrent);
    metatileBehaviourNew = MapGridGetMetatileBehaviorAt(xNew, yNew);

    if (MetatileBehavior_IsLandWildEncounter(metatileBehaviourCurrent)
     && MetatileBehavior_IsLandWildEncounter(metatileBehaviourNew))
        return FALSE;

    if (MetatileBehavior_IsWaterWildEncounter(metatileBehaviourCurrent)
     && MetatileBehavior_IsWaterWildEncounter(metatileBehaviourNew))
        return FALSE;

    if (!MetatileBehavior_IsLandWildEncounter(metatileBehaviourCurrent)
     && !MetatileBehavior_IsWaterWildEncounter(metatileBehaviourCurrent))
        return FALSE;

    return TRUE;
}

static bool32 CheckRestrictedOWEMovementMap(struct ObjectEvent *owe, s32 xNew, s32 yNew)
{
    if (!WE_OWE_RESTRICT_MAP)
        return FALSE;
    
    if (owe->mapGroup == gSaveBlock1Ptr->location.mapGroup
     && owe->mapNum == gSaveBlock1Ptr->location.mapNum)
        return !AreCoordsInsidePlayerMap(xNew, yNew);
    else
        return AreCoordsInsidePlayerMap(xNew, yNew);
}

bool32 CanAwareOWESeePlayer(struct ObjectEvent *owe)
{
    struct ObjectEvent *player;
    u16 speciesId;
    u32 viewDistance;
    u32 viewWidth;
    s32 halfWidth;
    u8 direction;

    if (owe == NULL)
        return FALSE;

    if (owe->movementType == MOVEMENT_TYPE_WANDER_AROUND_OWE)
        return FALSE;

    if (gPlayerAvatar.runningState == MOVING
     && TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_DASH | PLAYER_AVATAR_FLAG_BIKE)
     && IsPlayerInsideOWEActiveDistance(owe))
        return TRUE;

    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    speciesId = OW_SPECIES(owe);
    viewDistance = OWE_GetViewDistanceFromSpecies(speciesId);
    viewWidth = OWE_GetViewWidthFromSpecies(speciesId);
    halfWidth = (viewWidth - 1) / 2;
    direction = owe->facingDirection;

    switch (direction)
    {
    case DIR_NORTH:
        if (!(player->currentCoords.y <= owe->currentCoords.y
         && owe->currentCoords.y - player->currentCoords.y <= viewDistance
         && player->currentCoords.x >= owe->currentCoords.x - halfWidth
         && player->currentCoords.x <= owe->currentCoords.x + halfWidth))
            return FALSE;
        break;

    case DIR_SOUTH:
        if (!(player->currentCoords.y >= owe->currentCoords.y
         && player->currentCoords.y - owe->currentCoords.y <= viewDistance
         && player->currentCoords.x >= owe->currentCoords.x - halfWidth
         && player->currentCoords.x <= owe->currentCoords.x + halfWidth))
            return FALSE;
        break;

    case DIR_EAST:
        if (!(player->currentCoords.x >= owe->currentCoords.x
         && player->currentCoords.x - owe->currentCoords.x <= viewDistance
         && player->currentCoords.y >= owe->currentCoords.y - halfWidth
         && player->currentCoords.y <= owe->currentCoords.y + halfWidth))
            return FALSE;
        break;

    case DIR_WEST:
        if (!(player->currentCoords.x <= owe->currentCoords.x
         && owe->currentCoords.x - player->currentCoords.x <= viewDistance
         && player->currentCoords.y >= owe->currentCoords.y - halfWidth
         && player->currentCoords.y <= owe->currentCoords.y + halfWidth))
            return FALSE;
        break;

    default:
        return FALSE;
    }

    return CanOWEReachPlayer(owe);
}

static bool32 CanOWEReachPlayer(struct ObjectEvent *owe)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    return (owe->currentElevation == player->currentElevation || owe->currentElevation == 0 || player->currentElevation == 0);
}

bool32 IsPlayerInsideOWEActiveDistance(struct ObjectEvent *owe)
{
    struct ObjectEvent *player;
    u32 distance;
    u16 speciesId;
    s32 absX;
    s32 absY;
    s32 diagonalDistance;

    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    distance = OWE_DEFAULT_CHASE_RANGE;
    speciesId = OW_SPECIES(owe);

    if (speciesId != SPECIES_NONE)
        distance = OWE_GetViewActiveDistanceFromSpecies(speciesId);

    absX = abs(player->currentCoords.x - owe->currentCoords.x);
    absY = abs(player->currentCoords.y - owe->currentCoords.y);

    if (absX > distance || absY > distance)
        return FALSE;

    diagonalDistance = (distance * 362) >> 8; // binary approximation of multiplying distance by sqrt(2)
    if ((absX + absY) > diagonalDistance)
        return FALSE;

    return TRUE;
}

bool32 IsOWENextToPlayer(struct ObjectEvent *owe)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    return IsOWENextToObject(owe, player);
}

static bool32 IsOWENextToObject(struct ObjectEvent *owe, struct ObjectEvent *object)
{
    if (object == NULL)
        return FALSE;

    if ((owe->currentCoords.x != object->currentCoords.x && owe->currentCoords.y != object->currentCoords.y) || (owe->currentCoords.x < object->currentCoords.x - 1 || owe->currentCoords.x > object->currentCoords.x + 1 || owe->currentCoords.y < object->currentCoords.y - 1 || owe->currentCoords.y > object->currentCoords.y + 1))
        return FALSE;

    return TRUE;
}

u8 DirectionOfOWEToPlayerFromCollision(struct ObjectEvent *owe)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

    switch (owe->movementDirection)
    {
    case DIR_NORTH:
    case DIR_SOUTH:
        if (player->currentCoords.x < owe->currentCoords.x)
            return DIR_WEST;
        else if (player->currentCoords.x == owe->currentCoords.x)
            return CheckOWEPathToPlayerFromCollision(owe, (Random() & 1)  ? DIR_EAST : DIR_WEST);
        else
            return DIR_EAST;
    case DIR_EAST:
    case DIR_WEST:
        if (player->currentCoords.y < owe->currentCoords.y)
            return DIR_NORTH;
        else if (player->currentCoords.y == owe->currentCoords.y)
            return CheckOWEPathToPlayerFromCollision(owe, (Random() & 1)  ? DIR_NORTH : DIR_SOUTH);
        else
            return DIR_SOUTH;
    }

    return owe->movementDirection;
}

u32 GetApproachingOWEDistanceToPlayer(struct ObjectEvent *owe, bool32 *equalDistances)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    s32 absX, absY;
    s32 distanceX = player->currentCoords.x - owe->currentCoords.x;
    s32 distanceY = player->currentCoords.y - owe->currentCoords.y;

    if (distanceX < 0)
        absX = distanceX * -1;
    else
        absX = distanceX;

    if (distanceY < 0)
        absY = distanceY * -1;
    else
        absY = distanceY;

    if (absY == absX)
        *equalDistances = TRUE;

    if (absY > absX)
        return absY;
    else
        return absX;
}

u32 GetOWEWalkMovementActionInDirectionWithSpeed(u8 direction, enum SpeedOWE speed)
{
    switch (speed)
    {
    case OWE_SPEED_SLOW:
        return GetWalkSlowMovementAction(direction);
    case OWE_SPEED_FAST:
        return GetWalkFastMovementAction(direction);
    case OWE_SPEED_FASTER:
        return GetWalkFasterMovementAction(direction);
    case OWE_SPEED_NORMAL:
    default:
        return GetWalkNormalMovementAction(direction);
    }
}

static u8 CheckOWEPathToPlayerFromCollision(struct ObjectEvent *owe, u8 newDirection)
{
    s16 x = owe->currentCoords.x;
    s16 y = owe->currentCoords.y;

    MoveCoords(newDirection, &x, &y);
    if (CheckRestrictedOWEMovementAtCoords(owe, x, y, newDirection, newDirection))
    {
        if (owe->movementType == MOVEMENT_TYPE_FLEE_PLAYER_OWE)
            return GetOppositeDirection(newDirection);

        MoveCoords(owe->movementDirection, &x, &y);
        if (CheckRestrictedOWEMovementAtCoords(owe, x, y, newDirection, owe->movementDirection))
            return newDirection;
    }

    x = owe->currentCoords.x;
    y = owe->currentCoords.y;
    MoveCoords(GetOppositeDirection(newDirection), &x, &y);
    if (CheckRestrictedOWEMovementAtCoords(owe, x, y, newDirection, newDirection))
    {
        if (owe->movementType == MOVEMENT_TYPE_FLEE_PLAYER_OWE)
            return newDirection;

        MoveCoords(owe->movementDirection, &x, &y);
        if (CheckRestrictedOWEMovementAtCoords(owe, x, y, newDirection, owe->movementDirection))
            return GetOppositeDirection(newDirection);
    }

    return owe->movementDirection;
}

#define tObjectId data[0]
void OWEApproachForBattle(struct ScriptContext *ctx)
{
    u32 localId;
    u32 objectEventId;
    struct ObjectEvent *owe;
    u32 taskId;

    localId = VarGet(ScriptReadHalfword(ctx));
    objectEventId = GetObjectEventIdByLocalId(localId);
    if (objectEventId >= OBJECT_EVENTS_COUNT)
        return;
    owe = &gObjectEvents[objectEventId];
    
    if (!WE_OWE_APPROACH_FOR_BATTLE || !IsOverworldWildEncounter(owe, OWE_ANY))
    {
        FreezeObjectEvent(owe);
        return;
    }
    
    taskId = CreateTask(Task_OWEApproachForBattle, 2);
    if (FindTaskIdByFunc(Task_OWEApproachForBattle) == TASK_NONE)
    {
        FreezeObjectEvent(owe);
        return;
    }
    
    ScriptContext_Stop();
    gTasks[taskId].tObjectId = objectEventId;
}

static void Task_OWEApproachForBattle(u8 taskId)
{
    struct ObjectEvent *OWE = &gObjectEvents[gTasks[taskId].tObjectId];
    struct ObjectEvent *player;
    struct ObjectEvent *followerMon;
    bool32 oweNextToPlayer;
    bool32 oweNextToFollowerMon;
    u16 speciesId;
    u8 direction;
    u32 movementActionId;
    s16 x, y;
    u32 collidingObject;

    // Let the mon continue to take steps until right next to the player.
    if (ObjectEventClearHeldMovementIfFinished(OWE))
    {
        player = &gObjectEvents[gPlayerAvatar.objectEventId];
        followerMon = GetFollowerObject();
        oweNextToPlayer = IsOWENextToPlayer(OWE);
        oweNextToFollowerMon = (followerMon != NULL) && IsOWENextToObject(OWE, followerMon);

        if (oweNextToPlayer || oweNextToFollowerMon)
        {
            if (oweNextToPlayer)
            {
                ObjectEventsTurnToEachOther(player, OWE);
            }
            else
            {
                ObjectEventTurn(player, DetermineObjectEventDirectionFromObject(followerMon, player));
                ObjectEventsTurnToEachOther(followerMon, OWE);
            }
            ScriptContext_Enable();
            DestroyTask(taskId);
            return;
        }

        speciesId = OW_SPECIES(OWE);
        direction = DetermineObjectEventDirectionFromObject(player, OWE);
    
        SetObjectEventDirection(OWE, direction);
        movementActionId = GetOWEWalkMovementActionInDirectionWithSpeed(OWE->movementDirection, OWE_GetActiveSpeedFromSpecies(speciesId));
        
        if (CheckRestrictedOWEMovement(OWE, OWE->movementDirection))
        {
            x = OWE->currentCoords.x;
            y = OWE->currentCoords.y;

            MoveCoords(OWE->movementDirection, &x, &y);
            collidingObject = GetObjectObjectCollidesWith(OWE, x, y, FALSE);

            if (followerMon != NULL && collidingObject == GetObjectEventIdByLocalId(followerMon->localId) && !followerMon->invisible)
            {
                ClearObjectEventMovement(followerMon, &gSprites[followerMon->spriteId]);
                gSprites[followerMon->spriteId].animCmdIndex = 0;
                ObjectEventSetHeldMovement(followerMon, MOVEMENT_ACTION_ENTER_POKEBALL);
            }
            else if (collidingObject == gPlayerAvatar.objectEventId)
            {
                movementActionId = GetFaceDirectionMovementAction(OWE->facingDirection);
            }
            else
            {
                direction = DirectionOfOWEToPlayerFromCollision(OWE);
                SetObjectEventDirection(OWE, direction);
                movementActionId = GetOWEWalkMovementActionInDirectionWithSpeed(OWE->movementDirection, OWE_GetActiveSpeedFromSpecies(speciesId));
            }
        }
        ObjectEventSetHeldMovement(OWE, movementActionId);
    }
    
}
#undef tObjectId

bool32 TryPlayAmbientCryOWE(void)
{
    struct ObjectEvent *owe = GetRandomOWEObjectEvent();
    if (owe == NULL)
        return FALSE;
    
    PlayOWECry(owe);
    return TRUE;
}

u32 GetNumberOfActiveOWEs(enum TypeOWE oweType)
{
    u32 numActive = 0;
    u32 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (IsOverworldWildEncounter(&gObjectEvents[i], oweType))
            numActive++;
    }
    return numActive;
}

const struct ObjectEventTemplate TryGetObjectEventTemplateForOWE(const struct ObjectEventTemplate *template)
{
    struct ObjectEventTemplate templateOWE;
    struct InfoOWE info;
    u16 speciesTemplate;
    u32 x, y;

    if (template->trainerType != TRAINER_TYPE_OW_WILD_ENCOUNTER
     || IS_LOCALID_GENERATED_OWE(template->localId))
        return *template;

    templateOWE = *template;
    memset(&info, 0, sizeof(info));
    info.category = OWE_CATEGORY_WILD;
    
    speciesTemplate = SanitizeSpeciesId(templateOWE.graphicsId & OBJ_EVENT_GFX_SPECIES_MASK);
    x = template->x;
    y = template->y;

    SetSpeciesInfoForOWE(&info, x, y);
    if (speciesTemplate)
        info.speciesId = speciesTemplate;

    if (!CheckValidOWESpecies(info.speciesId))
    {
        templateOWE.graphicsId = OBJ_EVENT_GFX_BOY_1;
        templateOWE.trainerType = TRAINER_TYPE_NONE;
        templateOWE.movementType = MOVEMENT_TYPE_NONE;
        return templateOWE;
    }

    info.isFemale = GetGenderFromSpeciesAndPersonality(info.speciesId, Random32()) == MON_FEMALE;

    if (templateOWE.movementType == MOVEMENT_TYPE_NONE)
        templateOWE.movementType = OWE_GetMovementTypeFromSpecies(info.speciesId);

    templateOWE.graphicsId = GetGraphicsIdForOWE(&info);
    
    return templateOWE;
}

struct SpritePalette GetOWESpawnDespawnAnimFldEffPalette(enum SpawnDespawnTypeOWE spawnAnim)
{
    struct SpritePalette palette = gSpritePalette_GeneralFieldEffect0;
    switch (spawnAnim)
    {
    case OWE_SPAWN_ANIM_GRASS:
    case OWE_SPAWN_ANIM_LONG_GRASS:
        palette = gSpritePalette_GeneralFieldEffect1;
        break;

    case OWE_SPAWN_ANIM_WATER:
    case OWE_SPAWN_ANIM_UNDERWATER:
    case OWE_SPAWN_ANIM_CAVE:
    case OWE_SPAWN_ANIM_SHINY:
    default:
        break;
    }

    return palette;
}

static bool32 CheckValidOWESpecies(u16 speciesId)
{
    if (speciesId == SPECIES_NONE)
        return FALSE;

    if (speciesId >= NUM_SPECIES)
        return FALSE;

    return TRUE;
}

bool32 TrySpawnFishingOWE(u8 rod, s16 x, s16 y)
{
    u16 species;
    u8 level;
    bool32 isShiny;
    bool32 isFemale;
    struct ObjectEventTemplate objectEventTemplate;
    u8 objectEventId;
    struct ObjectEvent *owe;
    u8 dummyId;

    if (!WE_OW_ENCOUNTERS)
        return FALSE;

    species = GenerateFishingWildMonEncounter(rod);
    if (species == SPECIES_NONE)
        return FALSE;

    level = GetMonData(&gEnemyParty[0], MON_DATA_LEVEL);
    isShiny = IsMonShiny(&gEnemyParty[0]);
    isFemale = (GetMonGender(&gEnemyParty[0]) == MON_FEMALE);

    if (!CheckValidOWESpecies(species))
        return FALSE;

    if (GetNumberOfActiveOWEs(OWE_GENERATED) >= OWE_SPAWNS_MAX)
        TryAndDespawnOldestGeneratedOWE_ToFreeObject(&dummyId);

    if (!CheckCanLoadOWE(species, isFemale, isShiny, x, y))
        return FALSE;

    memset(&objectEventTemplate, 0, sizeof(objectEventTemplate));
    objectEventTemplate.localId = LOCALID_OW_ENCOUNTER_END;
    objectEventTemplate.graphicsId = OBJ_EVENT_GFX_MON_BASE + species;
    objectEventTemplate.x = x - MAP_OFFSET;
    objectEventTemplate.y = y - MAP_OFFSET;
    objectEventTemplate.elevation = MapGridGetElevationAt(x, y);
    objectEventTemplate.movementType = MOVEMENT_TYPE_NONE;
    objectEventTemplate.trainerType = TRAINER_TYPE_OW_WILD_ENCOUNTER;

    objectEventId = GetObjectEventIdByLocalId(objectEventTemplate.localId);
    if (objectEventId < OBJECT_EVENTS_COUNT)
        RemoveObjectEvent(&gObjectEvents[objectEventId]);

    objectEventId = SpawnSpecialObjectEvent(&objectEventTemplate);
    if (objectEventId >= OBJECT_EVENTS_COUNT)
        return FALSE;

    owe = &gObjectEvents[objectEventId];
    owe->disableCoveringGroundEffects = TRUE;
    owe->shiny = isShiny;
    ObjectEventRefreshShinyPalette(owe);
    owe->sOverworldEncounterLevel = level;
    owe->sOverworldEncounterCategory = OWE_CATEGORY_WILD;

    ObjectEventTurn(owe, GetOppositeDirection(GetPlayerFacingDirection()));
    sBattleOWEObjectEventId = objectEventId;

    return TRUE;
}

bool32 TrySpawnRockSmashOWE(s16 x, s16 y)
{
    u16 species;
    u8 level;
    bool32 isShiny;
    bool32 isFemale;
    struct ObjectEventTemplate objectEventTemplate;
    u8 objectEventId;
    struct ObjectEvent *owe;
    u8 dummyId;

    if (!WE_OW_ENCOUNTERS)
        return FALSE;

    species = GetMonData(&gEnemyParty[0], MON_DATA_SPECIES);
    if (species == SPECIES_NONE)
        return FALSE;

    level = GetMonData(&gEnemyParty[0], MON_DATA_LEVEL);
    isShiny = IsMonShiny(&gEnemyParty[0]);
    isFemale = (GetMonGender(&gEnemyParty[0]) == MON_FEMALE);

    if (!CheckValidOWESpecies(species))
        return FALSE;

    if (GetNumberOfActiveOWEs(OWE_GENERATED) >= OWE_SPAWNS_MAX)
        TryAndDespawnOldestGeneratedOWE_ToFreeObject(&dummyId);

    if (!CheckCanLoadOWE(species, isFemale, isShiny, x, y))
        return FALSE;

    memset(&objectEventTemplate, 0, sizeof(objectEventTemplate));
    objectEventTemplate.localId = LOCALID_OW_ENCOUNTER_END;
    objectEventTemplate.graphicsId = OBJ_EVENT_GFX_MON_BASE + species;
    objectEventTemplate.x = x - MAP_OFFSET;
    objectEventTemplate.y = y - MAP_OFFSET;
    objectEventTemplate.elevation = MapGridGetElevationAt(x, y);
    objectEventTemplate.movementType = MOVEMENT_TYPE_NONE;
    objectEventTemplate.trainerType = TRAINER_TYPE_OW_WILD_ENCOUNTER;

    objectEventId = GetObjectEventIdByLocalId(objectEventTemplate.localId);
    if (objectEventId < OBJECT_EVENTS_COUNT)
        RemoveObjectEvent(&gObjectEvents[objectEventId]);

    objectEventId = SpawnSpecialObjectEvent(&objectEventTemplate);
    if (objectEventId >= OBJECT_EVENTS_COUNT)
        return FALSE;

    owe = &gObjectEvents[objectEventId];
    owe->disableCoveringGroundEffects = TRUE;
    owe->shiny = isShiny;
    ObjectEventRefreshShinyPalette(owe);
    owe->sOverworldEncounterLevel = level;
    owe->sOverworldEncounterCategory = OWE_CATEGORY_WILD;

    ObjectEventTurn(owe, GetOppositeDirection(GetPlayerFacingDirection()));
    sBattleOWEObjectEventId = objectEventId;

    return TRUE;
}

#undef sOverworldEncounterLevel
#undef sOverworldEncounterAge
#undef sOverworldEncounterCategory
