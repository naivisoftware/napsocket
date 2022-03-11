/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Local Includes
#include "socketservice.h"
#include "socketthread.h"

// External includes
#include <memory>

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
	{
	}


	bool SocketService::init(utility::ErrorState& error)
	{
		return true;
	}


	void SocketService::shutdown()
	{
	}


	void SocketService::registerObjectCreators(rtti::Factory& factory)
	{
		factory.addObjectCreator(std::make_unique<SocketThreadObjectCreator>(*this));
	}


	void SocketService::update(double deltaTime)
	{
		for(auto* thread : mThreads)
		{
			thread->process();
		}
	}


	void SocketService::removeSocketThread(SocketThread* thread)
	{
		auto found_it = std::find_if(mThreads.begin(), mThreads.end(), [&](const auto& it)
		{
		  return it == thread;
		});
		assert(found_it != mThreads.end());
		mThreads.erase(found_it);
	}


	void SocketService::registerSocketThread(SocketThread* thread)
	{
		mThreads.emplace_back(thread);
	}
}
