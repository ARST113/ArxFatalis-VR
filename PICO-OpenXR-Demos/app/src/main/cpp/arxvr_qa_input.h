#pragma once

#include <cmath>
#include <cstdio>

// ADB-only, opt-in input rehearsal. No entity positions or story state enter
// this protocol. Axes have the same range as physical controller input.
struct ArxVrQaInput {
    unsigned sequence = 0;
    float duration = 0;
    float moveX = 0, moveY = 0;
    float yaw = 0, pitch = 0, height = 0;
    float handX = 0, handY = 0, handZ = 0;
    float handYaw = 0, handPitch = 0;
    unsigned buttons = 0;
};

inline bool ParseArxVrQaInput(const char* text, ArxVrQaInput& output) {
    ArxVrQaInput value;
    char trailing = 0;
    const int count = std::sscanf(text,
        "%u %f %f %f %f %f %f %f %f %f %f %f %u %c",
        &value.sequence, &value.duration, &value.moveX, &value.moveY,
        &value.yaw, &value.pitch, &value.height,
        &value.handX, &value.handY, &value.handZ,
        &value.handYaw, &value.handPitch, &value.buttons, &trailing);
    if(count != 13 || value.sequence == 0) return false;
    const auto range = [](float v, float low, float high) {
        return std::isfinite(v) && v >= low && v <= high;
    };
    if(!range(value.duration, 0.f, 2.f)
       || !range(value.moveX, -1.f, 1.f) || !range(value.moveY, -1.f, 1.f)
       || !range(value.yaw, -180.f, 180.f) || !range(value.pitch, -80.f, 80.f)
       || !range(value.height, -1.f, 0.1f)
       || !range(value.handYaw, -180.f, 180.f) || !range(value.handPitch, -85.f, 85.f)
       || !range(value.handX, -0.8f, 0.8f) || !range(value.handY, -1.f, 0.5f)
       || !range(value.handZ, -0.85f, 0.15f)
       || value.handX * value.handX + value.handY * value.handY
          + value.handZ * value.handZ > 1.f
       || (value.buttons & ~0x1b3fu) != 0u) return false;
    output = value;
    return true;
}
