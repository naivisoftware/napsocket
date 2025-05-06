/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// Local includes
#include "socketpool.h"
#include "socketpacket.h"
#include "socketid.h"

// ASIO Includes
#include <asio/ip/tcp.hpp>
#include <asio/streambuf.hpp>
#include <asio/steady_timer.hpp>

// External includes
#include <utility/dllexport.h>
#include <nap/timer.h>
#include <concurrentqueue.h>
#include <queue>
#include <future>

namespace nap
{
	/**
	 * SocketConnection
	 */
	class NAPAPI SocketConnection : public std::enable_shared_from_this<SocketConnection>
	{
		friend class SocketServer;
		friend class SocketClient;
		friend class SocketAdapter;
	public:
		// Constructor
		SocketConnection(asio::io_context& context, asio::ip::tcp::socket&& socket, SocketAdapter& adapter, const socket::ID& id);

		// Destructor
		~SocketConnection();

		// Disable copy
		SocketConnection& operator=(const SocketConnection&) = delete;
		SocketConnection(SocketConnection&) = delete;

		// Disable move
		SocketConnection& operator=(SocketConnection&& other) = delete;
		SocketConnection(SocketConnection&& other) = delete;

		/**
		 * Only valid for client
		 * @return whether the connection is valid
		 */
		bool isConnected();

		/**
		 * @return the connection id
		 */
		const socket::ID& getID() const { return mID; }

		/**
		 * @return the ip address
		 */
		const std::string& getEndPoint() const { return mEndpointText; }

	private:
		// Called from client thread
		std::future<bool> connect();
		std::future<void> disconnect();

		// Enqueue packets
		void enqueue(const SocketPacket& packet);
		void enqueue(SocketPacket&& packet);

		// Called from asio execution thread
		void write(const SocketPacket& packet);

		void readHeader();
		void readBody();

		void close();

		void timeout(const std::error_code& ec);
		void setTimer();

		asio::io_context&			mContext;			//< ASIO context
		socket::ID					mID;				//< Socket ID
		asio::ip::tcp::socket		mSocket;			//< Communication socket
		asio::ip::tcp::endpoint 	mEndpoint;			//< Endpoint description
		std::string					mEndpointText;		//< Cached endpoint text for quick lookup

		SocketAdapter& mAdapter;						//< Connection owner

		// Message queues
		std::deque<SocketPacket> mOutQueue;
		std::deque<SocketPacket> mInQueue;
		SocketPacket mIncomingMsgBuffer;

		// Async objects -> accessed from socket execution context
		asio::streambuf mRespBuffer;					//< Response buffer
		std::unique_ptr<asio::steady_timer> mTimeout;	//< Timeout connection timer

		std::atomic<bool> mIsConnected{false};

		// Misc
		const size_t mMaxMessageSize = 1<<20;
	};
}
