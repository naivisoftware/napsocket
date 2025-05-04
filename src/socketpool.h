/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/resource.h>
#include <thread>

// NAP includes
#include <nap/numeric.h>

// ASIO forward declaration
namespace asio
{
	class io_context;
}

namespace nap
{
	// forward declares
	class SocketAdapter;
	class SocketService;

    /**
     * SocketPool is responsible for creating an asio::io_service. Any attached SocketAdapters will use this service
     * to create sockets or other objects dependent on the asio::io_service. The thread will call the asio::io_service
     * poll method this updating any objects using the asio::io_service.
     */
	class NAPAPI SocketPool : public Resource
	{
		friend class SocketService;
		friend class SocketAdapter;

		RTTI_ENABLE(Resource)
	public:
		/**
		 * @param errorState contains any errors
		 * @return true on succes
		 */
		virtual bool init(utility::ErrorState& errorState) override;

		/**
		 * Finish outstanding work and quit
		 */
		void onDestroy() override;

		/**
		 * @return asio IO context
		 */
		asio::io_context& getContext();

	private:
		std::thread mThread;

		struct Impl;
		std::unique_ptr<Impl> mImpl;
	};
}
