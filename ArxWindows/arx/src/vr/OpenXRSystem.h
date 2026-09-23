/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Libertatis and is distributed under the terms of
 * the GNU General Public License, version 3 or later.
 */

#ifndef ARX_VR_OPENXRSYSTEM_H
#define ARX_VR_OPENXRSYSTEM_H

#include <functional>
#include <memory>

#include <glm/mat4x4.hpp>

#include "game/Camera.h"
#include "math/Rectangle.h"

struct VREyeRenderInfo {
	Camera camera;
	Rect viewport;
	glm::mat4x4 worldToView;
	glm::mat4x4 viewToClip;
};

class OpenXRSystem {

public:
	using RenderEye = std::function<void(VREyeRenderInfo &)>;

	OpenXRSystem();
	~OpenXRSystem();

	OpenXRSystem(const OpenXRSystem &) = delete;
	OpenXRSystem & operator=(const OpenXRSystem &) = delete;

	bool initialize();
	void shutdown();

	[[nodiscard]] bool isInitialized() const;
	[[nodiscard]] bool isSessionRunning() const;

	// Returns true when an OpenXR frame was submitted. A false result means the
	// caller should render the normal desktop frame instead.
	bool renderFrame(const Camera & bodyCamera, const RenderEye & renderEye);

	void recenter();

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

extern OpenXRSystem g_openXR;
extern bool g_openXRRequested;

#endif // ARX_VR_OPENXRSYSTEM_H
