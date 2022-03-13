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
    RTTI_PROPERTY("Reconnect On Disconnect",    &nap::SocketClient::mEnableAutoReconnect,           nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Reconnect Interval",         &nap::SocketClient::mAutoReconnectIntervalMillis,   nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Enable Log",                 &nap::SocketClient::mEnableLog,                     nap::rtti::EPropertyMetaData::Default)
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

		// init SocketAdapter, registering the client to an SocketThread
		if (!SocketAdapter::init(errorState))
			return false;

        // connect now if we need to
        if(mConnectOnInit)
            connect();

		return true;
	}


    void SocketClient::connect()
    {
        // try to open socket
        if(!mConnecting.load())
        {
            logInfo("Connecting");
            mConnecting.store(true);
            mSocket->async_connect(*mRemoteEndpoint.get(), [this](const asio::error_code& errorCode){ handleConnect(errorCode); });
        }
    }


	void SocketClient::onDestroy()
	{
        SocketAdapter::onDestroy();

        mSocketReady.store(false);
		asio::error_code err;
		mSocket->close(err);
		if (err)
		{
            logInfo(utility::stringFormat("error closing socket : %s", err.message().c_str()));
		}
	}


	void SocketClient::send(const std::string& message)
	{
        // only queue messages if socket is ready
        if(mSocketReady.load())
        {
            mQueue.enqueue(message);
        }
	}


    void SocketClient::handleConnect(const asio::error_code& errorCode)
    {
        // the process of connecting is finished, whether it succeeded or not
        mConnecting.store(false);

        // no error code
        if(!errorCode)
        {
            logInfo("Socket connected");

            // socket is ready to be used
            mSocketReady.store(true);

            // reconnect timer can be stopped
            mReconnectTimer.stop();

            // message queue can be cleared
            clearQueue();

            //
            connected.trigger();
        }else
        {
            // log error to console
            logError(errorCode.message());

            // close socket
            asio::error_code err;
            mSocket->close(err);
            if(err)
            {
                logError(err.message());
            }

            // if auto reconnect is enabled start the reconnection timer
            if(mEnableAutoReconnect)
            {
                mReconnectTimer.reset();
                mReconnectTimer.start();
            }
        }
    }


    bool SocketClient::handleError(const asio::error_code& errorCode)
    {
        // check if some error occurred, if so, close socket and start reconnecting if required
        if(errorCode)
        {
            // some error occured, log it to console
            logError(utility::stringFormat("Error occured, %s", errorCode.message().c_str()));
            logInfo("Socket disconnected");

            // close active socket
            asio::error_code err;
            mSocket->close(err);
            if (err)
            {
                logError(err.message());
            }

            // socket is not ready
            mSocketReady.store(false);

            // if auto reconnect is enabled start the reconnection time
            if(mEnableAutoReconnect)
            {
                mReconnectTimer.reset();
            }

            //
            disconnected.trigger();

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

                    // bail on error
                    if (handleError(err))
                        return;
                }

                // get available bytes to read
                size_t available = mSocket->available(err);

                // bail on error
                if (handleError(err))
                    return;

                // receive incoming messages
                asio::streambuf receivedStreamBuffer;
                asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
                mSocket->receive(bufs, asio::socket_base::message_end_of_record, err);

                // bail on error
                if (handleError(err))
                    return;

                // dispatch any received messages
                if (bufs.size() > 0)
                {
                    std::string received_message(asio::buffers_begin(bufs), asio::buffers_begin(bufs) + bufs.size());
                    messageReceived.trigger(received_message);
                }
            }
        }else
        {
            // check if we need to reconnect the socket
            if(mEnableAutoReconnect && !mConnecting.load())
            {
                if(mReconnectTimer.getMillis().count() > mAutoReconnectIntervalMillis)
                {
                    connect();
                }
            }
        }
	}


    void SocketClient::clearQueue()
    {
        while(mQueue.size_approx()>0)
        {
            std::string message;
            mQueue.try_dequeue(message);
        }
    }


    void SocketClient::logError(const std::string& message)
    {
        if(mEnableLog)
        {
            nap::Logger::error(*this, message);
        }
    }


    void SocketClient::logInfo(const std::string& message)
    {
        if(mEnableLog)
        {
            nap::Logger::info(*this, message);
        }
    }
}
