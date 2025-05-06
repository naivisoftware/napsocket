/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Local includes
#include "socketconnection.h"
#include "socketadapter.h"

// External includes
#include <nap/logger.h>
#include <asio/connect.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <asio/use_future.hpp>

namespace nap
{
	SocketConnection::SocketConnection(asio::io_context& context, asio::ip::tcp::socket&& socket, SocketAdapter& adapter, const socket::ID& id) :
		mContext(context),
		mSocket(std::move(socket)),
		mEndpoint(mSocket.remote_endpoint()),
		mEndpointText(mEndpoint.address().to_string() + ':' + std::to_string(mEndpoint.port())),
		mAdapter(adapter),
		mID(id)
	{ }


	SocketConnection::~SocketConnection()
	{
		nap::Logger::info("%s: Connection destroyed", getEndPoint().c_str());
	}


	std::future<bool> SocketConnection::connect()
	{
		auto cf = mSocket.async_connect(mEndpoint, asio::use_future([this](std::error_code ec)
			{
				// Handle error
				if (ec)
				{
					nap::Logger::error("Failed to connect to endpoint %s | %s", getEndPoint().c_str(), ec.message().c_str());
					return false;
				}

				// Connection success
				nap::Logger::debug("%s: connected", getEndPoint().c_str());

				// Notify socket connected
				mAdapter.onSocketConnected(getID());

				// Write enqueued cmd
				setTimer();
				if (!mOutQueue.empty())
					write(mOutQueue.front());

				// Start reading callback
				readHeader();

				// Return reference to self as future
				return false;
			}
		));
		return cf;
	}


	std::future<void> SocketConnection::disconnect()
	{
		// Schedule task to close socket when connected
		auto f = asio::post(mSocket.get_executor(), asio::use_future([this]
			{
				close();
			}
		));
		return f;
	}


	void SocketConnection::enqueue(const SocketPacket& packet)
	{
		asio::post([this, pack = packet]()
			{
				bool is_empty = mOutQueue.empty();
				mOutQueue.emplace_back(pack);
				if (is_empty)
					write(mOutQueue.front());
			}
		);
	}


	void SocketConnection::enqueue(SocketPacket&& packet)
	{
		asio::post([this, pack = std::move(packet)]()
			{
			   bool is_empty = mOutQueue.empty();
			   mOutQueue.emplace_back(pack);
			   if (is_empty)
				   write(mOutQueue.front());
			}
		);
	}


	void SocketConnection::write(const SocketPacket& packet)
	{
		if (!mSocket.is_open())
		{
			nap::Logger::error("%s: socket closed", getEndPoint().c_str());
			return;
		}

		auto write_buffer = asio::buffer(packet.data(), packet.size());
		asio::async_write(mSocket, write_buffer, [this](std::error_code ec, std::size_t size)
			{
				// Writing failed
				if (ec)
				{
					nap::Logger::error("%s: %s", getEndPoint().c_str(), ec.message().c_str());
					close();
					return;
				}

				// Writing succeeded -> schedule a response read before attempting a new write
				nap::Logger::debug("%s: Written %d byte(s)", getEndPoint().c_str(), size);
				mOutQueue.pop_front();
			}
		);
	}


	void SocketConnection::readHeader()
	{
		assert(mSocket.is_open());
		asio::async_read(mSocket, asio::buffer(&mIncomingMsgBuffer.mHeader, sizeof(mIncomingMsgBuffer.mHeader)), [this] (std::error_code ec, std::size_t size)
			{
				if (ec)
				{
					nap::Logger::error("%s: Failed to read header | %s", getEndPoint().c_str(), ec.message().c_str());
					close();
					return;
				}

				// Ensure there is a valid message body to read
				if (mIncomingMsgBuffer.mHeader.mSize == 0 || mIncomingMsgBuffer.mHeader.mSize > mMaxMessageSize)
				{
					nap::Logger::error("%s: Invalid packet header | size %u", getEndPoint().c_str(), mIncomingMsgBuffer.mHeader.mSize);
					readHeader();
					return;
				}

				// Read succeeded
				nap::Logger::debug("%s: Read header | %lu byte(s)", getEndPoint().c_str(), size);

				// Resize message body
				mIncomingMsgBuffer.mBuffer.resize(mIncomingMsgBuffer.mHeader.mSize);

				// Keep reading body
				readBody();
			}
		);
	}


	void SocketConnection::readBody()
	{
		assert(mSocket.is_open());
		asio::async_read(mSocket, asio::buffer(mIncomingMsgBuffer.mBuffer.data(), mIncomingMsgBuffer.mBuffer.size()), [this] (std::error_code ec, std::size_t size)
			{
				if (ec)
				{
					nap::Logger::error("%s: Failed to read body | %s", getEndPoint().c_str(), ec.message().c_str());
					close();
					return;
				}

				// Pass to packet received
				mAdapter.onPacketReceived(getID(), mIncomingMsgBuffer);

				// Keep reading new header
				readHeader();
			}
		);
	}


	void SocketConnection::close()
	{
		// Delete timer -> bail if closed
		mTimeout.reset();

		// Close -> must be open when called deferred
		if (!mSocket.is_open())
			return;

		std::error_code shutdown_err;
		if (mSocket.shutdown(asio::socket_base::shutdown_both, shutdown_err))
			nap::Logger::error("%s: %s", getEndPoint().c_str(), shutdown_err.message().c_str());

		std::error_code close_err;
		if (mSocket.close(close_err))
		{
			nap::Logger::error("%s: %s", getEndPoint().c_str(), close_err.message().c_str());
			return;
		}

		// Cancel outstanding timing operations
		nap::Logger::debug("%s: Connection closed", getEndPoint().c_str());

		// Notify connection is closed
		mAdapter.onSocketDisconnected(getID());
	}


	void SocketConnection::timeout(const std::error_code& ec)
	{
		if (!ec)
		{
			nap::Logger::debug("%s: Connection timed out", getEndPoint().c_str());
			assert(mSocket.is_open());
			close();
		}
	}


	void SocketConnection::setTimer()
	{
		mTimeout = std::make_unique<asio::steady_timer>(mSocket.get_executor(), nap::Seconds(5));
		mTimeout->async_wait(
			std::bind(&SocketConnection::timeout, shared_from_this(), std::placeholders::_1)
		);
	}
}
