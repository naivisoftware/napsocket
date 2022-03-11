/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketclient.h"
#include "socketthread.h"

// External includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>
#include <asio/streambuf.hpp>
#include <nap/logger.h>

#include <thread>

using asio::ip::address;
using asio::ip::tcp;
using asio::buffers_begin;

RTTI_BEGIN_CLASS(nap::SocketClient)
	RTTI_PROPERTY("Endpoint",					&nap::SocketClient::mRemoteIp,						nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("Port",						&nap::SocketClient::mPort,							nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Connect on init",            &nap::SocketClient::mConnectOnInit,                 nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketClient
	//////////////////////////////////////////////////////////////////////////

	bool SocketClient::init(utility::ErrorState& errorState)
	{
        // when asio error occurs, init_success indicates whether initialization should fail or succeed
        bool init_success = false;
        asio::error_code asio_error_code;

        // create address from string
        auto address = address::from_string(mRemoteIp, asio_error_code);
        if(handleAsioError(asio_error_code, errorState, init_success))
            return init_success;

        // create endpoint
        mRemoteEndpoint = std::make_unique<tcp::endpoint>(address, mPort);

        // create socket
        mSocket = std::make_unique<tcp::socket>(getIOService());

		// init UDPAdapter, registering the client to an SocketThread
		if (!SocketAdapter::init(errorState))
			return false;

        if(mConnectOnInit)
            connect();

		return true;
	}


    void SocketClient::connect()
    {
        // try to open socket
        mSocket->async_connect(*mRemoteEndpoint.get(), [this](const asio::error_code& errorCode){ handleConnect(errorCode); });
    }


	void SocketClient::onDestroy()
	{
        SocketAdapter::onDestroy();

        mSocketReady.store(false);
		asio::error_code err;
		mSocket->close(err);
		if (err)
		{
			nap::Logger::error(*this, "error closing socket : %s", err.message().c_str());
		}
	}


	void SocketClient::send(const std::string& message)
	{
        if(mSocketReady.load())
        {
            mQueue.enqueue(message);
        }
	}


    void SocketClient::handleConnect(const asio::error_code& errorCode)
    {
        if(!errorCode)
        {
            nap::Logger::info(*this, "Socket connected");
            mSocketReady.store(true);

            while(mQueue.size_approx()>0)
            {
                std::string message;
                mQueue.try_dequeue(message);
            }
        }else
        {
            nap::Logger::error(*this, errorCode.message());
        }
    }


    bool SocketClient::handleError(const asio::error_code& errorCode)
    {
        if(errorCode)
        {
            nap::Logger::error(*this, "Error occured, %s", errorCode.message().c_str());
            nap::Logger::info(*this, "Socket disconnected");

            asio::error_code err;
            mSocket->close(err);
            if (err)
            {
                nap::Logger::error(*this, err.message());
            }
            mSocketReady.store(false);
            return true;
        }

        return false;
    }


	void SocketClient::process()
	{
        if (mSocketReady.load())
        {
            if(mSocket->is_open())
            {
                // error code
                asio::error_code err;

                // let the socket send queued messages
                std::string message;
                while (mQueue.try_dequeue(message))
                {
                    mSocket->send(asio::buffer(message), asio::socket_base::message_end_of_record, err);

                    if (handleError(err))
                        return;
                }

                size_t available = mSocket->available(err);
                if (handleError(err))
                    return;

                // receive incoming messages
                asio::streambuf receivedStreamBuffer;
                asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
                mSocket->receive(bufs, asio::socket_base::message_end_of_record, err);

                if (handleError(err))
                    return;

                if (bufs.size() > 0)
                {
                    std::string received_message(asio::buffers_begin(bufs), asio::buffers_begin(bufs) + bufs.size());
                    messageReceived.trigger(received_message);
                }
            }
        }
	}
}
