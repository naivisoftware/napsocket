/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Local Includes
#include "socketservice.h"
#include "socketserver.h"
#include "socketclient.h"

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketService)
	RTTI_CONSTRUCTOR(nap::ServiceConfiguration*)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketService
	//////////////////////////////////////////////////////////////////////////

    SocketService::SocketService(ServiceConfiguration* configuration) :
		Service(configuration)
	{ }


	void SocketService::update(double deltaTime)
	{
		for (auto& adapter : mAdapters)
			adapter->process();
	}


	void SocketService::registerSocketAdapter(SocketAdapter& adapter)
	{
		auto result = mAdapters.emplace(&adapter);
		assert(result.second);
	}


	void SocketService::removeSocketAdapter(SocketAdapter& adapter)
	{
		auto found_it = std::find_if(mAdapters.begin(), mAdapters.end(), [&](const auto& it) {
			return it == &adapter;
		});
		assert(found_it != mAdapters.end());
		mAdapters.erase(found_it);
	}


	void SocketService::registerObjectCreators(rtti::Factory& factory)
	{
		factory.addObjectCreator(std::make_unique<SocketServerObjectCreator>(*this));
		factory.addObjectCreator(std::make_unique<SocketClientObjectCreator>(*this));
	}
}
