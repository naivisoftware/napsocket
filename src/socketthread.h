/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/resourceptr.h>
#include <nap/resource.h>
#include <nap/device.h>
#include <thread>

// NAP includes
#include <nap/numeric.h>
#include <concurrentqueue.h>
#include <rtti/factory.h>

// ASIO includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>

// ASIO forward declaration
namespace asio
{
	class io_context;
}

namespace nap
{
	//////////////////////////////////////////////////////////////////////////

	enum ESocketThreadUpdateMethod : int
	{
		MAIN_THREAD			= 0,
		SPAWN_OWN_THREAD	= 1,
		MANUAL				= 2
	};

	// forward declares
	class SocketAdapter;
	class SocketService;

    /**
     * SocketThread is responsible for creating an asio::io_service. Any attached SocketAdapters will use this service
     * to create sockets or other objects dependent on the asio::io_service. The thread will call the asio::io_service
     * poll method this updating any objects using the asio::io_service. The SocketThread can be updated on the Main thread
     * or create it's own thread that will call process() on any SocketAdapters that use this SocketThread. You can also
     * choose to update the SocketThread manually in which case you need to call manualProcess() yourself from some point
     * in your application. This is useful when wanting to sync the process loop to other running threads in your application.
     */
	class NAPAPI SocketThread : public Device
	{
		friend class SocketService;
		friend class SocketAdapter;

		RTTI_ENABLE(Device)
	public:
		/**
		 * Constructor
		 * @param service reference to UDP service
		 */
		SocketThread(SocketService& service);

		/**
		 * Starts the UDPThread, spawns new thread if necessary or registers to UDPService
		 * @param errorState contains any errors
		 * @return true on succes
		 */
		virtual bool start(utility::ErrorState& errorState) override;

		/**
		 * Stops the SocketThread, stops own thread or removes itself from service
		 */
		virtual void stop() override;

		/**
		 * @return asio IO context
		 */
		asio::io_context& getIOContext();

		/**
		 * Call this when update method is set to manual.
		 * If the update method is MAIN_THREAD or SPAWN_OWN_THREAD, this function will not do anything.
		 */
		void manualProcess();

		// properties
		ESocketThreadUpdateMethod mUpdateMethod = ESocketThreadUpdateMethod::MAIN_THREAD; ///< Property: 'Update Method' the way the SocketThread should process adapters

	private:
		/**
		 * the threaded function
		 */
		void thread();

		/**
		 * the process method, will call process on any registered adapter
		 */
		void process();

        /**
         * Register a socket adapter. Thread-safe
         * @param adapter pointer to the socket adapter
         */
		void registerAdapter(SocketAdapter* adapter);

		/**
		 * Removes an adapter. Thread-safe
		 * @param adapter pointer to the socket adapter
		 */
		void removeAdapter(SocketAdapter* adapter);

		// threading
		std::thread 										mThread;
		std::mutex											mMutex;
		std::atomic_bool 									mRun = { false };
		std::function<void()> 								mManualProcessFunc;

		// service
        SocketService& mService;

		// adapters
		std::vector<SocketAdapter*> mAdapters;

		struct Impl;
		std::unique_ptr<Impl> mImpl;
	};

	// Object creator used for constructing the Socket thread
	using SocketThreadObjectCreator = rtti::ObjectCreator<SocketThread, SocketService>;
}
