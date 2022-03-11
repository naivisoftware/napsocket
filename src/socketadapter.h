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
	//////////////////////////////////////////////////////////////////////////

	class NAPAPI SocketAdapter : public Resource
	{
		friend class SocketThread;

		RTTI_ENABLE(Resource)
	public:
		ResourcePtr<SocketThread> mThread = nullptr; ///< Property: 'Thread' the socket thread the adapter registers itself to

		/**
		 * Initialization
		 * @param error contains error information
		 * @return true on success
		 */
		virtual bool init(utility::ErrorState& errorState) override;

		/**
		 * called on destruction
		 */
		virtual void onDestroy() override;
    public:
        // Properties
        bool mAllowFailure 					= false;		///< Property: 'AllowFailure' if binding to socket is allowed to fail on initialization
	protected:
		/**
		 * called by a SocketThread
		 */
		virtual void process() = 0;

        bool handleAsioError(const asio::error_code& errorCode, utility::ErrorState& errorState, bool& success);

        asio::io_service& getIOService();
	};
}
