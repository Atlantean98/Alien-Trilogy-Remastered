#pragma once
#include "PlayerControl.h"

namespace trilogy::original {
inline constexpr std::uint16_t PerchedQueenDamageGate = 65535;
std::uint32_t nextGameRandom(PlayerControlMemory); // 0x100ec
void resetProjectilePool(PlayerControlMemory); // 0x2a2bc
std::uint32_t acquireProjectile(PlayerControlMemory); // 0x2a32c
void releaseProjectile(PlayerControlMemory,std::uint32_t); // 0x2a2a4
void activateProjectile(PlayerControlMemory,std::uint32_t); // 0x2a348
void unlinkProjectile(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x2a36c
void damageMapObject(PlayerControlMemory,std::uint8_t,std::uint16_t,const PlayerControlService&); // 0x3687c
std::uint32_t collideProjectileWithMap(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x29d38
// Kind 3 reads an uninitialized original stack word; callers testing that
// legacy branch must supply its prior contents explicitly.
std::uint32_t createPlayerShot(PlayerControlMemory,std::uint16_t speed,std::uint8_t kind,
    const PlayerControlService&,std::uint16_t priorStackYaw); // 0x2ae64
void advanceVectorBatch(PlayerControlMemory,std::uint32_t positions,std::uint32_t velocities,
    std::int32_t count,std::int32_t scale); // 0x46c18; count <= 65535
void damageEntity(PlayerControlMemory,std::uint32_t entity,std::uint16_t damage,
    std::uint16_t lethalFlags,const PlayerControlService&); // 0x30dc4
void emitEntityHitEffect(PlayerControlMemory,std::uint32_t entity,std::uint32_t projectile,
    const PlayerControlService&); // 0x2a860
std::uint32_t collideProjectileWithEntities(PlayerControlMemory,std::uint32_t,const PlayerControlService&,bool ignoreRegions=false); // 0x29f18
std::uint32_t collideProjectileWithPlayer(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x2a1e8
std::uint32_t collideBallisticWithMap(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x29bc8
void rewindProjectile(PlayerControlMemory,std::uint32_t); // 0x29b50
void updateProjectiles(PlayerControlMemory,std::uint32_t task,const PlayerControlService&,bool extendedPlayerRange=false,const std::function<void(unsigned,bool)>& observer={}); // 0x2d234
// Compose as withWeaponRuntime(withProjectileRuntime(external)). The undefined
// kind-3 construction branch stays external unless its stack input is supplied.
PlayerControlService withProjectileRuntime(const PlayerControlService& external);
}
