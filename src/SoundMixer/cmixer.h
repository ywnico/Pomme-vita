// Adapted from cmixer by rxi (https://github.com/rxi/cmixer)

/*
** Copyright (c) 2017 rxi
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
**/

#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include "CompilerSupport/span.h"

#define BUFFER_SIZE (512)

namespace cmixer
{

	enum
	{
		CM_STATE_STOPPED,
		CM_STATE_PLAYING,
		CM_STATE_PAUSED
	};

	struct Source
	{
		int16_t pcmbuf[BUFFER_SIZE];    // Internal buffer with raw stereo PCM
		int samplerate;                 // Stream's native samplerate
		int length;                     // Stream's length in frames
		int sustainOffset;              // Offset of the sustain loop in frames
		int end;                        // End index for the current play-through
		int state;                      // Current state (playing|paused|stopped)
		int64_t position;               // Current playhead position (fixed point)
		int lgain, rgain;               // Left and right gain (fixed point)
		int rate;                       // Playback rate (fixed point)
		int nextfill;                   // Next frame idx where the buffer needs to be filled
		bool loop;                      // Whether the source will loop when `end` is reached
		bool rewind;                    // Whether the source will rewind before playing
		bool active;                    // Whether the source is part of `sources` list
		bool interpolate;               // Interpolated resampling when played back at a non-native rate
		double gain;                    // Gain set by `cm_set_gain()`
		double pan;                     // Pan set by `cm_set_pan()`
		std::function<void()> onComplete;        // Callback

	private:
		void ClearPrivate();

	protected:
		Source();
		void Init(int samplerate, int length);
		virtual void RewindImplementation() = 0;
		virtual void ClearImplementation() = 0;
		virtual void FillBuffer(int16_t* buffer, int length) = 0;

	public:
		virtual ~Source();
		void RemoveFromMixer();
		void Clear();
		void Rewind();
		void RecalcGains();
		void FillBuffer(int offset, int length);
		void Process(int len);
		double GetLength() const;
		double GetPosition() const;
		int GetState() const;
		void SetGain(double gain);
		void SetPan(double pan);
		void SetPitch(double pitch);
		void SetLoop(bool loop);
		void SetInterpolation(bool interpolation);
		void Play();
		void Pause();
		void TogglePause();
		void Stop();
	};

	class WavStream : public Source
	{
		int bitdepth;
		int channels;
		bool bigEndian;
		int idx;
		std::span<char> span;
		std::vector<char> userBuffer;

		void ClearImplementation() override;
		void RewindImplementation() override;
		void FillBuffer(int16_t* buffer, int length) override;

		inline uint8_t* data8() const { return reinterpret_cast<uint8_t*>(span.data()); }
		inline int16_t* data16() const { return reinterpret_cast<int16_t*>(span.data()); }

	public:
		WavStream();
		void Init(int theSampleRate, int theBitDepth, int nChannels, bool bigEndian, std::span<char> data);
		std::span<char> GetBuffer(int nBytesOut);
		std::span<char> SetBuffer(std::vector<char>&& data);
	};

	// Guard class that safely removes the source from the mixer when the guard object is destroyed.
	class SourceMixGuard
	{
		Source& source;

	public:
		SourceMixGuard(Source& theSource) : source(theSource) {}
		~SourceMixGuard() { source.RemoveFromMixer(); }
	};

	void InitWithSDL();
	void ShutdownWithSDL();
	double GetMasterGain();
	void SetMasterGain(double);
}
