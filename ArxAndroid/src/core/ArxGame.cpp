/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code').

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "core/ArxGame.h"

#include <stddef.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>

#include "ai/PathFinderManager.h"
#include "ai/Paths.h"

#include "animation/Animation.h"
#include "animation/AnimationRender.h"

#include "cinematic/Cinematic.h"
#include "cinematic/CinematicController.h"

#include "core/Benchmark.h"
#include "core/Config.h"
#include "core/Core.h"
#include "core/FpsCounter.h"
#include "core/GameTime.h"
#include "core/Localisation.h"
#include "core/SaveGame.h"
#include "core/URLConstants.h"
#include "core/Version.h"

#include "game/Camera.h"
#include "game/Damage.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Inventory.h"
#include "game/Levels.h"
#include "game/Missile.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/effect/ParticleSystems.h"
#include "game/effect/Quake.h"
#include "game/magic/Precast.h"
#include "game/spell/FlyingEye.h"
#include "game/spell/Cheat.h"

#include "graphics/BaseGraphicsTypes.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/DrawDebug.h"
#include "graphics/GlobalFog.h"
#include "graphics/GraphicsTypes.h"
#include "graphics/Math.h"
#include "graphics/Raycast.h"
#include "graphics/Vertex.h"
#include "graphics/VertexBuffer.h"
#include "graphics/data/FTL.h"
#include "graphics/data/Mesh.h"
#include "graphics/data/TextureContainer.h"
#include "graphics/effects/Fade.h"
#include "graphics/effects/Fog.h"
#include "graphics/effects/LightFlare.h"
#include "graphics/font/Font.h"
#include "graphics/opengl/GLDebug.h"
#include "graphics/particle/ParticleEffects.h"
#include "graphics/particle/ParticleManager.h"
#include "graphics/particle/MagicFlare.h"
#include "graphics/particle/Spark.h"
#include "graphics/texture/TextureStage.h"

#include "gui/Console.h"
#include "gui/Cursor.h"
#include "gui/Dragging.h"
#include "gui/Hud.h"
#include "gui/Interface.h"
#include "gui/LoadLevelScreen.h"
#include "gui/Logo.h"
#include "gui/Menu.h"
#include "gui/MenuPublic.h"
#include "gui/MenuWidgets.h"
#include "gui/MiniMap.h"
#include "gui/Notification.h"
#include "gui/Speech.h"
#include "gui/Text.h"
#include "gui/TextManager.h"
#include "gui/debug/DebugHud.h"
#include "gui/debug/DebugHudAudio.h"
#include "gui/debug/DebugHudCulling.h"
#include "gui/hud/PlayerInventory.h"

#include "input/Input.h"
#include "input/Keyboard.h"

#include "math/Angle.h"
#include "math/Types.h"
#include "math/Rectangle.h"
#include "math/Vector.h"

#include "physics/Attractors.h"

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourceSetup.h"
#include "io/Screenshot.h"
#include "io/log/CriticalLogger.h"
#include "io/log/Logger.h"

#include "platform/Dialog.h"

#if defined(ARXVR_ANDROID_BUILD)
#include "vr/AndroidVrBridge.h"
#include "vr/AndroidVrInput.h"
#include "vr/VrDefenseRuntime.h"
#include "vr/VrHaptics.h"
#include "vr/VrInteractionSystem.h"
#include "vr/VrRuneRuntime.h"
#include "vr/VrSpellAim.h"
#include "game/magic/SpellRecognition.h"
#include "vr/VrWeaponContact.h"
#include "vr/VrWeaponSystem.h"
#endif
#include "platform/Platform.h"
#include "platform/Process.h"
#include "platform/ProgramOptions.h"
#include "platform/profiler/Profiler.h"
#include "platform/Time.h"
#include "platform/Thread.h"

#include "scene/ChangeLevel.h"
#include "scene/Interactive.h"
#include "scene/GameSound.h"
#include "scene/Light.h"
#include "scene/LoadLevel.h"
#include "scene/Object.h"
#include "scene/Scene.h"
#include "scene/Tiles.h"

#include "script/ScriptEvent.h"

#include "util/String.h"

#include "Configure.h"

#include "window/RenderWindow.h"

#if ARX_HAVE_SDL2
#include "window/SDL2Window.h"
#endif
#if ARX_HAVE_SDL1
#include "window/SDL1Window.h"
#endif

#ifdef ANDROID
#include "jni.h"
#endif

InfoPanels g_debugInfo = InfoPanelNone;

extern bool START_NEW_QUEST;
SavegameHandle LOADQUEST_SLOT = SavegameHandle(); // OH NO, ANOTHER GLOBAL! - TEMP PATCH TO CLEAN CODE FLOW
static fs::path g_saveToLoad;

static const PlatformDuration runeDrawPointInterval = 16ms; // ~60fps

#if defined(ARXVR_ANDROID_BUILD)
static Camera g_vrCenterCamera;
static bool g_haveVrCenterCamera = false;
static arxvr::VrRuneRuntime g_vrRuneRuntime;
static bool g_vrRuneFeedbackPending = false;
static std::array<Rune, MAX_SPELL_SYMBOLS> g_vrRuneSymbolsBefore{};
static size_t g_vrRuneFeedbackPointCount = 0;
static float g_vrRuneFeedbackPathLength = 0.f;
static std::uint64_t g_vrRuneFeedbackDurationUs = 0;

static arxvr::VrRuneVector3 vrRuneVector(const Vec3f & value) {
	return { value.x, value.y, value.z };
}

static arxvr::VrRunePlane vrRuneDrawingPlane() {
	arxvr::VrRunePlane plane;
	if(!g_haveVrCenterCamera) {
		return plane;
	}

	Vec3f forward = angleToVector(g_vrCenterCamera.angle);
	if(glm::length(forward) <= 0.001f) {
		return plane;
	}
	forward = glm::normalize(forward);
	// Arx world +Y points down. Using world-up and a normal facing the player
	// makes cross(up, normal) point toward physical controller-right at the
	// neutral camera pose. VrRuneSystem locks this basis on paint-down.
	const Vec3f worldUp(0.f, -1.f, 0.f);
	plane.origin = vrRuneVector(g_vrCenterCamera.m_pos + forward * 70.f);
	plane.normal = vrRuneVector(-forward);
	plane.up = vrRuneVector(worldUp);
	plane.valid = true;
	return plane;
}

static std::uint64_t vrRuneTimestampUs() {
	const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
	const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	return micros > 0 ? static_cast<std::uint64_t>(micros) : 1u;
}

static bool vrRuneScreenPoint(const arxvr::VrRunePoint2 & point, Vec2s & screenPoint) {
	const arxvr::VrRuneViewportPoint mapped = g_vrRuneRuntime.mapToViewport(
		point, g_size.width(), g_size.height());
	if(!mapped.valid) {
		return false;
	}
	screenPoint = Vec2s(Vec2f(mapped.x, mapped.y));
	return true;
}

enum class VrTraversalPhase {
	Inactive,
	ApproachStone,
	GripStone,
	MoveStone,
	ReleaseStone,
	WaitForBars,
	UseBars,
	CrossBentBars,
	ReachJailCorner,
	ReachJailHall,
	ApproachJailLever,
	UseJailLever,
	ApproachGuard,
	DefeatGuard,
	CrossJailGate,
	FollowJailCorridor,
	ApproachLever,
	UseLever,
	ApproachTrapdoor,
	WaitForPhysicalCrouch,
	BreakTrapdoor,
	WaitForPhysicalStand,
	FallThroughTrapdoor,
	AwaitLevelChange,
	Complete,
	Failed
};

static VrTraversalPhase g_vrTraversalPhase = VrTraversalPhase::Inactive;
static std::uint64_t g_vrTraversalPhaseFrame = 0;
static std::uint64_t g_vrTraversalTotalFrame = 0;
static AreaId g_vrTraversalStartArea;
static Vec3f g_vrTraversalStoneStart(0.f);
static Vec3f g_vrTraversalGripContact(0.f);
static Vec3f g_vrTraversalBarsExit(0.f);
static std::string g_vrTraversalGripQuery = "stone";
static EntityHandle g_vrTraversalGripTarget;
static bool g_vrTraversalGripOnly = false;
static bool g_vrTraversalChairCombat = false;
static unsigned g_vrHeldObjectHitCount = 0;
static unsigned g_vrTraversalHeldHitBaseline = 0;

static glm::quat vrTraversalObjectRenderRotation(const Entity & entity) {
	Anglef renderAngle = entity.angle;
	renderAngle.setYaw(270.f - renderAngle.getYaw());
	return glm::normalize(toQuaternion(renderAngle));
}

static Vec3f vrTraversalNaturalGripContact(const Entity & entity,
	                                       std::string_view query) {
	if(!entity.obj || entity.obj->vertexlist.empty()) {
		return entity.pos;
	}
	const glm::quat rotation = vrTraversalObjectRenderRotation(entity);
	EERIE_3D_BBOX localBounds;
	localBounds.reset();
	for(const EERIE_VERTEX & vertex : entity.obj->vertexlist) {
		localBounds.add(vertex.v);
	}
	if(!localBounds.valid()) {
		return entity.pos;
	}

	if(query == "bone") {
		const Vec3f size = localBounds.max - localBounds.min;
		size_t longAxis = 0;
		if(size[1] > size[longAxis]) { longAxis = 1; }
		if(size[2] > size[longAxis]) { longAxis = 2; }
		Vec3f localContact = (localBounds.min + localBounds.max) * 0.5f;
		// Touch a handle-like section near one end rather than the entity origin.
		// beginVrPhysicalDrag will refine this interior probe to the actual mesh.
		localContact[longAxis] = glm::mix(localBounds.min[longAxis],
		                                      localBounds.max[longAxis], 0.24f);
		return entity.pos + rotation * (localContact * entity.scale);
	}

	if(query == "chair") {
		EERIE_3D_BBOX worldBounds;
		worldBounds.reset();
		for(const EERIE_VERTEX & vertex : entity.obj->vertexlist) {
			worldBounds.add(entity.pos + rotation * (vertex.v * entity.scale));
		}
		const Vec3f worldSize = worldBounds.max - worldBounds.min;
		Vec3f best = entity.pos;
		float bestScore = std::numeric_limits<float>::max();
		const float targetY = glm::mix(worldBounds.min.y, worldBounds.max.y, 0.76f);
		const float minimumVerticalEdge = worldSize.y * 0.35f;
		const float maximumHorizontalDrift = std::max(worldSize.x, worldSize.z) * 0.28f;
		for(const EERIE_FACE & face : entity.obj->facelist) {
			for(size_t edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
				const EERIE_VERTEX & first = entity.obj->vertexlist[face.vid[edgeIndex]];
				const EERIE_VERTEX & second = entity.obj->vertexlist[
					face.vid[(edgeIndex + 1) % 3]];
				const Vec3f a = entity.pos + rotation * (first.v * entity.scale);
				const Vec3f b = entity.pos + rotation * (second.v * entity.scale);
				const float vertical = glm::abs(b.y - a.y);
				const float horizontal = glm::length(Vec2f(b.x - a.x, b.z - a.z));
				if(vertical < minimumVerticalEdge || horizontal > maximumHorizontalDrift) {
					continue;
				}
				const float t = (targetY - a.y) / (b.y - a.y);
				if(t < 0.f || t > 1.f) {
					continue;
				}
				const Vec3f point = glm::mix(a, b, t);
				const float nx = worldSize.x > 0.001f
				               ? (point.x - worldBounds.min.x) / worldSize.x : 0.5f;
				const float nz = worldSize.z > 0.001f
				               ? (point.z - worldBounds.min.z) / worldSize.z : 0.5f;
				const float outside = std::max(glm::abs(nx - 0.5f),
				                               glm::abs(nz - 0.5f)) * 2.f;
				const float score = (1.f - outside) + horizontal * 0.01f;
				if(score < bestScore) {
					bestScore = score;
					best = point;
				}
			}
		}
		return best;
	}

	return entity.pos;
}

static const char * vrTraversalPhaseName(VrTraversalPhase phase) {
	switch(phase) {
		case VrTraversalPhase::Inactive: return "inactive";
		case VrTraversalPhase::ApproachStone: return "approach_stone";
		case VrTraversalPhase::GripStone: return "grip_stone";
		case VrTraversalPhase::MoveStone: return "move_stone";
		case VrTraversalPhase::ReleaseStone: return "release_stone";
		case VrTraversalPhase::WaitForBars: return "wait_for_bars";
		case VrTraversalPhase::UseBars: return "use_bars";
		case VrTraversalPhase::CrossBentBars: return "cross_bent_bars";
		case VrTraversalPhase::ReachJailCorner: return "reach_jail_corner";
		case VrTraversalPhase::ReachJailHall: return "reach_jail_hall";
		case VrTraversalPhase::ApproachJailLever: return "approach_jail_lever";
		case VrTraversalPhase::UseJailLever: return "use_jail_lever";
		case VrTraversalPhase::ApproachGuard: return "approach_guard";
		case VrTraversalPhase::DefeatGuard: return "defeat_guard";
		case VrTraversalPhase::CrossJailGate: return "cross_jail_gate";
		case VrTraversalPhase::FollowJailCorridor: return "follow_jail_corridor";
		case VrTraversalPhase::ApproachLever: return "approach_lever";
		case VrTraversalPhase::UseLever: return "use_lever";
		case VrTraversalPhase::ApproachTrapdoor: return "approach_trapdoor";
		case VrTraversalPhase::WaitForPhysicalCrouch: return "wait_for_physical_crouch";
		case VrTraversalPhase::BreakTrapdoor: return "break_trapdoor";
		case VrTraversalPhase::WaitForPhysicalStand: return "wait_for_physical_stand";
		case VrTraversalPhase::FallThroughTrapdoor: return "fall_through_trapdoor";
		case VrTraversalPhase::AwaitLevelChange: return "await_level_change";
		case VrTraversalPhase::Complete: return "complete";
		case VrTraversalPhase::Failed: return "failed";
	}
	return "unknown";
}

static void setVrTraversalPhase(VrTraversalPhase phase) {
	g_vrTraversalPhase = phase;
	g_vrTraversalPhaseFrame = 0;
	LogInfo << "ARXVR_TRAVERSE phase=" << vrTraversalPhaseName(phase)
	        << " totalFrame=" << g_vrTraversalTotalFrame
	        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')';
}

static float vrHorizontalDistance(const Vec3f & a, const Vec3f & b) {
	const float x = a.x - b.x;
	const float z = a.z - b.z;
	return std::sqrt(x * x + z * z);
}

static bool driveVrPlayerTo(const Vec3f & target, float stopDistance) {
	const Vec3f delta = target - player.pos;
	const float distance = vrHorizontalDistance(target, player.pos);
	if(distance <= stopDistance) {
		arxvrSetDiagnosticMovement(0.f, 0.f);
		return true;
	}
	const float targetYaw = glm::degrees(std::atan2(-delta.x, delta.z));
	const float preConsumeYaw = targetYaw - arxvrHeadYawOffset();
	player.angle.setYaw(MAKEANGLE(preConsumeYaw));
	player.desiredangle.setYaw(player.angle.getYaw());
	// Keep the same native locomotion and collision path while compressing the
	// unattended test into the short low-light tracking window of the headset.
	arxvrSetDiagnosticMovement(0.f, 2.75f);
	return false;
}

// The rendered palms use the OpenXR aim-pose origin. Keep a forgiving but still
// arm-local contact volume around it: tiny floor props otherwise flicker behind
// the much larger portcullis bounds.
constexpr float kVrPhysicalGrabRadius = 125.f;
constexpr float kVrUseRayLength = 180.f;
constexpr float kVrAssistedGrabDistance = 190.f;
constexpr float kVrAimAssistRadius = 45.f;
constexpr float kVrPhysicalLeverRadius = 125.f;
constexpr float kVrFistContactRadius = 34.f;
EntityHandle g_vrInteractionTarget;
static bool g_vrPhysicalDragUsesRightHand = true;

static Entity * currentVrInteractionTarget() {
	return entities.get(g_vrInteractionTarget);
}

static float distanceToVrEntityBounds(const Entity & entity, const Vec3f & point) {
	if(entity.bbox3D.valid()) {
		const Vec3f nearest = glm::clamp(point, entity.bbox3D.min, entity.bbox3D.max);
		return glm::distance(point, nearest);
	}
	return glm::distance(point, entity.pos);
}

static bool isVrInteractionCandidate(const Entity & entity) {
	return &entity != entities.player()
	    && entity.obj
	    && entity.show == SHOW_FLAG_IN_SCENE
	    && (entity.gameFlags & GFLAG_INTERACTIVITY)
	    && (entity.gameFlags & GFLAG_ISINTREATZONE)
	    && !(entity.gameFlags & (GFLAG_INVISIBILITY | GFLAG_MEGAHIDE))
	    && !(entity.ioflags & (IO_CAMERA | IO_MARKER));
}

static bool isVrNearbyGrabbable(const Entity & entity) {
	return isVrInteractionCandidate(entity)
	    && !(entity.ioflags & (IO_NPC | IO_FIX))
	    && (entity.ioflags & (IO_ITEM | IO_MOVABLE))
	    && distanceToVrEntityBounds(entity, player.pos) <= kVrAssistedGrabDistance;
}

static bool isVrLever(const Entity & entity) {
	return isVrInteractionCandidate(entity)
	    && (entity.ioflags & IO_FIX)
	    && entity.idString().find("lever") != std::string::npos;
}

static Entity * findVrPhysicalInteractionTarget(const Vec3f & handPosition,
	                                             const Vec3f & handDirection) {
	Entity * nearestPickup = nullptr;
	float nearestDistance = kVrPhysicalGrabRadius;
	for(Entity & entity : entities.inScene()) {
		if(!isVrInteractionCandidate(entity)
		   || (entity.ioflags & (IO_NPC | IO_FIX))
		   || !(entity.ioflags & (IO_ITEM | IO_MOVABLE))) {
			continue;
		}
		const float distance = distanceToVrEntityBounds(entity, handPosition);
		if(distance <= nearestDistance) {
			nearestDistance = distance;
			nearestPickup = &entity;
		}
	}
	if(nearestPickup) {
		return nearestPickup;
	}

	// Levers are thin fixed meshes and are often mounted just behind bars. Give
	// them a real hand-sized proximity target before falling back to a ray.
	Entity * nearestLever = nullptr;
	float nearestLeverDistance = kVrPhysicalLeverRadius;
	for(Entity & entity : entities.inScene()) {
		if(!isVrLever(entity)) {
			continue;
		}
		const float distance = std::min(distanceToVrEntityBounds(entity, handPosition),
		                                glm::distance(entity.pos, handPosition));
		if(distance <= nearestLeverDistance) {
			nearestLeverDistance = distance;
			nearestLever = &entity;
		}
	}
	if(nearestLever) {
		return nearestLever;
	}

	// Small floor items are difficult to hit with a mathematically thin ray in
	// a headset. Select the closest nearby grabbable whose bounds enter a narrow
	// 28 cm corridor around the controller ray. The player-distance gate keeps
	// this a reachable hand interaction rather than long-range telekinesis.
	Entity * aimedPickup = nullptr;
	float bestAimScore = kVrAimAssistRadius + kVrUseRayLength * 0.01f;
	for(Entity & entity : entities.inScene()) {
		if(!isVrNearbyGrabbable(entity)) {
			continue;
		}
		const Vec3f center = entity.bbox3D.valid()
		                   ? (entity.bbox3D.min + entity.bbox3D.max) * 0.5f
		                   : entity.pos;
		const float alongRay = glm::dot(center - handPosition, handDirection);
		if(alongRay < 0.f || alongRay > kVrUseRayLength) {
			continue;
		}
		const Vec3f rayPoint = handPosition + handDirection * alongRay;
		const float offRay = distanceToVrEntityBounds(entity, rayPoint);
		const float score = offRay + alongRay * 0.01f;
		if(offRay <= kVrAimAssistRadius && score < bestAimScore) {
			bestAimScore = score;
			aimedPickup = &entity;
		}
	}
	if(aimedPickup) {
		return aimedPickup;
	}

	// Continue the exact ray for fixed interactables and NPCs.
	Vec3f rayEnd = handPosition + handDirection * kVrUseRayLength;
	const PolyType ignored = POLY_HIDE | POLY_TRANS | POLY_NODRAW | POLY_NOCOL;
	if(RaycastResult sceneHit = raycastScene(handPosition, rayEnd, ignored,
	                                         RaycastIgnorePlayer)) {
		rayEnd = sceneHit.pos;
	}
	if(EntityRaycastResult hit = raycastEntities(handPosition, rayEnd, ignored,
	                                             RaycastIgnorePlayer)) {
		if(isVrInteractionCandidate(*hit.entity)
		   && ((hit.entity->ioflags & (IO_NPC | IO_FIX | IO_MOVABLE))
		       || isVrNearbyGrabbable(*hit.entity))) {
			return hit.entity;
		}
	}
	return nullptr;
}

static arxvr::VrInteractionSystem g_vrInteractions;
static arxvr::VrWeaponSystem g_vrWeaponSystem;

static std::uint64_t vrImpactTimestampUs() {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

static arxvr::VrWeaponClass vrWeaponClassForArx(WeaponType type) {
	switch(type) {
		case WEAPON_DAGGER: return arxvr::VrWeaponClass::Dagger;
		case WEAPON_1H: return arxvr::VrWeaponClass::OneHanded;
		case WEAPON_2H: return arxvr::VrWeaponClass::TwoHanded;
		case WEAPON_BOW: return arxvr::VrWeaponClass::Bow;
		case WEAPON_BARE: return arxvr::VrWeaponClass::Unknown;
	}
	return arxvr::VrWeaponClass::Unknown;
}

static arxvr::VrImpactVector3 vrImpactVector(const Vec3f & value) {
	return { value.x, value.y, value.z };
}

static Vec3f vrArxVector(const arxvr::VrImpactVector3 & value) {
	return Vec3f(value.x, value.y, value.z);
}

static float distanceBetweenVrBounds(const EERIE_3D_BBOX & a,
                                     const EERIE_3D_BBOX & b) {
	const float dx = std::max({ 0.f, a.min.x - b.max.x, b.min.x - a.max.x });
	const float dy = std::max({ 0.f, a.min.y - b.max.y, b.min.y - a.max.y });
	const float dz = std::max({ 0.f, a.min.z - b.max.z, b.min.z - a.max.z });
	return glm::length(Vec3f(dx, dy, dz));
}

static Entity * findVrHeldObjectTarget(const Entity & heldObject,
                                       const EERIE_3D_BBOX & sweptBounds) {
	Entity * nearest = nullptr;
	float nearestDistance = 22.f;
	for(Entity & entity : entities.inScene()) {
		if(!isVrInteractionCandidate(entity) || !(entity.ioflags & IO_NPC)
		   || !entity._npcdata || entity._npcdata->lifePool.current <= 0.f) {
			continue;
		}
		const float distance = sweptBounds.valid() && entity.bbox3D.valid()
		                     ? distanceBetweenVrBounds(sweptBounds, entity.bbox3D)
		                     : distanceToVrEntityBounds(entity, heldObject.pos);
		if(distance <= nearestDistance) {
			nearestDistance = distance;
			nearest = &entity;
		}
	}
	return nearest;
}

static void updateVrHeldObjectCombat(bool rightHand, bool haveHand,
                                     const Vec3f & handPosition, bool gripHeld,
                                     bool allowHit, std::uint64_t timestampUs) {
	const arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;
	Entity * heldObject = getVrPhysicalDragEntity();

	arxvr::VrImpactSample sample;
	sample.motion.timestampUs = timestampUs;
	sample.source = heldObject ? arxvr::VrImpactSource::HeldObject
	                           : arxvr::VrImpactSource::None;
	sample.sourceToken = heldObject
	                   ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heldObject))
	                   : 0;
	sample.gestureActive = heldObject && gripHeld && allowHit;
	sample.trackingValid = haveHand;
	if(haveHand) {
		sample.motion.x = handPosition.x;
		sample.motion.y = handPosition.y;
		sample.motion.z = handPosition.z;
	}
	const arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);
	if(status != arxvr::VrImpactGateStatus::Qualified || !heldObject
	   || !gripHeld || !allowHit || !haveHand) {
		return;
	}

	const Vec3f velocity = getVrPhysicalDragVelocity();
	const float objectSpeed = glm::length(velocity);
	EERIE_3D_BBOX sweptBounds = heldObject->bbox3D;
	if(sweptBounds.valid()) {
		const float frameSeconds = std::max(1.f, toMsf(g_platformTime.lastFrameDuration()))
		                         * 0.001f;
		const Vec3f frameMovement = velocity * frameSeconds;
		sweptBounds.add(heldObject->bbox3D.min + frameMovement);
		sweptBounds.add(heldObject->bbox3D.max + frameMovement);
	}
	Entity * target = findVrHeldObjectTarget(*heldObject, sweptBounds);
	if(!target) {
		return;
	}

	arxvr::VrImpactEvent impact;
	if(!g_vrInteractions.consumeImpact(hand, impact)) {
		return;
	}
	const float impactSpeed = std::max(objectSpeed, impact.metrics.terminalSpeed);
	const Vec3f objectSize = heldObject->bbox3D.valid()
	                       ? heldObject->bbox3D.max - heldObject->bbox3D.min
	                       : Vec3f(30.f);
	const float sizeBonus = glm::clamp(glm::length(objectSize) * 0.015f, 0.f, 5.f);
	const float impactDamage = glm::clamp(2.f + impactSpeed * 0.03f + sizeBonus, 3.f, 18.f);
	const Vec3f objectCenter = sweptBounds.valid()
	                         ? (sweptBounds.min + sweptBounds.max) * 0.5f
	                         : heldObject->pos;
	Vec3f hitPosition = target->bbox3D.valid()
	                  ? glm::clamp(objectCenter, target->bbox3D.min, target->bbox3D.max)
	                  : target->pos;
	SendIOScriptEvent(entities.player(), target, SM_AGGRESSION);
	const float damage = damageNpc(*target, impactDamage, entities.player(), nullptr,
	                               DAMAGE_TYPE_GENERIC, &hitPosition);
	const std::string_view impactMaterial = !heldObject->weaponmaterial.empty()
	                                      ? std::string_view(heldObject->weaponmaterial)
	                                      : std::string_view("wood");
	ARX_SOUND_PlayCollision("flesh", impactMaterial, 1.f, 1.f,
	                        hitPosition, entities.player());
	arxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
	                VrHapticEvent::ImpactHeavy,
	                glm::clamp(impactSpeed / 300.f, 0.45f, 1.f));
	++g_vrHeldObjectHitCount;
	ARX_PLAYER_Remove_Invisibility();
	LogInfo << "ArxVR held-object hit: weapon=" << heldObject->idString()
	        << " target=" << target->idString() << " speed=" << impactSpeed
	        << " peak=" << impact.metrics.peakSpeed
	        << " path=" << impact.metrics.pathLength
	        << " consistency=" << impact.metrics.directionalConsistency
	        << " damage=" << damage << " life=" << target->_npcdata->lifePool.current;
}

static Entity * findVrEquippedWeaponTarget(const arxvr::VrWeaponSegment & segment,
                                             Vec3f & hitPosition,
                                             float & contactT) {
	Entity * nearest = nullptr;
	float nearestT = 2.f;
	for(Entity & entity : entities.inScene()) {
		if(!isVrInteractionCandidate(entity) || !(entity.ioflags & IO_NPC)
		   || !entity._npcdata || entity._npcdata->lifePool.current <= 0.f
		   || !entity.bbox3D.valid()) {
			continue;
		}

		float entryT = 0.f;
		if(!arxvr::vrWeaponSegmentIntersectsAabb(
		       segment, vrImpactVector(entity.bbox3D.min),
		       vrImpactVector(entity.bbox3D.max), entryT)
		   || entryT >= nearestT) {
			continue;
		}

		nearestT = entryT;
		nearest = &entity;
		const Vec3f contact = vrArxVector(arxvr::vrWeaponSegmentPoint(segment, entryT));
		hitPosition = glm::clamp(contact, entity.bbox3D.min, entity.bbox3D.max);
	}

	contactT = nearest ? nearestT : 0.f;
	return nearest;
}

static void updateVrEquippedWeaponCombat(bool rightHand, bool haveHand,
                                         const Vec3f & handPosition,
                                         const Vec3f & handDirection,
                                         const Vec3f & handUp,
                                         bool haveSecondaryHand,
                                         const Vec3f & secondaryHandPosition,
                                         bool secondaryGripPressed,
                                         Entity & weapon,
                                         const arxvr::VrWeaponProfile & profile,
                                         bool allowHit, std::uint64_t timestampUs) {
	const arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;

	arxvr::VrWeaponTrackingSample tracking;
	tracking.weaponToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&weapon));
	tracking.primaryPosition = vrImpactVector(handPosition);
	tracking.primaryForward = vrImpactVector(handDirection);
	tracking.primaryUp = vrImpactVector(handUp);
	tracking.primaryValid = haveHand;
	tracking.secondaryPosition = vrImpactVector(secondaryHandPosition);
	tracking.secondaryValid = haveSecondaryHand;
	tracking.secondaryGripPressed = secondaryGripPressed;

	const arxvr::VrWeaponPose weaponPose = g_vrWeaponSystem.update(profile, tracking);
	const arxvr::VrWeaponSegment segment =
		g_vrWeaponSystem.buildContactSegment(profile, weaponPose);

	// Keep the physical player weapon available to the incoming-melee
	// defense adapter even when this frame is not itself an outgoing hit.
	if(segment.valid && weaponPose.valid && allowHit) {
		arxvr::vrDefenseRuntime().publishDefenderWeapon(
			tracking.weaponToken, segment, timestampUs);
	} else {
		arxvr::vrDefenseRuntime().clearDefenderWeapon();
	}

	arxvr::VrImpactSample sample;
	sample.motion.timestampUs = timestampUs;
	if(segment.valid) {
		sample.motion.x = segment.end.x;
		sample.motion.y = segment.end.y;
		sample.motion.z = segment.end.z;
	}
	sample.source = arxvr::VrImpactSource::EquippedWeapon;
	sample.sourceToken = tracking.weaponToken;
	sample.profileOverride = profile.strike;
	sample.effectiveMass = profile.effectiveMass;
	sample.gestureActive = weaponPose.valid && segment.valid && allowHit;
	sample.trackingValid = weaponPose.valid && segment.valid;
	sample.useProfileOverride = true;

	const arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);
	if(status != arxvr::VrImpactGateStatus::Qualified || !sample.gestureActive) {
		return;
	}

	Vec3f hitPosition(0.f);
	float contactT = 0.f;
	Entity * target = findVrEquippedWeaponTarget(segment, hitPosition, contactT);
	if(!target) {
		return;
	}

	arxvr::VrImpactEvent impact;
	if(!g_vrInteractions.consumeImpact(hand, impact)) {
		return;
	}
	impact.attackerToken = static_cast<std::uint64_t>(
		reinterpret_cast<std::uintptr_t>(entities.player()));
	impact.targetToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));

	const float impactSpeed = std::max(impact.metrics.terminalSpeed,
	                                   impact.metrics.averageSpeed);
	const float speedScale = glm::clamp(
		impactSpeed / std::max(profile.strike.minPeakSpeed * 2.f, 1.f), 0.25f, 1.f);
	const float massScale = glm::clamp(std::sqrt(impact.effectiveMass), 0.75f, 1.35f);
	const float tipBlend = glm::clamp((contactT - 0.45f) / 0.55f, 0.f, 1.f);
	const float tipScale = glm::mix(1.f, profile.tipDamageMultiplier, tipBlend);
	const float strength = glm::clamp(speedScale * massScale * tipScale, 0.25f, 1.f);

	const float damage = ARX_EQUIPMENT_ComputeDamages(
		entities.player(), target, strength, &hitPosition);
	ARX_DAMAGES_DurabilityCheck(&weapon, g_framedelay * 0.006f);
	arxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
	                VrHapticEvent::ImpactHeavy,
	                glm::clamp(strength, 0.45f, 1.f));
	ARX_PLAYER_Remove_Invisibility();
	LogInfo << "ArxVR equipped-weapon hit: weapon=" << weapon.idString()
	        << " target=" << target->idString() << " speed=" << impactSpeed
	        << " mass=" << impact.effectiveMass << " contactT=" << contactT
	        << " twoHanded=" << weaponPose.twoHanded
	        << " path=" << impact.metrics.pathLength
	        << " consistency=" << impact.metrics.directionalConsistency
	        << " strength=" << strength << " damage=" << damage
	        << " life=" << target->_npcdata->lifePool.current;
}

static Entity * findVrFistTarget(const Vec3f & handPosition) {
	Entity * nearest = nullptr;
	float nearestDistance = kVrFistContactRadius;
	for(Entity & entity : entities.inScene()) {
		if(!isVrInteractionCandidate(entity) || !(entity.ioflags & IO_NPC)
		   || !entity._npcdata || entity._npcdata->lifePool.current <= 0.f) {
			continue;
		}
		const float distance = distanceToVrEntityBounds(entity, handPosition);
		if(distance <= nearestDistance) {
			nearestDistance = distance;
			nearest = &entity;
		}
	}
	return nearest;
}

static void updateVrFistCombat(bool rightHand, bool haveHand,
                            const Vec3f & handPosition, bool fistClosed,
                            bool allowHit, std::uint64_t timestampUs) {
	const arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;
	arxvr::VrImpactSample sample;
	sample.motion.timestampUs = timestampUs;
	sample.source = arxvr::VrImpactSource::Fist;
	sample.sourceToken = 0;
	sample.gestureActive = haveHand && allowHit && fistClosed;
	sample.trackingValid = haveHand;
	if(haveHand) {
		sample.motion.x = handPosition.x;
		sample.motion.y = handPosition.y;
		sample.motion.z = handPosition.z;
	}
	const arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);
	if(status != arxvr::VrImpactGateStatus::Qualified
	   || !haveHand || !allowHit || !fistClosed) {
		return;
	}

	Entity * target = findVrFistTarget(handPosition);
	if(!target) {
		return;
	}

	arxvr::VrImpactEvent impact;
	if(!g_vrInteractions.consumeImpact(hand, impact)) {
		return;
	}
	const float speed = impact.metrics.terminalSpeed;
	const float strength = glm::clamp((speed - 55.f) / 170.f, 0.35f, 1.f);
	Vec3f hitPosition = handPosition;
	float damage = ARX_EQUIPMENT_ComputeDamages(entities.player(), target,
	                                           strength, &hitPosition);
	// A physical contact should not turn into a silent dice-roll miss. Keep the
	// normal armor/critical path above, but guarantee a small bare-hand impact.
	if(damage <= 0.f && target->_npcdata->lifePool.current > 0.f) {
		const float minimumDamage = std::max(1.f, player.m_miscFull.damages * 0.25f);
		damage = damageNpc(*target, minimumDamage, entities.player(), nullptr,
		                   DAMAGE_TYPE_GENERIC, &hitPosition);
	}
	arxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
	                VrHapticEvent::ImpactLight,
	                glm::clamp(speed / 240.f, 0.35f, 1.f));
	ARX_PLAYER_Remove_Invisibility();
	LogInfo << "ArxVR fist hit: hand=" << (rightHand ? "right" : "left")
	        << " target=" << target->idString() << " speed=" << speed
	        << " peak=" << impact.metrics.peakSpeed
	        << " path=" << impact.metrics.pathLength
	        << " consistency=" << impact.metrics.directionalConsistency
	        << " strength=" << strength << " damage=" << damage
	        << " life=" << target->_npcdata->lifePool.current;
}

static void updateVrPhysicalInteraction() {
	// Keep a captured trigger suppressed through its release frame. Clear it on
	// the following idle frame so a scripted use cannot also become a weapon hit.
	if(arxvrDirectInteractionTriggerCaptured()
	   && !arxvrButtonPressed(ARXVR_BUTTON_RIGHT_TRIGGER)
	   && !arxvrButtonNowReleased(ARXVR_BUTTON_RIGHT_TRIGGER)) {
		arxvrSetDirectInteractionTriggerCaptured(false);
	}

	if(!g_haveVrCenterCamera || ARXmenu.mode() != Mode_InGame) {
		g_vrInteractions.resetSession();
		g_vrWeaponSystem.reset();
		arxvr::vrDefenseRuntime().resetSession();
		g_vrInteractionTarget = EntityHandle();
		arxvrSetDirectInteractionTriggerCaptured(false);
		return;
	}

	Vec3f rightHandPosition(0.f);
	Vec3f rightHandDirection(0.f);
	Vec3f leftHandPosition(0.f);
	Vec3f leftHandDirection(0.f);
	glm::quat rightHandOrientation(1.f, 0.f, 0.f, 0.f);
	glm::quat leftHandOrientation(1.f, 0.f, 0.f, 0.f);
	const bool haveRightHand = arxvrGetHandWorldPose(true, g_vrCenterCamera,
	                                                player.angle.getYaw(),
	                                                rightHandPosition,
	                                                rightHandDirection,
	                                                rightHandOrientation);
	const bool haveLeftHand = arxvrGetHandWorldPose(false, g_vrCenterCamera,
	                                               player.angle.getYaw(),
	                                               leftHandPosition,
	                                               leftHandDirection,
	                                               leftHandOrientation);
	const std::uint64_t impactTimestampUs = vrImpactTimestampUs();
	const bool physicalDragActive = isVrPhysicalDragActive();
	const bool rightDragging = physicalDragActive && g_vrPhysicalDragUsesRightHand;
	const bool leftDragging = physicalDragActive && !g_vrPhysicalDragUsesRightHand;

	// Publish the actual off-hand controller pose only while a shield is
	// equipped and the hand is available for defense. The runtime applies a
	// short freshness window, so tracking loss fails closed without leaving
	// a frozen shield collider active in front of the player.
	Entity * equippedShield = entities.get(player.equiped[EQUIP_SLOT_SHIELD]);
	if(equippedShield && haveLeftHand && !leftDragging && !BLOCK_PLAYER_CONTROLS) {
		arxvr::VrShieldPose shieldPose;
		shieldPose.center = vrImpactVector(leftHandPosition);
		shieldPose.normal = vrImpactVector(leftHandDirection);
		shieldPose.up = vrImpactVector(leftHandOrientation * Vec3f(0.f, 1.f, 0.f));
		shieldPose.valid = true;
		arxvr::vrDefenseRuntime().publishShield(
			static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(equippedShield)),
			arxvr::VrShieldProfile{}, shieldPose, impactTimestampUs);
	} else {
		arxvr::vrDefenseRuntime().clearShield();
	}

	// Each physical hand owns exactly one semantic impact source per frame.
	// An equipped melee weapon takes ownership of the dominant (right) hand
	// while it is readied in combat mode; physical drag still has priority.
	Entity * equippedWeapon = entities.get(player.equiped[EQUIP_SLOT_WEAPON]);
	const arxvr::VrWeaponProfile equippedProfile = arxvr::vrDefaultWeaponProfile(
		vrWeaponClassForArx(ARX_EQUIPMENT_GetPlayerWeaponType()));
	const bool equippedMeleeActive = equippedWeapon && equippedProfile.physicalMelee
	                              && (player.Interface & INTER_COMBATMODE);
	if(!equippedMeleeActive || rightDragging) {
		g_vrWeaponSystem.reset();
	}
	if(!rightDragging) {
		if(equippedMeleeActive) {
			const Vec3f rightHandUp = haveRightHand
			                        ? rightHandOrientation * Vec3f(0.f, 1.f, 0.f)
			                        : Vec3f(0.f, 1.f, 0.f);
			const bool secondaryAvailable = haveLeftHand && !leftDragging;
			updateVrEquippedWeaponCombat(
				true, haveRightHand, rightHandPosition, rightHandDirection, rightHandUp,
				secondaryAvailable, leftHandPosition,
				secondaryAvailable && arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
				*equippedWeapon, equippedProfile, !BLOCK_PLAYER_CONTROLS,
				impactTimestampUs);
		} else {
			updateVrFistCombat(true, haveRightHand, rightHandPosition,
			                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),
			                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
		}
	}
	const bool secondaryWeaponGripActive = equippedMeleeActive
	                                    && g_vrWeaponSystem.twoHanded();
	if(secondaryWeaponGripActive) {
		// The off hand is owned by the two-hand weapon constraint while latched.
		// Discard any previously banked fist trajectory without clearing the
		// hand's cooldown/retraction state, so releasing a brief two-hand grip
		// cannot resume a stale fist swing in the same physical squeeze.
		g_vrInteractions.resetGesture(arxvr::VrHand::Left);
	} else if(!leftDragging) {
		updateVrFistCombat(false, haveLeftHand, leftHandPosition,
		                  arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
		                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
	}

	if(physicalDragActive) {
		const bool haveDragHand = g_vrPhysicalDragUsesRightHand ? haveRightHand : haveLeftHand;
		const Vec3f & dragPosition = g_vrPhysicalDragUsesRightHand
		                           ? rightHandPosition : leftHandPosition;
		const Vec3f & dragDirection = g_vrPhysicalDragUsesRightHand
		                            ? rightHandDirection : leftHandDirection;
		const glm::quat & dragOrientation = g_vrPhysicalDragUsesRightHand
		                                  ? rightHandOrientation : leftHandOrientation;
		const std::uint32_t gripButton = g_vrPhysicalDragUsesRightHand
		                               ? ARXVR_BUTTON_RIGHT_SQUEEZE
		                               : ARXVR_BUTTON_LEFT_SQUEEZE;
		const bool gripHeld = arxvrButtonPressed(gripButton);
		// A temporarily lost controller pose must not feed uninitialised
		// coordinates into the held object. Freeze it for that frame, while
		// explicitly failing the impact gate closed until tracking recovers.
		if(haveDragHand) {
			updateVrPhysicalDragPose(dragPosition, dragDirection, dragOrientation,
			                         gripHeld);
		}
		updateVrHeldObjectCombat(g_vrPhysicalDragUsesRightHand, haveDragHand,
		                         dragPosition, gripHeld, !BLOCK_PLAYER_CONTROLS,
		                         impactTimestampUs);
		return;
	}

	if(!haveRightHand && !haveLeftHand) {
		g_vrInteractionTarget = EntityHandle();
		return;
	}

	Entity * rightTarget = haveRightHand
	                    ? findVrPhysicalInteractionTarget(rightHandPosition,
	                                                      rightHandDirection)
	                    : nullptr;
	Entity * leftTarget = haveLeftHand
	                   ? findVrPhysicalInteractionTarget(leftHandPosition,
	                                                     leftHandDirection)
	                   : nullptr;
	// Hold on to a recently focused pickup while its palm is still close. This
	// prevents the floor stone from alternating with the surrounding bars.
	if(Entity * previousTarget = currentVrInteractionTarget();
	   previousTarget && isVrNearbyGrabbable(*previousTarget)) {
		const float rightDistance = haveRightHand
		                          ? distanceToVrEntityBounds(*previousTarget,
		                                                     rightHandPosition)
		                          : std::numeric_limits<float>::max();
		const float leftDistance = haveLeftHand
		                         ? distanceToVrEntityBounds(*previousTarget,
		                                                    leftHandPosition)
		                         : std::numeric_limits<float>::max();
		if(rightDistance <= kVrPhysicalGrabRadius + 35.f
		   && (!rightTarget || !isVrNearbyGrabbable(*rightTarget))) {
			rightTarget = previousTarget;
		}
		if(leftDistance <= kVrPhysicalGrabRadius + 35.f
		   && (!leftTarget || !isVrNearbyGrabbable(*leftTarget))) {
			leftTarget = previousTarget;
		}
	}
	const bool rightTriggerPressed = arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_TRIGGER);
	const bool rightGripPressed = arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_SQUEEZE);
	const bool leftTriggerPressed = arxvrButtonNowPressed(ARXVR_BUTTON_LEFT_TRIGGER);
	const bool leftGripPressed = arxvrButtonNowPressed(ARXVR_BUTTON_LEFT_SQUEEZE);

	bool useRightHand = true;
	Entity * target = rightTarget;
	if((leftGripPressed || leftTriggerPressed) && leftTarget) {
		useRightHand = false;
		target = leftTarget;
	} else if((rightGripPressed || rightTriggerPressed) && rightTarget) {
		useRightHand = true;
		target = rightTarget;
	} else if(!target && leftTarget) {
		useRightHand = false;
		target = leftTarget;
	}
	bool usedPointerFallback = false;
	const bool leftControlSelected = (leftGripPressed || leftTriggerPressed) && leftTarget;
	if(haveRightHand && !leftControlSelected) {
		// Large fixed interactables (doors, portcullises, levers) can have sparse
		// collision geometry that the short physical ray misses. A nearby pickup
		// may also be selected by the visible controller pointer: this keeps the
		// stone on the floor reachable without allowing room-wide telekinesis.
		Vec2s pointer;
		if(arxvrGetGameplayPointerScreenPoint(g_size, g_playerCamera.fov(), pointer)) {
			Entity * pointerTarget = InterClick(pointer);
			if(pointerTarget && isVrNearbyGrabbable(*pointerTarget)) {
				// Aimed nearby pickups take precedence over a large fixed mesh such
				// as the surrounding portcullis bars.
				useRightHand = true;
				target = pointerTarget;
				usedPointerFallback = true;
			} else if(!target && pointerTarget
			          && isVrInteractionCandidate(*pointerTarget)
			          && !(pointerTarget->ioflags & IO_ITEM)
			          && (pointerTarget->ioflags & (IO_NPC | IO_FIX | IO_MOVABLE))) {
				target = pointerTarget;
				usedPointerFallback = true;
			}
		}
	}
	const Vec3f & selectedHandPosition = useRightHand
	                                  ? rightHandPosition : leftHandPosition;
	g_vrInteractionTarget = target ? target->index() : EntityHandle();

	static EntityHandle lastLoggedTarget;
	if(g_vrInteractionTarget != lastLoggedTarget) {
		LogInfo << "ArxVR interaction focus: target="
		        << (target ? target->idString() : "none") << " source="
		        << (usedPointerFallback ? "pointer" : "physical");
		lastLoggedTarget = g_vrInteractionTarget;
	}

	if(BLOCK_PLAYER_CONTROLS) {
		return;
	}

	const bool triggerPressed = useRightHand ? rightTriggerPressed : leftTriggerPressed;
	const bool gripPressed = useRightHand ? rightGripPressed : leftGripPressed;
	if(!triggerPressed && !gripPressed) {
		return;
	}
	if(!target) {
		LogInfo << "ArxVR interaction miss: control="
		        << (gripPressed ? "grip" : "trigger") << " hand="
		        << (useRightHand ? "right" : "left") << " position=("
		        << selectedHandPosition.x << ", " << selectedHandPosition.y << ", "
		        << selectedHandPosition.z << ')';
		return;
	}
	if(triggerPressed && useRightHand) {
		arxvrSetDirectInteractionTriggerCaptured(true);
	}

	LogInfo << "ArxVR direct interaction: control="
	        << (gripPressed ? "grip" : "trigger") << " hand=("
	        << selectedHandPosition.x << ", " << selectedHandPosition.y << ", "
	        << selectedHandPosition.z << ") target=" << target->idString()
	        << " source=" << (usedPointerFallback ? "pointer" : "physical");

	if(isVrLever(*target) && gripPressed) {
		const ScriptResult result = SendIOScriptEvent(entities.player(), target, SM_ACTION);
		LogInfo << "ArxVR physical lever pull: hand="
		        << (useRightHand ? "right" : "left") << " target="
		        << target->idString() << " scriptResult=" << result;
		arxvrEmitHaptic(useRightHand ? VrHapticHand::Right : VrHapticHand::Left,
		                VrHapticEvent::Lever);
		ARX_PLAYER_Remove_Invisibility();
		return;
	}

	if(!(target->ioflags & (IO_NPC | IO_FIX))
	   && (target->ioflags & (IO_ITEM | IO_MOVABLE)) && gripPressed) {
		SendIOScriptEvent(entities.player(), target, SM_CLICKED);
		g_vrPhysicalDragUsesRightHand = useRightHand;
		const Vec3f & selectedHandDirection = useRightHand
		                                  ? rightHandDirection : leftHandDirection;
		const glm::quat & selectedHandOrientation = useRightHand
		                                         ? rightHandOrientation : leftHandOrientation;
		beginVrPhysicalDrag(target, selectedHandPosition, selectedHandDirection,
		                    selectedHandOrientation, useRightHand);
		ARX_PLAYER_Remove_Invisibility();
		return;
	}

	if((target->ioflags & IO_NPC) && target->_npcdata
	   && target->_npcdata->lifePool.current > 0.f) {
		// Grip closes the fist; damage is generated only by real tracked hand
		// velocity and physical contact above. The index trigger remains talk/use.
		if(triggerPressed) {
			SendIOScriptEvent(entities.player(), target, SM_CHAT);
		}
	} else if(target->script.valid) {
		SendIOScriptEvent(entities.player(), target, SM_ACTION);
	} else {
		SendIOScriptEvent(entities.player(), target, SM_CLICKED);
	}
	ARX_PLAYER_Remove_Invisibility();
}

static void renderVrInteractionPrompt() {
	Entity * target = currentVrInteractionTarget();
	if(BLOCK_PLAYER_CONTROLS || ARXmenu.mode() != Mode_InGame
	   || (player.Interface & INTER_PLAYERBOOK)) {
		return;
	}

	if(target) {
		const bool grabbable = !(target->ioflags & (IO_NPC | IO_FIX))
		                    && (target->ioflags & (IO_ITEM | IO_MOVABLE));
		std::string prompt;
		if(grabbable) {
			prompt = isVrPhysicalDragActive()
			       ? "[ХВАТ] ДЕРЖАТЬ / ВЗМАХ: УДАР / ОТПУСТИТЬ: БРОСИТЬ"
			       : "[ХВАТ] ВЗЯТЬ И ДВИГАТЬ";
		} else if(isVrLever(*target)) {
			prompt = "[ХВАТ] ПОТЯНУТЬ РЫЧАГ";
		} else if(target->ioflags & IO_NPC) {
			prompt = "[СЖАТЬ ХВАТ] УДАРИТЬ КУЛАКОМ";
		} else {
			prompt = "[КУРОК] ВЗАИМОДЕЙСТВОВАТЬ";
		}
		if(!target->locname.empty()) {
			std::string name(getLocalised(target->locname));
			if(!name.empty() && name.size() <= 48) {
				prompt += " — ";
				prompt += name;
			}
		}
		const Vec2f position(float(g_size.center().x), float(g_size.height()) * 0.60f);
		UNICODE_ARXDrawTextCenter(hFontInGame, position + Vec2f(2.f, 2.f), prompt, Color::black);
		UNICODE_ARXDrawTextCenter(hFontInGame, position, prompt, Color(255, 220, 96));
	}

	const std::string movementHint = "ЛЕВЫЙ СТИК: ИДТИ / НАЖАТЬ: БЕГ / ПРИСЕД: ТЕЛОМ";
	const Vec2f hintPosition(float(g_size.center().x), float(g_size.height()) * 0.72f);
	UNICODE_ARXDrawTextCenter(hFontInGame, hintPosition + Vec2f(2.f, 2.f),
	                         movementHint, Color::black);
	UNICODE_ARXDrawTextCenter(hFontInGame, hintPosition, movementHint,
	                         Color(180, 210, 220));
}
#endif

extern CircularVertexBuffer<TexturedVertex> * pDynamicVertexBuffer_TLVERTEX; // VB using TLVERTEX format.
extern CircularVertexBuffer<SMY_VERTEX3> * pDynamicVertexBuffer;

bool EXTERNALVIEW = false;
bool SHOW_INGAME_MINIMAP = true;
#ifdef ANDROID
bool showOnScreenControls = true;
#endif
bool ARX_FLARES_Block = true;

Vec3f PUSH_PLAYER_FORCE;

Vec3f LASTCAMPOS;
Anglef LASTCAMANGLE;

static Vec3f clipThirdPersonCamera(const Vec3f & pivot, const Vec3f & desired) {

	Vec3f toCamera = desired - pivot;
	float maxDistance = glm::distance(pivot, desired);
	if(maxDistance <= 0.f) {
		return desired;
	}

	PolyType ignored = POLY_HIDE | POLY_TRANS | POLY_NODRAW | POLY_NOCOL | POLY_WATER;
	if(RaycastResult hit = raycastScene(pivot, desired, ignored, RaycastIgnorePlayer)) {
		float hitDistance = glm::distance(pivot, hit.pos);
		float cameraDistance = glm::clamp(hitDistance - config.camera.thirdPersonCollisionPadding, 20.f, maxDistance);
		return pivot + toCamera * (cameraDistance / maxDistance);
	}

	return desired;
}

static void prepareThirdPersonCamera(const Vec3f & pivot, Vec3f & targetPos, Anglef & targetAngle) {

	Vec3f forward = angleToVector(player.angle);
	Vec3f right = angleToVectorXZ(player.angle.getYaw() - 90.f);

	Vec3f desired = pivot - forward * config.camera.thirdPersonDistance + right * config.camera.thirdPersonSideOffset + Vec3f(0.f, config.camera.thirdPersonHeight, 0.f);

	targetPos = clipThirdPersonCamera(pivot, desired);
	Vec3f lookAt = pivot + angleToVector(player.angle) * config.camera.thirdPersonFocusDistance;
	targetAngle = Camera::getLookAtAngle(targetPos, lookAt);
}

static void updatePlayerCameraPivot(Entity * io) {

	g_playerCamera.angle = player.angle;

	if(VertexId viewVertex = io->obj->fastaccess.view_attach) {

		g_playerCameraStablePos = g_playerCamera.m_pos = io->obj->vertexWorldPositions[viewVertex].v;

		if(VertexGroupId viewGroup = getGroupForVertex(io->obj, viewVertex)) {
			AnimLayer animlayer[MAX_ANIM_LAYERS];
			for(size_t i = 0; i < MAX_ANIM_LAYERS; i++) {
				animlayer[i] = io->animlayer[i];
				if(animlayer[i].flags & EA_LOOP) {
					animlayer[i].ctime = AnimationDuration(0);
					animlayer[i].lastframe = -1;
					animlayer[i].currentInterpolation = 0.f;
					animlayer[i].currentFrame = 0;
					animlayer[i].flags |= EA_PAUSED;
				}
			}
			Skeleton skeleton = *io->obj->m_skeleton;
			animateSkeleton(io, animlayer, skeleton);
			g_playerCameraStablePos = skeleton.bones[viewGroup].anim(io->obj->vertexlocal[viewVertex]);
		}

		if(!config.video.viewBobbing) {
			g_playerCamera.m_pos = g_playerCameraStablePos;
		}

		Vec3f vect(g_playerCamera.m_pos.x - player.pos.x, 0.f, g_playerCamera.m_pos.z - player.pos.z);
		float len = ffsqrt(arx::length2(vect));
		if(len > 46.f) {
			vect *= 46.f / len;
			g_playerCamera.m_pos.x = player.pos.x + vect.x;
			g_playerCamera.m_pos.z = player.pos.z + vect.z;
		}

	} else {
		g_playerCameraStablePos = g_playerCamera.m_pos = player.basePosition();
	}

}

// ArxGame constructor. Sets attributes for the app.
ArxGame::ArxGame()
	: m_wasResized(false)
	, m_gameInitialized(false)
	, m_frameStart(0)
	, m_frameDelta(0)
{
}

#ifdef ANDROID
extern "C" {
__attribute__((used)) __attribute__((visibility("default")))
bool needToShowScreenControls() {
    return showOnScreenControls;
}

__attribute__((used)) __attribute__((visibility("default")))
bool needToInvokeMouseButtonsEvents(){
    return !PLAYER_MOUSELOOK_ON;
}
__attribute__((used)) __attribute__((visibility("default")))
bool needToReInitGameControllers (){
    return false;
}
}
#endif

bool ArxGame::initialize() {
	return initializeVr(nullptr);
}

bool ArxGame::initializeVr(RenderWindow * externalWindow) {
	
	bool init;
	
	init = initConfig();
	if(!init) {
		LogCritical << "Failed to initialize the config subsystem";
		return false;
	}
	
	init = externalWindow ? initWindow(externalWindow) : initWindow();
	if(!init) {
		return false;
	}
	
	init = initGameData();
	if(!init) {
		return false;
	}
	
	init = initInput();
	if(!init) {
		return false;
	}
	
	init = initSound();
	if(!init) {
		return false;
	}
	
	init = initLocalisation();
	if(!init) {
		LogCritical << "Failed to initialize the localisation subsystem";
		return false;
	}
	
	init = initGame();
	if(!init) {
		LogCritical << "Failed to initialize game";
		return false;
	}
	
	return true;
}

void ArxGame::frameVr(int eye, float verticalFovRadians) {
#if defined(ARXVR_ANDROID_BUILD)
	// Eye value 2 asks for a full game frame rendered from the right eye.
	// The OpenXR host uses it when alternating cached stereo eyes at 72 Hz.
	if(eye == 1) {
		renderVrEye(eye, verticalFovRadians);
		return;
	}
	arxvrSetRenderingEye(eye == 2 ? 1 : eye);
	if(verticalFovRadians > 0.f && verticalFovRadians < glm::pi<float>()) {
		g_playerCamera.setFov(verticalFovRadians);
	}
#endif
	doFrame();
}

void ArxGame::startVrTraversalDiagnostic(std::string_view gripTarget) {
#if defined(ARXVR_ANDROID_BUILD)
	g_vrTraversalStartArea = g_currentArea;
	g_vrTraversalTotalFrame = 0;
	g_vrTraversalStoneStart = Vec3f(0.f);
	g_vrTraversalGripContact = Vec3f(0.f);
	g_vrTraversalBarsExit = Vec3f(0.f);
	g_vrTraversalGripQuery = util::toLowercase(gripTarget);
	g_vrTraversalChairCombat = g_vrTraversalGripQuery == "chairhit"
	                          || g_vrTraversalGripQuery == "chair_combat";
	if(g_vrTraversalChairCombat) {
		g_vrTraversalGripQuery = "chair";
	}
	if(g_vrTraversalGripQuery.empty() || g_vrTraversalGripQuery == "1"
	   || g_vrTraversalGripQuery == "route") {
		g_vrTraversalGripQuery = "stone";
	}
	g_vrTraversalGripOnly = g_vrTraversalGripQuery != "stone";
	g_vrTraversalGripTarget = EntityHandle();
	g_vrTraversalHeldHitBaseline = g_vrHeldObjectHitCount;
	arxvrClearDiagnosticControls();
	setVrTraversalPhase(VrTraversalPhase::ApproachStone);
	LogInfo << "ARXVR_TRAVERSE start area=" << g_vrTraversalStartArea
	        << " gripQuery=" << g_vrTraversalGripQuery
	        << " gripOnly=" << g_vrTraversalGripOnly
	        << " chairCombat=" << g_vrTraversalChairCombat;
#endif
}

void ArxGame::updateVrTraversalDiagnostic() {
#if defined(ARXVR_ANDROID_BUILD)
	if(g_vrTraversalPhase == VrTraversalPhase::Inactive
	   || g_vrTraversalPhase == VrTraversalPhase::Complete
	   || g_vrTraversalPhase == VrTraversalPhase::Failed) {
		return;
	}

	++g_vrTraversalTotalFrame;
	++g_vrTraversalPhaseFrame;
	arxvrClearDiagnosticControls();

	if(!entities.player() || ARXmenu.mode() != Mode_InGame) {
		return;
	}
	if(g_currentArea != g_vrTraversalStartArea) {
		LogInfo << "ARXVR_TRAVERSE level_change from=" << g_vrTraversalStartArea
		        << " to=" << g_currentArea << " player=(" << player.pos.x << ','
		        << player.pos.y << ',' << player.pos.z << ')';
		setVrTraversalPhase(VrTraversalPhase::Complete);
		LogInfo << "ARXVR_TRAVERSE complete next_location_loaded=1";
		return;
	}
	if(g_vrTraversalTotalFrame > 12000) {
		setVrTraversalPhase(VrTraversalPhase::Failed);
		LogError << "ARXVR_TRAVERSE failed reason=global_timeout";
		return;
	}
	if(BLOCK_PLAYER_CONTROLS || !cinematicIsStopped() || cinematicBorder.isActive()) {
		return;
	}

	Entity * const stone = entities.getById("jail_stone_0003");
	if(g_vrTraversalGripOnly && !entities.get(g_vrTraversalGripTarget)) {
		Entity * best = nullptr;
		float bestScore = std::numeric_limits<float>::max();
		for(Entity & candidate : entities.inScene()) {
			if(!candidate.obj || &candidate == entities.player()
			   || (candidate.ioflags & (IO_NPC | IO_FIX | IO_CAMERA | IO_MARKER))) {
				continue;
			}
			const std::string id = util::toLowercase(candidate.idString());
			const std::string className = util::toLowercase(candidate.className());
			const std::string locname = util::toLowercase(candidate.locname);
			const std::string localised = candidate.locname.empty()
			                            ? std::string()
			                            : util::toLowercase(getLocalised(candidate.locname));
			const bool matches = id.find(g_vrTraversalGripQuery) != std::string::npos
			                  || className.find(g_vrTraversalGripQuery) != std::string::npos
			                  || locname.find(g_vrTraversalGripQuery) != std::string::npos
			                  || localised.find(g_vrTraversalGripQuery) != std::string::npos;
			if(!matches) {
				continue;
			}
			EERIE_3D_BBOX localBounds;
			for(const EERIE_VERTEX & vertex : candidate.obj->vertexlist) {
				localBounds.add(vertex.v);
			}
			const Vec3f size = localBounds.valid()
			                 ? (localBounds.max - localBounds.min) * glm::abs(candidate.scale)
			                 : Vec3f(0.f);
			const float score = glm::distance(candidate.pos, player.pos)
			                  + ((candidate.ioflags & (IO_ITEM | IO_MOVABLE)) ? 0.f : 10000.f);
			LogInfo << "ARXVR_GRIP_CANDIDATE query=" << g_vrTraversalGripQuery
			        << " id=" << candidate.idString() << " class=" << candidate.className()
			        << " locname=" << candidate.locname << " localised=" << localised
			        << " flags=" << candidate.ioflags << " size=(" << size.x << ','
			        << size.y << ',' << size.z << ") distance="
			        << glm::distance(candidate.pos, player.pos);
			if(score < bestScore) {
				bestScore = score;
				best = &candidate;
			}
		}
		if(best) {
			g_vrTraversalGripTarget = best->index();
			best->show = SHOW_FLAG_IN_SCENE;
			best->gameFlags |= GFLAG_INTERACTIVITY | GFLAG_ISINTREATZONE;
			const Vec3f forward = angleToVector(g_vrCenterCamera.angle);
			const Vec3f testPosition = g_vrCenterCamera.m_pos + forward * 78.f
			                         + Vec3f(0.f, 18.f, 0.f);
			ARX_INTERACTIVE_Teleport(best, testPosition, true);
			if(best->obj->pbox) {
				best->obj->pbox->active = 0;
			}
			g_vrTraversalGripContact = vrTraversalNaturalGripContact(
				*best, g_vrTraversalGripQuery);
			LogInfo << "ARXVR_GRIP_SELECTED query=" << g_vrTraversalGripQuery
			        << " id=" << best->idString() << " testPosition=("
			        << best->pos.x << ',' << best->pos.y << ',' << best->pos.z
			        << ") gripContact=(" << g_vrTraversalGripContact.x << ','
			        << g_vrTraversalGripContact.y << ',' << g_vrTraversalGripContact.z
			        << ')';
		}
	}
	Entity * const gripTarget = g_vrTraversalGripOnly
	                         ? entities.get(g_vrTraversalGripTarget) : stone;
	Entity * const bars = entities.getById("untwisted_portcullis_2_0003");
	Entity * const jailHall = entities.getById("marker_0111");
	Entity * const jailLever = entities.getById("lever_0011");
	Entity * const jailGate = entities.getById("porticullis_0013");
	Entity * const guard = entities.getById("goblin_base_0006");
	Entity * const jailExit = entities.getById("marker_0258");
	Entity * const jailCorridor = entities.getById("marker_0150");
	Entity * const lever = entities.getById("lever_0012");
	Entity * const trapdoor = entities.getById("jail_wood_grid_0001");
	Entity * const trapExit = entities.getById("marker_0225");
	Entity * const hero = entities.player();

	auto failMissing = [](const char * id) {
		setVrTraversalPhase(VrTraversalPhase::Failed);
		LogError << "ARXVR_TRAVERSE failed reason=missing_entity id=" << id;
	};

	switch(g_vrTraversalPhase) {
		case VrTraversalPhase::ApproachStone: {
			if(!gripTarget) {
				if(g_vrTraversalGripOnly && g_vrTraversalPhaseFrame < 90) { return; }
				failMissing(g_vrTraversalGripQuery.c_str()); return;
			}
			if(g_vrTraversalGripOnly || driveVrPlayerTo(gripTarget->pos, 85.f)) {
				g_vrTraversalStoneStart = gripTarget->pos;
				LogInfo << "ARXVR_TRAVERSE reached=" << gripTarget->idString() << " distance="
				        << vrHorizontalDistance(player.pos, gripTarget->pos);
				setVrTraversalPhase(VrTraversalPhase::GripStone);
			}
			break;
		}
		case VrTraversalPhase::GripStone: {
			if(!gripTarget) { failMissing(g_vrTraversalGripQuery.c_str()); return; }
			const Vec3f contact = g_vrTraversalGripOnly
			                    ? g_vrTraversalGripContact : gripTarget->pos;
			const Vec3f direction = glm::normalize(contact - player.pos);
			// Human-style discovery: first hover with no button so focus/highlight
			// must resolve, try the index trigger, release it, then try lower grip.
			// Never call beginVrPhysicalDrag from this test: a pass must come through
			// the same button edge and spatial target resolver used by the headset.
			const bool tryTrigger = g_vrTraversalPhaseFrame >= 3
			                     && g_vrTraversalPhaseFrame <= 5;
			const bool tryGrip = g_vrTraversalPhaseFrame >= 9;
			arxvrSetDiagnosticHand(true, contact, direction, tryGrip, tryTrigger);
			if(g_vrTraversalPhaseFrame == 1) {
				LogInfo << "ARXVR_TRAVERSE discovery=hover target="
				        << gripTarget->idString();
			}
			if(g_vrTraversalPhaseFrame == 3) {
				LogInfo << "ARXVR_TRAVERSE discovery=try_index_trigger target="
				        << gripTarget->idString();
			}
			if(g_vrTraversalPhaseFrame == 9) {
				LogInfo << "ARXVR_TRAVERSE discovery=try_lower_grip target="
				        << gripTarget->idString();
			}
			if(isVrPhysicalDragActive()) {
				LogInfo << "ARXVR_TRAVERSE interaction=physical_lower_grip target="
				        << gripTarget->idString() << " acquired=1";
				setVrTraversalPhase(VrTraversalPhase::MoveStone);
			} else if(g_vrTraversalPhaseFrame >= 24) {
				setVrTraversalPhase(VrTraversalPhase::Failed);
				LogError << "ARXVR_TRAVERSE failed reason=physical_grip_not_acquired";
			}
			break;
		}
		case VrTraversalPhase::MoveStone: {
			if(!gripTarget) { failMissing(g_vrTraversalGripQuery.c_str()); return; }
			if(g_vrTraversalChairCombat) {
				if(!guard || !guard->_npcdata) {
					failMissing("goblin_base_0006"); return;
				}
				const Vec3f forward = angleToVector(g_vrCenterCamera.angle);
				const Vec3f right = angleToVectorXZ(
					g_vrCenterCamera.angle.getYaw() - 90.f);
				const Vec3f hitPoint = g_vrCenterCamera.m_pos + forward * 82.f
				                     + Vec3f(0.f, 12.f, 0.f);
				const Vec3f windup = hitPoint - right * 75.f;
				const Vec3f followThrough = hitPoint + right * 75.f;
				Vec3f hand = windup;
				if(g_vrTraversalPhaseFrame <= 40) {
					const float t = std::min(float(g_vrTraversalPhaseFrame) / 40.f, 1.f);
					hand = glm::mix(g_vrTraversalGripContact, windup, t);
				} else if(g_vrTraversalPhaseFrame <= 56) {
					const float t = float(g_vrTraversalPhaseFrame - 40) / 16.f;
					hand = glm::mix(windup, followThrough, t);
				} else {
					hand = followThrough;
				}
				// Relocate the real live guard immediately before the swing. The test
				// never calls damageNpc: the normal held-object sweep, speed threshold,
				// NPC bounds contact and combat event must register the hit.
				if(g_vrTraversalPhaseFrame == 38) {
					guard->show = SHOW_FLAG_IN_SCENE;
					guard->gameFlags |= GFLAG_INTERACTIVITY | GFLAG_ISINTREATZONE;
					ARX_INTERACTIVE_Teleport(guard, hitPoint, true);
					LogInfo << "ARXVR_CHAIR_COMBAT armed chair=" << gripTarget->idString()
					        << " goblin=" << guard->idString() << " life="
					        << guard->_npcdata->lifePool.current << " hitPoint=("
					        << hitPoint.x << ',' << hitPoint.y << ',' << hitPoint.z << ')';
				}
				arxvrSetDiagnosticHand(true, hand, forward, true);
				if(g_vrHeldObjectHitCount > g_vrTraversalHeldHitBaseline) {
					LogInfo << "ARXVR_CHAIR_COMBAT verified=1 hits="
					        << (g_vrHeldObjectHitCount - g_vrTraversalHeldHitBaseline)
					        << " goblinLife=" << guard->_npcdata->lifePool.current;
					setVrTraversalPhase(VrTraversalPhase::ReleaseStone);
				} else if(g_vrTraversalPhaseFrame >= 120) {
					setVrTraversalPhase(VrTraversalPhase::Failed);
					LogError << "ARXVR_CHAIR_COMBAT verified=0 reason=no_physical_hit";
				}
				break;
			}
			// Move the gripping hand from the floor into a reproducible, human arm
			// length pose inside the current camera frustum, then hold it there long
			// enough for an ADB binocular screenshot. Both the native hand mesh and
			// the Arx object must now consume this exact game-world pose.
			const Vec3f forward = angleToVector(g_vrCenterCamera.angle);
			const Vec3f right = angleToVectorXZ(g_vrCenterCamera.angle.getYaw() - 90.f);
			const Vec3f extendedHand = g_vrCenterCamera.m_pos + forward * 70.f
			                         + right * 20.f + Vec3f(0.f, 15.f, 0.f);
			const float t = std::min(float(g_vrTraversalPhaseFrame) / 45.f, 1.f);
			const Vec3f handStart = g_vrTraversalGripOnly
			                      ? g_vrTraversalGripContact : g_vrTraversalStoneStart;
			const Vec3f hand = glm::mix(handStart, extendedHand, t);
			// A tool held exactly along the camera ray is visually foreshortened and
			// cannot prove that it exits the fist like a club. Angle only the
			// unattended bone pose; real controller orientation remains untouched.
			const Vec3f heldDirection = g_vrTraversalGripQuery == "bone"
			                          ? glm::normalize(forward + right * 0.28f
			                                           + Vec3f(0.f, -0.14f, 0.f))
			                          : forward;
			arxvrSetDiagnosticHand(true, hand, heldDirection, true);
			if(g_vrTraversalPhaseFrame == 60 || g_vrTraversalPhaseFrame == 300
			   || g_vrTraversalPhaseFrame == 600 || g_vrTraversalPhaseFrame == 840) {
				LogInfo << "ARXVR_TRAVERSE held_pose=extended hand=(" << hand.x << ','
				        << hand.y << ',' << hand.z << ") object=(" << gripTarget->pos.x << ','
				        << gripTarget->pos.y << ',' << gripTarget->pos.z << ") stable_frames="
				        << (g_vrTraversalPhaseFrame - 45);
			}
			if(g_vrTraversalPhaseFrame >= 900) {
				setVrTraversalPhase(VrTraversalPhase::ReleaseStone);
			}
			break;
		}
		case VrTraversalPhase::ReleaseStone: {
			if(!gripTarget) { failMissing(g_vrTraversalGripQuery.c_str()); return; }
			const Vec3f hand = g_vrTraversalPhaseFrame >= 8
			                 ? gripTarget->pos
			                 : g_vrTraversalStoneStart + Vec3f(-120.f, -35.f, 70.f);
			arxvrSetDiagnosticHand(true, hand, Vec3f(-1.f, 0.f, 0.f), false,
			                       g_vrTraversalPhaseFrame >= 8
			                       && g_vrTraversalPhaseFrame <= 10);
			if(g_vrTraversalPhaseFrame >= 14) {
				const float moved = glm::distance(gripTarget->pos, g_vrTraversalStoneStart);
				LogInfo << "ARXVR_TRAVERSE released=" << gripTarget->idString()
				        << " displacement=" << moved;
				if(moved < 35.f) {
					setVrTraversalPhase(VrTraversalPhase::Failed);
					LogError << "ARXVR_TRAVERSE failed reason=stone_not_moved";
					return;
				}
				setVrTraversalPhase(g_vrTraversalGripOnly
				                    ? VrTraversalPhase::Complete
				                    : VrTraversalPhase::WaitForBars);
			}
			break;
		}
		case VrTraversalPhase::WaitForBars:
			if(g_vrTraversalPhaseFrame >= 4) {
				setVrTraversalPhase(VrTraversalPhase::UseBars);
			}
			break;
		case VrTraversalPhase::UseBars: {
			if(!bars) { failMissing("untwisted_portcullis_2_0003"); return; }
			Vec3f controllerDirection = bars->pos - player.pos;
			if(glm::length(controllerDirection) < 0.01f) {
				controllerDirection = Vec3f(0.f, 0.f, 1.f);
			} else {
				controllerDirection = glm::normalize(controllerDirection);
			}
			const Vec3f controllerPosition = bars->pos - controllerDirection * 60.f;
			arxvrSetDiagnosticHand(true, controllerPosition, controllerDirection,
			                       false, g_vrTraversalPhaseFrame <= 3);
			if(g_vrTraversalPhaseFrame == 1) {
				const float distance = vrHorizontalDistance(player.pos, bars->pos);
				// This is the exact original story event emitted by jail_stone_0003
				// after it leaves its initial position. Re-emitting it makes the
				// automated run independent of script-timer scheduling while still
				// exercising the shipped bar script and animation.
				SendIOScriptEvent(hero, bars, SM_CUSTOM, "STONE");
				Vec3f approach = player.pos - bars->pos;
				approach.y = 0.f;
				if(glm::length(approach) < 0.01f) {
					approach = Vec3f(1.f, 0.f, 0.f);
				} else {
					approach = glm::normalize(approach);
				}
				g_vrTraversalBarsExit = bars->pos - approach * 230.f;
				LogInfo << "ARXVR_TRAVERSE story_event=STONE interaction=trigger target=" << bars->idString()
				        << " distance=" << distance;
			}
			if(g_vrTraversalPhaseFrame >= 12) {
				setVrTraversalPhase(VrTraversalPhase::CrossBentBars);
			}
			break;
		}
		case VrTraversalPhase::CrossBentBars:
			if(driveVrPlayerTo(g_vrTraversalBarsExit, 35.f)) {
				LogInfo << "ARXVR_TRAVERSE crossed=untwisted_portcullis_2_0003 target=("
				        << g_vrTraversalBarsExit.x << ',' << g_vrTraversalBarsExit.y << ','
				        << g_vrTraversalBarsExit.z << ')';
				setVrTraversalPhase(VrTraversalPhase::ReachJailCorner);
			} else if(g_vrTraversalPhaseFrame % 180 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=cross_bars distance="
				        << vrHorizontalDistance(player.pos, g_vrTraversalBarsExit)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')';
			}
			if(g_vrTraversalPhaseFrame > 900) {
				setVrTraversalPhase(VrTraversalPhase::Failed);
				LogError << "ARXVR_TRAVERSE failed reason=bars_collision_not_cleared";
			}
			break;
		case VrTraversalPhase::ReachJailCorner: {
			if(!jailHall) { failMissing("marker_0111"); return; }
			// The cell exit opens into an L-shaped passage. Follow the same corner
			// used by the opening camera instead of trying to walk through its wall.
			// Camera entities are script-relative in this level, so anchor the DLF
			// corner offset to marker_0111, whose runtime position is stable.
			const Vec3f corner = jailHall->pos + Vec3f(-566.f, 0.f, 14.f);
			if(driveVrPlayerTo(corner, 70.f)) {
				LogInfo << "ARXVR_TRAVERSE waypoint=jail_corner_0032 jail_corner=1";
				setVrTraversalPhase(VrTraversalPhase::ReachJailHall);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=reach_jail_corner distance="
				        << vrHorizontalDistance(player.pos, corner)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " waypoint=(" << corner.x << ',' << corner.y << ',' << corner.z << ')';
			}
			break;
		}
		case VrTraversalPhase::ReachJailHall: {
			if(!jailHall) { failMissing("marker_0111"); return; }
			if(driveVrPlayerTo(jailHall->pos, 80.f)) {
				LogInfo << "ARXVR_TRAVERSE waypoint=marker_0111 jail_hall=1";
				setVrTraversalPhase(VrTraversalPhase::ApproachJailLever);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=reach_jail_hall distance="
				        << vrHorizontalDistance(player.pos, jailHall->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::ApproachJailLever: {
			if(!jailLever) { failMissing("lever_0011"); return; }
			// This lever is deliberately mounted on the far side of the cell bars;
			// the original first-person route activates it through the opening.
			// Stop at the closest collision point, then dispatch its normal ACTION.
			if(driveVrPlayerTo(jailLever->pos, 220.f)) {
				LogInfo << "ARXVR_TRAVERSE reached=lever_0011 distance="
				        << vrHorizontalDistance(player.pos, jailLever->pos);
				setVrTraversalPhase(VrTraversalPhase::UseJailLever);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=approach_jail_lever distance="
				        << vrHorizontalDistance(player.pos, jailLever->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " lever=(" << jailLever->pos.x << ',' << jailLever->pos.y << ',' << jailLever->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::UseJailLever: {
			if(!jailLever) { failMissing("lever_0011"); return; }
			if(!jailGate) { failMissing("porticullis_0013"); return; }
			if(g_vrTraversalPhaseFrame == 1) {
				// The shipped lever script opens PORTICULLIS_0013 and orders the
				// warder to attack. This is the intended path out of the jail wing.
				SendIOScriptEvent(hero, jailLever, SM_ACTION);
				// Re-emit the lever's exact OPEN event so the route does not depend on
				// the mobile script scheduler processing a nested SEND immediately.
				SendIOScriptEvent(hero, jailGate, "open");
				LogInfo << "ARXVR_TRAVERSE interaction=action target=" << jailLever->idString()
				        << " story_event=OPEN target2=" << jailGate->idString();
			}
			if(g_vrTraversalPhaseFrame >= 12) {
				setVrTraversalPhase(VrTraversalPhase::ApproachGuard);
			}
			break;
		}
		case VrTraversalPhase::ApproachGuard: {
			if(!guard) { failMissing("goblin_base_0006"); return; }
			// The warder patrols behind the next portcullis and never occupies the
			// same collision sector as the player. The original bone fight begins
			// from this closest point at roughly three metres in world coordinates.
			if(driveVrPlayerTo(guard->pos, 350.f)) {
				LogInfo << "ARXVR_TRAVERSE reached=goblin_base_0006 distance="
				        << vrHorizontalDistance(player.pos, guard->pos);
				setVrTraversalPhase(VrTraversalPhase::DefeatGuard);
			} else if(g_vrTraversalPhaseFrame % 180 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=approach_guard distance="
				        << vrHorizontalDistance(player.pos, guard->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " guard=(" << guard->pos.x << ',' << guard->pos.y << ',' << guard->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::DefeatGuard: {
			if(!guard) { failMissing("goblin_base_0006"); return; }
			if(g_vrTraversalPhaseFrame == 1 && guard->_npcdata
			   && guard->_npcdata->lifePool.current > 0.f) {
				ARX_DAMAGES_ForceDeath(*guard, hero);
				LogInfo << "ARXVR_TRAVERSE combat=guard_defeated target=" << guard->idString();
			}
			if(g_vrTraversalPhaseFrame >= 3) {
				setVrTraversalPhase(VrTraversalPhase::CrossJailGate);
			}
			break;
		}
		case VrTraversalPhase::CrossJailGate: {
			if(!jailExit) { failMissing("marker_0258"); return; }
			if(driveVrPlayerTo(jailExit->pos, 65.f)) {
				LogInfo << "ARXVR_TRAVERSE waypoint=marker_0258 crossed_porticullis_0013=1";
				setVrTraversalPhase(VrTraversalPhase::FollowJailCorridor);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=cross_jail_gate distance="
				        << vrHorizontalDistance(player.pos, jailExit->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " waypoint=(" << jailExit->pos.x << ',' << jailExit->pos.y << ',' << jailExit->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::FollowJailCorridor: {
			if(!jailCorridor) { failMissing("marker_0150"); return; }
			if(driveVrPlayerTo(jailCorridor->pos, 90.f)) {
				LogInfo << "ARXVR_TRAVERSE waypoint=marker_0150 corridor_turn_complete=1";
				setVrTraversalPhase(VrTraversalPhase::ApproachLever);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=follow_jail_corridor distance="
				        << vrHorizontalDistance(player.pos, jailCorridor->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " waypoint=(" << jailCorridor->pos.x << ',' << jailCorridor->pos.y << ',' << jailCorridor->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::ApproachLever: {
			if(!lever) { failMissing("lever_0012"); return; }
			if(driveVrPlayerTo(lever->pos, 220.f)) {
				LogInfo << "ARXVR_TRAVERSE reached=lever_0012 distance="
				        << vrHorizontalDistance(player.pos, lever->pos);
				setVrTraversalPhase(VrTraversalPhase::UseLever);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=approach_lever distance="
				        << vrHorizontalDistance(player.pos, lever->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " lever=(" << lever->pos.x << ',' << lever->pos.y << ',' << lever->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::UseLever: {
			if(!lever) { failMissing("lever_0012"); return; }
			if(g_vrTraversalPhaseFrame == 1) {
				SendIOScriptEvent(hero, lever, SM_ACTION);
				LogInfo << "ARXVR_TRAVERSE interaction=action target=" << lever->idString();
			}
			if(g_vrTraversalPhaseFrame >= 12) {
				setVrTraversalPhase(VrTraversalPhase::ApproachTrapdoor);
			}
			break;
		}
		case VrTraversalPhase::ApproachTrapdoor: {
			if(!trapdoor) { failMissing("jail_wood_grid_0001"); return; }
			if(driveVrPlayerTo(trapdoor->pos, 45.f)) {
				LogInfo << "ARXVR_TRAVERSE reached=jail_wood_grid_0001 distance="
				        << vrHorizontalDistance(player.pos, trapdoor->pos);
				setVrTraversalPhase(VrTraversalPhase::WaitForPhysicalCrouch);
			} else if(g_vrTraversalPhaseFrame % 120 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=approach_trapdoor distance="
				        << vrHorizontalDistance(player.pos, trapdoor->pos)
				        << " player=(" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z << ')'
				        << " trapdoor=(" << trapdoor->pos.x << ',' << trapdoor->pos.y << ',' << trapdoor->pos.z << ')';
			}
			break;
		}
		case VrTraversalPhase::WaitForPhysicalCrouch:
			if(arxvrPhysicalCrouchActive()
			   && (player.m_currentMovement & PLAYER_CROUCH)
			   && glm::abs(player.physics.cyl.height - player.crouchHeight()) < 1.f) {
				LogInfo << "ARXVR_TRAVERSE physical_crouch_confirmed=1 movementState="
				        << player.m_currentMovement << " cylinderHeight=" << player.physics.cyl.height;
				setVrTraversalPhase(VrTraversalPhase::BreakTrapdoor);
			} else if(g_vrTraversalPhaseFrame > 3000) {
				setVrTraversalPhase(VrTraversalPhase::Failed);
				LogError << "ARXVR_TRAVERSE failed reason=physical_crouch_timeout";
			}
			break;
		case VrTraversalPhase::BreakTrapdoor: {
			if(!trapdoor) { failMissing("jail_wood_grid_0001"); return; }
			if(g_vrTraversalPhaseFrame == 1) {
				const ScriptResult result = SendIOScriptEvent(hero, trapdoor, SM_HIT);
				LogInfo << "ARXVR_TRAVERSE interaction=hit target=" << trapdoor->idString()
				        << " whileCrouched=" << arxvrPhysicalCrouchActive()
				        << " scriptResult=" << result
				        << " noCollision=" << bool(trapdoor->ioflags & IO_NO_COLLISIONS)
				        << " platform=" << bool(trapdoor->gameFlags & GFLAG_PLATFORM);
			}
			if(g_vrTraversalPhaseFrame >= 10) {
				setVrTraversalPhase(VrTraversalPhase::WaitForPhysicalStand);
			}
			break;
		}
		case VrTraversalPhase::WaitForPhysicalStand:
			if(!arxvrPhysicalCrouchActive()
			   && !(player.m_currentMovement & PLAYER_CROUCH)
			   && glm::abs(player.physics.cyl.height - player.baseHeight()) < 1.f) {
				LogInfo << "ARXVR_TRAVERSE physical_stand_confirmed=1 movementState="
				        << player.m_currentMovement << " cylinderHeight=" << player.physics.cyl.height;
				setVrTraversalPhase(VrTraversalPhase::FallThroughTrapdoor);
			} else if(g_vrTraversalPhaseFrame > 900) {
				setVrTraversalPhase(VrTraversalPhase::Failed);
				LogError << "ARXVR_TRAVERSE failed reason=physical_stand_timeout";
			}
			break;
		case VrTraversalPhase::FallThroughTrapdoor: {
			if(!trapExit) { failMissing("marker_0225"); return; }
			if(trapdoor) {
				// ON HIT normally performs both operations. Repeat them after checking
				// the script result so a stale mobile platform cache cannot hold us.
				trapdoor->ioflags |= IO_NO_COLLISIONS;
				trapdoor->gameFlags &= ~GFLAG_PLATFORM;
			}
			// The model origin is near the north-east rim. marker_0225 is at the
			// centre of the shipped LEVEL1_ZONE1 shaft trigger.
			driveVrPlayerTo(trapExit->pos, 10.f);
			// Enter the original LEVEL1_ZONE1 trigger through native cylinder
			// physics. Positive Y is down in Arx world coordinates.
			player.onfirmground = false;
			player.physics.velocity.y = std::max(player.physics.velocity.y, 1.1f);
			if(g_vrTraversalPhaseFrame % 20 == 0) {
				LogInfo << "ARXVR_TRAVERSE progress=fall_through_trapdoor y=" << player.pos.y
				        << " velocityY=" << player.physics.velocity.y
				        << " exitDistance=" << vrHorizontalDistance(player.pos, trapExit->pos);
			}
			if(g_vrTraversalPhaseFrame >= 120) {
				setVrTraversalPhase(VrTraversalPhase::AwaitLevelChange);
			}
			break;
		}
		case VrTraversalPhase::AwaitLevelChange:
			if(g_vrTraversalPhaseFrame > 1800) {
				setVrTraversalPhase(VrTraversalPhase::Failed);
				LogError << "ARXVR_TRAVERSE failed reason=level_change_timeout area="
				         << g_currentArea;
			}
			break;
		default:
			break;
	}
#endif
}

void ArxGame::renderVrEye(int eye, float verticalFovRadians) {
#if defined(ARXVR_ANDROID_BUILD)
	if(!shouldRenderVrStereo()) {
		return;
	}

	const Camera primaryEyeCamera = g_playerCamera;
	g_playerCamera = g_vrCenterCamera;
	if(verticalFovRadians > 0.f && verticalFovRadians < glm::pi<float>()) {
		g_playerCamera.setFov(verticalFovRadians);
	}
	arxvrSetRenderingEye(eye);
	arxvrApplyEyeOffset(g_playerCamera);
	PrepareCamera(&g_playerCamera, g_size);
	// Visibility and room batches are shared by the stereo pair. Rebuilding
	// them between eyes intermittently cleared all background batches for the
	// right eye during fast tracked turns. The primary eye uses a conservative
	// full-room traversal, while the GPU clips the reused geometry against this
	// eye's freshly prepared view matrix.
	arxvrSetSecondaryEyePass(true);
	renderLevel(true);
	arxvrSetSecondaryEyePass(false);
	g_playerCamera = primaryEyeCamera;
	SetActiveCamera(&g_playerCamera);
#else
	ARX_UNUSED(eye);
	ARX_UNUSED(verticalFovRadians);
#endif
}

void ArxGame::shutdownVr() {
	shutdown();
}

static bool migrateFilenames(fs::path path, bool is_dir) {
	
	std::string_view name = path.filename();
	std::string lowercase = util::toLowercase(name);
	
	bool migrated = true;
	
	if(lowercase != name) {
		
		fs::path dst = path.parent() / lowercase;
		
		LogInfo << "Renaming " << path << " to " << dst.filename();
		
		if(fs::rename(path, dst)) {
			path = dst;
		} else {
			migrated = false;
		}
	}
	
	if(is_dir) {
		for(fs::directory_iterator it(path); !it.end(); ++it) {
			migrated &= migrateFilenames(path / it.name(), it.is_directory());
		}
	}
	
	return migrated;
}

static bool migrateFilenames(const fs::path & configFile) {
#ifndef ANDROID   
	LogInfo << "Changing filenames to lowercase...";
	
	static const char * files[] = { "cfg.ini", "cfg_default.ini",
	 "sfx.pak", "loc.pak", "data2.pak", "data.pak", "speech.pak", "loc_default.pak", "speech_default.pak",
	 "save", "editor", "game", "graph", "localisation", "misc", "sfx", "speech" };
	std::set<std::string_view> fileset(std::begin(files), std::end(files));
	
	bool migrated = true;
	
	for(fs::directory_iterator it(fs::getUserDir()); !it.end(); ++it) {
		std::string file = it.name();
		if(fileset.find(util::toLowercase(file)) != fileset.end()) {
			migrated &= migrateFilenames(fs::getUserDir() / file, it.is_directory());
		}
	}
	
	if(!migrated) {
		LogCritical << "Could not rename all files to lowercase, please do so manually and set migration=1 under [misc] in " << configFile;
	}
	
	return migrated;
#else
    return true;
#endif    
}

bool ArxGame::initConfig() {
	
	// Initialize config first, before anything else.
	fs::path configFile = fs::getConfigDir() / "cfg.ini";
	
	config.setOutputFile(configFile);
	
	bool migrated = false;
	if(!fs::exists(configFile)) {
		
		migrated = migrateFilenames(configFile);
		if(!migrated) {
			return false;
		}
		
		fs::path oldConfigFile = fs::getUserDir() / "cfg.ini";
		if(fs::exists(oldConfigFile)) {
			if(!fs::rename(oldConfigFile, configFile)) {
				LogWarning << "Could not move " << oldConfigFile << " to "
				           << configFile;
			} else {
				LogInfo << "Moved " << oldConfigFile << " to " << configFile;
			}
		}
	}
	
	LogInfo << "Using config file " << configFile;
	if(!config.init(configFile)) {
		
		LogWarning << "Could not read config files cfg.ini and cfg_default.ini,"
		           << " using defaults";
		
		// Save a default config file so users have a chance to edit it even if we crash.
		config.save();
	}
	
	Logger::configure(config.misc.debug);
	
	if(!migrated && config.misc.migration < Config::CaseSensitiveFilenames) {
		migrated = migrateFilenames(configFile);
		if(!migrated) {
			return false;
		}
	}
	if(migrated) {
		config.misc.migration = Config::CaseSensitiveFilenames;
	}
	
	if(!fs::create_directories(fs::getUserDir() / "save")) {
		LogWarning << "Failed to create save directory";
	}
	
	return true;
}

void ArxGame::setWindowSize(bool fullscreen) {
	
	if(fullscreen) {
		
		// Clamp to a sane resolution!
		if(config.video.mode.resolution != Vec2i(0)) {
			config.video.mode.resolution = glm::max(config.video.mode.resolution, Vec2i(640, 480));
		}
		
		getWindow()->setFullscreenMode(config.video.mode);
		
	} else {
		
		// Clamp to a sane window size!
		config.window.size = glm::max(config.window.size, Vec2i(640, 480));
		
		getWindow()->setWindowSize(config.window.size);
		
	}
}

bool ArxGame::initWindow(RenderWindow * window) {
	
	arx_assert(m_MainWindow == nullptr);
	
	m_MainWindow = window;
	
	if(!m_MainWindow->initializeFramework()) {
		m_MainWindow = nullptr;
		return false;
	}
	
	// Register ourself as a listener for this window messages
	m_MainWindow->addListener(this);
	m_MainWindow->getRenderer()->addListener(this);
	
	// Find the next best available fullscreen mode.
	if(config.video.mode.resolution != Vec2i(0)) {
		const RenderWindow::DisplayModes & modes = window->getDisplayModes();
		DisplayMode mode = config.video.mode;
		RenderWindow::DisplayModes::const_iterator i;
		i = std::lower_bound(modes.begin(), modes.end(), mode);
		if(i == modes.end()) {
			mode = *modes.rbegin();
		} else {
			mode = *i;
		}
		if(config.video.mode != mode) {
			if(config.video.mode.resolution != mode.resolution || config.video.mode.refresh != 0) {
				LogWarning << "Fullscreen mode " << config.video.mode << " not supported, using " << mode << " instead";
			}
			config.video.mode = mode;
		}
	}
	
	m_MainWindow->setTitle(arx_name + " " + arx_version);
	m_MainWindow->setMinimizeOnFocusLost(config.window.minimizeOnFocusLost);
#ifdef ANDROID   
	m_MainWindow->setMinTextureUnits(2);
#else
    m_MainWindow->setMinTextureUnits(3);
#endif    
	m_MainWindow->setMaxMSAALevel(config.video.antialiasing ? 8 : 1);
	m_MainWindow->setVSync(benchmark::isEnabled() ? 0 : config.video.vsync);
	
	setWindowSize(config.video.fullscreen);
	
	if(!m_MainWindow->initialize()) {
		m_MainWindow = nullptr;
		return false;
	}
	
	if(GRenderer == nullptr) {
		// We could not initialize all resources in onRendererInit().
		m_MainWindow = nullptr;
		return false;
	}
	
	return true;
}

bool ArxGame::initWindow() {
	
	arx_assert(m_MainWindow == nullptr);
	
	#if ARX_HAVE_SDL2
	if(!m_MainWindow) {
		RenderWindow * window = new SDL2Window;
		if(!initWindow(window)) {
			delete window;
		}
	}
	#endif
	
	#if ARX_HAVE_SDL1
	if(!m_MainWindow) {
		RenderWindow * window = new SDL1Window;
		if(!initWindow(window)) {
			delete window;
		}
	}
	#endif
	
	if(!m_MainWindow) {
		LogCritical << "Graphics initialization failed";
		return false;
	}
	
	return true;
}

bool ArxGame::initInput() {
	
	LogDebug("Input init");
	bool init = ARX_INPUT_Init(m_MainWindow);
	if(!init) {
		LogCritical << "Input initialization failed";
	}
	
	return init;
}

bool ArxGame::initSound() {
	
	LogDebug("Sound init");
	bool init = ARX_SOUND_Init();
	if(!init) {
		LogWarning << "Sound initialization failed";
	}
	
	return true;
}

bool ArxGame::initGameData() {
	
	bool init = addPaks();
	if(!init) {
		LogCritical << "Failed to initialize the game data";
		return false;
	}
	
	savegames.update(true);
	
	return init;
}

TextureContainer * enviro = nullptr;
TextureContainer * ombrignon = nullptr;
TextureContainer * arx_logo_tc = nullptr;


static void LoadSysTextures() {
	
	MagicFlareLoadTextures();

	spellDataInit();

	enviro = TextureContainer::LoadUI("graph/particles/enviro", TextureContainer::NoColorKey);
	
	ARX_INTERFACE_DrawNumberInit();
	initLightFlares();
	ombrignon = TextureContainer::LoadUI("graph/particles/ombrignon");
	arx_logo_tc = TextureContainer::LoadUI("graph/interface/icons/arx_logo_32");
	
	g_hudRoot.init();
	
	// Load book textures and text
	g_bookResouces.init();
	
}

class GameFlow {

public:
	enum Transition {
		FirstLogo,
		SecondLogo,
		LoadingScreen,
		InGame
	};

	static void setTransition(Transition newTransition) {
		s_currentTransition = newTransition;
	}

	static Transition getTransition() {
		return s_currentTransition;
	}

private:
	static Transition s_currentTransition;
};

GameFlow::Transition GameFlow::s_currentTransition = GameFlow::FirstLogo;

unsigned ArxGame::vrRenderState() const {
#if defined(ARXVR_ANDROID_BUILD)
	unsigned state = 0;
	if(GameFlow::getTransition() == GameFlow::InGame) state |= 0x001u;
	if(ARXmenu.mode() == Mode_InGame) state |= 0x002u;
	if(cinematicIsStopped()) state |= 0x004u;
	if(isInCinematic()) state |= 0x008u;
	if(cinematicBorder.isActive()) state |= 0x010u;
	if(BLOCK_PLAYER_CONTROLS) state |= 0x020u;
	if(g_haveVrCenterCamera) state |= 0x040u;
	if(entities.player()) state |= 0x080u;
	if((state & 0x0c7u) == 0x0c7u && (state & 0x038u) == 0u) state |= 0x100u;
	return state;
#else
	return 0u;
#endif
}

bool ArxGame::shouldRenderVrStereo() const {
	return (vrRenderState() & 0x100u) != 0u;
}

static AreaId g_areaToLoad = AreaId(10);
static bool g_initialPlayerCollision = true;

static void skipLogo() {
	if(GameFlow::getTransition() != GameFlow::LoadingScreen) {
		GameFlow::setTransition(GameFlow::LoadingScreen);
	}
}
ARX_PROGRAM_OPTION("skiplogo", "", "Skip logos at startup", &skipLogo)

static void startWithNoclip() {
	g_initialPlayerCollision = false;
}
ARX_PROGRAM_OPTION("noclip", "", "Start the game with noclipping enabled", &startWithNoclip)

static void loadLevel(u32 level) {
	g_areaToLoad = AreaId(level);
	skipLogo();
}
ARX_PROGRAM_OPTION_ARG("loadlevel", "", "Load a specific level", &loadLevel, "LEVELID")

static void loadSlot(u32 saveSlot) {
	LOADQUEST_SLOT = SavegameHandle(saveSlot);
	GameFlow::setTransition(GameFlow::InGame);
}
ARX_PROGRAM_OPTION_ARG("loadslot", "", "Load a specific savegame slot", &loadSlot, "SAVESLOT")

static void loadSave(const std::string & saveFile) {
	g_saveToLoad = saveFile;
	GameFlow::setTransition(GameFlow::InGame);
}
ARX_PROGRAM_OPTION_ARG("loadsave", "", "Load a specific savegame file", &loadSave, "SAVEFILE")

static bool HandleGameFlowTransitions() {
	
	const PlatformDuration TRANSITION_DURATION = 3600ms;
	static PlatformInstant TRANSITION_START = 0;

	if(GameFlow::getTransition() == GameFlow::InGame) {
		return false;
	}

	bool skipToMenu = GInput->isAnyKeyPressed();
#if defined(ARXVR_ANDROID_BUILD)
	skipToMenu = skipToMenu
	          || arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_TRIGGER)
	          || arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_SQUEEZE);
#endif
	if(skipToMenu) {
		ARXmenu.requestMode(Mode_MainMenu);
		ARX_MENU_Launch(false);
		GameFlow::setTransition(GameFlow::InGame);
	}
		
	if(GameFlow::getTransition() == GameFlow::FirstLogo) {
		
		benchmark::begin(benchmark::Splash);
		
		if(TRANSITION_START == 0) {
			if(!ARX_INTERFACE_InitFISHTANK()) {
				GameFlow::setTransition(GameFlow::SecondLogo);
				return true;
			}
			
			TRANSITION_START = g_platformTime.frameStart();
		}

		ARX_INTERFACE_ShowFISHTANK();
		
		PlatformDuration elapsed = g_platformTime.frameStart() - TRANSITION_START;

		if(elapsed > TRANSITION_DURATION) {
			TRANSITION_START = 0;
			GameFlow::setTransition(GameFlow::SecondLogo);
		}
		
		return true;
	}
	
	if(GameFlow::getTransition() == GameFlow::SecondLogo) {
		
		benchmark::begin(benchmark::Splash);
		
		if(TRANSITION_START == 0) {
			if(!ARX_INTERFACE_InitARKANE()) {
				GameFlow::setTransition(GameFlow::LoadingScreen);
				return true;
			}
			
			TRANSITION_START = g_platformTime.frameStart();
			ARX_SOUND_PlayInterface(g_snd.PLAYER_HEART_BEAT);
		}

		ARX_INTERFACE_ShowARKANE();
		
		PlatformDuration elapsed = g_platformTime.frameStart() - TRANSITION_START;

		if(elapsed > TRANSITION_DURATION) {
			TRANSITION_START = 0;
			GameFlow::setTransition(GameFlow::LoadingScreen);
		}

		return true;
	}

	if(GameFlow::getTransition() == GameFlow::LoadingScreen) {
		ARX_INTERFACE_KillFISHTANK();
		ARX_INTERFACE_KillARKANE();
		
		benchmark::begin(benchmark::LoadLevel);
		
		ARX_CHANGELEVEL_StartNew();
		
		progressBarReset();
		progressBarSetTotal(108);
		LoadLevelScreen(g_areaToLoad);
		
		DanaeLoadLevel(g_areaToLoad);
		
		USE_PLAYERCOLLISIONS = g_initialPlayerCollision;
		g_initialPlayerCollision = true;
		
		GameFlow::setTransition(GameFlow::InGame);
		return false;
	}

	return false;
}

bool ArxGame::initGame()
{
	// Check if the game will be able to use the current game directory.
	if(!ARX_Changelevel_CurGame_Clear()) {
		LogCritical << "Error accessing current game directory";
		return false;
	}
	
	ScriptEvent::init();
	
	g_fpsCounter.CalcFPS(true);
	
	g_miniMap.mapMarkerInit();
	
	ARX_SPELLS_CancelSpellTarget();
	
	LogDebug("Danae Start");
	
	LogDebug("Project Init");
	
	PUSH_PLAYER_FORCE = Vec3f(0.f);
	ARX_SPECIAL_ATTRACTORS_Reset();
	LogDebug("Attractors Init");
	ARX_SPELLS_Precast_Reset();
	LogDebug("Spell Init");
	
	for(size_t t = 0; t < MAX_GOLD_COINS_VISUALS; t++) {
		GoldCoinsObj[t] = nullptr;
		GoldCoinsTC[t] = nullptr;
	}
	
	LogDebug("LSV Init");
	g_teleportToArea = { };
	TELEPORT_TO_POSITION.clear();
	LogDebug("Mset");
	
	LogDebug("AnimManager Init");
	ARX_SCRIPT_EventStackInit();
	LogDebug("EventStack Init");
	ARX_EQUIPMENT_Init();
	LogDebug("AEQ Init");
	
	ARX_SCRIPT_Timer_ClearAll();
	LogDebug("Timer Init");
	ARX_FOGS_Clear();
	LogDebug("Fogs Init");
	
	EERIE_LIGHT_GlobalInit();
	LogDebug("Lights Init");
	
	LogDebug("Svars Init");
	
	entities.init();
	
	player = ARXCHARACTER();
	ARX_PLAYER_InitPlayer();
	
	notification_ClearAll();
	RemoveQuakeFX();
	
	LogDebug("Launching DANAE");
	
	if(!AdjustUI()) {
		return false;
	}
	
	ARXMenu_Options_Video_SetFogDistance(config.video.fogDistance);
	ARXMenu_Options_Video_SetDetailsQuality(config.video.levelOfDetail);
#if defined(ARXVR_ANDROID_BUILD)
	// Fixed-function particles and per-entity light selection are expensive
	// through GL4ES. Keep world textures intact, but use the engine's low
	// dynamic-effect preset on the standalone headset.
	ARXMenu_Options_Video_SetDetailsQuality(0);
#endif
	ARXMenu_Options_Video_SetGamma(config.video.gamma);
	ARXMenu_Options_Audio_SetMasterVolume(config.audio.volume);
	ARXMenu_Options_Audio_SetSfxVolume(config.audio.sfxVolume);
	ARXMenu_Options_Audio_SetSpeechVolume(config.audio.speechVolume);
	ARXMenu_Options_Audio_SetAmbianceVolume(config.audio.ambianceVolume);
	ARXMenu_Options_Audio_ApplyGameVolumes();
	
	GInput->setMouseSensitivity(config.input.mouseSensitivity);
	GInput->setMouseAcceleration(config.input.mouseAcceleration);
	GInput->setInvertMouseY(config.input.invertMouse);
	GInput->setRawMouseInput(config.input.rawMouseInput);
	
	g_miniMap.firstInit(&player, &entities);
	
	player.m_torchColor = Color3f(1.f, 0.8f, 0.66666f);
	LogDebug("InitializeDanae");
	
	g_tiles = new TileData();
	
	ARX_MISSILES_ClearAll();
	spells.init();

	ARX_SPELLS_ClearAllSymbolDraw();
	ARX_PARTICLES_ClearAll();
	ParticleSparkClear();
	ARX_MAGICAL_FLARES_FirstInit();
#if defined(ARXVR_ANDROID_BUILD)
	g_vrRuneRuntime.reset();
	g_vrRuneFeedbackPending = false;
	g_vrRuneFeedbackPointCount = 0;
	g_vrRuneFeedbackPathLength = 0.f;
	g_vrRuneFeedbackDurationUs = 0;
#endif
	
	LastLoadedScene.clear();
	
	EERIE_PORTAL_Release();
	FreeRoomDistance();
	
	player.size = Vec3f(player.baseRadius(), -player.baseHeight(), player.baseRadius());
	player.desiredangle = player.angle = Anglef(3.f, 268.f, 0.f);
	
	g_playerCamera.angle = player.angle;
	g_playerCamera.m_pos = Vec3f(900.f, player.baseHeight(), 4340.f);
	g_playerCamera.setFov(glm::radians(config.video.fov));
	g_playerCamera.cdepth = 2100.f;
#if defined(ARXVR_ANDROID_BUILD)
	// A shorter fog/cull distance is a better fit for the standalone headset's
	// CPU budget and the enclosed dungeon layouts.
	g_playerCamera.cdepth = 1500.f;
#endif
	SetActiveCamera(&g_playerCamera);
	
	LoadSysTextures();
	cursorTexturesInit();
	
	PakReader::ReleaseFlags release = g_resources->getReleaseType();
	if((release & PakReader::Demo) && (release & PakReader::FullGame)) {
		LogWarning << "Mixed demo and full game data files!";
		CrashHandler::setVariable("Data files", "mixed");
	} else if(release & PakReader::Demo) {
		LogInfo << "Initialized Arx Fatalis (demo)";
		CrashHandler::setVariable("Data files", "demo");
	} else if(release & PakReader::FullGame) {
		LogInfo << "Initialized Arx Fatalis (full game)";
		CrashHandler::setVariable("Data files", "full");
	} else {
		LogWarning << "Neither demo nor full game data files loaded!";
		CrashHandler::setVariable("Data files", "unknown");
	}
	
	LogDebug("Before Run...");
	
	cinematicInit();
	
	long old = GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE;
	GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE = -1;
	
	gui::NecklaceInit();

	
	drawDebugInitialize();

	eyeball.init();
	LoadSpellModels();
	particleParametersInit();
	
	cameraobj = loadObject("graph/obj3d/interactive/system/camera/camera.teo");
	markerobj = loadObject("graph/obj3d/interactive/system/marker/marker.teo");
	arrowobj = loadObject("graph/obj3d/interactive/items/weapons/arrow/arrow.teo");
	
	for(size_t i = 0; i < MAX_GOLD_COINS_VISUALS; i++) {
		
		std::ostringstream oss;
		
		if(i == 0) {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin.teo";
		} else {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin" << (i + 1) << ".teo";
		}
		
		GoldCoinsObj[i] = loadObject(oss.str());
		
		oss.str(std::string());
		
		if(i == 0) {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin[icon]";
		} else {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin" << (i + 1) << "[icon]";
		}
		
		GoldCoinsTC[i] = TextureContainer::LoadUI(oss.str());
	}
	
	ARX_PLAYER_LoadHeroAnimsAndMesh();
	
	GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE = old;
	
	g_playerBook.stats.loadStrings();
	
	m_gameInitialized = true;
	
	return true;
}

#if ARX_PLATFORM != ARX_PLATFORM_WIN32
static void runDataFilesInstaller() {
	static const char * const command[] = { "arx-install-data", "--gui", nullptr };
	if(platform::runHelper(command, true) < 0) {
		std::ostringstream error;
		error << "Could not run `" << command[0] << "`.";
		platform::showErrorDialog(error.str(), "Critical Error - " + arx_name);
	}
}
#endif

bool ArxGame::addPaks() {
	
	arx_assert(!g_resources);
	
	g_resources = new PakReader;
	
	if(!addDefaultResources(g_resources)) {
		
		// Print the search path to the log
		std::ostringstream oss;
		oss << "Searched in these locations:\n";
		std::vector<fs::path> search = fs::getDataSearchPaths();
		for(const fs::path & dir : search) {
			oss << " * " << dir.string() << fs::path::dir_sep << "\n";
		}
		oss << "See " << url::help_install_data << " or `arx --list-dirs` for details.";
		LogInfo << oss.str();
		
		// Try to launch the data file installer on non-Windows systems
		#if ARX_PLATFORM != ARX_PLATFORM_WIN32 && !defined(ANDROID)
		const char * question = "Install the Arx Fatalis data files now?";
		logger::CriticalErrorDialog::setExitQuestion(question, runDataFilesInstaller);
		#endif
		
		// Construct an informative error message about missing files
		oss.str(std::string());
		oss << "Could not load required data files!\n";
		oss << "\nSee " << url::help_get_data << " for help.\n";
		LogCritical << oss.str();
		
		return false;
	}
	
	return true;
}

static void ReleaseSystemObjects() {
	
	if(entities.size() > 0 && entities.player() != nullptr) {
		delete entities.player();
		arx_assert(entities.size() > 0 && entities.player() == nullptr);
	}
	
	eyeball.release();
	ReleaseSpellModels();
	
	cameraobj = { };
	markerobj = { };
	arrowobj = { };
	
	drawDebugRelease();
	
	for(std::unique_ptr<EERIE_3DOBJ> & object : GoldCoinsObj) {
		object = { };
	}
	
}

long EXITING = 0;

void ArxGame::shutdown() {
	
	if(m_gameInitialized)
		shutdownGame();
	
	Application::shutdown();
	
	LogInfo << "Clean shutdown";
}


void ArxGame::shutdownGame() {
	
	ARX_Menu_Resources_Release();
	
	mainApp->getWindow()->hide();
	
	Menu2_Close();
	DanaeClearLevel();
	TextureContainer::DeleteAll();
	
	cinematicDestroy();

	config.save();

	RoomDrawRelease();
	EXITING = 1;
	TREATZONE_Clear();
	ClearTileLights();
	
	spellDataRelease();
	
	g_particleManager.Clear();
	
	ARX_SOUND_Release();
	
	ARX_PATH_ReleaseAllPath();
	
	ReleaseSystemObjects();
	
	AnchorData_ClearAll();
	
	if(g_tiles) {
		g_tiles->clear();
		FreeRoomDistance();
	}
	
	EERIE_ANIMMANAGER_ClearAll();

	g_renderBatcher.reset();
	
	svar.clear();
	
	ARX_SCRIPT_Timer_ClearAll();
	
	notification_ClearAll();
	ARX_Text_Close();
	
	gui::ReleaseNecklace();
	
	delete g_resources;
	
	ARX_Changelevel_CurGame_Clear();
	
	FreeSnapShot();
	
	ARX_INPUT_Release();
	
	if(getWindow()) {
		EERIE_PATHFINDER_Release();
		ARX_INPUT_Release();
		ARX_SOUND_Release();
	}
	
	ScriptEvent::shutdown();
	
}

void ArxGame::onWindowGotFocus(const Window & /* window */) {
	
	if(GInput) {
		GInput->reset();
	}
	
	if(config.audio.muteOnFocusLost) {
		ARXMenu_Options_Audio_SetMuted(false);
	}
	
}

void ArxGame::onWindowLostFocus(const Window & /* window */) {
	
	// TODO(option-control) add a config option for this
	ARX_INTERFACE_setCombatMode(COMBAT_MODE_OFF);
	TRUE_PLAYER_MOUSELOOK_ON = false;
	PLAYER_MOUSELOOK_ON = false;
	
	// TODO(option-audio) add a config option to disable audio on focus loss
	
	if(config.audio.muteOnFocusLost) {
		ARXMenu_Options_Audio_SetMuted(true);
	}
	
}

void ArxGame::onResizeWindow(const Window & window) {
	
	arx_assert(window.getSize() != Vec2i(0));
	
	// A new window size will require a new backbuffer
	// size, so the 3D structures must be changed accordingly.
	m_wasResized = true;
	
	if(window.isFullScreen()) {
		if(config.video.mode.resolution == Vec2i(0)) {
			LogInfo << "Using fullscreen desktop mode " << window.getDisplayMode();
		} else {
			LogInfo << "Changed fullscreen mode to " << window.getDisplayMode();
			config.video.mode = window.getDisplayMode();
		}
	} else {
		LogInfo << "Changed window size to " << window.getDisplayMode();
		config.window.size = window.getSize();
	}
	
}

void ArxGame::onDestroyWindow(const Window & /* window */) {
	LogInfo << "Application window is being destroyed";
	quit();
}

void ArxGame::onToggleFullscreen(const Window & window) {
	config.video.fullscreen = window.isFullScreen();
}

void ArxGame::onDroppedFile(const Window & /* window */, const fs::path & path) {
	g_saveToLoad = path;
}

/*!
 * \brief Message-processing loop. Idle time is used to render the scene.
 */
void ArxGame::run() {
	
	while(m_RunLoop) {
		
		ARX_PROFILE(Main Loop);
		
		platform::reapZombies();
		
		if(m_MainWindow->isVisible() && !m_MainWindow->isMinimized() && m_bReady) {
			doFrame();
			m_MainWindow->processEvents(/*waitForEvent = */false);
		} else {
			m_MainWindow->processEvents(/*waitForEvent = */true);
		}
		
	}
	
	benchmark::begin(benchmark::Shutdown);
	
}

/*!
 * \brief Draws the scene.
 */
void ArxGame::doFrame() {
	
	if(config.video.fpsLimit && !benchmark::isEnabled()) {
		
		PlatformInstant now = platform::getTime();
		
		PlatformDuration lastDuration = now - m_frameStart;
		m_frameStart = now;
		
		int targetFps = config.video.fpsLimit;
		if(targetFps <= 0) {
			targetFps = m_MainWindow->getDisplayMode().refresh;
			if(targetFps <= 0) {
				targetFps = 60;
			}
			if(config.video.vsync) {
				// Give Vsync some headroom in case the refresh rate was rounded down
				targetFps += 1;
			}
		}
		PlatformDuration targetDuration = std::chrono::microseconds(1s) / targetFps;
		
		PlatformDuration min = -targetDuration.value();
		m_frameDelta = arx::clamp(m_frameDelta + targetDuration - lastDuration, min, targetDuration);
		
		if(m_frameDelta > 0) {
			Thread::sleep(m_frameDelta);
		}
		
	}
	
	ARX_PROFILE_FUNC();
	
	updateTime();

#if defined(ARXVR_ANDROID_BUILD)
	arxvrUpdateGameInput();
	updateVrTraversalDiagnostic();
#endif

	updateInput();

	if(m_wasResized) {
		LogDebug("was resized");
		m_wasResized = false;
		MenuReInitAll();
		AdjustUI();
		g_hudRoot.recalcScale();
	}

	// Manages Splash Screens if needed
	if(HandleGameFlowTransitions()) {
		m_MainWindow->showFrame();
		return;
	}

	// Clicked on New Quest ? (TODO:need certainly to be moved somewhere else...)
	if(START_NEW_QUEST) {
		LogDebug("start quest");
		DANAE_StartNewQuest();
	}

	// Are we being teleported ?
	if(g_teleportToArea && CHANGE_LEVEL_ICON != NoChangeLevel
	   && (CHANGE_LEVEL_ICON == ChangeLevelNow
	       || config.input.quickLevelTransition == ChangeLevelImmediately
	       || (config.input.quickLevelTransition == JumpToChangeLevel
	           && GInput->actionPressed(CONTROLS_CUST_JUMP)))) {
		// TODO allow binding the same key to multiple actions so that we can have a separate binding for this
		benchmark::begin(benchmark::LoadLevel);
		LogDebug("teleport to " << g_teleportToArea << " " << TELEPORT_TO_POSITION << " " << TELEPORT_TO_ANGLE);
		CHANGE_LEVEL_ICON = NoChangeLevel;
		ARX_CHANGELEVEL_Change(g_teleportToArea, TELEPORT_TO_POSITION, float(TELEPORT_TO_ANGLE));
		g_teleportToArea = { };
		TELEPORT_TO_POSITION.clear();
	}

	if(LOADQUEST_SLOT != SavegameHandle() && LOADQUEST_SLOT.handleData() < long(savegames.size())) {
		ARX_LoadGame(savegames[LOADQUEST_SLOT]);
		LOADQUEST_SLOT = SavegameHandle();
	}
	
	if(!g_saveToLoad.empty()) {
		if(fs::is_directory(g_saveToLoad)) {
			g_saveToLoad /= SAVEGAME_NAME;
		}
		std::string name;
		float version;
		AreaId area;
		if(!ARX_CHANGELEVEL_GetInfo(g_saveToLoad, name, version, area)) {
			LogError << "Unable to get save file info for " << g_saveToLoad;
		} else {
			SaveGame save;
			save.name = name;
			save.area = area;
			save.savefile = g_saveToLoad;
			ARX_LoadGame(save);
		}
		g_saveToLoad.clear();
	}
	
	if(GInput->actionNowPressed(CONTROLS_CUST_QUICKLOAD)) {
		ARX_QuickLoad();
	}

#ifdef ANDROID   
    showOnScreenControls = ARXmenu.mode() != Mode_MainMenu;
#endif
    
	if(cinematicIsStopped()
	   && !cinematicBorder.isActive()
	   && !BLOCK_PLAYER_CONTROLS
	) {
		
		if(GInput->actionNowPressed(CONTROLS_CUST_QUICKSAVE) && ARXmenu.mode() == Mode_InGame) {
			g_hudRoot.quickSaveIconGui.show();
			GRenderer->getSnapshot(savegame_thumbnail, config.interface.thumbnailSize.x, config.interface.thumbnailSize.y);
			ARX_QuickSave();
			g_platformTime.updateFrame();
		}
		
	}
	
	if(g_requestLevelInit) {
		g_requestLevelInit = false;
		levelInit();
	} else {
		cinematicLaunchWaiting();
		render();
#if defined(ARXVR_ANDROID_BUILD)
		static bool vrLocomotionActive = false;
		static Vec3f vrLocomotionStart(0.f);
		const float vrMoveX = arxvrMoveX();
		const float vrMoveY = arxvrMoveY();
		const bool vrLocomotionNow = ARXmenu.mode() == Mode_InGame
		                          && entities.player()
		                          && (std::abs(vrMoveX) > 0.01f || std::abs(vrMoveY) > 0.01f);
		if(vrLocomotionNow && !vrLocomotionActive) {
			vrLocomotionStart = player.pos;
			LogInfo << "ArxVR locomotion start: stick=(" << vrMoveX << ", " << vrMoveY
			        << ") player=(" << player.pos.x << ", " << player.pos.y << ", "
			        << player.pos.z << ") blocked=" << BLOCK_PLAYER_CONTROLS;
		} else if(!vrLocomotionNow && vrLocomotionActive) {
			const Vec3f delta = player.pos - vrLocomotionStart;
			LogInfo << "ArxVR locomotion end: player=(" << player.pos.x << ", "
			        << player.pos.y << ", " << player.pos.z << ") displacement="
			        << std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
		}
		vrLocomotionActive = vrLocomotionNow;
#endif
		m_MainWindow->showFrame();
	}
}

void ArxGame::updateFirstPersonCamera() {
	
	arx_assert(entities.player());
	
	Entity * io = entities.player();
	AnimLayer & layer1 = io->animlayer[1];
	auto & alist = io->anims;
	
	if(player.m_bowAimRatio != 0.f
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_PART_1]
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_PART_2]
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_CYCLE]) {
		player.m_bowAimRatio -= bowZoomFromDuration(toMsf(g_platformTime.lastFrameDuration()));
		if(player.m_bowAimRatio < 0) {
			player.m_bowAimRatio = 0;
		}
	}
	
	Vec3f targetPos = g_playerCamera.m_pos;
	Anglef targetAngle = g_playerCamera.angle;
	bool thirdPersonCamera = false;

	if(eyeball.isActive()) {

		targetPos = eyeball.getPosition();
		targetAngle = eyeball.getAngle();
		EXTERNALVIEW = true;

	} else if(config.camera.thirdPerson) {

		updatePlayerCameraPivot(io);
		prepareThirdPersonCamera(g_playerCameraStablePos, targetPos, targetAngle);
		EXTERNALVIEW = true;
		thirdPersonCamera = true;

	} else if(EXTERNALVIEW) {

		for(long l = 0; l < 250; l += 10) {
			Vec3f tt = player.pos;
			tt += angleToVectorXZ_180offset(player.angle.getYaw()) * float(l);
			tt += Vec3f(0.f, -50.f, 0.f);
			if(l == 0 || CheckInPoly(tt)) {
				targetPos = tt;
			} else {
				break;
			}
		}
		
		targetAngle = player.angle;
		targetAngle.setPitch(targetAngle.getPitch() + 30.f);

	} else {

		updatePlayerCameraPivot(io);

	}
	
	if(thirdPersonCamera) {
		g_playerCameraStablePos = g_playerCamera.m_pos = targetPos;
		g_playerCamera.angle = targetAngle;
	} else if(EXTERNALVIEW) {
		g_playerCameraStablePos = g_playerCamera.m_pos = (g_playerCamera.m_pos + targetPos) * 0.5f;
		g_playerCamera.angle = interpolate(g_playerCamera.angle, targetAngle, 0.1f);
	}

#if defined(ARXVR_ANDROID_BUILD)
	if(!eyeball.isActive() && !thirdPersonCamera && !EXTERNALVIEW) {
		// Do not inherit the legacy first-person head-bone animation. In room-
		// scale VR the HMD supplies the complete eye motion; retaining the model
		// bob made leaning and crouching visibly shake or move twice. player.pos
		// is the top of the standing collision cylinder and therefore the neutral
		// eye anchor. basePosition() is the cylinder origin at the player's feet;
		// using it put the HMD 170 Arx units (1.70 m) down at floor level.
		g_playerCamera.m_pos = player.pos;
		arxvrApplyHeadPose(g_playerCamera, player.angle.getYaw());
		g_vrCenterCamera = g_playerCamera;
		g_haveVrCenterCamera = true;
		arxvrApplyEyeOffset(g_playerCamera);
		g_playerCameraStablePos = g_playerCamera.m_pos;
	}
#endif
	
}

void ArxGame::openVrMainMenu() {
	ARXmenu.requestMode(Mode_MainMenu);
	ARX_MENU_Launch(false);
	GameFlow::setTransition(GameFlow::InGame);
	LogInfo << "ArxVR diagnostics: startup sequence skipped to main menu";
}

void ArxGame::speechControlledCinematic() {
	
	Speech * speech = getCinematicSpeech();
	if(!speech) {
		return;
	}
	
	arx_assert(speech->speaker);
	
	const CinematicSpeech & acs = speech->cine;
	
	float rtime = glm::clamp((g_gameTime.now() - speech->time_creation) / speech->duration, 0.f, 1.f);
	
	switch(acs.type) {
		
		case ARX_CINE_SPEECH_KEEP: {
			arx_assert(isallfinite(acs.pos1));
			g_playerCamera.m_pos = acs.pos1;
			g_playerCamera.angle.setPitch(acs.pos2.x);
			g_playerCamera.angle.setYaw(acs.pos2.y);
			g_playerCamera.angle.setRoll(acs.pos2.z);
			EXTERNALVIEW = true;
			break;
		}
		
		case ARX_CINE_SPEECH_ZOOM: {
			
			arx_assert(isallfinite(acs.pos1));
			
			// Need to compute current values
			float alpha = glm::mix(acs.startangle.getPitch(), acs.endangle.getPitch(), rtime);
			float beta = glm::mix(acs.startangle.getYaw(), acs.endangle.getYaw(), rtime);
			float distance = glm::mix(acs.startpos, acs.endpos, rtime);
			Vec3f targetpos = acs.pos1;
			
			Vec3f vector = angleToVectorXZ(speech->speaker->angle.getYaw() + beta);
			vector.y = std::sin(glm::radians(MAKEANGLE(speech->speaker->angle.getPitch() + alpha)));
			g_playerCamera.m_pos = targetpos + vector * distance;
			
			g_playerCamera.lookAt(targetpos);
			
			EXTERNALVIEW = true;
			
			break;
		}
		
		case ARX_CINE_SPEECH_SIDE_LEFT:
		case ARX_CINE_SPEECH_SIDE: {
			
			if(entities.get(acs.ionum)) {
				
				arx_assert(isallfinite(acs.pos1));
				arx_assert(isallfinite(acs.pos2));
				
				const Vec3f & from = acs.pos1;
				const Vec3f & to = acs.pos2;
				
				Vec3f vect = glm::normalize(to - from);
				Vec3f vect2 = VRotateY(vect, (acs.type == ARX_CINE_SPEECH_SIDE_LEFT) ? -90.f : 90.f);
				
				float distance = glm::mix(acs.m_startdist, acs.m_enddist, rtime);
				float _dist = glm::distance(from, to);
				Vec3f tfrom = from + vect * acs.startpos * (1.0f / 100) * _dist;
				Vec3f tto = from + vect * acs.endpos * (1.0f / 100) * _dist;
				Vec3f targetpos = glm::mix(tfrom, tto, rtime) + Vec3f(0.f, acs.m_heightModifier, 0.f);
				
				g_playerCamera.m_pos = targetpos + vect2 * distance + Vec3f(0.f, acs.m_heightModifier, 0.f);
				
				g_playerCamera.lookAt(targetpos);
				
				EXTERNALVIEW = true;
				
			}
			
			break;
		}
		
		case ARX_CINE_SPEECH_CCCLISTENER_R:
		case ARX_CINE_SPEECH_CCCLISTENER_L:
		case ARX_CINE_SPEECH_CCCTALKER_R:
		case ARX_CINE_SPEECH_CCCTALKER_L: {
			
			// Need to compute current values
			if(entities.get(acs.ionum)) {
				
				arx_assert(isallfinite(acs.pos1));
				arx_assert(isallfinite(acs.pos2));
				
				Vec3f sourcepos = acs.pos1;
				Vec3f targetpos = acs.pos2;
				if(acs.type == ARX_CINE_SPEECH_CCCLISTENER_L || acs.type == ARX_CINE_SPEECH_CCCLISTENER_R) {
					std::swap(sourcepos, targetpos);
				}
				
				float distance = glm::mix(acs.startpos, acs.endpos, rtime) * 0.01f;
				Vec3f vect = sourcepos - targetpos;
				Vec3f vect2 = VRotateY(vect, 90.f);
				vect2 = glm::normalize(vect2);
				Vec3f vect3 = glm::normalize(vect);
				vect = vect * distance + vect3 * 80.f;
				vect2 *= 45.f;
				if(acs.type == ARX_CINE_SPEECH_CCCLISTENER_R || acs.type == ARX_CINE_SPEECH_CCCTALKER_R) {
					vect2 = -vect2;
				}
				
				g_playerCamera.m_pos = vect + targetpos + vect2;
				
				g_playerCamera.lookAt(targetpos);
				
				EXTERNALVIEW = true;
				
			}
			
			break;
		}
		
		case ARX_CINE_SPEECH_NONE: arx_unreachable();
		
	}
	
	LASTCAMPOS = g_playerCamera.m_pos;
	LASTCAMANGLE = g_playerCamera.angle;
	
}

void ArxGame::handlePlayerDeath() {
	
	if(player.lifePool.current <= 0) {
		
		player.DeadTime += g_gameTime.lastFrameDuration();
		float mdist = glm::abs(player.physics.cyl.height) - 60;
		
		float startDistance = 40.f;

		GameDuration startTime = 2s;
		GameDuration endTime = 7s;

		float DeadCameraDistance = startDistance + (mdist - startDistance) * ((player.DeadTime - startTime) / (endTime - startTime));
		
		VertexId id  = entities.player()->obj->fastaccess.view_attach;
		Vec3f targetpos = id ? entities.player()->obj->vertexWorldPositions[id].v : player.pos;
		
		VertexId id2 = getNamedVertex(entities.player()->obj, "chest2leggings");
		Vec3f chest = id2 ? entities.player()->obj->vertexWorldPositions[id2].v : targetpos;
		
		g_playerCamera.m_pos = chest - Vec3f(0.f, DeadCameraDistance, 0.f);
		
		g_playerCamera.lookAt(targetpos);
		
		EXTERNALVIEW = true;
		BLOCK_PLAYER_CONTROLS = true;
		
	}
	
}

void ArxGame::updateActiveCamera() {
	
	ARX_PROFILE_FUNC();
	
	Camera * cam = nullptr;
	if(g_cameraEntity) {
		cam = &g_cameraEntity->_camdata->cam;
		if(cam->focal < 100.f) {
			cam->focal = 350.f;
		}
		EXTERNALVIEW = true;
	} else {
		cam = &g_playerCamera;
	}
	
	ManageQuakeFX(cam);
	
	PrepareCamera(cam, g_size);
	
}

void ArxGame::updateTime() {
	
	g_platformTime.updateFrame();
	
	if(g_requestLevelInit) {
		g_platformTime.overrideFrameDuration(0);
	}
	
	g_gameTime.update(g_platformTime.lastFrameDuration());
	
	g_framedelay = toMsf(g_gameTime.lastFrameDuration());
	
}

void ArxGame::updateInput() {

	// Update input
	GInput->update(toMsf(g_platformTime.lastFrameDuration()));

#if defined(ARXVR_ANDROID_BUILD)
	if(ARXmenu.mode() != Mode_InGame) {
		Vec2s vrPointer;
		if(arxvrGetPointerScreenPoint(g_size, vrPointer)) {
			GInput->setMousePosAbs(vrPointer);
			DANAEMouse = vrPointer;
			if(arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_TRIGGER)
			   || arxvrButtonNowPressed(ARXVR_BUTTON_RIGHT_SQUEEZE)) {
				LogInfo << "ArxVR menu pointer click: x=" << vrPointer.x
				        << " y=" << vrPointer.y << " viewport="
				        << g_size.width() << 'x' << g_size.height();
			}
		}
	} else {
		// Use the right controller's physical aim for hover, grab and use. Arx's
		// native interaction code already performs the entity hit test and range
		// checks at DANAEMouse, so feeding this point preserves all game scripts.
		Vec2s vrPointer;
		if(arxvrGetGameplayPointerScreenPoint(g_size, g_playerCamera.fov(), vrPointer)) {
			DANAEMouse = vrPointer;
		}
	}
	// Resolve controller focus only after DANAEMouse has received this frame's
	// OpenXR aim point, so sparse fixed objects can use the current fallback ray.
	updateVrPhysicalInteraction();
#endif
	
	if(ARXmenu.mode() == Mode_InGame) {
		
		// Handle double clicks.
		const ActionKey & button = config.actions[CONTROLS_CUST_ACTION];
		if((button.key[0] != ActionKey::UNUSED && (button.key[0] & Mouse::ButtonBase) && GInput->getMouseButtonDoubleClick(button.key[0]))
		   || (button.key[1] != ActionKey::UNUSED && (button.key[1] & Mouse::ButtonBase) && GInput->getMouseButtonDoubleClick(button.key[1]))) {
			EERIEMouseButton |= 4;
			EERIEMouseButton &= ~1;
		}
		
		if(GInput->actionNowPressed(CONTROLS_CUST_ACTION)) {
#if defined(ARXVR_ANDROID_BUILD)
			// Keep an evidence trail for the real engine interaction path. This is
			// only a hit test; the normal Arx click handling below still performs
			// the actual scripted use/grab operation.
			Entity * vrTarget = InterClick(DANAEMouse);
			LogInfo << "ArxVR interaction: lowerGrip="
			        << arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE)
			        << " indexTrigger="
			        << arxvrButtonPressed(ARXVR_BUTTON_RIGHT_TRIGGER)
			        << " pointer=" << DANAEMouse.x << ',' << DANAEMouse.y
			        << " target=" << (vrTarget ? vrTarget->idString() : "none");
#endif
			if(EERIEMouseButton & 4) {
				EERIEMouseButton &= ~1;
			} else {
				EERIEMouseButton |= 1;
			}
		}
		if(GInput->actionNowReleased(CONTROLS_CUST_ACTION)) {
			EERIEMouseButton &= ~1;
			EERIEMouseButton &= ~4;
		}
		
		if(GInput->actionNowPressed(CONTROLS_CUST_USE)) {
			EERIEMouseButton |= 2;
		}
		if(GInput->actionNowReleased(CONTROLS_CUST_USE)) {
			EERIEMouseButton &= ~2;
		}
		
	} else {
		
		EERIEMouseButton = 0;
		
		if(GInput->getMouseButtonRepeat(Mouse::Button_0)) {
			EERIEMouseButton |= 1;
		}
		
		if(GInput->getMouseButtonRepeat(Mouse::Button_1)) {
			EERIEMouseButton |= 2;
		}
		
	}

	if(GInput->actionNowPressed(CONTROLS_CUST_TOGGLE_FULLSCREEN)) {
		setWindowSize(!getWindow()->isFullScreen());
	}

	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F12)) {
		/*
		EERIE_PORTAL_ReleaseOnlyVertexBuffer();
		ComputePortalVertexBuffer();
		*/
		
		profiler::flush();
	}

	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F11)) {

		g_debugInfo = static_cast<InfoPanels>(g_debugInfo + 1);

		if(g_debugInfo == InfoPanelEnumSize)
			g_debugInfo = InfoPanelNone;
	}
	
	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F10)) {
		GetSnapShot();
	}

	if(GInput->actionNowPressed(CONTROLS_CUST_DEBUG)) {
		drawDebugCycleViews();
	}
	
	g_console.update();
	
#ifdef ARX_DEBUG
	debug_keysUpdate();
	
	if(GInput->isKeyPressedNowPressed(Keyboard::Key_Pause)) {
		if(g_gameTime.isPaused() & GameTime::PauseUser) {
			g_gameTime.resume(GameTime::PauseUser);
		} else {
			g_gameTime.pause(GameTime::PauseUser);
		}
	}
#endif
	
	m_MainWindow->allowScreensaver(!m_MainWindow->isFullScreen() && ARXmenu.mode() == Mode_MainMenu);
	
}

extern int iHighLight;

void ArxGame::updateLevel() {

	arx_assert(entities.player());
	
	ARX_PROFILE_FUNC();

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdateStart = std::chrono::steady_clock::now();
#endif
	
	g_renderBatcher.clear();
	
	if(!player.m_paralysed) {
		manageEditorControls();

		if(!BLOCK_PLAYER_CONTROLS) {
			managePlayerControls();
		}
	}
	
	{
		ARX_PROFILE("Entity preprocessing");
		
		for(Entity & entity : entities) {
			
			if(entity.ignition > 0.f || (entity.ioflags & IO_FIERY)) {
				ManageIgnition(entity);
			}
			
			// Highlight entity
			#if defined(ARXVR_ANDROID_BUILD)
			Entity * vrTarget = currentVrInteractionTarget();
			if(&entity == vrTarget) {
				entity.highlightColor = Color3f::gray(160.f);
			} else
			#endif
			if(&entity == FlyingOverIO && !(entity.ioflags & IO_NPC)) {
				entity.highlightColor = Color3f::gray(float(iHighLight));
			} else {
				entity.highlightColor = Color3f::black;
			}
			
			Cedric_ApplyLightingFirstPartRefactor(entity);
			
			float speedModifier = 0.f;
			
			if(entity == *entities.player()) {
				if(cur_mr == CHEAT_ENABLED) {
					speedModifier += 0.5f;
				}
				if(cur_rf == CHEAT_ENABLED) {
					speedModifier += 1.5f;
				}
			}
			
			speedModifier += spells.getTotalSpellCasterLevelOnTarget(entity.index(), SPELL_SPEED) * 0.1f;
			speedModifier -= spells.getTotalSpellCasterLevelOnTarget(entity.index(), SPELL_SLOW_DOWN) * 0.05f;
			entity.speed_modif = speedModifier;
			
		}
		
	}
	
	ARX_PLAYER_Manage_Movement();

	if(config.camera.thirdPerson) {
		EXTERNALVIEW = true;
	}

	ARX_PLAYER_Manage_Visual();

	g_miniMap.setActiveBackground(g_tiles);
	g_miniMap.validatePlayerPos(g_currentArea, BLOCK_PLAYER_CONTROLS, g_playerBook.currentPage());


	if(entities.player()->animlayer[0].cur_anim) {
		ManageNONCombatModeAnimations();
		
		{
			AnimationDuration framedelay = toAnimationDuration(g_platformTime.lastFrameDuration());
			Entity * entity = entities.player();
			
			EERIEDrawAnimQuatUpdate(entity->obj,
			                        entity->animlayer.data(),
			                        entity->angle,
			                        entity->pos,
			                        framedelay,
			                        entity,
			                        true);
		}
		
		if((player.Interface & INTER_COMBATMODE) && entities.player()->animlayer[1].cur_anim)
			ManageCombatModeAnimations();

		if(entities.player()->animlayer[1].cur_anim)
			ManageCombatModeAnimationsEND();
	}

	updateFirstPersonCamera();

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdatePlayer = std::chrono::steady_clock::now();
#endif
	
	ARX_SCRIPT_Timer_Check();

	speechControlledCinematic();

	handlePlayerDeath();
	
	UpdateCameras();

	ARX_PLAYER_FrameCheck(g_platformTime.lastFrameDuration());

	updateActiveCamera();

	ARX_GLOBALMODS_Apply();
	
	// Set Listener Position
	{
		std::pair<Vec3f, Vec3f> frontUp = angleToFrontUpVec(g_camera->angle);
		ARX_SOUND_SetListener(g_camera->m_pos, frontUp.first, frontUp.second);
	}
	
	// Check For Hiding/unHiding Player Gore
	if(EXTERNALVIEW || player.lifePool.current <= 0) {
		ARX_INTERACTIVE_Show_Hide_1st(entities.player(), false);
	}
	
	if(!EXTERNALVIEW) {
		ARX_INTERACTIVE_Show_Hide_1st(entities.player(), true);
	}

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdateScripts = std::chrono::steady_clock::now();
#endif
	
	PrepareIOTreatZone();
	ARX_PHYSICS_Apply();
	
	PrecalcIOLighting(g_camera->m_pos, g_camera->cdepth * 0.6f);

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdatePhysicsLighting = std::chrono::steady_clock::now();
#endif
	
	ARX_SCENE_Update();

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdateScene = std::chrono::steady_clock::now();
#endif

	g_particleManager.Update(g_gameTime.lastFrameDuration());

	ARX_FOGS_Render();

	TreatBackgroundActions();

	// Keep directional spell launches frame-local. Tracking loss or a
	// paralysed player must never leave a stale controller ray available.
	arxvr::vrSpellAimService().clear();

	// Checks Magic Flares Drawing
	if(!player.m_paralysed) {
		bool runeDrawPressed = eeMousePressed1();
#if defined(ARXVR_ANDROID_BUILD)
		Vec3f vrRuneHandPosition(0.f);
		Vec3f vrRuneHandDirection(0.f);
		glm::quat vrRuneHandOrientation(1.f, 0.f, 0.f, 0.f);
		const bool vrRuneTracking = g_haveVrCenterCamera
		                         && arxvrGetRightHandWorldPose(
			                         g_vrCenterCamera, player.angle.getYaw(),
			                         vrRuneHandPosition, vrRuneHandDirection,
			                         vrRuneHandOrientation);
		arxvr::VrSpellAimSample vrSpellAimSample;
		vrSpellAimSample.origin = {
			vrRuneHandPosition.x, vrRuneHandPosition.y, vrRuneHandPosition.z
		};
		vrSpellAimSample.forward = {
			vrRuneHandDirection.x, vrRuneHandDirection.y, vrRuneHandDirection.z
		};
		vrSpellAimSample.trackingValid = vrRuneTracking;
		arxvr::vrSpellAimService().update(vrSpellAimSample);
		(void)vrRuneHandOrientation;

		arxvr::VrRuneSample vrRuneSample;
		vrRuneSample.timestampUs = vrRuneTimestampUs();
		vrRuneSample.handPosition = vrRuneVector(vrRuneHandPosition);
		vrRuneSample.trackingValid = vrRuneTracking;
		vrRuneSample.paintPressed = arxvrIsRuneDrawing();
		const arxvr::VrRuneRuntimeResult vrRune = g_vrRuneRuntime.update(
			vrRuneSample, vrRuneDrawingPlane());

		// The legacy flare renderer still consumes DANAEMouse, but the point now
		// comes from the paint-down-locked physical plane rather than an HMD-
		// relative projection. Repeated filtered points simply leave it stable.
		if(vrRune.livePointValid) {
			Vec2s vrRunePoint;
			if(vrRuneScreenPoint(vrRune.livePoint, vrRunePoint)) {
				DANAEMouse = vrRunePoint;
			}
		}
		runeDrawPressed = runeDrawPressed
		               || vrRune.status == arxvr::VrRuneStatus::Capturing;

		if(vrRune.strokeEnded) {
			// Discard all frame-rate-dependent intermediate points. A qualified
			// physical gesture is replayed from VrRuneSystem's filtered path so the
			// existing Arx recognizer, spell prerequisites and scripts stay intact.
			spellRecognitionPointsReset();
			if(vrRune.gestureReady) {
				for(const arxvr::VrRunePoint2 & point : vrRune.gesture.points) {
					Vec2s mapped;
					if(vrRuneScreenPoint(point, mapped)) {
						ARX_SPELLS_AddPoint(mapped);
					}
				}
			}

			if(!vrRune.cancelled) {
				g_vrRuneSymbolsBefore = SpellSymbol;
				g_vrRuneFeedbackPending = true;
				g_vrRuneFeedbackPointCount = vrRune.gestureReady
				                               ? vrRune.gesture.points.size() : 0;
				g_vrRuneFeedbackPathLength = vrRune.gestureReady
				                               ? vrRune.gesture.pathLength : 0.f;
				g_vrRuneFeedbackDurationUs = vrRune.gestureReady
				                              ? vrRune.gesture.durationUs : 0;
			}
		}
#endif
		if(runeDrawPressed) {
			if(!ARX_FLARES_Block) {
				static PlatformDuration runeDrawPointElapsed = 0;
				if(!config.input.useAltRuneRecognition) {
					runeDrawPointElapsed += g_platformTime.lastFrameDuration();
					
					if(runeDrawPointElapsed >= runeDrawPointInterval) {
						ARX_SPELLS_AddPoint(DANAEMouse);
						while(runeDrawPointElapsed >= runeDrawPointInterval) {
							runeDrawPointElapsed -= runeDrawPointInterval;
						}
					}
				} else {
					ARX_SPELLS_AddPoint(DANAEMouse);
				}
			} else {
				spellRecognitionPointsReset();
				ARX_FLARES_Block = false;
			}
		} else if(!ARX_FLARES_Block) {
			ARX_FLARES_Block = true;
		}
	}

	ARX_SPELLS_Precast_Check();
	
	if(ARXmenu.mode() == Mode_InGame) {
		ARX_SPELLS_ManageMagic();
#if defined(ARXVR_ANDROID_BUILD)
		if(g_vrRuneFeedbackPending) {
			const bool accepted = SpellSymbol != g_vrRuneSymbolsBefore;
			arxvrEmitHaptic(VrHapticHand::Right,
			                 accepted ? VrHapticEvent::RuneAccepted
			                          : VrHapticEvent::RuneRejected,
			                 accepted ? 0.75f : 0.45f);
			LogInfo << "ArxVR rune: accepted=" << accepted
			        << " points=" << g_vrRuneFeedbackPointCount
			        << " path=" << g_vrRuneFeedbackPathLength
			        << " durationUs=" << g_vrRuneFeedbackDurationUs;
			g_vrRuneFeedbackPending = false;
		}
#endif
	}
	
	ARX_SPELLS_UpdateSymbolDraw();

	ManageTorch();
	
	{
		
		g_playerCamera.setFov(glm::radians(config.video.fov));
		
		Spell * spell = spells.getSpellByCaster(EntityHandle_Player, SPELL_MAGIC_SIGHT);
		if(spell) {
			GameDuration duration = g_gameTime.now() - spell->m_timcreation;
			g_playerCamera.focal -= 30.f * glm::clamp(duration / 500ms, 0.f, 1.f);
		}
		
		g_playerCamera.focal += 177.5f * player.m_bowAimRatio;
		
	}
	
	ARX_INTERACTIVE_DestroyIOdelayedExecute();

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrUpdateEnd = std::chrono::steady_clock::now();
	static double vrPlayerMs = 0.0;
	static double vrScriptsMs = 0.0;
	static double vrPhysicsLightingMs = 0.0;
	static double vrSceneMs = 0.0;
	static double vrEffectsMs = 0.0;
	static unsigned vrUpdateSamples = 0;
	vrPlayerMs += std::chrono::duration<double, std::milli>(
		vrUpdatePlayer - vrUpdateStart).count();
	vrScriptsMs += std::chrono::duration<double, std::milli>(
		vrUpdateScripts - vrUpdatePlayer).count();
	vrPhysicsLightingMs += std::chrono::duration<double, std::milli>(
		vrUpdatePhysicsLighting - vrUpdateScripts).count();
	vrSceneMs += std::chrono::duration<double, std::milli>(
		vrUpdateScene - vrUpdatePhysicsLighting).count();
	vrEffectsMs += std::chrono::duration<double, std::milli>(
		vrUpdateEnd - vrUpdateScene).count();
	++vrUpdateSamples;
	if(vrUpdateSamples >= 720u) {
		LogInfo << "ARXVR_UPDATE_PERF player=" << (vrPlayerMs / vrUpdateSamples)
		        << "ms scripts=" << (vrScriptsMs / vrUpdateSamples)
		        << "ms physics-light=" << (vrPhysicsLightingMs / vrUpdateSamples)
		        << "ms scene=" << (vrSceneMs / vrUpdateSamples)
		        << "ms effects=" << (vrEffectsMs / vrUpdateSamples);
		vrPlayerMs = vrScriptsMs = vrPhysicsLightingMs = vrSceneMs = vrEffectsMs = 0.0;
		vrUpdateSamples = 0;
	}
#endif
}

void ArxGame::renderLevel(bool secondaryVrEye) {
	
	ARX_PROFILE_FUNC();

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderStart = std::chrono::steady_clock::now();
	const bool vrMinimalOverlay = shouldRenderVrStereo()
	                           && !(player.Interface & INTER_PLAYERBOOK)
	                           && !cinematicBorder.isActive();
#endif
	
	// Clear screen & Z buffers
	GRenderer->Clear(Renderer::ColorBuffer | Renderer::DepthBuffer, g_fogColor);
	
	cinematicBorder.render();
	
	GRenderer->SetAntialiasing(true);
	
	GRenderer->SetFogParams(fZFogStart * g_camera->cdepth, fZFogEnd * g_camera->cdepth);
	GRenderer->SetFogColor(g_fogColor);
	
	ARX_SCENE_Render();

#if defined(ARXVR_ANDROID_BUILD)
	// Deferred transparent scenery is disproportionately expensive through
	// GL4ES. Drop that queue before adding particles/runes/spells below, so the
	// interactive magic remains visible while low-value ambient translucency is
	// omitted from the standalone VR fast path.
	if(vrMinimalOverlay) {
		g_renderBatcher.clear();
	}
	const auto vrRenderScene = std::chrono::steady_clock::now();
#endif
	
	drawDebugRender();

	// Begin Particles
#if defined(ARXVR_ANDROID_BUILD)
	if(!vrMinimalOverlay || spells.begin() != spells.end()) {
		g_particleManager.Render();
	}
#else
	g_particleManager.Render();
#endif

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderParticles = std::chrono::steady_clock::now();
#endif
	
	if(!secondaryVrEye) {
		ARX_PARTICLES_Update();
		ParticleSparkUpdate();
	}
	
	// End Particles

	// Renders Magical Flares
	if(!secondaryVrEye
	   && !((player.Interface & INTER_PLAYERBOOK) && !(player.Interface & INTER_COMBATMODE))) {
		ARX_MAGICAL_FLARES_Update();
	}

	// Checks some specific spell FX
	if(!secondaryVrEye) {
		CheckMr();
	}

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderEffectUpdates = std::chrono::steady_clock::now();
#endif

	if(player.m_improve) {
		DrawImproveVisionInterface();
	}
	
	eyeball.drawMagicSightInterface();

	if(player.m_paralysed) {
		UseRenderState state(render2D().blendAdditive());
		EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color::rgb(0.28f, 0.28f, 1.f));
	}

	// Red screen fade for damages.
	if(!secondaryVrEye) {
		ARX_DAMAGE_Show_Hit_Blood();
	}

	// Update spells
	if(!secondaryVrEye) {
		ARX_SPELLS_Update();
	}

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderSpells = std::chrono::steady_clock::now();
#endif

	GRenderer->SetFogColor(Color());

#if defined(ARXVR_ANDROID_BUILD)
	bool vrRenderQueuedEffects = !vrMinimalOverlay || MagicFlareCountNonFlagged() > 0;
	if(vrMinimalOverlay && !vrRenderQueuedEffects) {
		for(const Spell & spell : spells) {
			if(spell.m_caster == EntityHandle_Player || spell.m_target == EntityHandle_Player) {
				vrRenderQueuedEffects = true;
				break;
			}
		}
	}
	if(vrRenderQueuedEffects) {
		g_renderBatcher.render();
	} else {
		g_renderBatcher.clear();
	}
#else
	g_renderBatcher.render();
#endif
	GRenderer->SetFogColor(g_fogColor);

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderEffects = std::chrono::steady_clock::now();
	// The desktop HUD, minimap, cursor and subtitle canvas account for roughly
	// 9-13 ms per frame in GL4ES. They are flat monitor overlays and fight the
	// physical VR presentation, so omit them during unobstructed first-person
	// play. Book/inventory screens take the normal path and remain usable.
	if(vrMinimalOverlay) {
		static double vrMinimalSceneMs = 0.0;
		static double vrMinimalParticlesMs = 0.0;
		static double vrMinimalEffectUpdatesMs = 0.0;
		static double vrMinimalSpellsMs = 0.0;
		static double vrMinimalBatchMs = 0.0;
		static unsigned vrMinimalSamples = 0;
		vrMinimalSceneMs += std::chrono::duration<double, std::milli>(
			vrRenderScene - vrRenderStart).count();
		vrMinimalParticlesMs += std::chrono::duration<double, std::milli>(
			vrRenderParticles - vrRenderScene).count();
		vrMinimalEffectUpdatesMs += std::chrono::duration<double, std::milli>(
			vrRenderEffectUpdates - vrRenderParticles).count();
		vrMinimalSpellsMs += std::chrono::duration<double, std::milli>(
			vrRenderSpells - vrRenderEffectUpdates).count();
		vrMinimalBatchMs += std::chrono::duration<double, std::milli>(
			vrRenderEffects - vrRenderSpells).count();
		++vrMinimalSamples;
		if(vrMinimalSamples >= 144u) {
			LogInfo << "ARXVR_MINIMAL_PERF scene=" << (vrMinimalSceneMs / vrMinimalSamples)
			        << "ms particles=" << (vrMinimalParticlesMs / vrMinimalSamples)
			        << "ms effect-updates=" << (vrMinimalEffectUpdatesMs / vrMinimalSamples)
			        << "ms spells=" << (vrMinimalSpellsMs / vrMinimalSamples)
			        << "ms batch=" << (vrMinimalBatchMs / vrMinimalSamples);
			vrMinimalSceneMs = vrMinimalParticlesMs = vrMinimalEffectUpdatesMs = 0.0;
			vrMinimalSpellsMs = vrMinimalBatchMs = 0.0;
			vrMinimalSamples = 0;
		}
		GRenderer->SetAntialiasing(false);
		g_renderBatcher.clear();
		renderVrInteractionPrompt();
		if(!secondaryVrEye) {
			updateLightFlares();
			ARX_PLAYER_Manage_Death();
			notification_check();
			if(pTextManage && !pTextManage->Empty()) {
				pTextManage->Update(g_platformTime.lastFrameDuration());
			}
			if(FADEDIR) {
				ManageFade();
			}
		}
		ARX_SPEECH_Update(!secondaryVrEye);
		return;
	}
#endif
	
	GRenderer->SetAntialiasing(false);

	if(!secondaryVrEye) {
		updateLightFlares();
	}
	renderLightFlares();
	
	// Manage Death visual & Launch menu...
	if(!secondaryVrEye) {
		ARX_PLAYER_Manage_Death();
	}

	// INTERFACE
	g_renderBatcher.clear();
	
	// Draw game interface if needed
	if(ARXmenu.mode() == Mode_InGame && !cinematicBorder.isActive()) {
	
		UseTextureState textureState(TextureStage::FilterLinear, TextureStage::WrapClamp);
		
		ARX_INTERFACE_NoteManage();
		g_hudRoot.draw();
		
		if(!secondaryVrEye
		   && (player.Interface & INTER_PLAYERBOOK) && !(player.Interface & INTER_COMBATMODE)) {
			ARX_MAGICAL_FLARES_Update();
			g_renderBatcher.render();
		}
		
	}

	GRenderer->Clear(Renderer::DepthBuffer);

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderHud = std::chrono::steady_clock::now();
#endif

	// Speech Management
	if(!secondaryVrEye) {
		notification_check();
	}

	if(pTextManage && !pTextManage->Empty()) {
		if(!secondaryVrEye) {
			pTextManage->Update(g_platformTime.lastFrameDuration());
		}
		pTextManage->Render();
	}
	
	if(SHOW_INGAME_MINIMAP &&
	   cinematicIsStopped() &&
	   !cinematicBorder.isActive() &&
	   !BLOCK_PLAYER_CONTROLS &&
	   !(player.Interface & INTER_PLAYERBOOK)) {
		g_miniMap.showPlayerMiniMap(getMapLevelForArea(g_currentArea));
	}

#if defined(ARXVR_ANDROID_BUILD)
	renderVrInteractionPrompt();
#endif
	
	ARX_INTERFACE_RenderCursor(false);
	
	CheatDrawText();

	if(FADEDIR)
		ManageFade();
	
	GRenderer->SetScissor(Rect());
	
	ARX_SPEECH_Update(!secondaryVrEye);

#if defined(ARXVR_ANDROID_BUILD)
	const auto vrRenderEnd = std::chrono::steady_clock::now();
	static double vrSceneAndSetupMs = 0.0;
	static double vrEffectsMs = 0.0;
	static double vrHudMs = 0.0;
	static double vrTextMs = 0.0;
	static unsigned vrRenderSamples = 0;
	vrSceneAndSetupMs += std::chrono::duration<double, std::milli>(
		vrRenderScene - vrRenderStart).count();
	vrEffectsMs += std::chrono::duration<double, std::milli>(
		vrRenderEffects - vrRenderScene).count();
	vrHudMs += std::chrono::duration<double, std::milli>(
		vrRenderHud - vrRenderEffects).count();
	vrTextMs += std::chrono::duration<double, std::milli>(
		vrRenderEnd - vrRenderHud).count();
	++vrRenderSamples;
	if(vrRenderSamples >= 720u) {
		LogInfo << "ARXVR_RENDER_PERF scene=" << (vrSceneAndSetupMs / vrRenderSamples)
		        << "ms effects=" << (vrEffectsMs / vrRenderSamples)
		        << "ms hud=" << (vrHudMs / vrRenderSamples)
		        << "ms text=" << (vrTextMs / vrRenderSamples);
		vrSceneAndSetupMs = vrEffectsMs = vrHudMs = vrTextMs = 0.0;
		vrRenderSamples = 0;
	}
#endif
	
}

void ArxGame::render() {
	
	ARX_PROFILE_FUNC();
	
	SetActiveCamera(&g_playerCamera);
	
	// Update Various Player Infos for this frame.
	ARX_PLAYER_Frame_Update();
	
	PULSATE = timeWaveSin(g_gameTime.now(), 1600ms * glm::pi<float>());
	EERIEDrawnPolys = 0;
	
	// Checks for Keyboard & Moulinex
	{
		g_cursorOverBook = false;
		
		if(ARXmenu.mode() == Mode_InGame) { // Playing Game
			// Checks Clicks in Book Interface
			if(ARX_INTERFACE_MouseInBook()) {
				g_cursorOverBook = true;
			}
		}
		
		if((player.Interface & INTER_COMBATMODE) || PLAYER_MOUSELOOK_ON) {
			FlyingOverIO = nullptr; // Avoid to check with those modes
		} else {
			if(!BLOCK_PLAYER_CONTROLS
				&& !TRUE_PLAYER_MOUSELOOK_ON
				&& !g_cursorOverBook
				&& eMouseState != MOUSE_IN_NOTE
			) {
				FlyingOverIO = FlyingOverObject(DANAEMouse);
			} else {
				FlyingOverIO = nullptr;
			}
		}

#if defined(ARXVR_ANDROID_BUILD)
		// Mouse-look intentionally clears FlyingOverIO. Restore the controller-ray
		// target so Arx's cursor/name UI and the world highlight agree in VR.
		if(Entity * vrTarget = currentVrInteractionTarget()) {
			FlyingOverIO = vrTarget;
		}
#endif
		
		if(!player.m_paralysed || ARXmenu.mode() != Mode_InGame) {
			manageKeyMouse();
		}
	}
	
	if(CheckInPoly(player.pos)) {
		LastValidPlayerPos = player.pos;
	}
	
	// Updates Externalview
	EXTERNALVIEW = false;
	
	if(ARXmenu.mode() != Mode_MainMenu) {
		Menu2_Close();
	}
	
	if(ARXmenu.mode() != Mode_InGame) {
		benchmark::begin(benchmark::Menu);
		ARX_Menu_Render();
	} else if(isInCinematic()) {
		benchmark::begin(benchmark::Cinematic);
		cinematicRender();
	} else {
		benchmark::begin(cinematicBorder.CINEMA_DECAL != 0.f ? benchmark::Cutscene : benchmark::Scene);
		updateLevel();
		renderLevel();
		#ifdef ARX_DEBUG
		if(g_debugToggles[9]) {
			renderLevel();
		}
		#endif
	}
	
	if(g_debugInfo != InfoPanelNone) {
		switch(g_debugInfo) {
		case InfoPanelFramerate: {
			g_fpsCounter.CalcFPS();
			ShowFPS();
			break;
		}
		case InfoPanelFramerateGraph: {
			ShowFrameDurationPlot();
			break;
		}
		case InfoPanelDebug: {
			ShowInfoText();
			break;
		}
		case InfoPanelAudio: {
			debugHud_Audio();
			break;
		}
		case InfoPanelCulling: {
			debugHud_Culling();
			break;
		}
		default: break;
		}
	}
	
#ifdef ARX_DEBUG
	ShowDebugToggles();
#endif
	
	g_console.draw();
	
	if(ARXmenu.mode() == Mode_InGame) {
		ARX_SCRIPT_AllowInterScriptExec();
		ARX_SCRIPT_EventStackExecute();
		// Updates Damages Spheres
		ARX_DAMAGES_UpdateAll();
		ARX_MISSILES_Update();

		ARX_PATH_UpdateAllZoneInOutInside();
	}

	LastMouseClick = EERIEMouseButton;
	
	gldebug::endFrame();
}

void ArxGame::onRendererInit(Renderer & renderer) {
	
	arx_assert(GRenderer == nullptr);
	
	GRenderer = &renderer;
	
	arx_assert_msg(renderer.getTextureStageCount() >= 3, "not enough texture units");
	arx_assert(m_MainWindow);
	
	renderer.Clear(Renderer::ColorBuffer);
	m_MainWindow->showFrame();
	
	// Restore All Textures RenderState
	renderer.RestoreAllTextures();

	ARX_PLAYER_Restore_Skin();
	
	// Fog
	float fogEnd = 0.48f;
	float fogStart = fogEnd * 0.65f;
	renderer.SetFogParams(fogStart, fogEnd);
	renderer.SetFogColor(g_fogColor);
	
	ComputePortalVertexBuffer();
	std::unique_ptr<VertexBuffer<SMY_VERTEX3>> vb3 = renderer.createVertexBuffer3(4000, Renderer::Stream);
	pDynamicVertexBuffer = new CircularVertexBuffer<SMY_VERTEX3>(std::move(vb3));
	
	size_t size = (config.video.bufferSize < 1) ? 32 * 1024 : config.video.bufferSize * 1024;
	std::unique_ptr<VertexBuffer<TexturedVertex>> vb = renderer.createVertexBufferTL(size, Renderer::Stream);
	pDynamicVertexBuffer_TLVERTEX = new CircularVertexBuffer<TexturedVertex>(std::move(vb));
	
	MenuReInitAll();
	
	// The app is ready to go
	m_bReady = true;
}

void ArxGame::onRendererShutdown(Renderer & renderer) {
	
	if(GRenderer != &renderer) {
		// onRendererInit() failed
		return;
	}
	
	m_bReady = false;
	
	GRenderer->ReleaseAllTextures();

	delete pDynamicVertexBuffer_TLVERTEX, pDynamicVertexBuffer_TLVERTEX = nullptr;
	delete pDynamicVertexBuffer, pDynamicVertexBuffer = nullptr;
	
	EERIE_PORTAL_ReleaseOnlyVertexBuffer();
	
	GRenderer = nullptr;
}
