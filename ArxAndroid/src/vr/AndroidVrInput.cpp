#include "vr/AndroidVrInput.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "game/Camera.h"
#include "graphics/Math.h"
#include "io/log/Logger.h"
#include "vr/AndroidVrBridge.h"
#include "vr/VrConfig.h"

namespace {

constexpr float kMetersToArxUnits = 100.f;
constexpr float kHalfIpdArxUnits = 3.2f;
constexpr float kStickDeadzone = 0.18f;
constexpr float kSnapActivation = 0.72f;
constexpr float kSnapReset = 0.35f;
constexpr float kRuneTriggerThreshold = 0.12f;
constexpr float kPhysicalCrouchEnterArxUnits = 10.f;
constexpr float kPhysicalCrouchExitArxUnits = 5.f;
// Left X is intentionally reserved as the accessibility crouch hold when
// physical crouching is unavailable. Y remains magic mode; both grips remain
// physical hand/object interaction inputs.
constexpr std::uint32_t kVrButtonCrouch = ARXVR_BUTTON_LEFT_X;
// Must match the world-fixed panel anchor in the OpenXR presentation layer.
// A mismatched ray plane shifts the controller cursor and makes visible menu
// items appear unclickable.
constexpr float kMenuPanelDistanceMeters = 1.8f;
constexpr float kMenuPanelWidthMeters = 1.05f;
constexpr float kMenuClickAnalogThreshold = 0.35f;

ArxVrTrackingState g_state = {};
ArxVrTrackingState g_previousState = {};
bool g_haveState = false;
bool g_haveReferenceHead = false;
ArxVrPose g_referenceHead = {};
glm::quat g_referenceHeadOrientation(1.f, 0.f, 0.f, 0.f);
glm::quat g_referenceMenuOrientation(1.f, 0.f, 0.f, 0.f);
glm::quat g_referenceTrackingOrientation(1.f, 0.f, 0.f, 0.f);
Anglef g_relativeHeadAngle;
Vec3f g_relativeHeadPosition(0.f);
bool g_haveStandingHeadHeight = false;
float g_standingHeadHeightMeters = 0.f;
float g_bodyFollowYaw = 0.f;
float g_pendingSnapTurn = 0.f;
bool g_snapTurnReady = true;
bool g_physicalCrouchActive = false;
bool g_directInteractionTriggerCaptured = false;
int g_renderingEye = 0;
bool g_secondaryEyePass = false;
std::uint64_t g_diagnosticWalkStartFrame = 0;
std::uint64_t g_diagnosticWalkEndFrame = 0;
bool g_diagnosticMovementActive = false;
float g_diagnosticMoveX = 0.f;
float g_diagnosticMoveY = 0.f;
bool g_diagnosticHandActive = false;
Vec3f g_diagnosticHandPosition(0.f);
Vec3f g_diagnosticHandDirection(0.f, 0.f, 1.f);
bool g_diagnosticLowerGrip = false;
bool g_previousDiagnosticLowerGrip = false;
bool g_diagnosticIndexTrigger = false;
bool g_previousDiagnosticIndexTrigger = false;
bool g_lastHandWorldPoseValid[2] = { false, false };
Vec3f g_lastHandWorldPosition[2] = { Vec3f(0.f), Vec3f(0.f) };
glm::quat g_lastHandWorldOrientation[2] = {
	glm::quat(1.f, 0.f, 0.f, 0.f), glm::quat(1.f, 0.f, 0.f, 0.f)
};
VrConfig g_vrConfig;
bool g_vrConfigLoaded = false;

glm::quat poseOrientation(const ArxVrPose & pose) {
	return glm::normalize(glm::quat(pose.orientation[3], pose.orientation[0],
	                                pose.orientation[1], pose.orientation[2]));
}

glm::vec3 posePosition(const ArxVrPose & pose) {
	return glm::vec3(pose.position[0], pose.position[1], pose.position[2]);
}

glm::quat uprightMenuOrientation(const glm::quat & orientation) {
	glm::vec3 forward = orientation * glm::vec3(0.f, 0.f, -1.f);
	forward.y = 0.f;
	if(glm::length(forward) < 0.001f) {
		forward = glm::vec3(0.f, 0.f, -1.f);
	} else {
		forward = glm::normalize(forward);
	}
	const glm::vec3 up(0.f, 1.f, 0.f);
	const glm::vec3 right = glm::normalize(glm::cross(forward, up));
	return glm::normalize(glm::quat_cast(glm::mat3(right, up, -forward)));
}

glm::quat diagnosticAimOrientation(const Vec3f & directionVector) {
	const Vec3f from(0.f, 0.f, 1.f);
	const Vec3f to = glm::length(directionVector) > 0.001f
	               ? glm::normalize(directionVector) : from;
	const float cosine = glm::clamp(glm::dot(from, to), -1.f, 1.f);
	if(cosine > 0.9999f) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}
	if(cosine < -0.9999f) {
		return glm::angleAxis(glm::radians(180.f), Vec3f(0.f, 1.f, 0.f));
	}
	const Vec3f axis = glm::cross(from, to);
	return glm::normalize(glm::quat(1.f + cosine, axis.x, axis.y, axis.z));
}

float applyDeadzone(float value) {
	float magnitude = std::abs(value);
	if(magnitude <= kStickDeadzone) {
		return 0.f;
	}
	float scaled = (magnitude - kStickDeadzone) / (1.f - kStickDeadzone);
	return std::copysign(std::min(scaled, 1.f), value);
}

float normalizeAngleDelta(float degrees) {
	while(degrees > 180.f) {
		degrees -= 360.f;
	}
	while(degrees < -180.f) {
		degrees += 360.f;
	}
	return degrees;
}

void ensureVrConfigLoaded() {
	if(!g_vrConfigLoaded) {
		arxvrReloadRuntimeConfig();
	}
}

bool buttonCrouchRequested(const ArxVrTrackingState & state) {
	return (state.buttonMask & kVrButtonCrouch) != 0u;
}

} // namespace

void arxvrReloadRuntimeConfig() {
	g_vrConfig = arxvrLoadRuntimeConfig();
	g_vrConfigLoaded = true;
	g_haveReferenceHead = false;
	g_relativeHeadAngle = Anglef();
	g_relativeHeadPosition = Vec3f(0.f);
	g_bodyFollowYaw = 0.f;
	g_pendingSnapTurn = 0.f;
	g_snapTurnReady = true;
	g_physicalCrouchActive = false;
	g_haveStandingHeadHeight = g_vrConfig.standingHeightOverrideMeters > 0.f;
	g_standingHeadHeightMeters = g_haveStandingHeadHeight
	                           ? g_vrConfig.standingHeightOverrideMeters : 0.f;
	LogInfo << "ArxVR config: stance="
	        << (g_vrConfig.stance == VrStanceMode::Seated ? "seated" : "standing")
	        << " physicalCrouch=" << g_vrConfig.physicalCrouch
	        << " snapTurn=" << g_vrConfig.snapTurnDegrees
	        << " standingHeightOverride=" << g_vrConfig.standingHeightOverrideMeters
	        << " eyeHeightOffset=" << g_vrConfig.eyeHeightOffsetMeters
	        << " buttonCrouch=left_x";
}

void arxvrUpdateGameInput() {
	ensureVrConfigLoaded();
	g_previousDiagnosticLowerGrip = g_diagnosticLowerGrip;
	g_previousDiagnosticIndexTrigger = g_diagnosticIndexTrigger;
	ArxVrTrackingState state = {};
	if(!arxvr_read_tracking(&state)) {
		g_haveState = false;
		return;
	}

	const std::uint32_t previousButtonMask = g_state.buttonMask;
	g_previousState = g_state;
	g_state = state;
	g_haveState = true;
	if(state.buttonMask != previousButtonMask) {
		LogInfo << "ArxVR controls: buttons=" << state.buttonMask
		        << " rightTrigger=" << state.rightTrigger
		        << " rightGrip=" << state.rightSqueeze;
	}

	if((state.validMask & ARXVR_VALID_HEAD) != 0) {
		const glm::quat currentXrOrientation = poseOrientation(state.head);
		if(!g_haveReferenceHead) {
			g_referenceHead = state.head;
			g_referenceHeadOrientation = currentXrOrientation;
			// The menu is placed at eye height but uses a yaw-only world basis: an
			// opening gaze pitched upward cannot put it in the sky, head roll cannot
			// tilt it, and later gaze changes cannot rotate the pointer plane.
			g_referenceMenuOrientation = uprightMenuOrientation(currentXrOrientation);
			g_referenceTrackingOrientation = uprightMenuOrientation(currentXrOrientation);
			const float currentHeight = state.head.position[1];
			if(g_vrConfig.standingHeightOverrideMeters <= 0.f
			   && (!g_haveStandingHeadHeight
			       || std::abs(currentHeight - g_standingHeadHeightMeters) > 0.75f)) {
				g_standingHeadHeightMeters = currentHeight;
				g_haveStandingHeadHeight = true;
				LogInfo << "ArxVR standing height calibrated: "
				        << g_standingHeadHeightMeters << "m";
			}
			g_haveReferenceHead = true;
		}

		// Derive pitch/yaw directly from OpenXR's relative forward vector. The
		// generic Arx quaternion-to-angle helper contains a model-space 90-degree
		// correction and must not be used for head tracking. Arx uses +Y down and
		// decreasing yaw to turn right, hence both signs below.
		const glm::vec3 currentForward = currentXrOrientation * glm::vec3(0.f, 0.f, -1.f);
		const glm::vec3 localForward = glm::normalize(
			glm::conjugate(g_referenceHeadOrientation) * currentForward
		);
		const float vertical = std::clamp(-localForward.y, -1.f, 1.f);
		g_relativeHeadAngle.setPitch(glm::degrees(std::asin(vertical)));
		g_relativeHeadAngle.setYaw(glm::degrees(std::atan2(-localForward.x, -localForward.z)));
		// Head roll must not tilt the game camera; it is uncomfortable in VR and
		// was also amplifying small tracking movements.
		g_relativeHeadAngle.setRoll(0.f);

		glm::vec3 xrOffset = posePosition(state.head) - posePosition(g_referenceHead);
		// Position must be expressed in an upright, yaw-only tracking basis.
		// Using the full recenter orientation mixed pitch into translation: after
		// recentering while looking slightly down, a real crouch also moved the
		// game camera backwards. Rotation still uses the full head orientation,
		// but room-scale X/Y/Z motion must remain orthogonal to the floor.
		xrOffset = glm::conjugate(g_referenceTrackingOrientation) * xrOffset;
		g_relativeHeadPosition = Vec3f(xrOffset.x, -xrOffset.y, -xrOffset.z)
		                       * kMetersToArxUnits;

		const float currentHeight = state.head.position[1];
		const bool useTrackedCrouch = g_vrConfig.stance == VrStanceMode::Standing
		                           && g_vrConfig.physicalCrouch;
		const bool previousCrouch = g_physicalCrouchActive;
		float verticalDrop = 0.f;
		if(useTrackedCrouch) {
			// Preserve the tallest observed standing pose across pause/menu recenter
			// events unless a fixed floor-relative height override is configured.
			if(g_vrConfig.standingHeightOverrideMeters <= 0.f
			   && (!g_haveStandingHeadHeight || currentHeight > g_standingHeadHeightMeters)) {
				g_standingHeadHeightMeters = currentHeight;
				g_haveStandingHeadHeight = true;
			}
			if(g_haveStandingHeadHeight) {
				verticalDrop = std::max(
					(g_standingHeadHeightMeters - currentHeight) * kMetersToArxUnits, 0.f);
			}
			g_relativeHeadPosition.y = verticalDrop
			                         + g_vrConfig.eyeHeightOffsetMeters * kMetersToArxUnits;
			if(g_physicalCrouchActive) {
				g_physicalCrouchActive = verticalDrop > kPhysicalCrouchExitArxUnits;
			} else {
				g_physicalCrouchActive = verticalDrop >= kPhysicalCrouchEnterArxUnits;
			}
		} else {
			// Seated play keeps natural local head motion for leaning/bobbing while
			// crouch becomes an explicit hold action. This prevents the user's chair
			// height from being interpreted as a permanently crouched avatar.
			g_relativeHeadPosition.y +=
				g_vrConfig.eyeHeightOffsetMeters * kMetersToArxUnits;
			g_physicalCrouchActive = buttonCrouchRequested(state);
		}
		if(g_physicalCrouchActive != previousCrouch) {
			LogInfo << "ArxVR crouch: active=" << g_physicalCrouchActive
			        << " source=" << (useTrackedCrouch ? "tracked_height" : "left_x")
			        << " verticalDrop=" << verticalDrop
			        << " standingHeight=" << g_standingHeadHeightMeters
			        << " currentHeight=" << currentHeight;
		}
	}

	const float turn = state.rightStick[0];
	if(std::abs(turn) <= kSnapReset) {
		g_snapTurnReady = true;
	} else if(g_snapTurnReady && std::abs(turn) >= kSnapActivation) {
		// Arx yaw decreases when turning to the right.
		g_pendingSnapTurn += (turn > 0.f)
		                   ? -g_vrConfig.snapTurnDegrees : g_vrConfig.snapTurnDegrees;
		g_snapTurnReady = false;
	}
}

void arxvrResetReferenceHead() {
	ensureVrConfigLoaded();
	g_haveReferenceHead = false;
	g_relativeHeadAngle = Anglef();
	g_relativeHeadPosition = Vec3f(0.f);
	g_bodyFollowYaw = 0.f;
	g_physicalCrouchActive = false;
	g_directInteractionTriggerCaptured = false;
	LogInfo << "ArxVR controls: reference head reset";
}

void arxvrStartDiagnosticWalk(std::uint64_t trackingFrames) {
	if(!g_haveState || trackingFrames == 0) {
		return;
	}
	g_diagnosticWalkStartFrame = g_state.frameNumber;
	g_diagnosticWalkEndFrame = g_diagnosticWalkStartFrame + trackingFrames;
	LogInfo << "ArxVR diagnostic walk armed for " << trackingFrames << " tracking frames";
}

void arxvrSetDiagnosticMovement(float strafe, float forward) {
	g_diagnosticMovementActive = true;
	// The unattended route runs against the normal player collision solver, but
	// needs to finish before PICO suspends tracking in an empty/dark headset.
	// Values above one are therefore allowed only through this diagnostic API;
	// real controller input remains clamped to the physical stick range.
	g_diagnosticMoveX = std::clamp(strafe, -3.f, 3.f);
	g_diagnosticMoveY = std::clamp(forward, -3.f, 3.f);
}

void arxvrSetDiagnosticHand(bool active, const Vec3f & position,
                            const Vec3f & direction, bool lowerGrip,
                            bool indexTrigger) {
	g_diagnosticHandActive = active;
	g_diagnosticHandPosition = position;
	const float length = glm::length(direction);
	g_diagnosticHandDirection = length > 0.001f
	                          ? direction / length
	                          : Vec3f(0.f, 0.f, 1.f);
	g_diagnosticLowerGrip = lowerGrip;
	g_diagnosticIndexTrigger = indexTrigger;
}

void arxvrClearDiagnosticControls() {
	g_diagnosticMovementActive = false;
	g_diagnosticMoveX = 0.f;
	g_diagnosticMoveY = 0.f;
	g_diagnosticHandActive = false;
	g_diagnosticLowerGrip = false;
	g_diagnosticIndexTrigger = false;
}

bool arxvrHasTracking() {
	return g_haveState && (g_state.validMask & ARXVR_VALID_HEAD) != 0;
}

bool arxvrSeatedModeActive() {
	ensureVrConfigLoaded();
	return g_vrConfig.stance == VrStanceMode::Seated;
}

float arxvrMoveX() {
	if(g_diagnosticMovementActive) {
		return g_diagnosticMoveX;
	}
	return g_haveState ? applyDeadzone(g_state.leftStick[0]) : 0.f;
}

float arxvrMoveY() {
	if(g_diagnosticMovementActive) {
		return g_diagnosticMoveY;
	}
	if(g_haveState && g_state.frameNumber >= g_diagnosticWalkStartFrame
	   && g_state.frameNumber < g_diagnosticWalkEndFrame) {
		return 1.f;
	}
	return g_haveState ? applyDeadzone(g_state.leftStick[1]) : 0.f;
}

float arxvrHeadYawOffset() {
	return g_haveReferenceHead
	     ? normalizeAngleDelta(g_relativeHeadAngle.getYaw() - g_bodyFollowYaw)
	     : 0.f;
}

float arxvrConsumeBodyYaw() {
	if(!g_haveReferenceHead) {
		return 0.f;
	}
	const float currentYaw = g_relativeHeadAngle.getYaw();
	const float delta = normalizeAngleDelta(currentYaw - g_bodyFollowYaw);
	g_bodyFollowYaw = currentYaw;
	return delta;
}

float arxvrConsumeSnapTurn() {
	const float turn = g_pendingSnapTurn;
	g_pendingSnapTurn = 0.f;
	return turn;
}

bool arxvrPhysicalCrouchActive() {
	return g_haveState && g_haveReferenceHead && g_physicalCrouchActive;
}

bool arxvrSprintActive() {
	return arxvrButtonPressed(ARXVR_BUTTON_LEFT_STICK);
}

bool arxvrIsMagicModePressed() {
	// Grips are reserved for physical hands and object manipulation. Requiring
	// the left Y face button prevents an ordinary two-handed squeeze from
	// accidentally entering rune mode and drawing the magical glow.
	return g_haveState && (g_state.buttonMask & ARXVR_BUTTON_LEFT_Y) != 0;
}

bool arxvrIsRuneDrawing() {
	return arxvrIsMagicModePressed() && g_state.rightTrigger >= kRuneTriggerThreshold;
}

bool arxvrButtonPressed(std::uint32_t button) {
	const bool tracked = g_haveState && (g_state.buttonMask & button) != 0;
	return tracked
	    || (button == ARXVR_BUTTON_RIGHT_SQUEEZE && g_diagnosticLowerGrip)
	    || (button == ARXVR_BUTTON_RIGHT_TRIGGER && g_diagnosticIndexTrigger);
}

float arxvrHandTriggerValue(bool rightHand) {
	float value = g_haveState
	            ? (rightHand ? g_state.rightTrigger : g_state.leftTrigger)
	            : 0.f;
	if(rightHand && g_diagnosticIndexTrigger) {
		value = 1.f;
	}
	return std::clamp(value, 0.f, 1.f);
}

float arxvrHandSqueezeValue(bool rightHand) {
	float value = g_haveState
	            ? (rightHand ? g_state.rightSqueeze : g_state.leftSqueeze)
	            : 0.f;
	if(rightHand && g_diagnosticLowerGrip) {
		value = 1.f;
	}
	return std::clamp(value, 0.f, 1.f);
}

void arxvrSetDirectInteractionTriggerCaptured(bool captured) {
	g_directInteractionTriggerCaptured = captured;
}

bool arxvrDirectInteractionTriggerCaptured() {
	return g_directInteractionTriggerCaptured;
}

bool arxvrMenuPrimaryPressed() {
	return g_haveState
	    && (((g_state.buttonMask & (ARXVR_BUTTON_RIGHT_TRIGGER
	                              | ARXVR_BUTTON_RIGHT_SQUEEZE)) != 0)
	        || g_state.rightTrigger >= kMenuClickAnalogThreshold
	        || g_state.rightSqueeze >= kMenuClickAnalogThreshold);
}

bool arxvrButtonNowPressed(std::uint32_t button) {
	const bool trackedNow = g_haveState && (g_state.buttonMask & button) != 0;
	const bool trackedBefore = (g_previousState.buttonMask & button) != 0;
	const bool diagnosticNow = button == ARXVR_BUTTON_RIGHT_SQUEEZE
	                        ? g_diagnosticLowerGrip
	                        : (button == ARXVR_BUTTON_RIGHT_TRIGGER
	                           && g_diagnosticIndexTrigger);
	const bool diagnosticBefore = button == ARXVR_BUTTON_RIGHT_SQUEEZE
	                           ? g_previousDiagnosticLowerGrip
	                           : (button == ARXVR_BUTTON_RIGHT_TRIGGER
	                              && g_previousDiagnosticIndexTrigger);
	return (trackedNow || diagnosticNow) && !(trackedBefore || diagnosticBefore);
}

bool arxvrButtonNowReleased(std::uint32_t button) {
	const bool trackedNow = g_haveState && (g_state.buttonMask & button) != 0;
	const bool trackedBefore = (g_previousState.buttonMask & button) != 0;
	const bool diagnosticNow = button == ARXVR_BUTTON_RIGHT_SQUEEZE
	                        ? g_diagnosticLowerGrip
	                        : (button == ARXVR_BUTTON_RIGHT_TRIGGER
	                           && g_diagnosticIndexTrigger);
	const bool diagnosticBefore = button == ARXVR_BUTTON_RIGHT_SQUEEZE
	                           ? g_previousDiagnosticLowerGrip
	                           : (button == ARXVR_BUTTON_RIGHT_TRIGGER
	                              && g_previousDiagnosticIndexTrigger);
	return !(trackedNow || diagnosticNow) && (trackedBefore || diagnosticBefore);
}

bool arxvrGetPointerScreenPoint(const Rect & viewport, Vec2s & point) {
	if(!g_haveState || !g_haveReferenceHead
	   || (g_state.validMask & ARXVR_VALID_HEAD) == 0
	   || (g_state.validMask & ARXVR_VALID_RIGHT_AIM) == 0) {
		return false;
	}

	const glm::quat aimOrientation = poseOrientation(g_state.rightAim);
	const glm::vec3 anchorForward = g_referenceMenuOrientation
	                              * glm::vec3(0.f, 0.f, -1.f);
	const glm::vec3 panelCenter = posePosition(g_referenceHead)
	                           + anchorForward * kMenuPanelDistanceMeters;
	const glm::vec3 worldDirection = aimOrientation * glm::vec3(0.f, 0.f, -1.f);
	const glm::vec3 localDirection = glm::conjugate(g_referenceMenuOrientation)
	                               * worldDirection;
	const glm::vec3 localOrigin = glm::conjugate(g_referenceMenuOrientation)
	                            * (posePosition(g_state.rightAim) - panelCenter);
	if(localDirection.z >= -0.02f) {
		return false;
	}
	const float distance = -localOrigin.z / localDirection.z;
	if(distance <= 0.f) {
		return false;
	}
	const glm::vec3 hit = localOrigin + localDirection * distance;
	const float aspect = std::max(float(viewport.width()) / float(viewport.height()), 0.1f);
	const float halfWidth = kMenuPanelWidthMeters * 0.5f;
	const float halfHeight = halfWidth / aspect;
	const Vec2f center(viewport.center());
	Vec2f projected(center.x + hit.x / halfWidth * float(viewport.width()) * 0.5f,
	                center.y - hit.y / halfHeight * float(viewport.height()) * 0.5f);
	projected.x = std::clamp(projected.x, float(viewport.left), float(viewport.right - 1));
	projected.y = std::clamp(projected.y, float(viewport.top), float(viewport.bottom - 1));
	point = Vec2s(projected);
	return true;
}

bool arxvrGetGameplayPointerScreenPoint(const Rect & viewport, float verticalFovRadians,
                                        Vec2s & point) {
	if(!g_haveState || viewport.width() <= 0 || viewport.height() <= 0
	   || (g_state.validMask & ARXVR_VALID_HEAD) == 0
	   || (g_state.validMask & ARXVR_VALID_RIGHT_AIM) == 0) {
		return false;
	}

	const glm::quat headOrientation = poseOrientation(g_state.head);
	const glm::quat aimOrientation = poseOrientation(g_state.rightAim);
	const glm::vec3 worldDirection = aimOrientation * glm::vec3(0.f, 0.f, -1.f);
	const glm::vec3 headDirection = glm::conjugate(headOrientation) * worldDirection;
	if(headDirection.z >= -0.02f) {
		return false;
	}

	const float safeFov = std::clamp(verticalFovRadians, glm::radians(45.f),
	                                 glm::radians(130.f));
	const float tanHalfVertical = std::tan(safeFov * 0.5f);
	const float aspect = float(viewport.width()) / float(viewport.height());
	const float tanHalfHorizontal = tanHalfVertical * aspect;
	const float ndcX = headDirection.x / (-headDirection.z * tanHalfHorizontal);
	const float ndcY = headDirection.y / (-headDirection.z * tanHalfVertical);
	if(std::abs(ndcX) > 1.f || std::abs(ndcY) > 1.f) {
		return false;
	}

	const Vec2f center(viewport.center());
	point = Vec2s(center.x + ndcX * float(viewport.width()) * 0.5f,
	              center.y - ndcY * float(viewport.height()) * 0.5f);
	return true;
}

bool arxvrGetRuneScreenPoint(const Rect & viewport, Vec2s & point) {
	if(!arxvrIsRuneDrawing()
	   || (g_state.validMask & ARXVR_VALID_HEAD) == 0
	   || (g_state.validMask & ARXVR_VALID_RIGHT_AIM) == 0) {
		return false;
	}

	const glm::quat headOrientation = poseOrientation(g_state.head);
	const glm::vec3 handOffset = posePosition(g_state.rightAim) - posePosition(g_state.head);
	const glm::vec3 headLocalHand = glm::conjugate(headOrientation) * handOffset;

	// A one-metre hand movement spans roughly 1.3 times the shorter viewport edge.
	const float scale = float(std::min(viewport.width(), viewport.height())) * 1.3f;
	const Vec2f center(viewport.center());
	Vec2f projected(center.x + headLocalHand.x * scale,
	                center.y - headLocalHand.y * scale);
	projected.x = std::clamp(projected.x, float(viewport.left), float(viewport.right - 1));
	projected.y = std::clamp(projected.y, float(viewport.top), float(viewport.bottom - 1));
	point = Vec2s(projected);
	return true;
}

bool arxvrGetHandWorldPose(bool rightHand, const Camera & camera, float bodyYaw,
                           Vec3f & position, Vec3f & direction,
                           glm::quat & orientation) {
	const size_t handIndex = rightHand ? 1u : 0u;
	g_lastHandWorldPoseValid[handIndex] = false;
    if(rightHand && g_diagnosticHandActive) {
		position = g_diagnosticHandPosition;
		direction = g_diagnosticHandDirection;
		// The unattended hand must have the same +Z aim axis as a tracked PICO
		// controller. Returning identity while supplying an arbitrary direction
		// rendered the fist sideways and made a correctly attached tool look wrong.
		orientation = diagnosticAimOrientation(direction);
		g_lastHandWorldPosition[handIndex] = position;
		g_lastHandWorldOrientation[handIndex] = orientation;
		g_lastHandWorldPoseValid[handIndex] = true;
		return true;
	}
	const std::uint32_t gripMask = rightHand ? ARXVR_VALID_RIGHT_GRIP
	                                         : ARXVR_VALID_LEFT_GRIP;
	const std::uint32_t aimMask = rightHand ? ARXVR_VALID_RIGHT_AIM
	                                        : ARXVR_VALID_LEFT_AIM;
	if(!g_haveState || !g_haveReferenceHead
	   || (g_state.validMask & ARXVR_VALID_HEAD) == 0
	   || (g_state.validMask & gripMask) == 0
	   || (g_state.validMask & aimMask) == 0) {
		return false;
	}

	// Rebuild both the head and controller positions in the same upright
	// tracking basis. Subtracting the tracked head contribution from the
	// already positioned game camera recovers the character-space eye origin.
	const glm::quat trackingBasis = glm::conjugate(g_referenceTrackingOrientation);
	const ArxVrPose & aimPose = rightHand ? g_state.rightAim : g_state.leftAim;
	// The visible FBX hands are attached to the OpenXR aim pose and centred on
	// that pose. Use the exact same origin for gameplay contact and held objects;
	// the OpenXR grip origin is down in the controller handle and made objects
	// appear beside the rendered palm.
	const glm::vec3 handOffsetXr = trackingBasis
	                             * (posePosition(aimPose)
	                                - posePosition(g_referenceHead));
	const Vec3f handOffsetArx(handOffsetXr.x, -handOffsetXr.y, -handOffsetXr.z);
	const Vec3f headOffsetWorld = VRotateY(g_relativeHeadPosition, -bodyYaw);
	const Vec3f characterEyeOrigin = camera.m_pos - headOffsetWorld;
	position = characterEyeOrigin
	         + VRotateY(handOffsetArx * kMetersToArxUnits, -bodyYaw);

	const glm::vec3 aimForwardXr = poseOrientation(aimPose)
	                             * glm::vec3(0.f, 0.f, -1.f);
	const glm::vec3 aimLocalXr = trackingBasis * aimForwardXr;
	direction = VRotateY(Vec3f(aimLocalXr.x, -aimLocalXr.y, -aimLocalXr.z),
	                     -bodyYaw);
	const float directionLength = glm::length(direction);
	if(directionLength < 0.001f) {
		return false;
	}
	direction /= directionLength;

	// Convert the complete tracked aim orientation into the same Arx world
	// basis as the hand position and aim ray. Conjugating with the 180-degree
	// X-axis basis conversion preserves controller-local axes; the final body
	// yaw keeps a held object aligned when the character turns.
	const glm::quat coordinateMap = glm::angleAxis(glm::radians(180.f),
	                                              glm::vec3(1.f, 0.f, 0.f));
	const glm::quat bodyRotation = glm::angleAxis(glm::radians(-bodyYaw),
	                                             glm::vec3(0.f, 1.f, 0.f));
	orientation = glm::normalize(bodyRotation * coordinateMap * trackingBasis
	                           * poseOrientation(aimPose)
	                           * glm::conjugate(coordinateMap));
	g_lastHandWorldPosition[handIndex] = position;
	g_lastHandWorldOrientation[handIndex] = orientation;
	g_lastHandWorldPoseValid[handIndex] = true;
	return true;
}

bool arxvrGetRightHandWorldPose(const Camera & camera, float bodyYaw,
                                Vec3f & position, Vec3f & direction,
                                glm::quat & orientation) {
	return arxvrGetHandWorldPose(true, camera, bodyYaw, position, direction,
	                             orientation);
}

bool arxvrGetLastHandWorldPose(bool rightHand, Vec3f & position,
                               glm::quat & orientation) {
	const size_t handIndex = rightHand ? 1u : 0u;
	if(!g_lastHandWorldPoseValid[handIndex]) {
		return false;
	}
	position = g_lastHandWorldPosition[handIndex];
	orientation = g_lastHandWorldOrientation[handIndex];
	return true;
}

void arxvrSetRenderingEye(int eye) {
	g_renderingEye = std::clamp(eye, -1, 1);
}

void arxvrSetSecondaryEyePass(bool secondary) {
	g_secondaryEyePass = secondary;
}

bool arxvrIsSecondaryEyePass() {
	return g_secondaryEyePass;
}

void arxvrApplyHeadPose(Camera & camera, float bodyYaw) {
	if(!arxvrHasTracking() || !g_haveReferenceHead) {
		return;
	}

	camera.m_pos += VRotateY(g_relativeHeadPosition, -bodyYaw);
	// Player yaw already consumed the tracked head delta this frame. Add only
	// the small unconsumed remainder so the camera is not rotated twice and the
	// body remains aligned with the view. Pitch belongs exclusively to the HMD;
	// legacy mouse/player pitch must not bias the neutral horizon. Never carry
	// game-camera roll into immersive first person.
	camera.angle.setPitch(MAKEANGLE(g_relativeHeadAngle.getPitch()));
	camera.angle.setYaw(MAKEANGLE(camera.angle.getYaw() + arxvrHeadYawOffset()));
	camera.angle.setRoll(0.f);
}

void arxvrApplyEyeOffset(Camera & camera) {
	if(!arxvrHasTracking()) {
		return;
	}
	if(g_renderingEye != 0) {
		const Vec3f eyeRight = angleToVectorXZ(camera.angle.getYaw() - 90.f);
		camera.m_pos += eyeRight * (kHalfIpdArxUnits * float(g_renderingEye));
	}
}
