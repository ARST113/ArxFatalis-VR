/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Libertatis and is distributed under the terms of
 * the GNU General Public License, version 3 or later.
 */

#include "vr/OpenXRSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <epoxy/gl.h>
#include <epoxy/wgl.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "graphics/Math.h"
#include "io/log/Logger.h"
#include "platform/ProgramOptions.h"

namespace {

constexpr float ArxUnitsPerMeter = 100.f;

glm::mat4x4 poseToMatrix(const XrPosef & pose) {
	const glm::quat orientation(pose.orientation.w, pose.orientation.x,
	                            pose.orientation.y, pose.orientation.z);
	glm::mat4x4 result = glm::translate(glm::mat4x4(1.f),
	                                  Vec3f(pose.position.x, pose.position.y,
	                                        pose.position.z));
	return result * glm::mat4_cast(orientation);
}

glm::mat4x4 createProjection(const XrFovf & fov, float nearDistance, float farDistance) {
	const float left = std::tan(fov.angleLeft);
	const float right = std::tan(fov.angleRight);
	const float down = std::tan(fov.angleDown);
	const float up = std::tan(fov.angleUp);

	// Arx uses +Z forward and +Y down. Keep its existing 0..1 depth mapping so
	// all fixed-function renderer assumptions remain unchanged.
	glm::mat4x4 projection(0.f);
	projection[0][0] = 2.f / (right - left);
	projection[1][1] = -2.f / (up - down);
	projection[2][0] = -(right + left) / (right - left);
	projection[2][1] = -(up + down) / (up - down);
	projection[2][2] = farDistance / (farDistance - nearDistance);
	projection[2][3] = 1.f;
	projection[3][2] = -farDistance * nearDistance / (farDistance - nearDistance);
	return projection;
}

void requestOpenXR() {
	g_openXRRequested = true;
}

ARX_PROGRAM_OPTION("openxr", "", "Enable the OpenXR virtual reality renderer", &requestOpenXR)

} // namespace

bool g_openXRRequested = false;
OpenXRSystem g_openXR;

class OpenXRSystem::Impl {

public:
	struct Swapchain {
		XrSwapchain handle = XR_NULL_HANDLE;
		int32_t width = 0;
		int32_t height = 0;
		std::vector<XrSwapchainImageOpenGLKHR> images;
		GLuint framebuffer = 0;
		GLuint depthBuffer = 0;
	};

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace referenceSpace = XR_NULL_HANDLE;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	bool sessionRunning = false;
	bool calibrated = false;
	glm::mat4x4 referenceHeadToSpace = glm::mat4x4(1.f);

	std::vector<XrViewConfigurationView> configurationViews;
	std::vector<XrView> views;
	std::vector<Swapchain> swapchains;

	bool check(XrResult result, const char * operation) const {
		if(XR_SUCCEEDED(result)) {
			return true;
		}
		char resultText[XR_MAX_RESULT_STRING_SIZE] = { };
		if(instance != XR_NULL_HANDLE) {
			xrResultToString(instance, result, resultText);
		}
		LogError << "OpenXR: " << operation << " failed (" << int(result) << ") "
		         << resultText;
		return false;
	}

	bool hasExtension(const char * wanted) const {
		uint32_t count = 0;
		if(XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr))) {
			return false;
		}
		std::vector<XrExtensionProperties> extensions(count,
		                                               { XR_TYPE_EXTENSION_PROPERTIES });
		if(XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, count, &count,
		                                                  extensions.data()))) {
			return false;
		}
		return std::any_of(extensions.begin(), extensions.end(), [wanted](const auto & extension) {
			return std::strcmp(extension.extensionName, wanted) == 0;
		});
	}

	bool createInstance() {
		if(!hasExtension(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
			LogError << "OpenXR: active runtime does not expose "
			         << XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
			return false;
		}

		const char * extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
		XrInstanceCreateInfo createInfo { XR_TYPE_INSTANCE_CREATE_INFO };
		std::strncpy(createInfo.applicationInfo.applicationName, "Arx Fatalis VR",
		             XR_MAX_APPLICATION_NAME_SIZE - 1);
		createInfo.applicationInfo.applicationVersion = XR_MAKE_VERSION(0, 1, 0);
		std::strncpy(createInfo.applicationInfo.engineName, "Arx Libertatis",
		             XR_MAX_ENGINE_NAME_SIZE - 1);
		createInfo.applicationInfo.engineVersion = XR_MAKE_VERSION(1, 3, 0);
		createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
		createInfo.enabledExtensionCount = 1;
		createInfo.enabledExtensionNames = extensions;
		return check(xrCreateInstance(&createInfo, &instance), "xrCreateInstance");
	}

	bool createSystemAndSession() {
		XrSystemGetInfo systemInfo { XR_TYPE_SYSTEM_GET_INFO };
		systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		if(!check(xrGetSystem(instance, &systemInfo, &system), "xrGetSystem")) {
			return false;
		}

		PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;
		if(!check(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
		                                   reinterpret_cast<PFN_xrVoidFunction *>(&getRequirements)),
		          "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)")
		   || !getRequirements) {
			return false;
		}
		XrGraphicsRequirementsOpenGLKHR requirements {
			XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR
		};
		if(!check(getRequirements(instance, system, &requirements),
		          "xrGetOpenGLGraphicsRequirementsKHR")) {
			return false;
		}
		LogInfo << "OpenXR: OpenGL runtime range "
		        << XR_VERSION_MAJOR(requirements.minApiVersionSupported) << '.'
		        << XR_VERSION_MINOR(requirements.minApiVersionSupported) << " to "
		        << XR_VERSION_MAJOR(requirements.maxApiVersionSupported) << '.'
		        << XR_VERSION_MINOR(requirements.maxApiVersionSupported);

		XrGraphicsBindingOpenGLWin32KHR graphicsBinding {
			XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR
		};
		graphicsBinding.hDC = wglGetCurrentDC();
		graphicsBinding.hGLRC = wglGetCurrentContext();
		if(!graphicsBinding.hDC || !graphicsBinding.hGLRC) {
			LogError << "OpenXR: no current WGL context";
			return false;
		}

		XrSessionCreateInfo sessionInfo { XR_TYPE_SESSION_CREATE_INFO };
		sessionInfo.next = &graphicsBinding;
		sessionInfo.systemId = system;
		if(!check(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession")) {
			return false;
		}

		uint32_t blendModeCount = 0;
		if(check(xrEnumerateEnvironmentBlendModes(instance, system,
		                                            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                            0, &blendModeCount, nullptr),
		         "xrEnumerateEnvironmentBlendModes")) {
			std::vector<XrEnvironmentBlendMode> modes(blendModeCount);
			if(check(xrEnumerateEnvironmentBlendModes(instance, system,
			                                            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			                                            blendModeCount, &blendModeCount, modes.data()),
			         "xrEnumerateEnvironmentBlendModes")) {
				auto opaque = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
				if(opaque != modes.end()) {
					blendMode = *opaque;
				} else if(!modes.empty()) {
					blendMode = modes.front();
				}
			}
		}

		return createReferenceSpace() && createSwapchains();
	}

	bool createReferenceSpace() {
		uint32_t count = 0;
		if(!check(xrEnumerateReferenceSpaces(session, 0, &count, nullptr),
		          "xrEnumerateReferenceSpaces")) {
			return false;
		}
		std::vector<XrReferenceSpaceType> spaces(count);
		if(!check(xrEnumerateReferenceSpaces(session, count, &count, spaces.data()),
		          "xrEnumerateReferenceSpaces")) {
			return false;
		}
		XrReferenceSpaceType selected = XR_REFERENCE_SPACE_TYPE_LOCAL;
		if(std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end()) {
			selected = XR_REFERENCE_SPACE_TYPE_STAGE;
		}

		XrReferenceSpaceCreateInfo spaceInfo { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		spaceInfo.referenceSpaceType = selected;
		spaceInfo.poseInReferenceSpace.orientation.w = 1.f;
		if(!check(xrCreateReferenceSpace(session, &spaceInfo, &referenceSpace),
		          "xrCreateReferenceSpace")) {
			return false;
		}
		LogInfo << "OpenXR: using "
		        << (selected == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "local")
		        << " reference space";
		return true;
	}

	bool createSwapchains() {
		uint32_t viewCount = 0;
		if(!check(xrEnumerateViewConfigurationViews(instance, system,
		                                               XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                               0, &viewCount, nullptr),
		          "xrEnumerateViewConfigurationViews")) {
			return false;
		}
		if(viewCount == 0) {
			LogError << "OpenXR: runtime returned no stereo views";
			return false;
		}
		configurationViews.assign(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
		if(!check(xrEnumerateViewConfigurationViews(instance, system,
		                                               XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                               viewCount, &viewCount,
		                                               configurationViews.data()),
		          "xrEnumerateViewConfigurationViews")) {
			return false;
		}
		views.assign(viewCount, { XR_TYPE_VIEW });

		uint32_t formatCount = 0;
		if(!check(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr),
		          "xrEnumerateSwapchainFormats")) {
			return false;
		}
		std::vector<int64_t> formats(formatCount);
		if(!check(xrEnumerateSwapchainFormats(session, formatCount, &formatCount,
		                                        formats.data()),
		          "xrEnumerateSwapchainFormats")) {
			return false;
		}
		const std::array<int64_t, 3> preferredFormats = {
			GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA16F
		};
		int64_t selectedFormat = formats.empty() ? 0 : formats.front();
		for(int64_t preferred : preferredFormats) {
			if(std::find(formats.begin(), formats.end(), preferred) != formats.end()) {
				selectedFormat = preferred;
				break;
			}
		}
		if(selectedFormat == 0) {
			LogError << "OpenXR: runtime returned no color swapchain format";
			return false;
		}

		swapchains.resize(viewCount);
		for(uint32_t i = 0; i < viewCount; ++i) {
			Swapchain & swapchain = swapchains[i];
			swapchain.width = int32_t(configurationViews[i].recommendedImageRectWidth);
			swapchain.height = int32_t(configurationViews[i].recommendedImageRectHeight);

			XrSwapchainCreateInfo createInfo { XR_TYPE_SWAPCHAIN_CREATE_INFO };
			createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			createInfo.format = selectedFormat;
			createInfo.sampleCount = 1;
			createInfo.width = uint32_t(swapchain.width);
			createInfo.height = uint32_t(swapchain.height);
			createInfo.faceCount = 1;
			createInfo.arraySize = 1;
			createInfo.mipCount = 1;
			if(!check(xrCreateSwapchain(session, &createInfo, &swapchain.handle),
			          "xrCreateSwapchain")) {
				return false;
			}

			uint32_t imageCount = 0;
			if(!check(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr),
			          "xrEnumerateSwapchainImages(count)")) {
				return false;
			}
			swapchain.images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
			if(!check(xrEnumerateSwapchainImages(
			              swapchain.handle, imageCount, &imageCount,
			              reinterpret_cast<XrSwapchainImageBaseHeader *>(swapchain.images.data())),
			          "xrEnumerateSwapchainImages(images)")) {
				return false;
			}

			glGenFramebuffers(1, &swapchain.framebuffer);
			glGenRenderbuffers(1, &swapchain.depthBuffer);
			glBindRenderbuffer(GL_RENDERBUFFER, swapchain.depthBuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
			                      swapchain.width, swapchain.height);
			glBindFramebuffer(GL_FRAMEBUFFER, swapchain.framebuffer);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			                          GL_RENDERBUFFER, swapchain.depthBuffer);
		}
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		LogInfo << "OpenXR: created " << viewCount << " view swapchains at "
		        << swapchains.front().width << 'x' << swapchains.front().height;
		return true;
	}

	void pollEvents() {
		XrEventDataBuffer event { XR_TYPE_EVENT_DATA_BUFFER };
		while(xrPollEvent(instance, &event) == XR_SUCCESS) {
			switch(event.type) {
				case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
					const auto & stateEvent =
						reinterpret_cast<const XrEventDataSessionStateChanged &>(event);
					sessionState = stateEvent.state;
					if(sessionState == XR_SESSION_STATE_READY && !sessionRunning) {
						XrSessionBeginInfo beginInfo { XR_TYPE_SESSION_BEGIN_INFO };
						beginInfo.primaryViewConfigurationType =
							XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
						if(check(xrBeginSession(session, &beginInfo), "xrBeginSession")) {
							sessionRunning = true;
							calibrated = false;
						}
					} else if(sessionState == XR_SESSION_STATE_STOPPING && sessionRunning) {
						check(xrEndSession(session), "xrEndSession");
						sessionRunning = false;
					} else if(sessionState == XR_SESSION_STATE_EXITING
					       || sessionState == XR_SESSION_STATE_LOSS_PENDING) {
						sessionRunning = false;
					}
					break;
				}
				case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
					calibrated = false;
					break;
				case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
					sessionRunning = false;
					break;
				default:
					break;
			}
			event = { XR_TYPE_EVENT_DATA_BUFFER };
		}
	}

	void calibrateFromViews(uint32_t viewCount) {
		XrPosef center = views[0].pose;
		center.position = { 0.f, 0.f, 0.f };
		for(uint32_t i = 0; i < viewCount; ++i) {
			center.position.x += views[i].pose.position.x / float(viewCount);
			center.position.y += views[i].pose.position.y / float(viewCount);
			center.position.z += views[i].pose.position.z / float(viewCount);
		}
		referenceHeadToSpace = poseToMatrix(center);
		calibrated = true;
		LogInfo << "OpenXR: recentered HMD pose";
	}

	VREyeRenderInfo makeEye(const Camera & bodyCamera, uint32_t index) const {
		glm::mat4x4 xrToArx(1.f);
		xrToArx[1][1] = -1.f;
		xrToArx[2][2] = -1.f;

		glm::mat4x4 relativeXR = glm::inverse(referenceHeadToSpace)
		                             * poseToMatrix(views[index].pose);
		glm::mat4x4 relativeArx = xrToArx * relativeXR * xrToArx;
		relativeArx[3][0] *= ArxUnitsPerMeter;
		relativeArx[3][1] *= ArxUnitsPerMeter;
		relativeArx[3][2] *= ArxUnitsPerMeter;

		const Anglef bodyYaw(0.f, bodyCamera.angle.getYaw(), 0.f);
		glm::mat4x4 bodyToWorld = glm::translate(glm::mat4x4(1.f), bodyCamera.m_pos)
		                              * glm::inverse(toRotationMatrix(bodyYaw));
		glm::mat4x4 cameraToWorld = bodyToWorld * relativeArx;

		VREyeRenderInfo result;
		result.camera = bodyCamera;
		result.camera.m_pos = Vec3f(cameraToWorld[3]);
		const Vec3f forward = glm::normalize(Vec3f(cameraToWorld * Vec4f(0.f, 0.f, 1.f, 0.f)));
		result.camera.angle = vectorToAngle(forward);
		result.viewport = Rect(swapchains[index].width, swapchains[index].height);
		result.worldToView = glm::inverse(cameraToWorld);
		result.viewToClip = createProjection(views[index].fov, 1.f, bodyCamera.cdepth);
		return result;
	}
};

OpenXRSystem::OpenXRSystem() = default;

OpenXRSystem::~OpenXRSystem() {
	shutdown();
}

bool OpenXRSystem::initialize() {
	if(m_impl) {
		return true;
	}

	auto implementation = std::make_unique<Impl>();
	if(!implementation->createInstance() || !implementation->createSystemAndSession()) {
		m_impl = std::move(implementation);
		shutdown();
		return false;
	}

	m_impl = std::move(implementation);
	LogInfo << "OpenXR: VR renderer initialized";
	return true;
}

void OpenXRSystem::shutdown() {
	if(!m_impl) {
		return;
	}

	for(Impl::Swapchain & swapchain : m_impl->swapchains) {
		if(swapchain.framebuffer) {
			glDeleteFramebuffers(1, &swapchain.framebuffer);
		}
		if(swapchain.depthBuffer) {
			glDeleteRenderbuffers(1, &swapchain.depthBuffer);
		}
		if(swapchain.handle != XR_NULL_HANDLE) {
			xrDestroySwapchain(swapchain.handle);
		}
	}
	if(m_impl->referenceSpace != XR_NULL_HANDLE) {
		xrDestroySpace(m_impl->referenceSpace);
	}
	if(m_impl->session != XR_NULL_HANDLE) {
		xrDestroySession(m_impl->session);
	}
	if(m_impl->instance != XR_NULL_HANDLE) {
		xrDestroyInstance(m_impl->instance);
	}
	m_impl.reset();
}

bool OpenXRSystem::isInitialized() const {
	return bool(m_impl);
}

bool OpenXRSystem::isSessionRunning() const {
	return m_impl && m_impl->sessionRunning;
}

bool OpenXRSystem::renderFrame(const Camera & bodyCamera, const RenderEye & renderEye) {
	if(!m_impl) {
		return false;
	}
	m_impl->pollEvents();
	if(!m_impl->sessionRunning) {
		return false;
	}

	XrFrameState frameState { XR_TYPE_FRAME_STATE };
	XrFrameWaitInfo waitInfo { XR_TYPE_FRAME_WAIT_INFO };
	if(!m_impl->check(xrWaitFrame(m_impl->session, &waitInfo, &frameState), "xrWaitFrame")) {
		return false;
	}
	XrFrameBeginInfo beginInfo { XR_TYPE_FRAME_BEGIN_INFO };
	if(!m_impl->check(xrBeginFrame(m_impl->session, &beginInfo), "xrBeginFrame")) {
		return false;
	}

	std::vector<XrCompositionLayerProjectionView> projectionViews;
	bool submitted = false;
	if(frameState.shouldRender) {
		XrViewState viewState { XR_TYPE_VIEW_STATE };
		XrViewLocateInfo locateInfo { XR_TYPE_VIEW_LOCATE_INFO };
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = frameState.predictedDisplayTime;
		locateInfo.space = m_impl->referenceSpace;
		uint32_t viewCount = 0;
		if(m_impl->check(xrLocateViews(m_impl->session, &locateInfo, &viewState,
		                                    uint32_t(m_impl->views.size()), &viewCount,
		                                    m_impl->views.data()),
		                 "xrLocateViews")
		   && viewCount == m_impl->swapchains.size()
		   && (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
		   && (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {

			if(!m_impl->calibrated) {
				m_impl->calibrateFromViews(viewCount);
			}

			GLint previousFramebuffer = 0;
			GLint previousViewport[4] = { 0, 0, 0, 0 };
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
			glGetIntegerv(GL_VIEWPORT, previousViewport);
			projectionViews.resize(viewCount);
			bool allViewsRendered = true;

			for(uint32_t i = 0; i < viewCount; ++i) {
				Impl::Swapchain & swapchain = m_impl->swapchains[i];
				uint32_t imageIndex = 0;
				XrSwapchainImageAcquireInfo acquireInfo {
					XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
				};
				if(!m_impl->check(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo,
				                                              &imageIndex),
				                  "xrAcquireSwapchainImage")) {
					allViewsRendered = false;
					break;
				}
				XrSwapchainImageWaitInfo imageWaitInfo { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
				imageWaitInfo.timeout = XR_INFINITE_DURATION;
				if(!m_impl->check(xrWaitSwapchainImage(swapchain.handle, &imageWaitInfo),
				                  "xrWaitSwapchainImage")) {
					XrSwapchainImageReleaseInfo releaseInfo {
						XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
					};
					m_impl->check(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo),
					              "xrReleaseSwapchainImage(after failed wait)");
					allViewsRendered = false;
					break;
				}

				glBindFramebuffer(GL_FRAMEBUFFER, swapchain.framebuffer);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
				                       swapchain.images[imageIndex].image, 0);
				if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
					LogError << "OpenXR: incomplete eye framebuffer";
					allViewsRendered = false;
				} else {
					VREyeRenderInfo eye = m_impl->makeEye(bodyCamera, i);
					renderEye(eye);
					glFlush();
				}

				if(i + 1 == viewCount && previousViewport[2] > 0 && previousViewport[3] > 0) {
					glBindFramebuffer(GL_READ_FRAMEBUFFER, swapchain.framebuffer);
					glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousFramebuffer));
					glBlitFramebuffer(0, 0, swapchain.width, swapchain.height,
					                  previousViewport[0], previousViewport[1],
					                  previousViewport[0] + previousViewport[2],
					                  previousViewport[1] + previousViewport[3],
					                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
				}

				XrSwapchainImageReleaseInfo releaseInfo {
					XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
				};
				m_impl->check(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo),
				              "xrReleaseSwapchainImage");

				XrCompositionLayerProjectionView & projectionView = projectionViews[i];
				projectionView = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
				projectionView.pose = m_impl->views[i].pose;
				projectionView.fov = m_impl->views[i].fov;
				projectionView.subImage.swapchain = swapchain.handle;
				projectionView.subImage.imageRect.offset = { 0, 0 };
				projectionView.subImage.imageRect.extent = {
					swapchain.width, swapchain.height
				};
				projectionView.subImage.imageArrayIndex = 0;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFramebuffer));
			glViewport(previousViewport[0], previousViewport[1],
			           previousViewport[2], previousViewport[3]);
			submitted = allViewsRendered && projectionViews.size() == viewCount;
		}
	}

	XrCompositionLayerProjection layer { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	layer.space = m_impl->referenceSpace;
	layer.viewCount = uint32_t(projectionViews.size());
	layer.views = projectionViews.data();
	const XrCompositionLayerBaseHeader * layers[] = {
		reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)
	};
	XrFrameEndInfo endInfo { XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = m_impl->blendMode;
	endInfo.layerCount = submitted ? 1u : 0u;
	endInfo.layers = submitted ? layers : nullptr;
	const bool frameEnded = m_impl->check(xrEndFrame(m_impl->session, &endInfo),
	                                      "xrEndFrame");
	return frameEnded && submitted;
}

void OpenXRSystem::recenter() {
	if(m_impl) {
		m_impl->calibrated = false;
	}
}
