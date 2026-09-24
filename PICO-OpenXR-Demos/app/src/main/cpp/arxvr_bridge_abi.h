/*
 * Shared ABI between the PICO OpenXR host and libarx.so.
 * Keep this definition synchronized with ArxAndroid/src/vr/AndroidVrBridge.h.
 */
#pragma once

#include <cstdint>

struct ArxVrPose {
    float orientation[4];
    float position[3];
};

struct ArxVrTrackingState {
    std::uint64_t frameNumber;
    std::uint32_t validMask;
    std::uint32_t buttonMask;
    ArxVrPose head;
    ArxVrPose leftAim;
    ArxVrPose leftGrip;
    ArxVrPose rightAim;
    ArxVrPose rightGrip;
    float leftTrigger;
    float leftSqueeze;
    float leftStick[2];
    float rightTrigger;
    float rightSqueeze;
    float rightStick[2];
};

struct ArxVrVisualHandPose {
    std::uint32_t valid;
    float position[3];
    float orientation[4];
    float trigger;
    float squeeze;
    std::uint32_t gripActive;
    float gripCurls[5]; // thumb, index, middle, ring, pinky
    float gripDiameter;
    float gripSpan;
};

struct ArxVrVisualState {
    std::uint32_t version;
    std::int32_t eye;
    float view[16];
    float projection[16];
    ArxVrVisualHandPose leftHand;
    ArxVrVisualHandPose rightHand;
};

constexpr std::uint32_t ARXVR_HAPTIC_REQUEST_VERSION = 1u;

struct ArxVrHapticRequest {
    std::uint32_t version;
    std::uint32_t hand;
    std::uint32_t event;
    float amplitude;
    float durationSeconds;
    float frequencyHz;
};

static_assert(sizeof(ArxVrHapticRequest) == 24,
              "ArxVrHapticRequest ABI layout changed");

enum ArxVrTrackingValidBits : std::uint32_t {
    ARXVR_VALID_HEAD = 1u << 0,
    ARXVR_VALID_LEFT_AIM = 1u << 1,
    ARXVR_VALID_LEFT_GRIP = 1u << 2,
    ARXVR_VALID_RIGHT_AIM = 1u << 3,
    ARXVR_VALID_RIGHT_GRIP = 1u << 4
};

enum ArxVrButtonBits : std::uint32_t {
    ARXVR_BUTTON_LEFT_TRIGGER = 1u << 0,
    ARXVR_BUTTON_LEFT_SQUEEZE = 1u << 1,
    ARXVR_BUTTON_LEFT_STICK = 1u << 2,
    ARXVR_BUTTON_LEFT_X = 1u << 3,
    ARXVR_BUTTON_LEFT_Y = 1u << 4,
    ARXVR_BUTTON_LEFT_MENU = 1u << 5,
    ARXVR_BUTTON_LEFT_TRIGGER_TOUCH = 1u << 6,
    ARXVR_BUTTON_LEFT_STICK_TOUCH = 1u << 7,
    ARXVR_BUTTON_RIGHT_TRIGGER = 1u << 8,
    ARXVR_BUTTON_RIGHT_SQUEEZE = 1u << 9,
    ARXVR_BUTTON_RIGHT_STICK = 1u << 10,
    ARXVR_BUTTON_RIGHT_A = 1u << 11,
    ARXVR_BUTTON_RIGHT_B = 1u << 12,
    ARXVR_BUTTON_RIGHT_MENU = 1u << 13,
    ARXVR_BUTTON_RIGHT_TRIGGER_TOUCH = 1u << 14,
    ARXVR_BUTTON_RIGHT_STICK_TOUCH = 1u << 15
};

using ArxVrUpdateTrackingFn = void (*)(const ArxVrTrackingState * state);
