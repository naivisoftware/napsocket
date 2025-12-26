/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External Includes
#include <nap/service.h>

// Local includes
#include "socketadapter.h"

namespace nap
{
	/**
	 * SocketService
	 */
	class NAPAPI SocketService : public Service
	{
		friend class SocketAdapter;

		RTTI_ENABLE(Service)
	public:
		/**
		 *	Default constructor
		 */
        SocketService(ServiceConfiguration* configuration);

	private:
		/**
		 *
		 * @param deltaTime
		 */
		virtual void update(double deltaTime) override;

		/**
		 * Registers a SocketAdapter
		 * @param adapter the adapter to register
		 */
		void registerSocketAdapter(SocketAdapter& adapter);

		/**
		 * Removes a SocketAdapter
		 * @param adapter the adapter do remove
		 */
		void removeSocketAdapter(SocketAdapter& adapter);

		/**
		 * Registers all objects that need a specific way of construction
		 * @param factory the factory to register the object creators with
		 */
		virtual void registerObjectCreators(rtti::Factory& factory) override;

		// Adapters registry
		std::set<SocketAdapter*> mAdapters;
	};
}
