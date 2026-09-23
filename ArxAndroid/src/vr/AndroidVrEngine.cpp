#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "core/ArxGame.h"
#include "core/Benchmark.h"
#include "core/Core.h"
#include "cinematic/CinematicController.h"
#include "game/Camera.h"
#include "game/Player.h"
#include "gui/Dragging.h"
#include "gui/Console.h"
#include "gui/Menu.h"
#include "gui/menu/MenuFader.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/CriticalLogger.h"
#include "io/log/FileLogger.h"
#include "io/log/Logger.h"
#include "math/Random.h"
#include "platform/CrashHandler.h"
#include "platform/Thread.h"
#include "platform/profiler/Profiler.h"
#include "util/cmdline/CommandLine.h"
#include "vr/AndroidVrBridge.h"
#include "vr/AndroidVrInput.h"
#include "vr/AndroidVrWindow.h"

namespace {

constexpr const char * kLogTag = "ArxVR";

std::mutex g_engineMutex;
std::string g_gameDirectory;
std::string g_userDirectory;
ArxGame * g_vrGame = nullptr;
bool g_runtimeInitialized = false;
bool g_openMenuAfterFirstFrame = false;
bool g_autoPlayRequested = false;
bool g_autoWalkStarted = false;
bool g_autoTraverseRequested = false;
bool g_autoTraverseIntroSkipped = false;
std::string g_autoTraverseTarget;

std::string fromJavaString(JNIEnv * environment, jstring value) {
	if(!value) {
		return std::string();
	}
	const char * utf = environment->GetStringUTFChars(value, nullptr);
	if(!utf) {
		return std::string();
	}
	std::string result(utf);
	environment->ReleaseStringUTFChars(value, utf);
	return result;
}

bool initializeRuntime() {
	std::vector<std::string> arguments = {
		"arx",
		"--data-dir", g_gameDirectory,
		"--user-dir", g_userDirectory,
		"--config-dir", g_userDirectory
	};
	std::vector<char *> argv;
	argv.reserve(arguments.size());
	for(std::string & argument : arguments) {
		argv.push_back(argument.data());
	}

	Thread::disableFloatDenormals();
	Random::seed();
	CrashHandler::initialize(int(argv.size()), argv.data());
	Logger::initialize();
	CrashHandler::registerCrashCallback(Logger::quickShutdown);
	Logger::add(new logger::CriticalErrorDialog);

	if(parseCommandLine(int(argv.size()), argv.data()) != RunProgram) {
		LogError << "ArxVR command-line initialization failed";
		return false;
	}
	if(fs::initSystemPaths() != RunProgram) {
		LogError << "ArxVR filesystem initialization failed";
		return false;
	}

	CrashHandler::setReportLocation(fs::getUserDir() / "crashes");
	Logger::add(new logger::File(fs::getUserDir() / "arx-vr.log"));
	Logger::add(new MemoryLogger(&g_console.buffer()));
	profiler::initialize();
	benchmark::begin(benchmark::Startup);
	g_runtimeInitialized = true;
	return true;
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_picovr_openxr_MainActivity_configureArxEnginePaths(JNIEnv * environment, jobject,
                                                            jstring gameDirectory,
                                                            jstring userDirectory) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	g_gameDirectory = fromJavaString(environment, gameDirectory);
	g_userDirectory = fromJavaString(environment, userDirectory);
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "Arx engine paths configured: data=%s user=%s",
	                    g_gameDirectory.c_str(), g_userDirectory.c_str());
}

extern "C" __attribute__((visibility("default")))
int arxvr_engine_start(int width, int height) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if(g_vrGame) {
		return 1;
	}
	if(g_gameDirectory.empty() || g_userDirectory.empty()) {
		__android_log_print(ANDROID_LOG_WARN, kLogTag,
		                    "Arx engine start deferred: paths are not configured");
		return 0;
	}
	if(!g_runtimeInitialized && !initializeRuntime()) {
		return -1;
	}

	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "Starting Arx engine on OpenXR framebuffer %dx%d", width, height);
	g_vrGame = new ArxGame();
	mainApp = g_vrGame;
	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "Entering ArxGame VR initialization");
	if(!g_vrGame->initializeVr(arxvrCreateRenderWindow(width, height))) {
		__android_log_print(ANDROID_LOG_ERROR, kLogTag,
		                    "Arx engine initialization on OpenXR context failed");
		delete g_vrGame;
		g_vrGame = nullptr;
		mainApp = nullptr;
		return -1;
	}

	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "Arx engine initialized on OpenXR context");
	const std::string skipIntroMarker = g_userDirectory + "/diagnostics/skip-intro";
	struct stat skipIntroInfo = {};
	char skipIntroProperty[PROP_VALUE_MAX] = {};
	const bool skipIntroRequested = __system_property_get("debug.arxvr.skip_intro",
	                                                     skipIntroProperty) > 0
	                             && skipIntroProperty[0] == '1';
	char autoPlayProperty[PROP_VALUE_MAX] = {};
	g_autoPlayRequested = __system_property_get("debug.arxvr.auto_play",
	                                           autoPlayProperty) > 0
	                     && autoPlayProperty[0] == '1';
	char autoTraverseProperty[PROP_VALUE_MAX] = {};
	const int autoTraverseLength = __system_property_get("debug.arxvr.auto_traverse",
	                                                     autoTraverseProperty);
	g_autoTraverseRequested = autoTraverseLength > 0
	                       && std::strcmp(autoTraverseProperty, "0") != 0;
	g_autoTraverseTarget = g_autoTraverseRequested
	                     ? std::string(autoTraverseProperty) : std::string();
	g_autoWalkStarted = false;
	g_autoTraverseIntroSkipped = false;
	if(skipIntroRequested || g_autoPlayRequested
	   || stat(skipIntroMarker.c_str(), &skipIntroInfo) == 0) {
		g_openMenuAfterFirstFrame = true;
	}
	return 1;
}

extern "C" __attribute__((visibility("default")))
int arxvr_engine_frame(int eye, float verticalFovRadians) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if(!g_vrGame) {
		return 0;
	}
	g_vrGame->frameVr(eye, verticalFovRadians);
	if(eye <= 0 && g_autoTraverseRequested && !g_autoTraverseIntroSkipped
	   && isInCinematic()) {
		g_autoTraverseIntroSkipped = true;
		cinematicEnd();
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
		                    "ARXVR_TRAVERSE intro cinematic completed early for route test");
	}
	if(eye <= 0 && g_openMenuAfterFirstFrame) {
		g_openMenuAfterFirstFrame = false;
		g_vrGame->openVrMainMenu();
		if(g_autoPlayRequested) {
			ARX_MENU_Clicked_NEWQUEST();
			ARX_PLAYER_MakeAverageHero();
			MenuFader_start(Fade_In, Mode_InGame);
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
			                    "ArxVR diagnostic new game started");
		}
	}
	if(eye <= 0 && g_autoPlayRequested && !g_autoWalkStarted
	   && (g_vrGame->vrRenderState() & 0x100u) != 0u) {
		g_autoWalkStarted = true;
		if(g_autoTraverseRequested) {
			g_vrGame->startVrTraversalDiagnostic(g_autoTraverseTarget);
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
			                    "ARXVR_TRAVERSE diagnostic started target=%s",
			                    g_autoTraverseTarget.c_str());
		} else {
			arxvrStartDiagnosticWalk(180);
			__android_log_print(ANDROID_LOG_INFO, kLogTag,
			                    "ArxVR diagnostic locomotion started in real gameplay");
		}
	}
	return 1;
}

extern "C" __attribute__((visibility("default")))
int arxvr_engine_get_visual_state(int eye, ArxVrVisualState * state) {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if(!g_vrGame || !state) {
		return 0;
	}

	*state = ArxVrVisualState{};
	state->version = 3u;
	state->eye = eye;
	std::memcpy(state->view, glm::value_ptr(g_preparedCamera.m_worldToView),
	            sizeof(state->view));
	std::memcpy(state->projection, glm::value_ptr(g_preparedCamera.m_viewToClip),
	            sizeof(state->projection));

	auto copyHand = [](bool rightHand, ArxVrVisualHandPose & output) {
		Vec3f position;
		glm::quat orientation(1.f, 0.f, 0.f, 0.f);
		if(!arxvrGetLastHandWorldPose(rightHand, position, orientation)) {
			return;
		}
		output.valid = 1u;
		output.position[0] = position.x;
		output.position[1] = position.y;
		output.position[2] = position.z;
		output.orientation[0] = orientation.x;
		output.orientation[1] = orientation.y;
		output.orientation[2] = orientation.z;
		output.orientation[3] = orientation.w;
		output.trigger = arxvrHandTriggerValue(rightHand);
		output.squeeze = arxvrHandSqueezeValue(rightHand);
		std::array<float, 5> curls = {};
		float diameter = 0.f;
		float span = 0.f;
		if(getVrPhysicalGripProfile(rightHand, curls, diameter, span)) {
			output.gripActive = 1u;
			std::copy(curls.begin(), curls.end(), output.gripCurls);
			output.gripDiameter = diameter;
			output.gripSpan = span;
		}
	};
	copyHand(false, state->leftHand);
	copyHand(true, state->rightHand);
	return 1;
}

extern "C" __attribute__((visibility("default")))
int arxvr_engine_is_in_game() {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_vrGame && ARXmenu.mode() == Mode_InGame ? 1 : 0;
}

extern "C" __attribute__((visibility("default")))
int arxvr_engine_is_cinematic() {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_vrGame && isInCinematic() ? 1 : 0;
}

extern "C" __attribute__((visibility("default")))
unsigned arxvr_engine_vr_render_state() {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	return g_vrGame ? g_vrGame->vrRenderState() : 0u;
}

extern "C" __attribute__((visibility("default")))
void arxvr_engine_recenter_2d() {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	arxvrResetReferenceHead();
}

extern "C" __attribute__((visibility("default")))
void arxvr_engine_stop() {
	std::lock_guard<std::mutex> lock(g_engineMutex);
	if(g_vrGame) {
		g_vrGame->shutdownVr();
		delete g_vrGame;
		g_vrGame = nullptr;
		mainApp = nullptr;
	}
	if(g_runtimeInitialized) {
		benchmark::shutdown();
		Logger::shutdown();
		CrashHandler::shutdown();
		Random::shutdown();
		g_runtimeInitialized = false;
	}
}
