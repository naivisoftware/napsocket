/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/device.h>
#include <queue>
#include <mutex>

// ASIO includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>

// NAP includes
#include <utility/threading.h>
#include <concurrentqueue.h>
#include <nap/signalslot.h>
#include <nap/timer.h>

// Local includes
#include "socketadapter.h"

namespace nap
{
	//////////////////////////////////////////////////////////////////////////

	class NAPAPI SocketClient final : public SocketAdapter
	{
		RTTI_ENABLE(SocketAdapter)
	public:
		/**
		 * Initializes the Socket client
		 * @param error contains error information
		 * @return true on success
		 */
		bool init(utility::ErrorState& errorState) override;

		/**
		 * called on destruction
		 */
		void onDestroy() override;

		void send(const std::string& message);

        void connect();

        Signal<const std::string&> messageReceived;
	public:
		// properties
		int mPort 							= 13251; 		///< Property: 'Port' the port the client socket binds to
		std::string mRemoteIp 				= "10.8.0.3";	///< Property: 'Endpoint' the ip address the client socket binds to
		bool mConnectOnInit                 = true;
        bool mEnableAutoReconnect           = true;
        int  mAutoReconnectIntervalMillis   = 5000;
        bool mEnableLog                     = false;
	protected:
		/**
		 * The process function
		 */
		void process() override;
	private:
        void handleConnect(const asio::error_code& errorCode);

        bool handleError(const asio::error_code& errorCode);

        void clearQueue();

        void logError(const std::string& message);

        void logInfo(const std::string& message);

		// ASIO
		std::unique_ptr<asio::ip::tcp::socket> 		mSocket;
        std::unique_ptr<asio::ip::tcp::endpoint> 	mRemoteEndpoint;

		// Threading
		moodycamel::ConcurrentQueue<std::string> 	mQueue;
        std::atomic_bool mSocketReady = { false };
        std::atomic_bool mConnecting = { false };
        SteadyTimer mReconnectTimer;
	};
}
