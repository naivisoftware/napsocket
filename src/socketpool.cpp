/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketpool.h"
#include "socketadapter.h"

// ASIO includes
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>

RTTI_BEGIN_CLASS(nap::SocketPool)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketPool::Impl
	//////////////////////////////////////////////////////////////////////////

	class SocketPool::Impl
	{
	public:
		using Guard = asio::executor_work_guard<asio::io_context::executor_type>;

		Impl() : mGuard(std::make_unique<Guard>(asio::make_work_guard(mContext))) {}

		asio::io_context mContext;
		std::unique_ptr<Guard> mGuard;	//< Ensures there is work outstanding in the context
	};


	//////////////////////////////////////////////////////////////////////////
	// SocketPool
	//////////////////////////////////////////////////////////////////////////

	bool SocketPool::init(utility::ErrorState& errorState)
	{
		// Create context
		mImpl = std::make_unique<Impl>();

		// Start thread
		mThread = std::thread([this] { getContext().run(); });

		return true;
	}


	void SocketPool::onDestroy()
	{
		mImpl->mGuard.reset();
		if (mThread.joinable())
			mThread.join();

		mImpl.reset();
	}


	asio::io_service& SocketPool::getContext()
	{
		return mImpl->mContext;
	}
}
