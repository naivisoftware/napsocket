/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External Includes
#include <nap/service.h>

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// forward declares
	class SocketThread;

	/**
	 * The SocketServer is responsible for processing any SocketThread that has registered itself to receive an
	 * update call by the service. The Update Method of the SocketThread is set to "Main Thread" in that case
	 */
	class NAPAPI SocketService : public Service
	{
		friend class SocketThread;

		RTTI_ENABLE(Service)
	public:
		/**
		 *	Default constructor
		 */
        SocketService(ServiceConfiguration* configuration);

	protected:
		/**
		 * Registers all objects that need a specific way of construction
		 * @param factory the factory to register the object creators with
		 */
		virtual void registerObjectCreators(rtti::Factory& factory) override;

		/**
		 * Initialization
		 * @param error contains error information
		 * @return true on succes
		 */
		virtual bool init(utility::ErrorState& error) override;

		/**
		 * Shuts down the service
		 */
		virtual void shutdown() override;

		/**
		 * Update call wil call process on any registered SocketThread
		 * @param deltaTime time since last update
		 */
		virtual void update(double deltaTime) override;

	private:
		/**
		 * Registers an SocketThread
		 * @param thread the thread to register
		 */
		void registerSocketThread(SocketThread* thread);

		/**
		 * Removes an SocketThread
		 * @param thread the thread do remove
		 */
		void removeSocketThread(SocketThread* thread);

	private:
		// registered udp threads
		std::vector<SocketThread*> mThreads;
	};
}
