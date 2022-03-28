/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketthread.h"
#include "socketadapter.h"
#include "socketservice.h"

#include <nap/logger.h>

using asio::ip::address;
using asio::ip::tcp;
using namespace std::chrono_literals;

RTTI_BEGIN_ENUM(nap::ESocketThreadUpdateMethod)
	RTTI_ENUM_VALUE(nap::ESocketThreadUpdateMethod::MAIN_THREAD, 		"Main Thread"),
	RTTI_ENUM_VALUE(nap::ESocketThreadUpdateMethod::SPAWN_OWN_THREAD, 	"Spawn Own Thread"),
	RTTI_ENUM_VALUE(nap::ESocketThreadUpdateMethod::MANUAL, 			"Manual")
RTTI_END_ENUM

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketThread)
	RTTI_PROPERTY("Update Method", 	&nap::SocketThread::mUpdateMethod, nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketThread
	//////////////////////////////////////////////////////////////////////////

    SocketThread::SocketThread(SocketService & service) : mService(service)
	{
		mManualProcessFunc = [this]()
		{
			nap::Logger::warn(*this, "calling manual process function when thread update method is not manual!");
		};
	}


    bool SocketThread::init(utility::ErrorState &errorState)
    {
        return true;
    }


	bool SocketThread::start(utility::ErrorState& errorState)
	{
        mRun.store(true);

		switch (mUpdateMethod)
		{
		case ESocketThreadUpdateMethod::SPAWN_OWN_THREAD:
            mThread = std::thread([this]
                    {
                        std::this_thread::sleep_for(2000ms);
                        thread();
                    });
			break;
		case ESocketThreadUpdateMethod::MAIN_THREAD:
			mService.registerSocketThread(this);
			break;
		case ESocketThreadUpdateMethod::MANUAL:
			mManualProcessFunc = [this]() { process(); };
			break;
		default:
			errorState.fail("Unknown Socket thread update method");
			return false;
		}

		return true;
	}


	void SocketThread::stop()
	{
		if(mRun.load())
		{
            mRun.store(false);

			switch (mUpdateMethod)
			{
			case ESocketThreadUpdateMethod::SPAWN_OWN_THREAD:
				mThread.join();
				break;
			case ESocketThreadUpdateMethod::MAIN_THREAD:
				mService.removeSocketThread(this);
				break;
			default:
				break;
			}
		}
	}


	void SocketThread::thread()
	{
        while (mRun.load())
        {
            process();
        }
	}


	void SocketThread::process()
	{
		std::lock_guard lock(mMutex);

        if(mIOService.stopped())
            mIOService.restart();

        for(auto& adapter : mAdapters)
        {
            adapter->process();
        }

        asio::error_code err;
        mIOService.poll(err);

        if(err)
        {
            nap::Logger::error(*this, err.message());
        }
	}


	void SocketThread::manualProcess()
	{
		mManualProcessFunc();
	}


	void SocketThread::removeAdapter(SocketAdapter * adapter)
	{
		std::lock_guard lock(mMutex);

		auto found_it = std::find_if(mAdapters.begin(), mAdapters.end(), [&](const auto& it)
			{
				return it == adapter;
			});
		assert(found_it != mAdapters.end());
		mAdapters.erase(found_it);
	}


	void SocketThread::registerAdapter(SocketAdapter * adapter)
	{
		std::lock_guard lock(mMutex);

		mAdapters.emplace_back(adapter);
	}
}
