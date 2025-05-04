/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketadapter.h"
#include "socketservice.h"
#include "socketconnection.h"

// ASIO includes
#include <asio/error_code.hpp>

// External includes
#include <nap/logger.h>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketAdapter)
	RTTI_PROPERTY("Pool",			&nap::SocketAdapter::mPool,			nap::rtti::EPropertyMetaData::Required)
    RTTI_PROPERTY("AllowFailure", 	&nap::SocketAdapter::mAllowFailure, nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("No Delay", 		&nap::SocketAdapter::mNoDelay, 		nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketAdapter
	//////////////////////////////////////////////////////////////////////////

	bool SocketAdapter::init(utility::ErrorState& errorState)
	{
		if (!errorState.check(mPool != nullptr, "Missing Socket Pool"))
			return false;

		return true;
	}


	bool SocketAdapter::start(utility::ErrorState& errorState)
	{
		mService.registerSocketAdapter(*this);
		return true;
	}


	void SocketAdapter::stop()
	{
		mService.removeSocketAdapter(*this);
	}


	bool SocketAdapter::handleAsioError(const asio::error_code& errorCode, utility::ErrorState& errorState)
    {
		if (!errorCode)
			return true;

		if (!mAllowFailure)
		{
			errorState.fail("%s: %s", mID.c_str(), errorCode.message().c_str());
			return false;
		}
		nap::Logger::error(*this, errorCode.message().c_str());
		return true;
    }
}
