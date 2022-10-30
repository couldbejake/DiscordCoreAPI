/*
	DiscordCoreAPI, A bot library for Discord, written in C++, and featuring explicit multithreading through the usage of custom, asynchronous C++ CoRoutines.

	Copyright 2021, 2022 Chris M. (RealTimeChris)

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
	USA
*/
/// AudioEncoder.cpp - Source file for the audio encoder class.
/// Aug 22, 2021
/// https://discordcoreapi.com
/// \file AudioEncoder.cpp

#include <discordcoreapi/AudioEncoder.hpp>
#include <opus/opus_defines.h>

namespace DiscordCoreAPI {

	void OpusEncoderWrapper::OpusEncoderDeleter::operator()(OpusEncoder* other) noexcept {
		if (other) {
			opus_encoder_destroy(other);
			other = nullptr;
		}
	}

	OpusEncoderWrapper& OpusEncoderWrapper::operator=(OpusEncoderWrapper&& other) noexcept {
		if (this != &other) {
			this->ptr.reset(nullptr);
			this->ptr.reset(other.ptr.release());
		}
		return *this;
	}

	OpusEncoderWrapper::OpusEncoderWrapper(OpusEncoderWrapper&& other) noexcept {
		*this = std::move(other);
	}

	OpusEncoderWrapper::OpusEncoderWrapper() noexcept {
	}

	OpusEncoderWrapper::operator OpusEncoder*() noexcept {
		return this->ptr.get();
	}

	OpusEncoderWrapper::OpusEncoderWrapper(OpusEncoder* other) noexcept {
		this->ptr.reset(nullptr);
		this->ptr.reset(other);
	}

	AudioEncoder::AudioEncoder() {
		int32_t error{};
		this->encoder = opus_encoder_create(this->sampleRate, this->nChannels, OPUS_APPLICATION_AUDIO, &error);
		auto result = opus_encoder_ctl(this->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
		if (result != OPUS_OK) {
			throw std::runtime_error{ "Failed to set the Opus signal type." };
		}
		result = opus_encoder_ctl(this->encoder, OPUS_SET_APPLICATION(OPUS_APPLICATION_AUDIO));
		if (result != OPUS_OK) {
			throw std::runtime_error{ "Failed to set the Opus application type." };
		}
		result = opus_encoder_ctl(this->encoder, OPUS_SET_BITRATE(OPUS_BITRATE_MAX));
		if (result != OPUS_OK) {
			throw std::runtime_error{ "Failed to set the Opus bitrate." };
		}
	}

	DiscordCoreAPI::AudioFrameData AudioEncoder::encodeSingleAudioFrame(std::basic_string_view<opus_int16> inputFrame) {
		std::vector<uint8_t> newBuffer{};
		newBuffer.resize(this->maxBufferSize);
		int32_t count =
			opus_encode(this->encoder, inputFrame.data(), static_cast<int32_t>(inputFrame.size() / 2), newBuffer.data(), this->maxBufferSize);
		if (count <= 0 || count > newBuffer.size()) {
			return DiscordCoreAPI::AudioFrameData();
		}
		DiscordCoreAPI::AudioFrameData encodedFrame{};
		encodedFrame.data.insert(encodedFrame.data.begin(), newBuffer.begin(), newBuffer.begin() + count);
		encodedFrame.sampleCount = inputFrame.size() / 2;
		encodedFrame.type = DiscordCoreAPI::AudioFrameType::Encoded;
		return encodedFrame;
	}

	DiscordCoreAPI::AudioFrameData AudioEncoder::encodeSingleAudioFrame(DiscordCoreAPI::AudioFrameData& inputFrame) {
		std::vector<opus_int16> newVector{};
		for (uint64_t x = 0; x < inputFrame.data.size() / 2; ++x) {
			opus_int16 newValue{};
			newValue |= inputFrame.data[x * 2] << 0;
			newValue |= inputFrame.data[x * 2 + 1] << 8;
			newVector.emplace_back(newValue);
		}
		std::vector<uint8_t> newBuffer{};
		newBuffer.resize(this->maxBufferSize);
		int32_t count = opus_encode(this->encoder, newVector.data(), inputFrame.sampleCount, newBuffer.data(), this->maxBufferSize);
		if (count <= 0 || count > newBuffer.size()) {
			return DiscordCoreAPI::AudioFrameData();
		}
		DiscordCoreAPI::AudioFrameData encodedFrame{};
		encodedFrame.data.insert(encodedFrame.data.begin(), newBuffer.begin(), newBuffer.begin() + count);
		encodedFrame.sampleCount = inputFrame.sampleCount;
		encodedFrame.type = DiscordCoreAPI::AudioFrameType::Encoded;
		encodedFrame.guildMemberId = inputFrame.guildMemberId;
		return encodedFrame;
	}
}