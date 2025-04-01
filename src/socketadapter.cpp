/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketadapter.h"
#include "socketthread.h"

#include <nap/logger.h>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketAdapter)
	RTTI_PROPERTY("Thread", &nap::SocketAdapter::mThread, nap::rtti::EPropertyMetaData::Required)
    RTTI_PROPERTY("AllowFailure", &nap::SocketAdapter::mAllowFailure, nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("No Delay", &nap::SocketAdapter::mNoDelay, nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketAdapter
	//////////////////////////////////////////////////////////////////////////

	bool SocketAdapter::init(utility::ErrorState& errorState)
	{
		if(!errorState.check(mThread !=nullptr, "Thread cannot be nullptr"))
			return false;

		return true;
	}


	bool SocketAdapter::start(utility::ErrorState& errorState)
	{
		mThread->registerAdapter(this);
		return true;
	}


	void SocketAdapter::stop()
	{
		mThread->removeAdapter(this);
		onStop();
	}


	void SocketAdapter::process()
	{
		onProcess();
	}


	bool SocketAdapter::handleAsioError(const asio::error_code& errorCode, utility::ErrorState& errorState, bool& success)
    {
        if(errorCode)
        {
            if(!mAllowFailure)
            {
                success = false;
                errorState.fail(errorCode.message());

                return true;
            }else
            {
                success = true;
                nap::Logger::error(*this, errorCode.message());

                return true;
            }
        }

        return false;
    }


    asio::io_service& SocketAdapter::getIOContext()
    {
        return mThread->getIOContext();
    }
}
