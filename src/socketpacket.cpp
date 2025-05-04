/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once
#include "socketpacket.h"

namespace nap
{
	// String constructor
	SocketPacket::SocketPacket(const std::string& string) noexcept
	{
		std::copy(string.begin(), string.end(), std::back_inserter(mBuffer));
		mHeader = {mBuffer.size()};
	}


	// Buffer move constructor
	SocketPacket::SocketPacket(std::vector<nap::uint8>&& buffer) :
		mBuffer(std::move(buffer)), mHeader(buffer.size())
	{}


	// Buffer copy constructor
	SocketPacket::SocketPacket(const std::vector<nap::uint8>& buffer) :
		mBuffer(buffer), mHeader(buffer.size())
	{}


	// Memcpy constructor
	SocketPacket::SocketPacket(const uint8* data, size_t size) :
		mBuffer(size), mHeader(size)
	{
		std::memcpy(mBuffer.data(), data, size);
	}


	// Move constructor
	SocketPacket::SocketPacket(SocketPacket&& other) noexcept :
		mBuffer(std::move(other.mBuffer)), mHeader(other.mHeader)
	{}


	// Move assignment operator
	SocketPacket& SocketPacket::operator=(SocketPacket&& other) noexcept
	{
		mBuffer = std::move(other.mBuffer);
		mHeader = other.mHeader;
		return *this;
	}
}
