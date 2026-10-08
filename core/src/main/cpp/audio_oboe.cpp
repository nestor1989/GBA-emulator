// Audio output with Oboe (AAudio on Android 8.1+, OpenSL ES below).
// The callback only copies from the engine's lock-free ring: no locks, no
// allocation, no logging on the audio thread.
#include "audio_out.h"

#include "emu_core.h"

#include <android/log.h>
#include <oboe/Oboe.h>

#include <memory>
#include <mutex>

namespace {

class Player : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
	bool start() {
		std::lock_guard<std::mutex> lock(mMutex);
		mWanted = true;
		return openAndStartLocked();
	}

	void stop() {
		std::lock_guard<std::mutex> lock(mMutex);
		mWanted = false;
		emu_audio_set_active(false);
		if (mStream) {
			mStream->stop();
			mStream->close();
			mStream.reset();
		}
	}

	oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) override {
		emu_audio_read(static_cast<int16_t*>(audioData), static_cast<size_t>(numFrames));
		return oboe::DataCallbackResult::Continue;
	}

	// Called on an Oboe-owned thread after the stream died (e.g. headphones
	// unplugged or Bluetooth switched). Open a new one on the current device.
	void onErrorAfterClose(oboe::AudioStream*, oboe::Result error) override {
		__android_log_print(ANDROID_LOG_WARN, "emucore", "audio stream closed: %s", oboe::convertToText(error));
		std::lock_guard<std::mutex> lock(mMutex);
		emu_audio_set_active(false);
		mStream.reset();
		if (mWanted) {
			openAndStartLocked();
		}
	}

private:
	bool openAndStartLocked() {
		if (mStream) {
			return true;
		}
		oboe::AudioStreamBuilder builder;
		builder.setDirection(oboe::Direction::Output)
			->setPerformanceMode(oboe::PerformanceMode::LowLatency)
			->setSharingMode(oboe::SharingMode::Exclusive)
			->setFormat(oboe::AudioFormat::I16)
			->setFormatConversionAllowed(true)
			->setChannelCount(oboe::ChannelCount::Stereo)
			->setChannelConversionAllowed(true)
			->setUsage(oboe::Usage::Game)
			->setContentType(oboe::ContentType::Music)
			->setDataCallback(this)
			->setErrorCallback(this);

		oboe::Result result = builder.openStream(mStream);
		if (result != oboe::Result::OK || !mStream) {
			__android_log_print(ANDROID_LOG_ERROR, "emucore", "openStream: %s", oboe::convertToText(result));
			mStream.reset();
			return false;
		}
		emu_audio_set_output_rate(static_cast<unsigned>(mStream->getSampleRate()));
		result = mStream->requestStart();
		if (result != oboe::Result::OK) {
			__android_log_print(ANDROID_LOG_ERROR, "emucore", "requestStart: %s", oboe::convertToText(result));
			mStream->close();
			mStream.reset();
			return false;
		}
		emu_audio_set_active(true);
		return true;
	}

	std::mutex mMutex;
	std::shared_ptr<oboe::AudioStream> mStream;
	bool mWanted = false;
};

Player gPlayer;

} // namespace

extern "C" bool audio_out_start(void) {
	return gPlayer.start();
}

extern "C" void audio_out_stop(void) {
	gPlayer.stop();
}
