/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/core.h>

namespace nap
{
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Sent to an endpoint by an SocketClient or created by the SocketServer upon receiving data.
	 * Light object that can be copied and moved.
	 */
	struct NAPAPI SocketPacket final
	{
		friend class SocketConnection;
	public:
		struct Header
		{
			Header() = default;
			Header(uint32 size) : mSize(size) {}
			Header(size_t size) : mSize(size) {}
			uint32 mSize = 0;
		};

		// Default constructor
		SocketPacket() = default;

		// Default copy constructor
		SocketPacket(const SocketPacket& other) = default;

		// Default copy assignment operator
		SocketPacket& operator=(const SocketPacket& other) = default;

		// Move constructor
		SocketPacket(SocketPacket&& other) noexcept;

		// Move assignment operator
		SocketPacket& operator=(SocketPacket&& other) noexcept;

		/**
		 * SocketPacket constructor copies the contents of string into buffer
		 */
		SocketPacket(const std::string& string) noexcept;

		/**
		 * SocketPacket constructor moves the contents of supplied buffer if rvalue
		 * @param buffer the buffer to be copied
		 */
		SocketPacket(std::vector<nap::uint8>&& buffer);

		/**
		 * SocketPacket constructor copies the contents of supplied buffer
		 * @param buffer the buffer to be copied
		 */
		SocketPacket(const std::vector<nap::uint8>& buffer);

		/**
		 * SocketPacket constructor copies the contents of supplied data
		 * @param data pointer to the buffer to be copied
		 * @param size size of the copy in bytes
		 */
		SocketPacket(const uint8* data, size_t size);

		/**
		 * returns const reference to vector holding data
		 * @return the data
		 */
		const std::vector<nap::uint8>& data() const { return mBuffer; }

		/**
		 * returns size of data buffer
		 * @return size of data buffer
		 */
		size_t size() const{ return mBuffer.size(); }

		/**
		 * @return string with contents of internal buffer
		 */
		std::string toString() const { return std::string(mBuffer.begin(), mBuffer.end()); }

	private:
		Header mHeader;				///< Header containing information about the message
		std::vector<uint8> mBuffer; ///< Vector containing packet data
	};
}
