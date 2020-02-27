#pragma once

/*  This file is part of EmuFramework.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/audio/OutputStream.hh>
#include <imagine/time/Time.hh>
#include <imagine/util/ringbuffer/RingBuffer.hh>
#include <imagine/util/audio/PcmFormat.hh>
#include <memory>
#include <atomic>

class EmuAudio
{
public:
	enum class AudioWriteState : uint8_t
	{
		BUFFER,
		ACTIVE,
		UNDERRUN,
		MULTI_UNDERRUN
	};

	constexpr EmuAudio() {}
	void start();
	void stop();
	void close();
	void flush();
	void writeFrames(const void *samples, uint framesToWrite);
	void setRate(uint32_t rate);
	void setFormat(IG::Audio::SampleFormat sample, uint8_t channels);
	void setDefaultMonoFormat();
	void setDoingFrameSkip(bool);
	bool shouldRenderAudioForSkippedFrame() const;
	IG::Audio::PcmFormat pcmFormat() const;

protected:
	std::unique_ptr<IG::Audio::SysOutputStream> audioStream{};
	IG::RingBuffer rBuff{};
	IG::Audio::PcmFormat format{44100, IG::Audio::SampleFormats::s16, 2};
	IG::Time lastUnderrunTime{};
	std::atomic<AudioWriteState> audioWriteState = AudioWriteState::BUFFER;
	uint32_t targetBufferFillBytes = 0;
	uint8_t extraSoundBuffers = 0;
	#ifndef NDEBUG
	bool doingFrameSkip = false;
	#endif

	uint32_t framesFree() const;
	uint32_t framesWritten() const;
	uint32_t framesCapacity() const;
	bool shouldStartAudioWrites(uint32_t bytesToWrite = 0) const;
	void resizeAudioBuffer(uint32_t wantedLatency);
};
