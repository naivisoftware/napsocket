/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// Nap includes
#include <nap/resourceptr.h>
#include <socketthread.h>

// ASIO includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>

namespace nap
{
	/**
	 * Base class of specific Socket client and server resources.
	 * process() is automatically called by the thread this adapter links to.
	 * Both SocketClient & SocketServer extend UDPAdapter.
	 */
	class NAPAPI SocketAdapter : public Device
	{
		friend class SocketThread;

		RTTI_ENABLE(Device)
	public:
		/**
		 * Initialization
		 * @param error contains error information
		 * @return true on success
		 */
		virtual bool init(utility::ErrorState& errorState) override;

		/**
		 * Start the adapter. Called after initialization.
		 * When called it is safe to assume that all dependencies have been resolved up to this point.
		 * Internally calls virtual method 'onStart' that is implemented in derived class
		 * Upon successfull start, registers adapter to UDP thread
		 * @param errorState The error state
		 * @return: true on success
		 */
		virtual bool start(utility::ErrorState& errorState) override final;

		/**
		 * Called when the adapter needs to be stopped, but only if start has previously been called on this Device.
		 * It is safe to assume that when stop is called the device is in a 'started' state. Called in reverse init order.
		 * Removed adapter from UDP thread
		 */
		virtual void stop() override final;

		ResourcePtr<SocketThread> mThread;		///< Property: 'Thread' the socket thread the adapter registers itself to
        bool mAllowFailure = false;				///< Property: 'AllowFailure' if binding to socket is allowed to fail on initialization
	    bool mNoDelay = true;					///< Property: 'No Delay' disables Nagle algorithm

	protected:
		/**
         * Called by start method and needs to be implemented by derived class
         * @param errorState The error state
         * @return: true on success
         */
		virtual bool onStart(utility::ErrorState& errorState) = 0;

		/**
		 * Called by stop method and needs to be implemented by derived class
		 */
		virtual void onStop() = 0;

		/**
		 * called by a UDPThread
		 */
		virtual void onProcess() = 0;

		void process();

        bool handleAsioError(const asio::error_code& errorCode, utility::ErrorState& errorState, bool& success);

        asio::io_context& getIOContext();
	};
}
