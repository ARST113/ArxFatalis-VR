#include "vr/AndroidVrWindow.h"

#include <android/log.h>
#include <dlfcn.h>
#include <EGL/egl.h>

#include <algorithm>
#include <string>

#include "glad/glad.h"

#include "graphics/opengl/OpenGLRenderer.h"
#include "input/InputBackend.h"
#include "io/log/Logger.h"
#include "window/RenderWindow.h"

namespace {

constexpr const char * kLogTag = "ArxVR";
void * g_gl4esHandle = nullptr;

void * loadNativeOpenGlSymbol(const char * name) {
	void * symbol = reinterpret_cast<void *>(eglGetProcAddress(name));
	if(!symbol) {
		// eglGetProcAddress is only required to expose extension entry points.
		// Android's core GLES symbols live in libGLESv3 and are commonly only
		// reachable through the dynamic linker.
		symbol = dlsym(RTLD_DEFAULT, name);
	}
	return symbol;
}

bool initializeGl4es() {
	if(g_gl4esHandle) {
		return true;
	}
	g_gl4esHandle = dlopen("libgl4es.so", RTLD_NOW | RTLD_LOCAL);
	if(!g_gl4esHandle) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
		                    "Could not load libgl4es.so: %s", dlerror());
		return false;
	}
	using SetGetProcAddress = void (*)(void * (*)(const char *));
	using Initialize = void (*)();
	auto setGetProcAddress = reinterpret_cast<SetGetProcAddress>(
		dlsym(g_gl4esHandle, "set_getprocaddress"));
	auto initialize = reinterpret_cast<Initialize>(
		dlsym(g_gl4esHandle, "initialize_gl4es"));
	if(!setGetProcAddress || !initialize) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
		                    "libgl4es initialization exports are missing");
		return false;
	}
	setGetProcAddress(loadNativeOpenGlSymbol);
	initialize();
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "GL4ES fixed-function compatibility initialized");
	return true;
}

void * loadOpenGlSymbol(const char * name) {
	if(g_gl4esHandle) {
		if(void * symbol = dlsym(g_gl4esHandle, name)) {
			return symbol;
		}
	}
	return loadNativeOpenGlSymbol(name);
}

class VrInputBackend final : public InputBackend {
public:
	bool update() override { return true; }
	bool setMouseMode(Mouse::Mode) override { return true; }
	bool getAbsoluteMouseCoords(int & x, int & y) const override {
		x = m_mouseX;
		y = m_mouseY;
		return true;
	}
	void setAbsoluteMouseCoords(int x, int y) override {
		m_mouseX = x;
		m_mouseY = y;
	}
	void getRelativeMouseCoords(int & x, int & y, int & wheel) const override {
		x = 0;
		y = 0;
		wheel = 0;
	}
	void getMouseButtonClickCount(int, int & clicks, int & releases) const override {
		clicks = 0;
		releases = 0;
	}
	bool isKeyboardKeyPressed(int) const override { return false; }
	void startTextInput(const Rect &, TextInputHandler *) override { }
	void stopTextInput() override { }
	std::string getKeyName(Keyboard::Key) const override { return std::string(); }

private:
	int m_mouseX = 0;
	int m_mouseY = 0;
};

class AndroidVrWindow final : public RenderWindow {
public:
	AndroidVrWindow(int width, int height)
		: m_externalSize(std::max(width, 640), std::max(height, 480)) {
		m_mode = DisplayMode(m_externalSize, 72);
		m_displayModes.push_back(m_mode);
		m_fullscreen = true;
		m_renderer = new OpenGLRenderer;
		m_input = new VrInputBackend;
		m_input->setAbsoluteMouseCoords(m_externalSize.x / 2, m_externalSize.y / 2);
	}

	~AndroidVrWindow() override {
		delete m_input;
		m_input = nullptr;
		delete m_renderer;
		m_renderer = nullptr;
	}

	bool initializeFramework() override {
		return true;
	}

	void setTitle(const std::string & title) override {
		m_title = title;
	}

	bool setVSync(int vsync) override {
		m_vsync = vsync;
		return true;
	}

	void setFullscreenMode(const DisplayMode &) override {
		m_mode = DisplayMode(m_externalSize, 72);
		m_fullscreen = true;
	}

	void setWindowSize(const Vec2i &) override {
		m_mode = DisplayMode(m_externalSize, 72);
		m_fullscreen = true;
	}

	bool setGamma(float) override { return true; }

	bool initialize() override {
		EGLContext context = eglGetCurrentContext();
		EGLDisplay display = eglGetCurrentDisplay();
		EGLSurface drawSurface = eglGetCurrentSurface(EGL_DRAW);
		EGLSurface readSurface = eglGetCurrentSurface(EGL_READ);
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
		                    "External window init: EGL context=%p display=%p surface=%p",
		                    context, display, drawSurface);
		if(context == EGL_NO_CONTEXT) {
			LogError << "ArxVR external window has no current EGL context";
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			                    "External window has no current EGL context");
			return false;
		}
		if(!initializeGl4es()) {
			return false;
		}
		// GL4ES probes capabilities using a temporary EGL context and leaves no
		// context current afterwards. OpenXR owns this context, so restore the
		// exact display/surfaces we received before loading any GL entry points.
		if(eglGetCurrentContext() != context
		   && !eglMakeCurrent(display, drawSurface, readSurface, context)) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			                    "Could not restore OpenXR EGL context after GL4ES init: 0x%x",
			                    eglGetError());
			return false;
		}
		if(!gladLoadGLLoader(loadOpenGlSymbol)) {
			LogError << "ArxVR could not load OpenGL ES entry points";
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			                    "GLAD could not load OpenGL ES entry points");
			return false;
		}
		const char * version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
		const char * renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
		if(eglGetCurrentContext() != context
		   && !eglMakeCurrent(display, drawSurface, readSurface, context)) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			                    "Could not restore OpenXR EGL context after GL probe: 0x%x",
			                    eglGetError());
			return false;
		}
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
		                    "External OpenGL ready: version=%s renderer=%s",
		                    version ? version : "(null)", renderer ? renderer : "(null)");

		m_renderer->initialize();
		onCreate();
		onToggleFullscreen(true);
		// OpenGLRenderer::initialize() probes the context. SDL then completes
		// renderer creation from its first drawable-size update; reproduce that
		// lifecycle for the externally owned OpenXR framebuffer.
		m_renderer->afterResize();
		m_renderer->SetViewport(Rect(m_externalSize.x, m_externalSize.y));
		onResize(m_externalSize);
		if(!m_renderer->isInitialized()) {
			LogError << "ArxVR OpenGL renderer initialization failed";
			__android_log_print(ANDROID_LOG_ERROR, kLogTag,
			                    "OpenGL renderer initialization failed");
			return false;
		}
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
		                    "Arx OpenGL renderer initialized");

		onShow(true);
		onFocus(true);
		return true;
	}

	void processEvents(bool) override { }
	void showFrame() override { }
	void hide() override { onShow(false); }
	void setMinimizeOnFocusLost(bool) override { }
	MinimizeSetting willMinimizeOnFocusLost() override { return AlwaysDisabled; }
	std::string getClipboardText() override { return std::string(); }
	void setClipboardText(const std::string &) override { }
	void allowScreensaver(bool) override { }
	InputBackend * getInputBackend() override { return m_input; }

private:
	Vec2i m_externalSize;
	VrInputBackend * m_input;
};

} // namespace

RenderWindow * arxvrCreateRenderWindow(int width, int height) {
	return new AndroidVrWindow(width, height);
}
