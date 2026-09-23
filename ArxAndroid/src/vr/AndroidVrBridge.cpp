#include "vr/AndroidVrBridge.h"

#include <android/log.h>
#include <jni.h>

#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace {

constexpr const char * kLogTag = "ArxVR";

std::mutex g_trackingMutex;
ArxVrTrackingState g_trackingState = {};
bool g_haveTrackingState = false;
bool g_loggedFirstTrackingState = false;

constexpr std::array<const char *, 7> kRequiredPakFiles = {
	"data.pak",
	"data2.pak",
	"SFX.pak",
	"SPEECH_default.pak",
	"SPEECH.pak",
	"LOC_default.pak",
	"LOC.pak"
};

} // namespace

extern "C" __attribute__((visibility("default")))
void arxvr_update_tracking(const ArxVrTrackingState * state) {
	if(!state) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_trackingMutex);
	g_trackingState = *state;
	g_haveTrackingState = true;
	if(!g_loggedFirstTrackingState) {
		__android_log_print(ANDROID_LOG_INFO, kLogTag,
		                    "6DoF tracking received: frame=%llu validMask=0x%02x buttonMask=0x%04x",
		                    static_cast<unsigned long long>(state->frameNumber),
		                    state->validMask, state->buttonMask);
		g_loggedFirstTrackingState = true;
	}
}

extern "C" __attribute__((visibility("default")))
int arxvr_read_tracking(ArxVrTrackingState * state) {
	if(!state) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(g_trackingMutex);
	if(!g_haveTrackingState) {
		return 0;
	}
	*state = g_trackingState;
	return 1;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_picovr_openxr_MainActivity_probeArxData(JNIEnv * environment, jobject,
                                                  jstring gameDirectory) {
	if(!gameDirectory) {
		return -1;
	}

	const char * utfPath = environment->GetStringUTFChars(gameDirectory, nullptr);
	if(!utfPath) {
		return -1;
	}
	const std::string root(utfPath);
	environment->ReleaseStringUTFChars(gameDirectory, utfPath);

	jlong totalBytes = 0;
	for(const char * filename : kRequiredPakFiles) {
		const std::string path = root + "/" + filename;
		struct stat info = {};
		if(stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) {
			__android_log_print(ANDROID_LOG_ERROR, kLogTag, "Missing required game archive: %s",
			                    path.c_str());
			return -1;
		}
		totalBytes += static_cast<jlong>(info.st_size);
	}

	__android_log_print(ANDROID_LOG_INFO, kLogTag,
	                    "Arx data probe succeeded: %lld bytes across %zu PAK files",
	                    static_cast<long long>(totalBytes), kRequiredPakFiles.size());
	return totalBytes;
}
