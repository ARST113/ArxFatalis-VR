/* Copyright (2021-2023) Bytedance Ltd. and/or its affiliates, All rights reserved. */
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "pch.h"
#include "common.h"
#include "options.h"
#include "application.h"
#include "controller.h"
#include "hand.h"
#include <common/xr_linear.h>
#include "logger.h"
#include "gui.h"
#include "ray.h"
#include "text.h"
#include "player.h"
#include "utils.h"
#include "graphicsplugin.h"
#include "cube.h"
#include "shader.h"
#include "../arxvr_bridge_abi.h"
#include <chrono>

class Application : public IApplication {
public:
    Application(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin);
    virtual ~Application() override;
    virtual bool initialize(const XrInstance instance, const XrSession session, Extentions* extentions) override;
    virtual void setHapticCallback(void* arg, hapticCallback hapticCb) override;
    virtual void setControllerPose(int leftright, const XrPosef& pose) override;
    virtual void setControllerGripPose(int leftright, const XrPosef& pose) override;
    virtual void setControllerPower(int leftright, int power) override;
    virtual void setGazeLocation(XrSpaceLocation& gazeLocation, std::vector<XrView>& views, float ipd, XrResult result = XR_SUCCESS) override;
    virtual void setHandJointLocation(XrHandJointLocationEXT* location) override;
    virtual void inputEvent(int leftright, const ApplicationEvent& event) override;
    virtual void onReferenceSpaceChanged() override;
    virtual void setTrackingFrameHead(const XrPosef& pose, bool focused) override;
    virtual void onSessionFocusLost() override;
    virtual void renderFrame(const XrPosef& pose, const glm::mat4& project, const glm::mat4& view, int32_t eye) override;
private:
    bool resolveArxEngine();
    bool ensureArxRenderTarget(int width, int height);
    bool ensureArxPanelRenderer();
    bool renderArxToCurrentEye(const XrPosef& pose, int32_t eye, const glm::mat4& project, const glm::mat4& view);
    void renderArxTexture(const XrPosef& pose, int32_t eye, const glm::mat4& project,
                          const glm::mat4& view, bool immersive);
    bool captureArxEye(int32_t eye, const GLint viewport[4]);
    bool captureHoldEye(int32_t eye, const GLint viewport[4]);
    void layout();
    void showDashboard(const glm::mat4& project, const glm::mat4& view);
    void showDashboardController();
    void showDeviceInformation(const glm::mat4& project, const glm::mat4& view);
    void renderEyeTracking(const glm::mat4& project, const glm::mat4& view, int32_t eye);
    void renderHandTracking(const glm::mat4& project, const glm::mat4& view);
    void renderControllerHands(const glm::mat4& project, const glm::mat4& view);
    void getAllVideoFiles(const std::string& path, std::vector<std::string>& files);
    void startPlayVideo(const std::string& file);
    void haptic(int leftright, float amplitude, float frequency, float duration/*seconds*/);
    // Calculate the angle between the vector v and the plane normal vector n
    float angleBetweenVectorAndPlane(const glm::vec3& vector, const glm::vec3& normal);


    hapticCallback mHapticCallback;
    void* mHapticCallbackArg;

private:
    std::shared_ptr<IGraphicsPlugin> mGraphicsPlugin;
    std::shared_ptr<Controller> mController;
    std::shared_ptr<Hand> mHands;
    std::shared_ptr<Ray> mEyeTrackingRay;
    std::shared_ptr<Gui> mPanel;
    std::shared_ptr<Text> mTextRender;
    std::shared_ptr<Player> mPlayer;
    glm::mat4 mControllerModel;
    XrPosef mControllerPose[HAND_COUNT];
    bool mControllerPoseValid[HAND_COUNT] = {false, false};
    std::shared_ptr<CubeRender> mCubeRender;

    //openxr
    XrInstance m_instance;          //Keep the same naming as openxr_program.cpp
    XrSession m_session;
    Extentions* m_extentions;
    XrSpaceLocation m_gazeLocation;
    std::vector<XrView> m_views;
    float mIpd;
    XrHandJointLocationEXT m_jointLocations[HAND_COUNT][XR_HAND_JOINT_COUNT_EXT];

    //app data
    std::string mDeviceModel;
    std::string mDeviceOS;

    bool mIsShowDashboard = true;

    std::vector<std::string> mAllVideoFiles;
    int32_t mCount = 0;

    ApplicationEvent mControllerEvent[HAND_COUNT] = {};

    using ArxEngineStartFn = int (*)(int width, int height);
    using ArxEngineFrameFn = int (*)(int eye, float verticalFovRadians);
    using ArxEngineIsInGameFn = int (*)();
    using ArxEngineIsCinematicFn = int (*)();
    using ArxEngineVrRenderStateFn = unsigned (*)();
    using ArxEngineGetVisualStateFn = int (*)(int eye, ArxVrVisualState* state);
    using ArxEngineRecenter2dFn = void (*)();
    using ArxEngineStopFn = void (*)();
    void* mArxLibrary = nullptr;
    ArxEngineStartFn mArxEngineStart = nullptr;
    ArxEngineFrameFn mArxEngineFrame = nullptr;
    ArxEngineIsInGameFn mArxEngineIsInGame = nullptr;
    ArxEngineIsCinematicFn mArxEngineIsCinematic = nullptr;
    ArxEngineVrRenderStateFn mArxEngineVrRenderState = nullptr;
    ArxEngineGetVisualStateFn mArxEngineGetVisualState = nullptr;
    ArxEngineRecenter2dFn mArxEngineRecenter2d = nullptr;
    ArxEngineStopFn mArxEngineStop = nullptr;
    GLuint mArxFramebuffer = 0;
    GLuint mArxColorTexture = 0;
    GLuint mArxStereoFramebuffer[2] = {0, 0};
    GLuint mArxStereoColorTexture[2] = {0, 0};
    GLuint mArxStereoDepthBuffer[2] = {0, 0};
    bool mArxStereoColorValid[2] = {false, false};
    GLuint mArxDepthBuffer = 0;
    int mArxWidth = 0;
    int mArxHeight = 0;
    int mArxEngineStatus = 0;
    bool mArxBridgeResolutionAttempted = false;
    Shader mArxPanelShader;
    GLuint mArxPanelVao = 0;
    GLuint mArxPanelVbo = 0;
    bool mArxPanelReady = false;
    bool mArxMenuAnchorValid = false;
    bool mArxHeadFocused = false;
    XrPosef mArxHeadPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    glm::vec3 mArxMenuCenter{0.0f};
    glm::mat4 mArxMenuModel{1.0f};
    unsigned mArxVrState = 0;
    bool mArxStereoForFrame = false;
    // Both eyes must be rendered from the same current tracking sample. Reusing
    // an eye from the preceding frame makes a head turn expose stale/black
    // borders and creates visible stereo judder. Measure and optimize the real
    // dual-eye path instead of sacrificing head-tracked correctness.
    bool mArxAlternatingStereo = false;
    int mArxNextStereoEye = 0;
    bool mArxWasImmersive = false;
    int mArxCinematicFrames = 0;
    int mArxMainMenuFrames = 0;
    bool mArxEyeCaptured[2] = {false, false};
    double mArxEyeCpuMilliseconds[2] = {0.0, 0.0};
    uint32_t mArxEyeCpuSamples[2] = {0u, 0u};
    ArxVrVisualState mArxVisualState[2] = {};
    bool mArxVisualStateValid[2] = {false, false};
    glm::vec3 mArxHoldLastHand[2] = {glm::vec3(0.0f), glm::vec3(0.0f)};
    uint32_t mArxHoldStableFrames[2] = {0u, 0u};
    bool mArxHoldPoseSeen[2] = {false, false};
    bool mArxHoldEyeCaptured[2] = {false, false};

};

std::shared_ptr<IApplication> createApplication(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin) {
    return std::make_shared<Application>(options, graphicsPlugin);
}

Application::Application(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin) {
    mGraphicsPlugin = graphicsPlugin;
    mController = std::make_shared<Controller>();
    mHands = std::make_shared<Hand>();
    mEyeTrackingRay = std::make_shared<Ray>();
    mPanel = std::make_shared<Gui>("dashboard");
    mTextRender = std::make_shared<Text>();
    mPlayer = std::make_shared<Player>();
    mHapticCallback = nullptr;
    mCubeRender = std::make_shared<CubeRender>();
}

Application::~Application() {
    if (mArxEngineStatus == 1 && mArxEngineStop) {
        mArxEngineStop();
    }
    if (mArxDepthBuffer) {
        glDeleteRenderbuffers(1, &mArxDepthBuffer);
    }
    if (mArxStereoDepthBuffer[0] || mArxStereoDepthBuffer[1]) {
        glDeleteRenderbuffers(2, mArxStereoDepthBuffer);
    }
    if (mArxColorTexture) {
        glDeleteTextures(1, &mArxColorTexture);
    }
    if (mArxStereoColorTexture[0] || mArxStereoColorTexture[1]) {
        glDeleteTextures(2, mArxStereoColorTexture);
    }
    if (mArxStereoFramebuffer[0] || mArxStereoFramebuffer[1]) {
        glDeleteFramebuffers(2, mArxStereoFramebuffer);
    }
    if (mArxFramebuffer) {
        glDeleteFramebuffers(1, &mArxFramebuffer);
    }
    if (mArxPanelVbo) {
        glDeleteBuffers(1, &mArxPanelVbo);
    }
    if (mArxPanelVao) {
        glDeleteVertexArrays(1, &mArxPanelVao);
    }
}

bool Application::resolveArxEngine() {
    if (mArxEngineStart && mArxEngineFrame) {
        return true;
    }
    if (mArxBridgeResolutionAttempted) {
        return false;
    }
    mArxBridgeResolutionAttempted = true;

    mArxLibrary = dlopen("libarx.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mArxLibrary) {
        mArxLibrary = dlopen("libarx.so", RTLD_NOW);
    }
    if (!mArxLibrary) {
        errorf("ArxVR renderer: libarx.so unavailable: %s", dlerror());
        return false;
    }

    mArxEngineStart = reinterpret_cast<ArxEngineStartFn>(dlsym(mArxLibrary, "arxvr_engine_start"));
    mArxEngineFrame = reinterpret_cast<ArxEngineFrameFn>(dlsym(mArxLibrary, "arxvr_engine_frame"));
    mArxEngineIsInGame = reinterpret_cast<ArxEngineIsInGameFn>(dlsym(mArxLibrary, "arxvr_engine_is_in_game"));
    mArxEngineIsCinematic = reinterpret_cast<ArxEngineIsCinematicFn>(dlsym(mArxLibrary, "arxvr_engine_is_cinematic"));
    mArxEngineVrRenderState = reinterpret_cast<ArxEngineVrRenderStateFn>(
        dlsym(mArxLibrary, "arxvr_engine_vr_render_state"));
    mArxEngineGetVisualState = reinterpret_cast<ArxEngineGetVisualStateFn>(
        dlsym(mArxLibrary, "arxvr_engine_get_visual_state"));
    mArxEngineRecenter2d = reinterpret_cast<ArxEngineRecenter2dFn>(
        dlsym(mArxLibrary, "arxvr_engine_recenter_2d"));
    mArxEngineStop = reinterpret_cast<ArxEngineStopFn>(dlsym(mArxLibrary, "arxvr_engine_stop"));
    if (!mArxEngineStart || !mArxEngineFrame || !mArxEngineIsInGame
        || !mArxEngineIsCinematic || !mArxEngineVrRenderState
        || !mArxEngineGetVisualState || !mArxEngineRecenter2d || !mArxEngineStop) {
        errorf("ArxVR renderer: engine ABI is incomplete: %s", dlerror());
        mArxEngineStart = nullptr;
        mArxEngineFrame = nullptr;
        mArxEngineIsInGame = nullptr;
        mArxEngineIsCinematic = nullptr;
        mArxEngineVrRenderState = nullptr;
        mArxEngineGetVisualState = nullptr;
        mArxEngineRecenter2d = nullptr;
        mArxEngineStop = nullptr;
        return false;
    }

    infof("ArxVR renderer bridge resolved");
    return true;
}

void Application::onSessionFocusLost() {
    // The runtime may skip rendering entirely while the headset is asleep.
    // Remember the loss from the event, then wait for a fresh valid head pose.
    mArxHeadFocused = false;
}

void Application::setTrackingFrameHead(const XrPosef& pose, bool focused) {
    mArxHeadPose = pose;
    if (focused && !mArxHeadFocused && !mArxStereoForFrame) {
        // A menu may already have been drawn while the headset was on a table
        // or behind the system dashboard. Re-anchor once when it is actually
        // usable, not continuously as the user looks around. Reset before the
        // engine consumes this same frame's head/controller sample.
        mArxMenuAnchorValid = false;
        if (mArxEngineRecenter2d) {
            mArxEngineRecenter2d();
        }
        infof("ArxVR menu re-anchor requested on focus return");
    }
    mArxHeadFocused = focused;
}

void Application::onReferenceSpaceChanged() {
    if (resolveArxEngine() && mArxEngineRecenter2d) {
        // OpenXR has rebased LOCAL space (Guardian/system recenter). The next
        // engine frame must treat the first pose in that new space as neutral;
        // otherwise the coordinate discontinuity is consumed as a real body
        // turn and can leave the avatar facing 180 degrees away from the view.
        mArxEngineRecenter2d();
        mArxMenuAnchorValid = false;
        infof("ArxVR head pose rebased after OpenXR reference-space change");
    }
}

bool Application::ensureArxPanelRenderer() {
    if (mArxPanelReady) {
        return true;
    }

    const GLchar* vertexShader = R"_(
        #version 320 es
        precision highp float;
        layout (location = 0) in vec3 position;
        layout (location = 1) in vec2 texCoord;
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        out vec2 uv;
        void main() {
            uv = texCoord;
            gl_Position = projection * view * model * vec4(position, 1.0);
        }
    )_";
    const GLchar* fragmentShader = R"_(
        #version 320 es
        precision mediump float;
        uniform sampler2D colorTexture;
        in vec2 uv;
        layout (location = 0) out vec4 outColor;
        void main() {
            outColor = texture(colorTexture, uv);
        }
    )_";
    if (!mArxPanelShader.loadShader(vertexShader, fragmentShader)) {
        errorf("ArxVR renderer: failed to create the native texture shader");
        return false;
    }

    const GLfloat vertices[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f, 0.0f, 0.0f, 1.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &mArxPanelVao);
    glGenBuffers(1, &mArxPanelVbo);
    glBindVertexArray(mArxPanelVao);
    glBindBuffer(GL_ARRAY_BUFFER, mArxPanelVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                          reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    mArxPanelReady = true;
    infof("ArxVR native texture renderer initialized");
    return true;
}

bool Application::ensureArxRenderTarget(int width, int height) {
    if (mArxFramebuffer && mArxWidth == width && mArxHeight == height) {
        return true;
    }

    if (mArxDepthBuffer) glDeleteRenderbuffers(1, &mArxDepthBuffer);
    if (mArxStereoDepthBuffer[0] || mArxStereoDepthBuffer[1]) {
        glDeleteRenderbuffers(2, mArxStereoDepthBuffer);
    }
    if (mArxColorTexture) glDeleteTextures(1, &mArxColorTexture);
    if (mArxStereoColorTexture[0] || mArxStereoColorTexture[1]) {
        glDeleteTextures(2, mArxStereoColorTexture);
    }
    if (mArxStereoFramebuffer[0] || mArxStereoFramebuffer[1]) {
        glDeleteFramebuffers(2, mArxStereoFramebuffer);
    }
    if (mArxFramebuffer) glDeleteFramebuffers(1, &mArxFramebuffer);
    mArxDepthBuffer = 0;
    mArxStereoDepthBuffer[0] = mArxStereoDepthBuffer[1] = 0;
    mArxColorTexture = 0;
    mArxStereoColorTexture[0] = mArxStereoColorTexture[1] = 0;
    mArxStereoFramebuffer[0] = mArxStereoFramebuffer[1] = 0;
    mArxStereoColorValid[0] = mArxStereoColorValid[1] = false;
    mArxFramebuffer = 0;

    glGenFramebuffers(1, &mArxFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mArxFramebuffer);

    glGenTextures(1, &mArxColorTexture);
    glBindTexture(GL_TEXTURE_2D, mArxColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mArxColorTexture, 0);

    glGenTextures(2, mArxStereoColorTexture);
    for (GLuint texture : mArxStereoColorTexture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, mArxColorTexture);

    glGenRenderbuffers(1, &mArxDepthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, mArxDepthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mArxDepthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        errorf("ArxVR renderer: offscreen framebuffer is incomplete");
        return false;
    }

    // Keep each immersive eye attached to its own permanent FBO. Reattaching a
    // texture or copying the completed legacy frame every display frame forces
    // GL4ES/GLES synchronization and costs several CPU milliseconds on PICO.
    glGenFramebuffers(2, mArxStereoFramebuffer);
    glGenRenderbuffers(2, mArxStereoDepthBuffer);
    for (int eye = 0; eye < 2; ++eye) {
        glBindFramebuffer(GL_FRAMEBUFFER, mArxStereoFramebuffer[eye]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               mArxStereoColorTexture[eye], 0);
        glBindRenderbuffer(GL_RENDERBUFFER, mArxStereoDepthBuffer[eye]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, mArxStereoDepthBuffer[eye]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            errorf("ArxVR renderer: stereo framebuffer %d is incomplete", eye);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mArxFramebuffer);

    mArxWidth = width;
    mArxHeight = height;
    infof("ArxVR optimized stereo render target created: %dx%d", width, height);
    return true;
}

void Application::renderArxTexture(const XrPosef& pose, int32_t eye, const glm::mat4& project,
                                   const glm::mat4& view, bool immersive) {
    // GL4ES emulates legacy desktop GL and can leave a benign error queued.
    // Drain it before native GLES calls so diagnostics identify the real caller.
    while (glGetError() != GL_NO_ERROR) {
    }

    GLint previousProgram = 0;
    GLint previousVao = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture0 = 0;
    GLboolean previousColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean previousDepthMask = GL_TRUE;
    const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousStencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    GLuint presentationTexture = mArxColorTexture;
    if (immersive && eye >= 0 && eye < 2 && mArxStereoColorValid[eye]) {
        presentationTexture = mArxStereoColorTexture[eye];
    }
    glBindTexture(GL_TEXTURE_2D, presentationTexture);

    glm::mat4 textureProjection(1.0f);
    glm::mat4 textureView(1.0f);
    glm::mat4 textureModel(1.0f);
    if (immersive) {
        textureModel = glm::scale(textureModel, glm::vec3(2.0f, 2.0f, 1.0f));
    } else {
        const glm::quat headOrientation = glm::normalize(glm::quat(
            mArxHeadPose.orientation.w, mArxHeadPose.orientation.x,
            mArxHeadPose.orientation.y, mArxHeadPose.orientation.z));
        const glm::vec3 trackedForward = glm::normalize(
            headOrientation * glm::vec3(0.0f, 0.0f, -1.0f));
        constexpr float kMenuDistanceMetres = 1.8f;
        constexpr float kMenuWidthMetres = 1.05f;
        const float aspect = std::max(float(mArxWidth) / float(mArxHeight), 0.1f);
        const float menuHeight = kMenuWidthMetres / aspect;

        if (!mArxMenuAnchorValid && eye == 0) {
            // Anchor once in a level, yaw-only world basis. Pitch and roll from
            // the opening gaze must never send the menu into the sky or tilt it,
            // and later head motion must not rotate the panel like a HUD.
            glm::vec3 forward = trackedForward;
            forward.y = 0.0f;
            if (glm::length(forward) < 0.001f) {
                forward = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                forward = glm::normalize(forward);
            }
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(forward, up));
            // Use the centre-head sample sent to the engine, not the optional
            // eye-tracking views (which stop updating when gaze is inactive)
            // nor an IPD offset guessed in an upright rather than rolled basis.
            const glm::vec3 headCenter(mArxHeadPose.position.x,
                                       mArxHeadPose.position.y,
                                       mArxHeadPose.position.z);
            mArxMenuCenter = headCenter + forward * kMenuDistanceMetres;
            mArxMenuModel = glm::mat4(1.0f);
            mArxMenuModel[0] = glm::vec4(right, 0.0f);
            mArxMenuModel[1] = glm::vec4(up, 0.0f);
            mArxMenuModel[2] = glm::vec4(-forward, 0.0f);
            mArxMenuModel[3] = glm::vec4(mArxMenuCenter, 1.0f);
            mArxMenuModel = glm::scale(mArxMenuModel,
                                       glm::vec3(kMenuWidthMetres, menuHeight, 1.0f));
            mArxMenuAnchorValid = true;

            const float orthogonality = std::max(
                std::abs(glm::dot(right, up)),
                std::max(std::abs(glm::dot(right, forward)),
                         std::abs(glm::dot(up, forward))));
            const glm::vec3 halfRight = right * (kMenuWidthMetres * 0.5f);
            const glm::vec3 halfUp = up * (menuHeight * 0.5f);
            const glm::vec3 corners[] = {
                mArxMenuCenter - halfRight - halfUp,
                mArxMenuCenter + halfRight - halfUp,
                mArxMenuCenter - halfRight + halfUp,
                mArxMenuCenter + halfRight + halfUp
            };
            float minimumDepth = 1000.0f;
            float maximumDepth = -1000.0f;
            for (const glm::vec3& corner : corners) {
                const float depth = glm::dot(corner - mArxMenuCenter, forward);
                minimumDepth = std::min(minimumDepth, depth);
                maximumDepth = std::max(maximumDepth, depth);
            }
            infof("ArxVR 2D panel anchored: fixedPosition=1 fixedOrientation=1 "
                  "worldUpright=1 size=%.3fx%.3fm cornerDepthSpread=%.3fmm "
                  "orthogonalityError=%.6f centerY=%.3f",
                  kMenuWidthMetres, menuHeight,
                  (maximumDepth - minimumDepth) * 1000.0f, orthogonality,
                  mArxMenuCenter.y);
        }
        textureProjection = project;
        textureView = view;
        textureModel = mArxMenuModel;
    }

    mArxPanelShader.use();
    mArxPanelShader.setUniformInt("colorTexture", 0);
    mArxPanelShader.setUniformMat4("projection", textureProjection);
    mArxPanelShader.setUniformMat4("view", textureView);
    mArxPanelShader.setUniformMat4("model", textureModel);
    glBindVertexArray(mArxPanelVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, previousTexture0);
    glBindVertexArray(previousVao);
    glUseProgram(previousProgram);
    glColorMask(previousColorMask[0], previousColorMask[1],
                previousColorMask[2], previousColorMask[3]);
    glDepthMask(previousDepthMask);
    const auto restoreEnable = [](GLenum capability, GLboolean enabled) {
        if (enabled) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    };
    restoreEnable(GL_SCISSOR_TEST, previousScissor);
    restoreEnable(GL_BLEND, previousBlend);
    restoreEnable(GL_DEPTH_TEST, previousDepth);
    restoreEnable(GL_STENCIL_TEST, previousStencil);
    restoreEnable(GL_CULL_FACE, previousCull);
    glActiveTexture(previousActiveTexture);
}

bool Application::captureArxEye(int32_t eye, const GLint viewport[4]) {
    if (eye < 0 || eye > 1 || mArxEyeCaptured[eye]) {
        return false;
    }
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<unsigned char> rgba(size_t(width) * size_t(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(viewport[0], viewport[1], width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (glGetError() != GL_NO_ERROR) {
        errorf("ArxVR eye capture: glReadPixels failed for eye %d", eye);
        return false;
    }

#pragma pack(push, 1)
    struct BitmapFileHeader {
        uint16_t type;
        uint32_t size;
        uint16_t reserved1;
        uint16_t reserved2;
        uint32_t pixelOffset;
    };
    struct BitmapInfoHeader {
        uint32_t size;
        int32_t width;
        int32_t height;
        uint16_t planes;
        uint16_t bitCount;
        uint32_t compression;
        uint32_t imageSize;
        int32_t xPixelsPerMeter;
        int32_t yPixelsPerMeter;
        uint32_t colorsUsed;
        uint32_t importantColors;
    };
#pragma pack(pop)

    const uint32_t rowSize = (uint32_t(width) * 3u + 3u) & ~3u;
    const uint32_t imageSize = rowSize * uint32_t(height);
    const BitmapFileHeader fileHeader{0x4d42u,
        uint32_t(sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader)) + imageSize,
        0u, 0u, uint32_t(sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader))};
    const BitmapInfoHeader infoHeader{sizeof(BitmapInfoHeader), width, height, 1u, 24u,
        0u, imageSize, 2835, 2835, 0u, 0u};

    mkdir("/sdcard/Android/data/com.arxvr.android/files/user/diagnostics", 0755);
    const char* filename = eye == 0
        ? "/sdcard/Android/data/com.arxvr.android/files/user/diagnostics/cutscene-eye-left.bmp"
        : "/sdcard/Android/data/com.arxvr.android/files/user/diagnostics/cutscene-eye-right.bmp";
    FILE* output = fopen(filename, "wb");
    if (!output) {
        errorf("ArxVR eye capture: cannot open %s", filename);
        return false;
    }
    fwrite(&fileHeader, sizeof(fileHeader), 1, output);
    fwrite(&infoHeader, sizeof(infoHeader), 1, output);
    std::vector<unsigned char> row(rowSize, 0u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t source = (size_t(y) * size_t(width) + size_t(x)) * 4u;
            const size_t target = size_t(x) * 3u;
            row[target + 0] = rgba[source + 2];
            row[target + 1] = rgba[source + 1];
            row[target + 2] = rgba[source + 0];
        }
        fwrite(row.data(), row.size(), 1, output);
    }
    fclose(output);
    mArxEyeCaptured[eye] = true;
    infof("ArxVR captured cutscene eye %d to %s", eye, filename);
    return true;
}

bool Application::captureHoldEye(int32_t eye, const GLint viewport[4]) {
    if (eye < 0 || eye > 1 || mArxHoldEyeCaptured[eye]) {
        return false;
    }
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<unsigned char> rgba(size_t(width) * size_t(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(viewport[0], viewport[1], width, height,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (glGetError() != GL_NO_ERROR) {
        errorf("ArxVR hold capture: glReadPixels failed for eye %d", eye);
        return false;
    }

    mkdir("/sdcard/Android/data/com.arxvr.android/files/user/diagnostics", 0755);
    const char* filename = eye == 0
        ? "/sdcard/Android/data/com.arxvr.android/files/user/diagnostics/hold-eye-left.ppm"
        : "/sdcard/Android/data/com.arxvr.android/files/user/diagnostics/hold-eye-right.ppm";
    FILE* output = fopen(filename, "wb");
    if (!output) {
        errorf("ArxVR hold capture: cannot open %s", filename);
        return false;
    }
    fprintf(output, "P6\n%d %d\n255\n", width, height);
    std::vector<unsigned char> row(size_t(width) * 3u);
    // OpenGL's origin is bottom-left; PPM viewers expect the first row at top.
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const size_t source = (size_t(y) * size_t(width) + size_t(x)) * 4u;
            const size_t target = size_t(x) * 3u;
            row[target + 0] = rgba[source + 0];
            row[target + 1] = rgba[source + 1];
            row[target + 2] = rgba[source + 2];
        }
        fwrite(row.data(), row.size(), 1, output);
    }
    fclose(output);
    mArxHoldEyeCaptured[eye] = true;
    infof("ArxVR captured extended-hold eye %d to %s", eye, filename);
    return true;
}

bool Application::renderArxToCurrentEye(const XrPosef& pose, int32_t eye,
                                        const glm::mat4& project, const glm::mat4& view) {
    if (!resolveArxEngine() || mArxEngineStatus < 0) {
        return false;
    }

    GLint targetFramebuffer = 0;
    GLint targetViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &targetFramebuffer);
    glGetIntegerv(GL_VIEWPORT, targetViewport);
    const int width = targetViewport[2];
    const int height = targetViewport[3];
    if (width <= 0 || height <= 0) {
        return false;
    }

    // 0.60 remained visibly grainy in the headset. Cool-device measurements
    // leave several milliseconds of GPU headroom, so render at 0.70 while
    // retaining the runtime's native 72 Hz submission cadence.
    constexpr float kArxRenderScale = 0.70f;
    const int renderWidth = std::max(640, int(std::lround(float(width) * kArxRenderScale)));
    const int renderHeight = std::max(480, int(std::lround(float(height) * kArxRenderScale)));
    if (!ensureArxRenderTarget(renderWidth, renderHeight)) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        return false;
    }
    if (!ensureArxPanelRenderer()) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        return false;
    }

    const int scheduledStereoEye = mArxAlternatingStereo && mArxStereoForFrame
                                 ? mArxNextStereoEye : 0;
    const GLuint engineFramebuffer = mArxAlternatingStereo && mArxStereoForFrame
                                   ? mArxStereoFramebuffer[scheduledStereoEye]
                                   : mArxFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, engineFramebuffer);
    glViewport(0, 0, mArxWidth, mArxHeight);

    if (mArxEngineStatus == 0) {
        mArxEngineStatus = mArxEngineStart(mArxWidth, mArxHeight);
    }
    const float verticalFovRadians = 2.0f * std::atan(1.0f / std::abs(project[1][1]));
    bool captureEngineFrame = false;
    const bool rendersEngineEye = eye == 0
                               || (mArxStereoForFrame && !mArxAlternatingStereo);
    if (mArxEngineStatus == 1 && rendersEngineEye) {
        // The two real eyes share one legacy depth renderbuffer in dual-eye
        // mode. Clear before *each* eye; retaining the left-eye depth while
        // drawing the right eye rejects valid geometry and produces black
        // holes even when the room itself is visible.
        GLboolean clearColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean clearDepthMask = GL_TRUE;
        GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const GLboolean clearScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetBooleanv(GL_COLOR_WRITEMASK, clearColorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &clearDepthMask);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(previousClearColor[0], previousClearColor[1],
                     previousClearColor[2], previousClearColor[3]);
        glColorMask(clearColorMask[0], clearColorMask[1],
                    clearColorMask[2], clearColorMask[3]);
        glDepthMask(clearDepthMask);
        if (clearScissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        }
    }
    if (mArxEngineStatus == 1) {
        if (eye == 0) {
            const int renderedStereoEye = mArxAlternatingStereo && mArxStereoForFrame
                                        ? mArxNextStereoEye : 0;
            const int engineEye = mArxAlternatingStereo && renderedStereoEye == 1 ? 2 : -1;

            const auto frameStart = std::chrono::steady_clock::now();
            const int frameResult = mArxEngineFrame(engineEye, verticalFovRadians);
            const auto frameEnd = std::chrono::steady_clock::now();
            mArxEyeCpuMilliseconds[renderedStereoEye] +=
                std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
            ++mArxEyeCpuSamples[renderedStereoEye];
            if (frameResult == 1) {
                mArxVisualStateValid[renderedStereoEye] =
                    mArxEngineGetVisualState(renderedStereoEye,
                                             &mArxVisualState[renderedStereoEye]) == 1;
            }
            if (frameResult != 1) {
                mArxEngineStatus = -1;
            } else {
                const unsigned previousState = mArxVrState;
                mArxVrState = mArxEngineVrRenderState();
                mArxStereoForFrame = (mArxVrState & 0x100u) != 0u;
                const bool cinematicChanged = ((previousState ^ mArxVrState) & 0x008u) != 0u;
                const bool isMainMenu = (mArxVrState & 0x001u) != 0u
                                     && (mArxVrState & 0x002u) == 0u;
                const bool enteredMainMenu = isMainMenu
                                          && (previousState == 0u
                                              || (previousState & 0x002u) != 0u);
                const bool enteredStereo = (mArxVrState & 0x100u) != 0u
                                        && (previousState & 0x100u) == 0u;
                const bool exitedStereo = (mArxVrState & 0x100u) == 0u
                                       && (previousState & 0x100u) != 0u;
                if (exitedStereo) {
                    if (engineFramebuffer != mArxFramebuffer) {
                        // Alternating stereo renders into an eye cache, so copy
                        // the newly drawn pause/menu frame to the flat target.
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, engineFramebuffer);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mArxFramebuffer);
                        glBlitFramebuffer(0, 0, mArxWidth, mArxHeight,
                                          0, 0, mArxWidth, mArxHeight,
                                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    }
                    // Dual-eye mode already drew the menu directly into the
                    // flat target, but it still needs a fresh world anchor.
                    // Keeping this inside the copy-only branch made the pause
                    // panel inherit an obsolete pose and appear to fly away.
                    glBindFramebuffer(GL_FRAMEBUFFER, mArxFramebuffer);
                    mArxMenuAnchorValid = false;
                    mArxEngineRecenter2d();
                    infof("ArxVR stereo to world-fixed 2D transition complete and menu re-anchored");
                }
                if (cinematicChanged || enteredMainMenu) {
                    mArxMenuAnchorValid = false;
                    mArxEngineRecenter2d();
                    infof("ArxVR 2D screen re-anchor requested for state transition");
                }
                if (enteredStereo) {
                    // The character camera may have turned while the 2D menus and
                    // cutscenes were active. Make the player's current real-world
                    // forward direction the neutral pose for actual VR gameplay.
                    mArxEngineRecenter2d();
                    mArxStereoColorValid[0] = mArxStereoColorValid[1] = false;
                    mArxEyeCpuMilliseconds[0] = mArxEyeCpuMilliseconds[1] = 0.0;
                    mArxEyeCpuSamples[0] = mArxEyeCpuSamples[1] = 0u;
                    infof("ArxVR head pose recentered on stereo gameplay entry");
                }
                if (enteredMainMenu) {
                    mArxEyeCaptured[0] = false;
                }
                if (isMainMenu) {
                    ++mArxMainMenuFrames;
                    captureEngineFrame = mArxMainMenuFrames == 120;
                } else {
                    mArxMainMenuFrames = 0;
                }
                if (mArxVrState != previousState) {
                    infof("ArxVR render state=0x%03x flow=%d menu=%d stopped=%d cinematic=%d "
                          "border=%d blocked=%d camera=%d player=%d stereo=%d",
                          mArxVrState,
                          (mArxVrState & 0x001u) != 0u, (mArxVrState & 0x002u) != 0u,
                          (mArxVrState & 0x004u) != 0u, (mArxVrState & 0x008u) != 0u,
                          (mArxVrState & 0x010u) != 0u, (mArxVrState & 0x020u) != 0u,
                          (mArxVrState & 0x040u) != 0u, (mArxVrState & 0x080u) != 0u,
                          mArxStereoForFrame);
                }

                if (mArxStereoForFrame && mArxAlternatingStereo) {
                    const int cacheEye = enteredStereo ? 0 : renderedStereoEye;
                    mArxStereoColorValid[cacheEye] = true;
                    const int otherEye = 1 - cacheEye;
                    if (enteredStereo && !mArxStereoColorValid[otherEye]) {
                        // The transition frame was rendered into the 2D target
                        // before the engine reported immersive state. Seed both
                        // eye caches once; subsequent frames render in-place.
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, mArxFramebuffer);
                        for (int eyeToSeed = 0; eyeToSeed < 2; ++eyeToSeed) {
                            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                              mArxStereoFramebuffer[eyeToSeed]);
                            glBlitFramebuffer(0, 0, mArxWidth, mArxHeight,
                                              0, 0, mArxWidth, mArxHeight,
                                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
                        }
                        glBindFramebuffer(GL_FRAMEBUFFER, mArxFramebuffer);
                        mArxStereoColorValid[cacheEye] = true;
                        mArxStereoColorValid[otherEye] = true;
                    }
                    mArxNextStereoEye = otherEye;
                } else if (!mArxStereoForFrame) {
                    mArxStereoColorValid[0] = mArxStereoColorValid[1] = false;
                    mArxNextStereoEye = 0;
                }
            }
        } else if (mArxStereoForFrame && !mArxAlternatingStereo) {
            const auto frameStart = std::chrono::steady_clock::now();
            const int frameResult = mArxEngineFrame(1, verticalFovRadians);
            const auto frameEnd = std::chrono::steady_clock::now();
            mArxEyeCpuMilliseconds[1] += std::chrono::duration<double, std::milli>(
                frameEnd - frameStart).count();
            ++mArxEyeCpuSamples[1];
            if (frameResult == 1) {
                mArxVisualStateValid[1] =
                    mArxEngineGetVisualState(1, &mArxVisualState[1]) == 1;
            }
            if (frameResult != 1) {
                mArxEngineStatus = -1;
            } else if (mArxEyeCpuSamples[0] >= 120u && mArxEyeCpuSamples[1] >= 120u) {
                infof("ARXVR_PERF legacy eye0=%.2fms eye1=%.2fms total=%.2fms samples=%u/%u",
                      mArxEyeCpuMilliseconds[0] / double(mArxEyeCpuSamples[0]),
                      mArxEyeCpuMilliseconds[1] / double(mArxEyeCpuSamples[1]),
                      mArxEyeCpuMilliseconds[0] / double(mArxEyeCpuSamples[0])
                          + mArxEyeCpuMilliseconds[1] / double(mArxEyeCpuSamples[1]),
                      mArxEyeCpuSamples[0], mArxEyeCpuSamples[1]);
                mArxEyeCpuMilliseconds[0] = mArxEyeCpuMilliseconds[1] = 0.0;
                mArxEyeCpuSamples[0] = mArxEyeCpuSamples[1] = 0u;
            }
        } else if (mArxStereoForFrame && mArxAlternatingStereo) {
            if (mArxEyeCpuSamples[0] >= 360u && mArxEyeCpuSamples[1] >= 360u) {
                infof("ARXVR_PERF alternating-stereo left=%.2fms right=%.2fms "
                      "submitted=72Hz scale=%.2f samples=%u/%u",
                      mArxEyeCpuMilliseconds[0] / double(mArxEyeCpuSamples[0]),
                      mArxEyeCpuMilliseconds[1] / double(mArxEyeCpuSamples[1]),
                      kArxRenderScale, mArxEyeCpuSamples[0], mArxEyeCpuSamples[1]);
                mArxEyeCpuMilliseconds[0] = mArxEyeCpuMilliseconds[1] = 0.0;
                mArxEyeCpuSamples[0] = mArxEyeCpuSamples[1] = 0u;
            }
        }
    }

    if (captureEngineFrame) {
        GLint engineFramebufferAfterRender = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &engineFramebufferAfterRender);
        infof("ArxVR engine capture framebuffer after render=%d expected=%d",
              engineFramebufferAfterRender, mArxFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mArxFramebuffer);
        const GLint engineViewport[4] = {0, 0, mArxWidth, mArxHeight};
        captureArxEye(0, engineViewport);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glViewport(targetViewport[0], targetViewport[1], targetViewport[2], targetViewport[3]);
    if (mArxEngineStatus == 1) {
        const bool cinematic = mArxEngineIsCinematic() != 0;
        if (cinematic && eye == 0) {
            ++mArxCinematicFrames;
        } else if (!cinematic) {
            mArxCinematicFrames = 0;
        }
        if (!mArxStereoForFrame && mArxWasImmersive) {
            mArxMenuAnchorValid = false;
        }
        renderArxTexture(pose, eye, project, view, mArxStereoForFrame);

        if (mArxStereoForFrame) {
            // The immersive colour image and its depth attachment are produced
            // with the exact projection/view matrices exported in
            // ArxVrVisualState.  Preserve that depth in the OpenXR target so a
            // held prop can sit between the curled fingers and the palm.  The
            // former colour-only presentation forced the whole hand mesh to be
            // an overlay: even a correctly centred shaft looked as if it were
            // floating behind a closed fist.
            GLuint depthSource = mArxFramebuffer;
            if (eye >= 0 && eye < 2 && mArxStereoColorValid[eye]) {
                depthSource = mArxStereoFramebuffer[eye];
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, depthSource);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFramebuffer);
            glBlitFramebuffer(0, 0, mArxWidth, mArxHeight,
                              targetViewport[0], targetViewport[1],
                              targetViewport[0] + targetViewport[2],
                              targetViewport[1] + targetViewport[3],
                              GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
            glViewport(targetViewport[0], targetViewport[1],
                       targetViewport[2], targetViewport[3]);
        }
        mArxWasImmersive = mArxStereoForFrame;
        if (cinematic && mArxCinematicFrames >= 90) {
            captureArxEye(eye, targetViewport);
        }
    }
    return mArxEngineStatus == 1;
}

void Application::getAllVideoFiles(const std::string& path, std::vector<std::string>& allFiles) {
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        errorf("opendir %s error %d", path.c_str(), errno);
        return;
    }
    struct dirent *file;
    while ((file = readdir(dir)) != nullptr) {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }
        if (file->d_type == DT_DIR) {
            std::string path_next = path + "/" + file->d_name;
            getAllVideoFiles(path_next, allFiles);
        } else {
            std::string fileFullName = path + "/" + file->d_name;
            std::string extension = fileFullName.substr(fileFullName.find_last_of('.') + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](char& c) {
                return std::tolower(c);
            });
            if (extension == "mp4" || extension == "mkv" || extension == "avi") {
                allFiles.push_back(fileFullName);
            }
            //infof("count:%d file:%s", mCount++, fileFullName.c_str());
        }
    }
}

bool Application::initialize(const XrInstance instance, const XrSession session, Extentions* extentions) {
    m_instance = instance;
    m_session = session;
    m_extentions = extentions;

    // get device model
    char buffer[64] = {0};
    __system_property_get("sys.pxr.product.name", buffer);
    mDeviceModel = buffer;

    //get OS version
    __system_property_get("ro.build.id", buffer);
    //__system_property_get("ro.system.build.id", buffer); // You can also call this function, the result is the same
    mDeviceOS = buffer;

    mController->initialize(mDeviceModel);
    mHands->initialize();
    mEyeTrackingRay->initialize();
    mPanel->initialize(600, 800);  //set resolution
    mTextRender->initialize();
    mCubeRender->initialize();

    const XrGraphicsBindingOpenGLESAndroidKHR *binding = reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(mGraphicsPlugin->GetGraphicsBinding());
    mPlayer->initialize(binding->display);

    // This sample used to recursively scan all shared storage for videos.
    // ArxVR never plays external videos, and on scoped-storage Android builds
    // the walk can spend minutes under /sdcard before the first XR frame.
    // Game data is opened by libarx from the app's own external-files path.
    infof("ArxVR external video scan skipped");

    //copyFile("/sdcard/Pictures/Screenshots/20230426-105301.jpg", "/sdcard/Pictures/2.jpg");
    //refreshMedia("/sdcard/Pictures/");

    infof("ArxVR application initialization complete");
    return true;
}

void Application::setHapticCallback(void* arg, hapticCallback hapticCb) {
    mHapticCallbackArg = arg;
    mHapticCallback = hapticCb;
}

void Application::setControllerPower(int leftright, int power) {
    mController->setPowerValue(leftright, power);
}

void Application::setControllerPose(int leftright, const XrPosef& pose) {
    XrMatrix4x4f model{};
    XrVector3f scale{1.0f, 1.0f, 1.0f};
    XrMatrix4x4f_CreateTranslationRotationScale(&model, &pose.position, &pose.orientation, &scale);
    glm::mat4 m = glm::make_mat4((float*)&model);
    mController->setModel(leftright, m);
    // The bundled PICO hand FBX files were authored for the controller aim
    // pose used by the original sample. Attaching them to the differently
    // oriented grip pose twisted both wrists.
    mHands->setModel(leftright, m);
    mControllerPose[leftright] = pose;
    mControllerPoseValid[leftright] = true;
}

void Application::setControllerGripPose(int leftright, const XrPosef& pose) {
    // Grip poses are streamed to the game for physical object interaction.
    // Hand visuals stay on the asset-compatible aim pose in setControllerPose.
    (void)leftright;
    (void)pose;
}

void Application::setGazeLocation(XrSpaceLocation& gazeLocation, std::vector<XrView>& views, float ipd, XrResult result) {
    mIpd = ipd;
    memcpy(&m_gazeLocation, &gazeLocation, sizeof(gazeLocation));
    m_views = views;
}

void Application::setHandJointLocation(XrHandJointLocationEXT* location) {
    memcpy(&m_jointLocations, location, sizeof(m_jointLocations));
}

void Application::startPlayVideo(const std::string& file) {
    //mPlayer->stop();
    mPlayer->start(file);
}

void Application::inputEvent(int leftright, const ApplicationEvent& event) {
    const float previousSqueeze = mControllerEvent[leftright].squeeze;
    mControllerEvent[leftright] = event;
    mHands->setFingerCurl(leftright, event.trigger, event.squeeze);
    if (previousSqueeze < 0.55f && event.squeeze >= 0.55f) {
        haptic(leftright, 0.35f, 0.0f, 0.035f);
    }

    if (event.controllerEventBit & CONTROLLER_EVENT_BIT_click_menu) {
        if (event.click_menu == true) {
            mIsShowDashboard = !mIsShowDashboard;
        }
    }

    if (leftright == HAND_LEFT) {
        return;
    }
    const bool stickPressed = (event.controllerEventBit & CONTROLLER_EVENT_BIT_click_thumbstick)
                           && event.click_thumbstck;
    if (!mArxStereoForFrame && stickPressed) {
        mArxMenuAnchorValid = false;
        if (mArxEngineRecenter2d) {
            mArxEngineRecenter2d();
        }
        infof("ArxVR 2D screen re-anchor requested by right stick click");
    }
    if (event.controllerEventBit & CONTROLLER_EVENT_BIT_click_trigger) {
        //infof("controllerEventBit:0x%02x, event.click_trigger:0x%d", event.controllerEventBit, event.click_trigger);
        mPanel->triggerEvent(event.click_trigger);
    }

}

void Application::layout() {
    glm::mat4 model = glm::mat4(1.0f);
    float scale = 1.0f;

    float width, height;
    mPanel->getWidthHeight(width, height);
    scale = 0.7;
    model = glm::translate(model, glm::vec3(-0.0f, -0.3f, -1.0f));
    model = glm::rotate(model, glm::radians(10.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(scale * (width / height), scale, 1.0f));
    mPanel->setModel(model);

    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(1.0f, -0.0f, -1.5f));
    model = glm::rotate(model, glm::radians(-20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(scale*2, scale, 1.0f));
    mPlayer->setModel(model);
}

void Application::haptic(int leftright, float amplitude, float frequency/*not used now*/, float duration/*seconds*/) {
    if (mHapticCallback) {
        // hapticCallback expects amplitude, duration in seconds, then frequency.
        // Keep Application::haptic's public call order (amplitude, frequency, duration)
        // and translate it here before crossing the callback ABI.
        mHapticCallback(mHapticCallbackArg, leftright, amplitude, duration, frequency);
    }
}

void Application::showDashboardController() {
#define HAND_BIT_LEFT HAND_LEFT+1
#define HAND_BIT_RIGHT HAND_RIGHT+1
#define SHOW_CONTROLLER_ROW_float(x)    ImGui::TableNextRow();\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%s", MEMBER_NAME(ApplicationEvent, x));\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%f", mControllerEvent[HAND_LEFT].x);\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%f", mControllerEvent[HAND_RIGHT].x);

#define SHOW_CONTROLLER_ROW_bool(hand, x)   ImGui::TableNextRow();\
                                            ImGui::TableNextColumn();\
                                            ImGui::Text("%s", MEMBER_NAME(ApplicationEvent, x));\
                                            ImGui::TableNextColumn();\
                                            if (hand & HAND_BIT_LEFT && mControllerEvent[HAND_LEFT].x) {\
                                                ImGui::Text("true");\
                                            }\
                                            ImGui::TableNextColumn();\
                                            if (hand & HAND_BIT_RIGHT && mControllerEvent[HAND_RIGHT].x) {\
                                                ImGui::Text("true");\
                                            }  

    //controller event
    if (ImGui::CollapsingHeader("controller")) {
        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
        static ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("controller event", 3, flags, ImVec2(0.0f, TEXT_BASE_HEIGHT * 19), 0.0f)) {
            ImGui::TableSetupColumn("event name",        ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,   0.0f);
            ImGui::TableSetupColumn("left controller",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,   0.0f);
            ImGui::TableSetupColumn("right controller",  ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch, 0.0f);
            ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
            ImGui::TableHeadersRow();

            SHOW_CONTROLLER_ROW_float(trigger);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_trigger);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, touch_trigger);

            SHOW_CONTROLLER_ROW_float(thumbstick_x);
            SHOW_CONTROLLER_ROW_float(thumbstick_y);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_thumbstck);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, touch_thumbstick);

            SHOW_CONTROLLER_ROW_float(squeeze);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_squeeze);

            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, click_a);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, click_b);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_x);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_y);
            
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, touch_a);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, touch_b);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, touch_x);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, touch_y);

            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_menu);

            ImGui::EndTable();
        }

        for (int i = HAND_LEFT; i < HAND_COUNT; i++) {
            if (mControllerEvent[i].squeeze > 0) {
                haptic(i, mControllerEvent[i].squeeze, 1.0f, 0.02);
            } else {
                haptic(i, 0.0f, 0, 0.0f);
            }
        }
    }
}

void Application::showDashboard(const glm::mat4& project, const glm::mat4& view) {

    // get refreshrate
    uint32_t count = 0;
    m_extentions->xrEnumerateDisplayRefreshRatesFB(m_session, 0, &count, nullptr);
    std::vector<float> refreshRate(count);
    m_extentions->xrEnumerateDisplayRefreshRatesFB(m_session, count, &count, refreshRate.data());
    float currentreFreshRate = 0;
    m_extentions->xrGetDisplayRefreshRateFB(m_session, &currentreFreshRate);
    int currentreFreshRateTmp = (int)currentreFreshRate;

    PlayModel playModel = mPlayer->getPlayStyle();

    const XrPosef& controllerPose = mControllerPose[1];
    glm::vec3 linePoint = glm::make_vec3((float*)&controllerPose.position);
    glm::vec3 lineDirection = mController->getRayDirection(1);
    mPanel->isIntersectWithLine(linePoint, lineDirection);

    mPanel->begin();
    if (ImGui::CollapsingHeader("information")) {
        ImGui::BulletText("device model: %s", mDeviceModel.c_str());
        ImGui::BulletText("device OS: %s", mDeviceOS.c_str());
    }
    
    //test controller
    showDashboardController();

    if (ImGui::CollapsingHeader("framerate")) {
        ImGui::RadioButton("72fps", (int*)&currentreFreshRateTmp, 72); 
        if (refreshRate.size() > 1 || currentreFreshRateTmp == 92) {
            ImGui::SameLine();
            ImGui::RadioButton("90fps", (int*)&currentreFreshRateTmp, 90); 
        }
    }

    if (ImGui::CollapsingHeader("sample options")) {
        if (ImGui::BeginTable("split", 2)) {
            ImGui::TableNextColumn(); ImGui::Checkbox("XR_FB_passthrough", &m_extentions->activePassthrough);
            if (m_extentions->isSupportEyeTracking) {
                ImGui::TableNextColumn(); ImGui::Checkbox("Eye Tracking", &m_extentions->activeEyeTracking);
            }
            ImGui::EndTable();
        }
    }

    int32_t selectFileIndex = -1;
    if (ImGui::CollapsingHeader("video player")) {
        ImGui::SeparatorText("play model");
        ImGui::RadioButton("2D",         (int*)&playModel, (int)playModel_2D); ImGui::SameLine();
        ImGui::RadioButton("2D-180",     (int*)&playModel, (int)playModel_2D_180); ImGui::SameLine();
        ImGui::RadioButton("2D-360",     (int*)&playModel, (int)playModel_2D_360); ImGui::SameLine();
        ImGui::RadioButton("3D-SBS",     (int*)&playModel, (int)playModel_3D_SBS); ImGui::SameLine();
        ImGui::RadioButton("3D-SBS-360", (int*)&playModel, (int)playModel_3D_SBS_360); ImGui::SameLine();
        ImGui::RadioButton("3D-OU",      (int*)&playModel, (int)playModel_3D_OU); ImGui::SameLine();
        ImGui::RadioButton("3D-OU-360",  (int*)&playModel, (int)playModel_3D_OU_360);

        if (ImGui::CollapsingHeader("select media file")) {
            const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
            const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
            static ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("meida files", 2, flags, ImVec2(0.0f, TEXT_BASE_HEIGHT * 11), 0.0f)) {
                ImGui::TableSetupColumn("ID",   ImGuiTableColumnFlags_NoSort     | ImGuiTableColumnFlags_WidthFixed,   0.0f);
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_NoSort     | ImGuiTableColumnFlags_WidthStretch, 0.0f);
                ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
                ImGui::TableHeadersRow();
                for (int32_t i = 0; i < mAllVideoFiles.size(); i++) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(Fmt("%02d", i).c_str(), true, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectFileIndex = i;
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", mAllVideoFiles[i].c_str());
                }
                ImGui::EndTable();
            }
        }
    }
    if (selectFileIndex != -1) {
        infof("item:%d, exchange video file %s", selectFileIndex, mAllVideoFiles[selectFileIndex].c_str());
        startPlayVideo(mAllVideoFiles[selectFileIndex]);
    }

    ImGui::Text("This is some useful text.");

    mPanel->end();
    mPanel->render(project, view);

    mPlayer->setPlayStyle(playModel);

    if (currentreFreshRateTmp != (int)currentreFreshRate) {  //changed
        m_extentions->xrRequestDisplayRefreshRateFB(m_session, (float)currentreFreshRateTmp);
    }
}

void Application::showDeviceInformation(const glm::mat4& project, const glm::mat4& view) {
    wchar_t text[1024] = {0};
    swprintf(text, 1024, L"model: %s, OS: %s", mDeviceModel.c_str(), mDeviceOS.c_str());

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.5f, -0.6f, -1.0f));
    model = glm::rotate(model, glm::radians(-30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5, 0.5, 1.0f));
    mTextRender->render(project, view, model, text, wcslen(text), glm::vec3(1.0, 1.0, 1.0));
}

float Application::angleBetweenVectorAndPlane(const glm::vec3& vector, const glm::vec3& normal) {
    float dotProduct = glm::dot(vector, normal);
    float lengthVector = glm::length(vector);
    float lengthNormal = glm::length(normal);
    if (lengthNormal != 1.0f) {
        lengthNormal = 1.0f;  //normalnize
    }
    float cosAngle = dotProduct / (lengthVector * lengthNormal);
    float angleRadians = std::acos(cosAngle);
    //Convert radians to degrees
    //float angleInDegrees = glm::degrees(angleRadians);
    return PI/2 - angleRadians;
}

void Application::renderEyeTracking(const glm::mat4& project, const glm::mat4& view, int32_t eye) {
    if (m_extentions->isSupportEyeTracking && m_extentions->activeEyeTracking) {
        if (m_views.size() == 0) {
            return;
        }

        XrMatrix4x4f m{};
        XrVector3f scale{1.0f, 1.0f, 1.0f};
        XrMatrix4x4f_CreateTranslationRotationScale(&m, &m_gazeLocation.pose.position, &m_gazeLocation.pose.orientation, &scale);
        glm::mat4 model = glm::make_mat4((float*)&m);
        float halfIpd = mIpd / 2;
        if (eye == EYE_LEFT) {
            halfIpd = 0 - halfIpd;
        }
        model = glm::translate(model, glm::vec3(halfIpd, 0.0f, -0.2f));
        model = glm::scale(model, glm::vec3(1.0, 1.0, 1.0f));
        mEyeTrackingRay->setColor(1.0f, 0.0f, 0.0f);

        //Maps the direction of eye gaze to a point on the screen (x, y) in percentage
        
        glm::vec3 direction = mEyeTrackingRay->getDirectionVector(model);

        XrMatrix4x4f m2{};
        XrMatrix4x4f_CreateTranslationRotationScale(&m2, &m_views[eye].pose.position, &m_views[eye].pose.orientation, &scale);
        glm::mat4 model2 = glm::make_mat4((float*)&m2);

        glm::vec3 pointO = glm::vec3(model2 * glm::vec4(0.0, 0.0, -1.0, 1.0f));
        glm::vec3 pointX = glm::vec3(model2 * glm::vec4(1.0, 0.0, -1.0, 1.0f));
        glm::vec3 pointY = glm::vec3(model2 * glm::vec4(0.0, 1.0, -1.0, 1.0f));
        glm::vec3 normalYOZ = pointX - pointO;
        glm::vec3 normalXOZ = pointY - pointO;

        float angleAndYOZ = angleBetweenVectorAndPlane(direction, normalYOZ);
        float angleAndXOZ = angleBetweenVectorAndPlane(direction, normalXOZ);

        float x, y;
        if (angleAndYOZ < 0) {
            x = (1 - tanf(angleAndYOZ) / tanf(m_views[eye].fov.angleLeft)) * 0.5;
        } else {
            x = (1 + tanf(angleAndYOZ) / tanf(m_views[eye].fov.angleRight)) * 0.5;
        }
        if (angleAndXOZ) {
            y = (1 - tanf(angleAndXOZ) / tanf(m_views[eye].fov.angleUp)) * 0.5;
        } else {
            y = (1 + tanf(angleAndXOZ) / tanf(m_views[eye].fov.angleDown)) * 0.5;
        }

        //infof("angleAndYOZ:%f, angleAndXOZ:%f", angleAndYOZ, angleAndXOZ);

        mEyeTrackingRay->render(project, view, model);

        // show the coordinates
        wchar_t text[1024] = {0};
        swprintf(text, 1024, L"x:%0.2f, y:%0.2f", x, y);
        model = glm::translate(model, glm::vec3(-0.2f, 0.0f, -1.5f));
        model = glm::scale(model, glm::vec3(0.5, 0.5, 0.5f));
        mTextRender->render(project, view, model, text, wcslen(text), glm::vec3(1.0, 1.0, 1.0));
    }
}

void Application::renderHandTracking(const glm::mat4& project, const glm::mat4& view) {
    std::vector<CubeRender::Cube> cubes;
    for (auto hand = 0; hand < HAND_COUNT; hand++) {
        for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++) {
            XrHandJointLocationEXT& jointLocation = m_jointLocations[hand][i];
            if (jointLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT && jointLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) {

                XrMatrix4x4f m{};
                XrVector3f scale{1.0f, 1.0f, 1.0f};
                XrMatrix4x4f_CreateTranslationRotationScale(&m, &jointLocation.pose.position, &jointLocation.pose.orientation, &scale);
                glm::mat4 model = glm::make_mat4((float*)&m);

                CubeRender::Cube cube;
                cube.model = model;
                cube.scale = 0.01f;
                cubes.push_back(cube);
            }
        }
    }
    mCubeRender->render(project, view, cubes);
}

void Application::renderControllerHands(const glm::mat4& project, const glm::mat4& view) {
    std::vector<CubeRender::Cube> cubes;
    for (int hand = 0; hand < HAND_COUNT; ++hand) {
        if (!mControllerPoseValid[hand]) {
            continue;
        }
        XrMatrix4x4f xrModel{};
        const XrVector3f unitScale{1.f, 1.f, 1.f};
        XrMatrix4x4f_CreateTranslationRotationScale(&xrModel, &mControllerPose[hand].position,
                                                    &mControllerPose[hand].orientation, &unitScale);
        const glm::mat4 handModel = glm::make_mat4(reinterpret_cast<float*>(&xrModel));

        auto addPart = [&](const glm::vec3& offset, const glm::vec3& scale) {
            CubeRender::Cube cube;
            cube.model = glm::translate(handModel, offset);
            cube.model = glm::scale(cube.model, scale);
            cube.scale = 1.f;
            // The legacy Arx renderer does not expose a compatible depth image
            // to the OpenXR host yet. Keep tracked hands visible until proper
            // depth composition replaces this explicit overlay.
            cube.depthTest = false;
            cubes.push_back(cube);
        };

        // Compact palm and five digit proxies, all driven by the real OpenXR
        // hand/controller pose. Unlike the sample shell, these stay hand-sized.
        addPart({0.f, -0.015f, 0.015f}, {0.075f, 0.025f, 0.095f});
        const float handedness = hand == HAND_LEFT ? -1.f : 1.f;
        addPart({handedness * 0.052f, -0.002f, -0.015f}, {0.050f, 0.016f, 0.018f});
        for (int finger = 0; finger < 4; ++finger) {
            const float x = -0.035f + 0.023f * static_cast<float>(finger);
            addPart({x, 0.003f, -0.082f}, {0.016f, 0.016f, 0.090f});
        }
    }
    mCubeRender->render(project, view, cubes);
}

void Application::renderFrame(const XrPosef& pose, const glm::mat4& project, const glm::mat4& view, int32_t eye) {
    if (renderArxToCurrentEye(pose, eye, project, view)) {
        if (mArxStereoForFrame) {
            // Render the textured hand assets at the real tracked controller
            // poses. The former cuboid proxies were useful for bring-up but do
            // not provide a believable hand shape or scale.
            //
            // GL4ES caches the legacy engine state. Native model rendering must
            // therefore restore every state it changes before the next eye is
            // rendered, otherwise the right-eye background is submitted with
            // the hand shader's blend/cull/depth state and turns black.
            GLint previousProgram = 0;
            GLint previousVao = 0;
            GLint previousActiveTexture = GL_TEXTURE0;
            GLint previousTexture0 = 0;
            GLint previousFrontFace = GL_CCW;
            GLint previousCullFaceMode = GL_BACK;
            GLint previousDepthFunc = GL_LESS;
            GLint previousBlendSrcRgb = GL_ONE;
            GLint previousBlendDstRgb = GL_ZERO;
            GLint previousBlendSrcAlpha = GL_ONE;
            GLint previousBlendDstAlpha = GL_ZERO;
            GLint previousBlendEquationRgb = GL_FUNC_ADD;
            GLint previousBlendEquationAlpha = GL_FUNC_ADD;
            GLboolean previousColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
            GLboolean previousDepthMask = GL_TRUE;
            const GLboolean previousBlend = glIsEnabled(GL_BLEND);
            const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
            const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
            const GLboolean previousStencil = glIsEnabled(GL_STENCIL_TEST);
            glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
            glGetIntegerv(GL_FRONT_FACE, &previousFrontFace);
            glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFaceMode);
            glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
            glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
            glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &previousBlendEquationRgb);
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &previousBlendEquationAlpha);
            glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

            // The legacy renderer can leave a 1344x1344 scissor and a disabled
            // color mask behind when control returns to the native OpenXR
            // target. The world panel resets those while it draws, then restores
            // them; without resetting again here the tracked hands are submitted
            // but produce no pixels in the headset image.
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
			glDepthFunc(GL_LEQUAL);

            glm::mat4 handProjection = project;
            glm::mat4 handView = view;
            if (eye >= 0 && eye < 2 && mArxVisualStateValid[eye]
                && mArxVisualState[eye].version == 3u) {
                const ArxVrVisualState& visual = mArxVisualState[eye];
                handProjection = glm::make_mat4(visual.projection);
                handView = glm::make_mat4(visual.view);

                auto applyGameHandPose = [&](int hand,
                                             const ArxVrVisualHandPose& poseState) {
                    if (poseState.valid == 0u) {
                        return;
                    }
                    const glm::vec3 position(poseState.position[0],
                                             poseState.position[1],
                                             poseState.position[2]);
                    const glm::quat orientation = glm::normalize(glm::quat(
                        poseState.orientation[3], poseState.orientation[0],
                        poseState.orientation[1], poseState.orientation[2]));
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), position)
                                    * glm::mat4_cast(orientation);
                    // The engine exports an Arx-local hand rotation (+Z aim,
                    // -Y up), while Hand::setModel maps the FBX into OpenXR
                    // local axes (-Z aim, +Y up). Convert the mesh basis once;
                    // without this, both fingers and palms face backwards.
                    // Do not change the engine pose: held objects use its +Z.
                    model = glm::scale(model, glm::vec3(1.0f, -1.0f, -1.0f));
                    // FBX vertices are authored in centimetres. HandBase's
                    // normal 0.01 scale converts them to metres; compensate by
                    // 100 here because the Arx world and matrices use cm.
                    model = glm::scale(model, glm::vec3(100.0f));
                    mHands->setModel(hand, model);
                    // Drive the skinned fingers from the engine's effective
                    // controls as well as the pose. This keeps unattended
                    // interaction tests honest: their lower-grip override now
                    // closes the same hand that owns the held Arx object.
                    if (poseState.gripActive != 0u) {
                        mHands->setGripProfile(hand, poseState.gripCurls,
                                               poseState.gripDiameter);
                    } else {
                        mHands->setFingerCurl(hand, poseState.trigger,
                                             poseState.squeeze);
                    }
                };
                applyGameHandPose(HAND_LEFT, visual.leftHand);
                applyGameHandPose(HAND_RIGHT, visual.rightHand);
            }

            // Diagnostic-only visual check. The normal value is empty, so real
            // controller analog input remains authoritative. Automated headset
            // tests can request two reproducible frames without fabricating an
            // OpenXR controller event or bypassing the actual skinning shader.
            char handPoseTest[PROP_VALUE_MAX] = {};
            __system_property_get("debug.arxvr.hand_pose_test", handPoseTest);
            const bool testOpen = std::strcmp(handPoseTest, "open") == 0;
            const bool testCurl = std::strcmp(handPoseTest, "curl") == 0;
            const bool testTrigger = std::strcmp(handPoseTest, "trigger") == 0;
            if (testOpen || testCurl || testTrigger) {
                handProjection = project;
                handView = view;
                // Construct directly from the eye's camera-to-world matrix.
                // The pose argument can be in a different reference space on
                // some PICO firmware revisions, while inverse(view) is exactly
                // the space consumed below by Model::render.
                const glm::mat4 cameraWorld = glm::inverse(view);
                // Reproducible palms inside the binocular field. setModel still
                // applies the real per-hand FBX corrections being tested.
                glm::mat4 leftTest = glm::translate(cameraWorld,
                    glm::vec3(-0.19f, 0.02f, -0.75f));
                leftTest = glm::rotate(leftTest, glm::radians(90.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 rightTest = glm::translate(cameraWorld,
                    glm::vec3(0.19f, 0.02f, -0.75f));
                rightTest = glm::rotate(rightTest, glm::radians(-90.0f),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
                mHands->setModel(HAND_LEFT, leftTest);
                mHands->setModel(HAND_RIGHT, rightTest);
            }
            if (testOpen) {
                mHands->setFingerCurl(HAND_LEFT, 0.0f, 0.0f);
                mHands->setFingerCurl(HAND_RIGHT, 0.0f, 0.0f);
            } else if (testCurl) {
                mHands->setFingerCurl(HAND_LEFT, 0.0f, 1.0f);
                mHands->setFingerCurl(HAND_RIGHT, 0.0f, 1.0f);
            } else if (testTrigger) {
                mHands->setFingerCurl(HAND_LEFT, 1.0f, 0.0f);
                mHands->setFingerCurl(HAND_RIGHT, 1.0f, 0.0f);
            }

            mHands->render(handProjection, handView);

            // Device-side proof of the real interaction path. The system PICO
            // low-light panel is composited after the app and masks screencap,
            // so capture the OpenXR eye target itself once the diagnostic right
            // hand has stopped moving after its 45-frame extension. The short
            // grip-discovery pause is deliberately below this stability window.
            char captureHoldProperty[PROP_VALUE_MAX] = {};
            __system_property_get("debug.arxvr.capture_hold", captureHoldProperty);
            if (std::strcmp(captureHoldProperty, "1") == 0
                && eye >= 0 && eye < 2 && mArxVisualStateValid[eye]
                && mArxVisualState[eye].version == 3u
                && mArxVisualState[eye].rightHand.valid != 0u
                && !mArxHoldEyeCaptured[eye]) {
                const ArxVrVisualHandPose& handState = mArxVisualState[eye].rightHand;
                const glm::vec3 handPosition(handState.position[0], handState.position[1],
                                             handState.position[2]);
                if (mArxHoldPoseSeen[eye]
                    && glm::distance(handPosition, mArxHoldLastHand[eye]) < 0.05f) {
                    ++mArxHoldStableFrames[eye];
                } else {
                    mArxHoldStableFrames[eye] = 0u;
                }
                mArxHoldPoseSeen[eye] = true;
                mArxHoldLastHand[eye] = handPosition;
                if (mArxHoldStableFrames[eye] >= 60u) {
                    GLint holdViewport[4] = {0, 0, 0, 0};
                    glGetIntegerv(GL_VIEWPORT, holdViewport);
                    captureHoldEye(eye, holdViewport);
                }
            }

            glUseProgram(previousProgram);
            glBindVertexArray(previousVao);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, previousTexture0);
            glActiveTexture(previousActiveTexture);
            glFrontFace(previousFrontFace);
            glCullFace(previousCullFaceMode);
            glDepthFunc(previousDepthFunc);
            glBlendFuncSeparate(previousBlendSrcRgb, previousBlendDstRgb,
                                previousBlendSrcAlpha, previousBlendDstAlpha);
            glBlendEquationSeparate(previousBlendEquationRgb, previousBlendEquationAlpha);
            glColorMask(previousColorMask[0], previousColorMask[1],
                        previousColorMask[2], previousColorMask[3]);
            glDepthMask(previousDepthMask);
            const auto restoreEnable = [](GLenum capability, GLboolean enabled) {
                if (enabled) {
                    glEnable(capability);
                } else {
                    glDisable(capability);
                }
            };
            restoreEnable(GL_BLEND, previousBlend);
            restoreEnable(GL_DEPTH_TEST, previousDepth);
            restoreEnable(GL_CULL_FACE, previousCull);
            restoreEnable(GL_SCISSOR_TEST, previousScissor);
            restoreEnable(GL_STENCIL_TEST, previousStencil);
        }
        return;
    }

    layout();
    showDeviceInformation(project, view);

    mPlayer->render(project, view, eye);

    if (mIsShowDashboard) {
        showDashboard(project, view);
    }

    renderEyeTracking(project, view, eye);
    
    mController->render(project, view);

    renderHandTracking(project, view);

}
