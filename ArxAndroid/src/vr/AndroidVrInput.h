/*
 * Game-facing interpretation of the raw OpenXR tracking bridge.
 */
#pragma once

#include <cstdint>

#include <glm/gtc/quaternion.hpp>

#include "math/Rectangle.h"
#include "math/Vector.h"

class Camera;

void arxvrUpdateGameInput();
void arxvrResetReferenceHead();
void arxvrReloadRuntimeConfig();
void arxvrStartDiagnosticWalk(std::uint64_t trackingFrames);
void arxvrSetDiagnosticMovement(float strafe, float forward);
void arxvrSetDiagnosticHand(bool active, const Vec3f & position,
                            const Vec3f & direction, bool lowerGrip,
                            bool indexTrigger = false);
void arxvrClearDiagnosticControls();
bool arxvrHasTracking();
bool arxvrSeatedModeActive();

float arxvrMoveX();
float arxvrMoveY();
float arxvrHeadYawOffset();
float arxvrConsumeBodyYaw();
float arxvrConsumeSnapTurn();
bool arxvrPhysicalCrouchActive();
bool arxvrSprintActive();

bool arxvrIsMagicModePressed();
bool arxvrIsRuneDrawing();
bool arxvrMenuPrimaryPressed();
bool arxvrButtonPressed(std::uint32_t button);
bool arxvrButtonNowPressed(std::uint32_t button);
bool arxvrButtonNowReleased(std::uint32_t button);
float arxvrHandTriggerValue(bool rightHand);
float arxvrHandSqueezeValue(bool rightHand);
void arxvrSetDirectInteractionTriggerCaptured(bool captured);
bool arxvrDirectInteractionTriggerCaptured();
bool arxvrGetPointerScreenPoint(const Rect & viewport, Vec2s & point);
bool arxvrGetGameplayPointerScreenPoint(const Rect & viewport, float verticalFovRadians,
                                        Vec2s & point);
bool arxvrGetRuneScreenPoint(const Rect & viewport, Vec2s & point);
bool arxvrGetHandWorldPose(bool rightHand, const Camera & camera, float bodyYaw,
                           Vec3f & position, Vec3f & direction,
                           glm::quat & orientation);
bool arxvrGetRightHandWorldPose(const Camera & camera, float bodyYaw,
                                Vec3f & position, Vec3f & direction,
                                glm::quat & orientation);
bool arxvrGetLastHandWorldPose(bool rightHand, Vec3f & position,
                               glm::quat & orientation);

void arxvrSetRenderingEye(int eye);
void arxvrSetSecondaryEyePass(bool secondary);
bool arxvrIsSecondaryEyePass();
void arxvrApplyHeadPose(Camera & camera, float bodyYaw);
void arxvrApplyEyeOffset(Camera & camera);
