/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// Local includes
#include "socketpool.h"
#include "socketpacket.h"
#include "socketid.h"

// ASIO includes
#include <asio/error_code.hpp>

// NAP includes
#include <nap/resourceptr.h>
#include <nap/device.h>

namespace nap
{
	/**
	 * Base class of specific Socket client and server resources.
	 * process() is automatically called by the thread this adapter links to.
	 * Both SocketClient & SocketServer extend SocketAdapter.
	 */
	class NAPAPI SocketAdapter : public Device
	{
		friend class SocketConnection;

		RTTI_ENABLE(Device)
	public:
		/**
		 * Constructor
		 * @param service reference to Socket service
		 */
		SocketAdapter(SocketService& service) :
			mService(service) {}

		/**
		 * Initialization
		 * @param error contains error information
		 * @return true on success
		 */
		virtual bool init(utility::ErrorState& errorState) override;

		/**
		 *
		 */
		virtual bool start(utility::ErrorState& errorState) override;

		/**
		 *
		 */
		virtual void stop() override;

		/**
		 *
		 */
		virtual void process() = 0;

		ResourcePtr<SocketPool> mPool;			///< Property: 'Pool' Shared context
		bool mAllowFailure = false;				///< Property: 'AllowFailure' if binding to socket is allowed to fail on initialization
	    bool mNoDelay = true;					///< Property: 'No Delay' disables Nagle algorithm

	protected:
		/**
		 * Handles asio error, return value based on whether the action should have succeeded
		 * @param errorCode the asio error code to evaluate
		 * @param errorState the errorState if the action failed and success is mandatory
		 * @return whether the program may keep running
		 */
        bool handleAsioError(const asio::error_code& errorCode, utility::ErrorState& errorState);

		// Events
		virtual void onPacketReceived(const socket::ID& id, const SocketPacket& packet) {};
		virtual void onSocketConnected(const socket::ID& id) {};
		virtual void onSocketDisconnected(const socket::ID& id) {};

	private:
		SocketService& mService;
	};
}
