/*
 * Copyright 2013-2022 Arx Libertatis Team (see the AUTHORS file)
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

#include "gui/Dragging.h"

#include <array>
#include <cmath>
#include <limits>

#include "core/Core.h"
#include "core/GameTime.h"
#include "game/Camera.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/Player.h"
#include "graphics/Math.h"
#include "gui/Hud.h"
#include "gui/Interface.h"
#include "gui/book/Book.h"
#include "gui/hud/PlayerInventory.h"
#include "gui/hud/SecondaryInventory.h"
#include "input/Input.h"
#include "io/log/Logger.h"
#include "math/Angle.h"
#include "math/GtxFunctions.h"
#include "math/RandomVector.h"
#include "math/Vector.h"
#include "physics/Collisions.h"
#include "physics/Physics.h"
#include "platform/Platform.h"
#include "platform/profiler/Profiler.h"
#include "scene/GameSound.h"
#include "scene/Interactive.h"

EntityDragStatus g_dragStatus = EntityDragStatus_Invalid;
Entity * g_draggedEntity = nullptr;
InventoryPos g_draggedItemPreviousPosition;
Vec2f g_draggedIconOffset;
static Vec2f g_draggedObjectOffset;
static float g_dragStartAngle = 0;
static Camera * g_dragStartCamera = nullptr;
#if defined(ARXVR_ANDROID_BUILD)
static Entity * g_vrPhysicalDragEntity = nullptr;
static bool g_vrPhysicalGripHeld = false;
static Vec3f g_vrPhysicalHandPosition(0.f);
static Vec3f g_vrPhysicalPreviousHandPosition(0.f);
static Vec3f g_vrPhysicalHandDirection(0.f, 0.f, 1.f);
static glm::quat g_vrPhysicalHandOrientation(1.f, 0.f, 0.f, 0.f);
static glm::quat g_vrPhysicalObjectRotationOffset(1.f, 0.f, 0.f, 0.f);
static Vec3f g_vrPhysicalCurrentVelocity(0.f);
static Vec3f g_vrPhysicalReleaseVelocity(0.f);
static std::array<Vec3f, 6> g_vrPhysicalVelocityHistory;
static size_t g_vrPhysicalVelocityHistoryIndex = 0;
static size_t g_vrPhysicalVelocityHistoryCount = 0;
static Vec3f g_vrPhysicalObjectOffset(0.f);
static Vec3f g_vrPhysicalGrabPointLocal(0.f);
static Vec3f g_vrPhysicalGripHandLocalOffset(0.f);
static Vec3f g_vrPhysicalObjectStartPosition(0.f);
static bool g_vrPhysicalMovementConfirmed = false;
static unsigned g_vrPhysicalGripReleaseFrames = 0;
static bool g_vrPhysicalRightHand = true;
static std::array<float, 5> g_vrPhysicalFingerCurls = { 1.f, 1.f, 1.f, 1.f, 1.f };
static float g_vrPhysicalGripDiameter = 18.f;
static float g_vrPhysicalGripSpan = 18.f;
static float g_vrPhysicalPalmHoldDistance = 6.f;
constexpr unsigned kVrGripReleaseDebounceFrames = 3;
constexpr float kVrThrowSpeedThreshold = 110.f;
#endif
#ifdef ANDROID
bool allowDrop = false;
static bool screenControlsHided = false;
#endif

void setDraggedEntity(Entity * entity) {
#ifdef ANDROID   
    allowDrop = false;
#endif    
	if(entity != g_draggedEntity) {
		g_dragStartCamera = g_camera;
		g_dragStartAngle = g_camera->angle.getYaw();
	}
	
	if(entity) {
		g_draggedItemPreviousPosition = locateInInventories(entity);
		entity->setOwner(nullptr);
		if(entity->obj && entity->show == SHOW_FLAG_IN_SCENE && (entity->gameFlags & GFLAG_ISINTREATZONE)
		   && !(entity->gameFlags & (GFLAG_INVISIBILITY | GFLAG_MEGAHIDE))) {
			EERIE_3D_BBOX bbox;
			for(const EERIE_VERTEX & vertex : entity->obj->vertexlist) {
				bbox.add(vertex.v);
			}
			Vec3f center = bbox.min + (bbox.max - bbox.min) * Vec3f(0.5f);
			Anglef angle = entity->angle;
			angle.setYaw(270.f - angle.getYaw());
			center = toQuaternion(angle) * center;
			center.y = 0.f;
			Vec4f p = worldToClipSpace(entity->pos + center);
			if(p.w > 0.f) {
				Vec2f pos = Vec2f(p) / p.w;
				if(g_size.contains(Rect::Vec2(pos))) {
					g_draggedIconOffset = Vec2f(0.f);
					g_draggedObjectOffset = pos - Vec2f(DANAEMouse);
				}
			}
		}
		entity->show = g_draggedItemPreviousPosition ? SHOW_FLAG_ON_PLAYER : SHOW_FLAG_IN_SCENE;
	} else {
		g_draggedItemPreviousPosition = InventoryPos();
		g_draggedIconOffset = Vec2f(0.f);
		g_draggedObjectOffset = Vec2f(0.f);
	}
	
	g_draggedEntity = entity;
	
	if(entity && entity->obj && entity->obj->pbox) {
		entity->obj->pbox->active = 0;
	}
	
}

#if defined(ARXVR_ANDROID_BUILD)
static glm::quat vrPhysicalObjectRenderRotation(const Entity & entity) {
	Anglef renderAngle = entity.angle;
	renderAngle.setYaw(270.f - renderAngle.getYaw());
	return glm::normalize(toQuaternion(renderAngle));
}

static Anglef vrPhysicalRenderRotationToEntityAngle(const glm::quat & rotation) {
	// toAngle() is not the inverse of toQuaternion(): it contains the legacy
	// extra 90-degree sprite convention. Interactive.cpp renders ordinary
	// objects with Rz(-roll) * Rx(pitch) * Ry(yaw), so decompose that exact
	// order here. The old conversion made the visible mesh rotate differently
	// from the quaternion used to position its grab point. The resulting lever
	// arm is why an object swam away when the user extended or turned a hand.
	const glm::mat3 matrix = glm::mat3_cast(glm::normalize(rotation));
	const float sinPitch = glm::clamp(matrix[1][2], -1.f, 1.f);
	const float pitch = std::asin(sinPitch);
	const float cosPitch = std::cos(pitch);

	float yaw = 0.f;
	float roll = 0.f;
	if(glm::abs(cosPitch) > 0.0001f) {
		yaw = std::atan2(-matrix[0][2], matrix[2][2]);
		roll = std::atan2(matrix[1][0], matrix[1][1]);
	} else {
		// At the X-axis gimbal lock yaw and roll are coupled. Preserve a stable
		// representative instead of allowing tiny tracking noise to flip them.
		yaw = std::atan2(matrix[2][0], matrix[0][0]);
		roll = 0.f;
	}

	Anglef renderAngle;
	renderAngle.setPitch(glm::degrees(pitch));
	renderAngle.setYaw(glm::degrees(yaw));
	renderAngle.setRoll(glm::degrees(roll));
	Anglef entityAngle = renderAngle;
	entityAngle.setYaw(270.f - renderAngle.getYaw());
	return entityAngle;
}

static Vec3f closestPointOnVrTriangle(const Vec3f & point, const Vec3f & a,
                                      const Vec3f & b, const Vec3f & c) {
	const Vec3f ab = b - a;
	const Vec3f ac = c - a;
	const Vec3f ap = point - a;
	const float d1 = glm::dot(ab, ap);
	const float d2 = glm::dot(ac, ap);
	if(d1 <= 0.f && d2 <= 0.f) {
		return a;
	}

	const Vec3f bp = point - b;
	const float d3 = glm::dot(ab, bp);
	const float d4 = glm::dot(ac, bp);
	if(d3 >= 0.f && d4 <= d3) {
		return b;
	}

	const float vc = d1 * d4 - d3 * d2;
	if(vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
		return a + ab * (d1 / (d1 - d3));
	}

	const Vec3f cp = point - c;
	const float d5 = glm::dot(ab, cp);
	const float d6 = glm::dot(ac, cp);
	if(d6 >= 0.f && d5 <= d6) {
		return c;
	}

	const float vb = d5 * d2 - d1 * d6;
	if(vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
		return a + ac * (d2 / (d2 - d6));
	}

	const float va = d3 * d6 - d5 * d4;
	if(va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
		const Vec3f bc = c - b;
		return b + bc * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	}

	const float denominator = 1.f / (va + vb + vc);
	const float v = vb * denominator;
	const float w = vc * denominator;
	return a + ab * v + ac * w;
}

static glm::quat vrPhysicalRotationBetween(const Vec3f & fromVector,
                                           const Vec3f & toVector) {
	const Vec3f from = glm::normalize(fromVector);
	const Vec3f to = glm::normalize(toVector);
	const float cosine = glm::clamp(glm::dot(from, to), -1.f, 1.f);
	if(cosine > 0.9999f) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}
	if(cosine < -0.9999f) {
		Vec3f helper = glm::abs(from.x) < 0.8f ? Vec3f(1.f, 0.f, 0.f)
		                                          : Vec3f(0.f, 1.f, 0.f);
		const Vec3f axis = glm::normalize(glm::cross(from, helper));
		return glm::angleAxis(glm::radians(180.f), axis);
	}
	const Vec3f axis = glm::cross(from, to);
	return glm::normalize(glm::quat(1.f + cosine, axis.x, axis.y, axis.z));
}

static void setVrPhysicalObjectRenderRotation(Entity & entity,
                                               const glm::quat & rotation) {
	entity.angle = vrPhysicalRenderRotationToEntityAngle(rotation);
}

static void addVrPhysicalVelocitySample(const Vec3f & velocity) {
	// Reject an isolated tracking-space discontinuity while retaining fast human
	// throws. Arx units are centimetres, so this still allows 12 m/s gestures.
	if(!isallfinite(velocity) || glm::length(velocity) > 1200.f) {
		return;
	}
	g_vrPhysicalVelocityHistory[g_vrPhysicalVelocityHistoryIndex] = velocity;
	g_vrPhysicalVelocityHistoryIndex = (g_vrPhysicalVelocityHistoryIndex + 1)
	                                 % g_vrPhysicalVelocityHistory.size();
	g_vrPhysicalVelocityHistoryCount = std::min(g_vrPhysicalVelocityHistoryCount + 1,
	                                           g_vrPhysicalVelocityHistory.size());
}

static Vec3f recentVrPhysicalThrowVelocity() {
	Vec3f best = g_vrPhysicalReleaseVelocity;
	for(size_t i = 0; i < g_vrPhysicalVelocityHistoryCount; ++i) {
		const Vec3f & sample = g_vrPhysicalVelocityHistory[i];
		if(glm::dot(sample, sample) > glm::dot(best, best)) {
			best = sample;
		}
	}
	return best;
}

void beginVrPhysicalDrag(Entity * entity, const Vec3f & handPosition,
                         const Vec3f & handDirection,
                         const glm::quat & handOrientation, bool rightHand) {
	if(!entity || !entity->obj || (entity->ioflags & (IO_NPC | IO_FIX))) {
		return;
	}

	setDraggedEntity(entity);
	g_vrPhysicalDragEntity = entity;
	g_vrPhysicalGripHeld = true;
	g_vrPhysicalHandPosition = handPosition;
	g_vrPhysicalPreviousHandPosition = handPosition;
	g_vrPhysicalHandDirection = handDirection;
	g_vrPhysicalHandOrientation = glm::normalize(handOrientation);
	g_vrPhysicalRightHand = rightHand;
	const glm::quat initialRenderRotation = vrPhysicalObjectRenderRotation(*entity);
	g_vrPhysicalCurrentVelocity = Vec3f(0.f);
	g_vrPhysicalReleaseVelocity = Vec3f(0.f);
	g_vrPhysicalVelocityHistory.fill(Vec3f(0.f));
	g_vrPhysicalVelocityHistoryIndex = 0;
	g_vrPhysicalVelocityHistoryCount = 0;
	g_vrPhysicalGripReleaseFrames = 0;
	g_vrPhysicalObjectStartPosition = entity->pos;
	// Attach the point on the actual mesh nearest to the hand. Snapping the
	// object's bounding-box centre to the controller made a chair float around
	// its seat and put small props visibly beside the rendered palm.
	float nearestDistanceSquared = std::numeric_limits<float>::max();
	g_vrPhysicalGrabPointLocal = Vec3f(0.f);
	const EERIE_FACE * nearestFace = nullptr;
	const float inverseScale = glm::abs(entity->scale) > 0.0001f
	                         ? 1.f / entity->scale : 1.f;
	const Vec3f localHandPosition = glm::conjugate(initialRenderRotation)
	                              * (handPosition - entity->pos) * inverseScale;
	for(const EERIE_FACE & face : entity->obj->facelist) {
		const Vec3f candidate = closestPointOnVrTriangle(
			localHandPosition,
			entity->obj->vertexlist[face.vid[0]].v,
			entity->obj->vertexlist[face.vid[1]].v,
			entity->obj->vertexlist[face.vid[2]].v);
		const Vec3f handDelta = candidate - localHandPosition;
		const float distanceSquared = glm::dot(handDelta, handDelta);
		if(distanceSquared < nearestDistanceSquared) {
			nearestDistanceSquared = distanceSquared;
			g_vrPhysicalGrabPointLocal = candidate;
			nearestFace = &face;
		}
	}
	if(entity->obj->facelist.empty() && !entity->obj->vertexlist.empty()) {
		g_vrPhysicalGrabPointLocal = entity->obj->vertexlist.front().v;
	}

	// Derive a hand pose from the mesh around the actual touched point, not from
	// an item-name table or the object's full bounds. This matters for compound
	// props: a chair may be taller than a metre but a hand normally contacts a
	// thin leg, rail or seat edge. Each finger receives its own closure amount.
	EERIE_3D_BBOX fullGripBounds;
	EERIE_3D_BBOX localGripBounds;
	EERIE_3D_BBOX tightGripBounds;
	fullGripBounds.reset();
	localGripBounds.reset();
	tightGripBounds.reset();
	unsigned localGripVertices = 0;
	unsigned tightGripVertices = 0;
	const float absoluteScale = std::max(glm::abs(entity->scale), 0.0001f);
	const float localNeighbourRadius = 38.f / absoluteScale;
	const float tightNeighbourRadius = 20.f / absoluteScale;
	// Seed the tight section with the contacted triangle. Low-poly chair legs
	// often have vertices only at their two ends, so a radius-only vertex query
	// at the middle of the leg otherwise finds nothing and falls back to the
	// entire chair bounds.
	if(nearestFace) {
		for(VertexId vertex : nearestFace->vid) {
			tightGripBounds.add(entity->obj->vertexlist[vertex].v);
			++tightGripVertices;
		}
	}
	for(const EERIE_VERTEX & vertex : entity->obj->vertexlist) {
		fullGripBounds.add(vertex.v);
		const float distance = glm::distance(vertex.v, g_vrPhysicalGrabPointLocal);
		if(distance <= localNeighbourRadius) {
			localGripBounds.add(vertex.v);
			++localGripVertices;
		}
		if(distance <= tightNeighbourRadius) {
			tightGripBounds.add(vertex.v);
			++tightGripVertices;
		}
	}
	const Vec3f fullGripSize = fullGripBounds.valid()
	                         ? (fullGripBounds.max - fullGripBounds.min) * absoluteScale
	                         : Vec3f(0.f);
	const float fullMaximum = std::max({ fullGripSize.x, fullGripSize.y,
	                                     fullGripSize.z });
	// On large compound props a 38 cm sphere can include the chair seat and a
	// second rail even when the palm is touching one thin leg. Use the tighter
	// local section only for such props; small rounded objects (notably skulls)
	// retain the already validated whole-surface profile.
	const bool useTightGrip = fullMaximum > 80.f && tightGripVertices >= 3
	                       && tightGripBounds.valid();
	const EERIE_3D_BBOX & gripBounds = useTightGrip
	                                    ? tightGripBounds
	                                    : (localGripVertices >= 4 && localGripBounds.valid()
	                                       ? localGripBounds : fullGripBounds);
	Vec3f gripSize = gripBounds.valid()
	               ? (gripBounds.max - gripBounds.min) * absoluteScale
	               : Vec3f(18.f);
	std::array<float, 3> dimensions = {
		std::max(gripSize.x, 1.f), std::max(gripSize.y, 1.f), std::max(gripSize.z, 1.f)
	};
	std::sort(dimensions.begin(), dimensions.end());
	const float thin = dimensions[0];
	const float middle = dimensions[1];
	g_vrPhysicalGripSpan = dimensions[2];
	// Geometric mean prevents a broad but thin seat edge from being mistaken for
	// an impossible 60 cm cylindrical grip, while retaining a skull's bulk and
	// a bone's narrow shaft.
	g_vrPhysicalGripDiameter = std::sqrt(thin * std::min(middle, 38.f));

	std::array<float, 3> fullDimensions = {
		std::max(fullGripSize.x, 1.f), std::max(fullGripSize.y, 1.f),
		std::max(fullGripSize.z, 1.f)
	};
	std::array<float, 3> sortedFullDimensions = fullDimensions;
	std::sort(sortedFullDimensions.begin(), sortedFullDimensions.end());
	size_t fullLongAxis = 0;
	if(fullDimensions[1] > fullDimensions[fullLongAxis]) { fullLongAxis = 1; }
	if(fullDimensions[2] > fullDimensions[fullLongAxis]) { fullLongAxis = 2; }
	const bool alignAsHandTool = fullGripBounds.valid()
	                          && sortedFullDimensions[2] >= sortedFullDimensions[1] * 2.2f
	                          && sortedFullDimensions[2] >= 35.f
	                          && sortedFullDimensions[2] <= 110.f;

	std::array<float, 3> gripDimensions = {
		std::max(gripSize.x, 1.f), std::max(gripSize.y, 1.f),
		std::max(gripSize.z, 1.f)
	};
	size_t localLongAxis = 0;
	if(gripDimensions[1] > gripDimensions[localLongAxis]) { localLongAxis = 1; }
	if(gripDimensions[2] > gripDimensions[localLongAxis]) { localLongAxis = 2; }
	std::array<float, 3> sortedGripDimensions = gripDimensions;
	std::sort(sortedGripDimensions.begin(), sortedGripDimensions.end());
	const bool localShaftGrip = useTightGrip
	                         && sortedGripDimensions[2] >= sortedGripDimensions[1] * 1.55f
	                         && sortedGripDimensions[1] <= 20.f;

	Vec3f localForward(0.f);
	if(alignAsHandTool) {
		// A club/sword grip is on the centreline, inset from the nearer end. The
		// previous nearest-surface anchor put the whole bone beside the fingers.
		const float localLength = fullGripBounds.max[fullLongAxis]
		                        - fullGripBounds.min[fullLongAxis];
		const bool nearerMinimum = glm::abs(g_vrPhysicalGrabPointLocal[fullLongAxis]
		                                     - fullGripBounds.min[fullLongAxis])
		                         <= glm::abs(fullGripBounds.max[fullLongAxis]
		                                     - g_vrPhysicalGrabPointLocal[fullLongAxis]);
		const float inset = glm::clamp(localLength * 0.24f,
		                               8.f / absoluteScale, 18.f / absoluteScale);
		g_vrPhysicalGrabPointLocal = (fullGripBounds.min + fullGripBounds.max) * 0.5f;
		g_vrPhysicalGrabPointLocal[fullLongAxis] = nearerMinimum
			? fullGripBounds.min[fullLongAxis] + inset
			: fullGripBounds.max[fullLongAxis] - inset;
		localForward[fullLongAxis] = nearerMinimum ? 1.f : -1.f;
		Vec3f localAim = glm::conjugate(g_vrPhysicalHandOrientation) * handDirection;
		if(glm::length(localAim) < 0.001f) {
			localAim = Vec3f(0.f, 0.f, 1.f);
		} else {
			localAim = glm::normalize(localAim);
		}
		g_vrPhysicalObjectRotationOffset = vrPhysicalRotationBetween(localForward,
		                                                                localAim);
	} else {
		g_vrPhysicalObjectRotationOffset = glm::conjugate(g_vrPhysicalHandOrientation)
		                                * initialRenderRotation;
		if(localShaftGrip) {
			// For a chair leg or rail, keep the furniture's world rotation but move
			// the attachment from its visible surface to the section centreline.
			const Vec3f localCenter = (gripBounds.min + gripBounds.max) * 0.5f;
			for(size_t axis = 0; axis < 3; ++axis) {
				if(axis != localLongAxis) {
					g_vrPhysicalGrabPointLocal[axis] = localCenter[axis];
				}
			}
		}
	}
	const bool cylindricalGrip = alignAsHandTool || localShaftGrip;
	if(cylindricalGrip) {
		// A controller squeeze is a power grip, not a size-proportional open-hand
		// pose.  The previous diameter curve treated the bulbous ends of a bone
		// (and the coarse triangle around a chair leg) as the actual shaft width,
		// leaving the index and thumb visibly outside the object.  Thin sections
		// must close into the same proven fist pose as a full lower-trigger press;
		// the tailored joint angles in Hand::setGripProfile keep the fingertips
		// hooked around the cylinder instead of clipping straight through it.
		g_vrPhysicalFingerCurls = { 0.98f, 1.f, 1.f, 1.f, 1.f };
	} else {
		const float baseCurl = glm::clamp(1.08f - g_vrPhysicalGripDiameter / 43.f,
		                                  0.28f, 0.96f);
		g_vrPhysicalFingerCurls = {
			glm::clamp(baseCurl - 0.12f, 0.22f, 0.92f), // thumb opposes the fingers
			glm::clamp(baseCurl - 0.06f, 0.24f, 0.96f), // index reaches least deeply
			glm::clamp(baseCurl,         0.26f, 0.98f),
			glm::clamp(baseCurl + 0.05f, 0.28f, 1.00f),
			glm::clamp(baseCurl + 0.10f, 0.30f, 1.00f)  // pinky closes furthest
		};
	}
	// Keep the touched surface just beyond the palm centre. The former fixed 3 cm
	// offset put the entire glove inside skulls and chair seats before the fingers
	// had any chance to wrap around them.
	if(alignAsHandTool) {
		g_vrPhysicalPalmHoldDistance = glm::clamp(
			-6.f + g_vrPhysicalGripDiameter * 0.04f, -5.8f, -4.5f);
	} else if(localShaftGrip) {
		// A furniture rail keeps the chair's authored world rotation, so its near
		// face already projects several centimetres in front of the section
		// centre.  Reusing the tool centre offset put the entire glove behind the
		// timber.  Keep the rail centre just behind the controller origin; its near
		// surface then falls inside the finger/palm depth interval.
		g_vrPhysicalPalmHoldDistance = -0.25f;
	} else {
		g_vrPhysicalPalmHoldDistance = glm::clamp(
			5.5f + g_vrPhysicalGripDiameter * 0.10f, 6.f, 8.5f);
	}
	// The FBX hand origin is the centre of its open-hand bounds. Curling the
	// fingers shifts the visible cylindrical grip pocket sideways from that
	// origin. Put thin shafts into that pocket; leave rounded objects on their
	// already validated palm anchor.
	g_vrPhysicalGripHandLocalOffset = (alignAsHandTool || localShaftGrip)
	                                 ? Vec3f(rightHand ? -5.f : 5.f, 0.f, 0.f)
	                                 : Vec3f(0.f);
	// Derive the translation from the rotation the renderer will actually use,
	// not merely from the requested quaternion. This enforces the grab
	// invariant even at Euler singularities:
	// entity.pos + renderedRotation * localGrabPoint == palm hold point.
	const glm::quat intendedRotation = glm::normalize(g_vrPhysicalHandOrientation
	                                                * g_vrPhysicalObjectRotationOffset);
	setVrPhysicalObjectRenderRotation(*entity, intendedRotation);
	const glm::quat actualInitialRotation = vrPhysicalObjectRenderRotation(*entity);
	g_vrPhysicalObjectOffset = -(actualInitialRotation
	                           * (g_vrPhysicalGrabPointLocal * entity->scale));
	g_vrPhysicalMovementConfirmed = false;
	entity->show = SHOW_FLAG_IN_SCENE;
	g_dragStatus = EntityDragStatus_OnGround;
	const Vec3f holdPosition = handPosition
	                         + handDirection * g_vrPhysicalPalmHoldDistance
	                         + g_vrPhysicalHandOrientation
	                           * g_vrPhysicalGripHandLocalOffset;
	ARX_INTERACTIVE_Teleport(entity, holdPosition + g_vrPhysicalObjectOffset, true);
	LogInfo << "ArxVR physical drag begin: target=" << entity->idString()
	        << " snappedObject=(" << entity->pos.x << ", " << entity->pos.y << ", "
	        << entity->pos.z << ") hand=(" << handPosition.x << ", "
	        << handPosition.y << ", " << handPosition.z << ") gripSize=("
	        << gripSize.x << ", " << gripSize.y << ", " << gripSize.z
	        << ") diameter=" << g_vrPhysicalGripDiameter << " span="
	        << g_vrPhysicalGripSpan << " curls=(" << g_vrPhysicalFingerCurls[0]
	        << ',' << g_vrPhysicalFingerCurls[1] << ',' << g_vrPhysicalFingerCurls[2]
	        << ',' << g_vrPhysicalFingerCurls[3] << ',' << g_vrPhysicalFingerCurls[4]
		        << ") palmDistance=" << g_vrPhysicalPalmHoldDistance
	        << " handLocalOffset=(" << g_vrPhysicalGripHandLocalOffset.x << ','
	        << g_vrPhysicalGripHandLocalOffset.y << ','
	        << g_vrPhysicalGripHandLocalOffset.z << ')'
	        << " longTool=" << alignAsHandTool << " shaftGrip=" << localShaftGrip
	        << " grabLocal=(" << g_vrPhysicalGrabPointLocal.x << ','
	        << g_vrPhysicalGrabPointLocal.y << ',' << g_vrPhysicalGrabPointLocal.z
	        << ") axisAlignment="
	        << (alignAsHandTool
	            ? glm::dot(glm::normalize(actualInitialRotation * localForward),
	                       glm::normalize(handDirection)) : 0.f);
}

void updateVrPhysicalDragPose(const Vec3f & handPosition, const Vec3f & handDirection,
                              const glm::quat & handOrientation, bool gripHeld) {
	if(!g_vrPhysicalDragEntity) {
		return;
	}

	const float frameMs = std::max(1.f, toMsf(g_platformTime.lastFrameDuration()));
	const Vec3f frameVelocity = (handPosition - g_vrPhysicalPreviousHandPosition)
	                          * (1000.f / frameMs);
	g_vrPhysicalCurrentVelocity = frameVelocity;
	g_vrPhysicalReleaseVelocity = glm::mix(g_vrPhysicalReleaseVelocity,
	                                      frameVelocity, 0.25f);
	addVrPhysicalVelocitySample(frameVelocity);
	g_vrPhysicalPreviousHandPosition = handPosition;
	// The object and the rendered hand must share the same pose on this frame.
	// Filtering here made props visibly trail the hand, especially under CPU
	// reprojection, while OpenXR already supplies a filtered tracked pose.
	g_vrPhysicalHandPosition = handPosition;
	g_vrPhysicalHandDirection = handDirection;
	g_vrPhysicalHandOrientation = glm::normalize(handOrientation);
	if(gripHeld) {
		g_vrPhysicalGripReleaseFrames = 0;
		g_vrPhysicalGripHeld = true;
	} else if(++g_vrPhysicalGripReleaseFrames >= kVrGripReleaseDebounceFrames) {
		g_vrPhysicalGripHeld = false;
	}
}

bool isVrPhysicalDragActive() {
	return g_vrPhysicalDragEntity != nullptr;
}

Entity * getVrPhysicalDragEntity() {
	return g_vrPhysicalDragEntity;
}

Vec3f getVrPhysicalDragVelocity() {
	return g_vrPhysicalCurrentVelocity;
}

bool getVrPhysicalGripProfile(bool rightHand, std::array<float, 5> & fingerCurls,
                              float & diameter, float & span) {
	if(!g_vrPhysicalDragEntity || rightHand != g_vrPhysicalRightHand) {
		return false;
	}
	fingerCurls = g_vrPhysicalFingerCurls;
	diameter = g_vrPhysicalGripDiameter;
	span = g_vrPhysicalGripSpan;
	return true;
}
#endif

EntityDragResult findSpotForDraggedEntity(Vec3f origin, Vec3f dir, Entity * entity, Sphere limit) {
	
	EntityDragResult result;
	result.foundSpot = false;
	result.foundCollision = false;
	
	EERIE_3D_BBOX bbox;
	for(const EERIE_VERTEX & vertex : entity->obj->vertexlist) {
		bbox.add(vertex.v);
	}
	
	result.offset = bbox.min + (bbox.max - bbox.min) * Vec3f(0.5f);
	result.height = std::max(bbox.max.y - bbox.min.y, 30.f);
	
	float maxdist = 0.f;
	for(const EERIE_VERTEX & vertex : entity->obj->vertexlist) {
		maxdist = std::max(maxdist, glm::distance(getXZ(result.offset), getXZ(vertex.v)) - 4.f);
	}
	
	if(entity->obj->pbox) {
		Vec2f tmpVert(entity->obj->pbox->vert[0].initpos.x, entity->obj->pbox->vert[0].initpos.z);
		for(size_t i = 1; i < entity->obj->pbox->vert.size(); i++) {
			const PhysicsParticle & physVert = entity->obj->pbox->vert[i];
			maxdist = std::max(maxdist, glm::distance(tmpVert, getXZ(physVert.initpos)) + 14.f);
		}
	}
	
	Cylinder cyl(origin + Vec3f(0.f, bbox.max.y, 0.f), glm::clamp(maxdist, 20.f, 150.f), -result.height);
	
	float inc = 10.f;
	cyl.origin += dir * inc;
	
	if(!limit.contains(cyl.origin)) {
		float t;
		if(!arx::intersectRaySphere(cyl.origin, dir, limit.origin, limit.radius, t)) {
			t = glm::dot(dir, player.pos - cyl.origin);
		}
		if(t >= 0.f) {
			cyl.origin += dir * (t + 0.1f);
		}
	}
	
	while(true) {
		
		float offsetY = CheckAnythingInCylinder(cyl, entity,
		                                        CFLAG_JUST_TEST | CFLAG_COLLIDE_NOCOL | CFLAG_NO_NPC_COLLIDE);
		
		if(offsetY < 0.f) {
			
			result.foundCollision = true;
			
			if(inc / 2.f < 0.1f) {
				break;
			}
			
			// Decrease step distance by half and step back
			inc /= 2.f;
			cyl.origin -= dir * inc;
			
		} else {
			
			result.foundSpot = true;
			result.pos = cyl.origin;
			result.offsetY = offsetY;
			
			if(!limit.contains(result.pos)) {
				break;
			}
			
			// Step forward
			cyl.origin += dir * inc;
			
		}
		
	}
	
	if(result.foundCollision && !limit.contains(result.pos)) {
		result.foundCollision = false;
	}
	
	Anglef angle = entity->angle;
	angle.setYaw(270.f - angle.getYaw());
	result.offset = toQuaternion(angle) * result.offset;
	
	return result;
}

#ifdef ANDROID
extern "C" {
__attribute__((used)) __attribute__((visibility("default")))
void updateScreenControlsHidingState(bool controlsHided) {
    screenControlsHided = controlsHided;
}
}
#endif

void updateDraggedEntity() {
	
	Entity * entity = g_draggedEntity;

    if(!entity || BLOCK_PLAYER_CONTROLS || !PLAYER_INTERFACE_SHOW) {
		return;
	}
	
	ARX_PROFILE_FUNC();

#if defined(ARXVR_ANDROID_BUILD)
	if(g_vrPhysicalDragEntity) {
		if(entity != g_vrPhysicalDragEntity) {
			g_vrPhysicalDragEntity = nullptr;
			g_vrPhysicalGripHeld = false;
			return;
		}

		if(g_vrPhysicalGripHeld) {
			entity->show = SHOW_FLAG_IN_SCENE;
			g_dragStatus = EntityDragStatus_OnGround;
			const glm::quat renderRotation = glm::normalize(g_vrPhysicalHandOrientation
			                                      * g_vrPhysicalObjectRotationOffset);
			setVrPhysicalObjectRenderRotation(*entity, renderRotation);
			const glm::quat actualRenderRotation = vrPhysicalObjectRenderRotation(*entity);
			g_vrPhysicalObjectOffset = -(actualRenderRotation
			                           * (g_vrPhysicalGrabPointLocal * entity->scale));
			const Vec3f holdPosition = g_vrPhysicalHandPosition
			                         + g_vrPhysicalHandDirection
			                           * g_vrPhysicalPalmHoldDistance
			                         + g_vrPhysicalHandOrientation
			                           * g_vrPhysicalGripHandLocalOffset;
			ARX_INTERACTIVE_Teleport(entity,
			                         holdPosition + g_vrPhysicalObjectOffset,
			                         true);
			const float displacement = glm::distance(entity->pos,
			                                         g_vrPhysicalObjectStartPosition);
			if(!g_vrPhysicalMovementConfirmed && displacement >= 20.f) {
				g_vrPhysicalMovementConfirmed = true;
				LogInfo << "ArxVR physical drag moved: target=" << entity->idString()
				        << " displacement=" << displacement;
			}
			if(entity->obj->pbox) {
				entity->obj->pbox->active = 0;
			}
			return;
		}

		Entity * releasedEntity = g_vrPhysicalDragEntity;
		const Vec3f releaseVelocity = recentVrPhysicalThrowVelocity();
		const Vec3f handDirection = g_vrPhysicalHandDirection;
		const float releaseSpeed = glm::length(releaseVelocity);
		g_vrPhysicalDragEntity = nullptr;
		g_vrPhysicalGripHeld = false;
		LogInfo << "ArxVR physical drag release: target=" << releasedEntity->idString()
		        << " displacement="
		        << glm::distance(releasedEntity->pos, g_vrPhysicalObjectStartPosition)
		        << " movementConfirmed=" << g_vrPhysicalMovementConfirmed
		        << " speed=" << releaseSpeed
		        << " filteredSpeed=" << glm::length(g_vrPhysicalReleaseVelocity);
		releasedEntity->show = SHOW_FLAG_IN_SCENE;
		releasedEntity->gameFlags &= ~GFLAG_NOCOMPUTATION;
		setDraggedEntity(nullptr);

		if(releasedEntity->obj && releasedEntity->obj->pbox) {
			if(releaseSpeed >= kVrThrowSpeedThreshold) {
				const Vec3f launchDirection = glm::normalize(releaseVelocity
				                                            + handDirection * 25.f);
				EERIE_PHYSICS_BOX_Launch(releasedEntity->obj, releasedEntity->pos,
				                         releasedEntity->angle, launchDirection);
				ARX_SOUND_PlaySFX(g_snd.WHOOSH, &releasedEntity->pos);
			} else {
				// A relaxed grip means place/drop, not an automatic throw. Let normal
				// gravity settle the item from its current palm position.
				ARX_INTERACTIVE_ActivatePhysics(*releasedEntity);
			}
		}
		ARX_SOUND_PlayInterface(g_snd.INVSTD);
		return;
	}
#endif
	
	arx_assert(!locateInInventories(entity));
	
#ifndef ANDROID   
	bool drop = eeMouseUp1();
#else
    bool drop = (allowDrop || screenControlsHided) && eeMouseUp1();

    if (drop) {
        allowDrop = false;
    }
#endif    
    Vec2f mouse = Vec2f(DANAEMouse) + g_draggedIconOffset;
	
	g_dragStatus = EntityDragStatus_OverHud;
	entity->show = SHOW_FLAG_ON_PLAYER;
	
	if(g_secondaryInventoryHud.containsPos(Vec2s(mouse))) {
		if(drop) {
			g_secondaryInventoryHud.dropEntity();
		}
		return;
	}
	if(g_playerInventoryHud.containsPos(Vec2s(mouse))) {
		if(drop) {
			g_playerInventoryHud.dropEntity();
		}
		return;
	}
	if(ARX_INTERFACE_MouseInBook()) {
		if(drop && g_playerBook.currentPage() == BOOKMODE_STATS && (entity->gameFlags & GFLAG_INTERACTIVITY)) {
			SendIOScriptEvent(entities.player(), entity, SM_INVENTORYUSE);
			COMBINE = nullptr;
		}
		return;
	}
	if(drop && (entity->ioflags & IO_GOLD)) {
		ARX_PLAYER_AddGold(entity);
		return;
	}
	
	if(!entity->obj) {
		// Cannot place noncorporeal entity into the world
		g_dragStatus = EntityDragStatus_Invalid;
		return;
	}
	
	if(g_camera == g_dragStartCamera && g_camera->angle.getYaw() != g_dragStartAngle) {
		float deltaYaw = g_camera->angle.getYaw() - g_dragStartAngle;
		Anglef angle = entity->angle;
		angle.setYaw(270.f - angle.getYaw());
		angle = toAngle(glm::quat(glm::vec3(0.f, glm::radians(-deltaYaw), 0.f)) * toQuaternion(angle));
		angle.setYaw(270.f - angle.getYaw());
		entity->angle = angle;
	}
	g_dragStartCamera = g_camera;
	g_dragStartAngle = g_camera->angle.getYaw();
	
	Vec3f origin = g_camera->m_pos;
	Vec3f dest = screenToWorldSpace(mouse + g_draggedObjectOffset, 1000.f);
	
	if(g_camera == &g_playerCamera) {
		// Use stable camera position so the dragged entity does not move around with the player head animation
		dest = dest - origin + g_playerCameraStablePos;
		origin = g_playerCameraStablePos;
	}
	
	Vec3f dir = glm::normalize(dest - origin);
	
	// Only allow dropping entities near the player
	Sphere limit(player.pos, 300.f);
	
	EntityDragResult result = findSpotForDraggedEntity(origin, dir, entity, limit);
	if(!result.foundSpot) {
		// No space to drop the entity
		g_dragStatus = EntityDragStatus_Invalid;
		return;
	}
	
	Vec3f pos = result.pos;
	
	// Snap entities to the ground up to a threshold
	float threshold = std::min(result.height, 12.0f);
	if(result.offsetY <= threshold) {
		pos += Vec3f(0.f, result.offsetY, 0.f);
	} else {
		pos += Vec3f(0.f, threshold - std::min(result.offsetY - threshold, threshold), 0.f);
	}
	
	ARX_INTERACTIVE_Teleport(entity, pos - toXZ(result.offset), true);
	
	if(!result.foundCollision || pos.y < player.pos.y) {
		// Throw item if there is no collision withthin the maximum distance
		g_dragStatus = EntityDragStatus_Throw;
	} else if(glm::abs(result.offsetY) > result.height) {
		arx_assert(!locateInInventories(entity));
		entity->show = SHOW_FLAG_IN_SCENE;
		g_dragStatus = EntityDragStatus_Drop;
	} else {
		arx_assert(!locateInInventories(entity));
		entity->show = SHOW_FLAG_IN_SCENE;
		g_dragStatus = EntityDragStatus_OnGround;
	}
	
	if(!drop) {
		return;
	}
	
	ARX_PLAYER_Remove_Invisibility();
	entity->soundtime = 0;
	entity->soundcount = 0;
	arx_assert(!locateInInventories(entity));
	entity->show = SHOW_FLAG_IN_SCENE;
	entity->obj->pbox->active = 0;
	entity->gameFlags &= ~GFLAG_NOCOMPUTATION;
	setDraggedEntity(nullptr);
	
	if((entity->ioflags & IO_ITEM) && entity->_itemdata->count > 1) {
		
		EERIE_3D_BBOX bbox;
		for(const EERIE_VERTEX & vertex : entity->obj->vertexlist) {
			bbox.add(vertex.v);
		}
		Vec3f delta = (bbox.max - bbox.min) * std::pow(float(entity->_itemdata->count), 1.f / 3.f);
		
		while(entity->_itemdata->count > 1) {
			Entity * unstackedEntity = CloneIOItem(entity);
			unstackedEntity->scriptload = 1;
			unstackedEntity->_itemdata->count = 1;
			unstackedEntity->pos = entity->pos;
			unstackedEntity->angle = entity->angle;
			arx_assert(!locateInInventories(unstackedEntity));
			unstackedEntity->show = SHOW_FLAG_IN_SCENE;
			if(g_dragStatus == EntityDragStatus_Throw) {
				Vec3f start = player.pos + Vec3f(0.f, 80.f, 0.f) - toXZ(result.offset);
				Vec3f direction = glm::normalize(unstackedEntity->pos - start + arx::randomVec(-1.f, 1.f) * delta);
				unstackedEntity->pos = start;
				EERIE_PHYSICS_BOX_Launch(unstackedEntity->obj, unstackedEntity->pos, unstackedEntity->angle, direction);
			} else if(glm::abs(result.offsetY) > threshold) {
				EERIE_PHYSICS_BOX_Launch(unstackedEntity->obj, unstackedEntity->pos, unstackedEntity->angle, Vec3f(0.f, 0.1f, 0.f));
			}
			entity->_itemdata->count--;
		}
		
	}
	
	if(g_dragStatus == EntityDragStatus_Throw) {
		
		Vec3f start = player.pos + Vec3f(0.f, 80.f, 0.f) - toXZ(result.offset);
		Vec3f direction = glm::normalize(entity->pos - start);
		entity->pos = start;
		EERIE_PHYSICS_BOX_Launch(entity->obj, entity->pos, entity->angle, direction);
		ARX_SOUND_PlaySFX(g_snd.WHOOSH, &entity->pos);
		
	} else if(glm::abs(result.offsetY) > threshold) {
		
		EERIE_PHYSICS_BOX_Launch(entity->obj, entity->pos, entity->angle, Vec3f(0.f, 0.1f, 0.f));
		ARX_SOUND_PlaySFX(g_snd.WHOOSH, &entity->pos);
		
	} else {
		
		ARX_SOUND_PlayInterface(g_snd.INVSTD);
		
	}
	
}
