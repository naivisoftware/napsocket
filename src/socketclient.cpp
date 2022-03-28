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
    RTTI_PROPERTY("Connect Timeout",            &nap::SocketClient::mConnectTimeOutMillis,          nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Enable Log",                 &nap::SocketClient::mEnableLog,                     nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Write Timeout",              &nap::SocketClient::mWriteTimeOutMillis,            nap::rtti::EPropertyMetaData::Default)
    RTTI_PROPERTY("Read Timeout",               &nap::SocketClient::mReadTimeOutMillis,             nap::rtti::EPropertyMetaData::Default)
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
            mConnecting.store(true);
            mTimeoutTimer.reset();
            mTimeoutTimer.start();

            logInfo("Connecting");
            mSocket->async_connect(*mRemoteEndpoint.get(), [this](const asio::error_code& errorCode){ handleConnect(errorCode); });
        }
    }


    void SocketClient::disconnect()
    {
        asio::error_code err;
        mSocket->shutdown(asio::socket_base::shutdown_both, err);
        if (err)
        {
            logInfo(utility::stringFormat("error closing socket : %s", err.message().c_str()));
        }

        if(mConnecting.load())
        {
            mConnecting.store(false);
        }

        if(mSocketReady.load())
        {
            mSocketReady.store(false);
        }
    }


	void SocketClient::onDestroy()
	{
        SocketAdapter::onDestroy();

        mSocketReady.store(false);
		asio::error_code err;
		mSocket->shutdown(asio::socket_base::shutdown_both, err);
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

        // stop timeout timer
        mTimeoutTimer.stop();

        bool error = errorCode.operator bool();
        asio::error_code error_code = errorCode;

        // no error code
        if(!error)
        {
            // set socket options

            // no delay
            mSocket->set_option(tcp::no_delay(mNoDelay), error_code);

            if (error_code)
            {
                error = true;
            } else
            {
                // socket is ready to be used
                mSocketReady.store(true);

                logInfo("Socket connected");

                // reconnect timer can be stopped
                mReconnectTimer.stop();

                // message queue can be cleared
                clearQueue();

                // trigger connected signal
                connected.trigger();
            }
        }

        if(error)
        {
            // log error to console
            logError(error_code.message());

            // close socket
            mSocket->close(error_code);
            if(error_code)
            {
                logError(error_code.message());
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
        if(errorCode && mSocketReady.load())
        {
            // socket is not ready
            mSocketReady.store(false);

            // some error occured, log it to console
            logError(utility::stringFormat("Error occured, %s", errorCode.message().c_str()));
            logInfo("Socket disconnected");

            // shutdown active socket
            asio::error_code err;
            mSocket->shutdown(asio::socket_base::shutdown_both, err);
            if (err)
            {
                logError(err.message());
            }

            // if auto reconnect is enabled start the reconnection time
            if(mEnableAutoReconnect)
            {
                mReconnectTimer.reset();
            }

            // trigger disconnected signal
            disconnected.trigger();

            return true;
        }

        return false;
    }


	void SocketClient::process()
	{
        std::function<void()> action;
        while(mActionQueue.try_dequeue(action))
        {
            action();
        }

        if (mSocketReady.load())
        {
            if(mSocket->is_open())
            {
                // error code
                asio::error_code err;

                // let the socket send queued messages
                std::string message;
                if(!mWritingData)
                {
                    if (mQueue.try_dequeue(message))
                    {
                        mWritingData = true;
                        mWriteResponseTimer.reset();
                        mWriteResponseTimer.start();

                        mWriteBuffer = message;
                        asio::async_write(*mSocket,
                                          asio::buffer(mWriteBuffer),
                                          asio::transfer_exactly(mWriteBuffer.size()),
                                          [this](const asio::error_code& errorCode, std::size_t bytes_transferred)
                        {
                            // not writing data anymore
                            mWritingData = false;

                            // handle error
                            handleError(errorCode);

                            // stop response timer
                            mWriteResponseTimer.stop();
                        });
                    }
                }else
                {
                    if(mWriteResponseTimer.getMillis().count() > mWriteTimeOutMillis)
                    {
                        // stop response timer
                        mWriteResponseTimer.stop();

                        // not writing data
                        mWritingData = false;

                        // socket is not ready
                        mSocketReady.store(false);

                        // timeout occured
                        // log error to console
                        logError("Write timeout occured!");

                        // close socket
                        asio::error_code error_code;
                        mSocket->close(error_code);
                        if(error_code)
                        {
                            logError(error_code.message());
                        }

                        // if auto reconnect is enabled start the reconnection timer
                        if(mEnableAutoReconnect)
                        {
                            mReconnectTimer.reset();
                            mReconnectTimer.start();
                        }
                    }
                }

                if(!mReceivingData)
                {
                    // get available bytes to read
                    size_t available = mSocket->available(err);

                    // bail on error
                    if (handleError(err))
                        return;

                    if(available>0)
                    {
                        mReceivingData = true;
                        mReadResponseTimer.reset();
                        mReadResponseTimer.start();

                        // receive incoming messages
                        asio::async_read(*mSocket,
                                         mStreamBuffer,
                                         asio::transfer_exactly(available),
                                         [this](const asio::error_code& errorCode, std::size_t bytes_transferred)
                        {
                            // not receiving any data
                            mReceivingData = false;

                            // stop timer
                            mReadResponseTimer.stop();

                            // Read the data received
                            auto data = mStreamBuffer.data();

                            // Consume it after
                            mStreamBuffer.consume(bytes_transferred);

                            if(!handleError(errorCode))
                            {
                                // dispatch any received messages
                                std::string data_string;
                                if(bytes_transferred>0)
                                {
                                    data_string += std::string(asio::buffers_begin(data), asio::buffers_end(data));

                                    if(!data_string.empty())
                                    {
                                        dataReceived.trigger(data_string);
                                    }
                                }
                            }
                        });
                    }
                }else
                {
                    if(mReadResponseTimer.getMillis().count() > mReadTimeOutMillis)
                    {
                        // stop read response timer
                        mReadResponseTimer.stop();

                        // stop sending data
                        mReceivingData = false;

                        // socket is not ready
                        mSocketReady.store(false);

                        // timeout occured
                        // log error to console
                        logError("Read timeout occured!");

                        // close socket
                        asio::error_code error_code;
                        mSocket->close(error_code);
                        if(error_code)
                        {
                            logError(error_code.message());
                        }

                        // if auto reconnect is enabled start the reconnection timer
                        if(mEnableAutoReconnect)
                        {
                            mReconnectTimer.reset();
                            mReconnectTimer.start();
                        }
                    }
                }
            }else
            {
                // log
                logInfo("Socket disconnected");

                // socket is not ready
                mSocketReady.store(false);

                // shutdown active socket
                asio::error_code err;
                mSocket->shutdown(asio::socket_base::shutdown_both, err);
                if (err)
                {
                    logError(err.message());
                }

                // if auto reconnect is enabled start the reconnection time
                if(mEnableAutoReconnect)
                {
                    mReconnectTimer.reset();
                }

                // trigger disconnected signal
                disconnected.trigger();
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

        if(mConnecting.load())
        {
            if(mTimeoutTimer.getMillis().count() > mConnectTimeOutMillis)
            {
                mConnecting.store(false);

                asio::error_code error_code;
                mTimeoutTimer.reset();
                mTimeoutTimer.stop();

                // log error to console
                logError("Connect timeout occured!");

                // close socket
                mSocket->close(error_code);
                if(error_code)
                {
                    logError(error_code.message());
                }

                // if auto reconnect is enabled start the reconnection timer
                if(mEnableAutoReconnect)
                {
                    mReconnectTimer.reset();
                    mReconnectTimer.start();
                }
            }
        }

        postProcessSignal.trigger();
	}


    void SocketClient::clearQueue()
    {
        while(mQueue.size_approx()>0)
        {
            std::string message;
            mQueue.try_dequeue(message);
        }
    }


    bool SocketClient::isConnected() const
    {
        return mSocketReady.load();
    }


    bool SocketClient::isConnecting() const
    {
        return mConnecting.load();
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


    void SocketClient::addMessageReceivedSlot(Slot<const std::string&>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            dataReceived.connect(slot);
        });
    }


    void SocketClient::removeMessageReceivedSlot(Slot<const std::string&>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            dataReceived.disconnect(slot);
        });
    }


    void SocketClient::addConnectedSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            connected.connect(slot);
        });
    }


    void SocketClient::removeConnectedSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            connected.disconnect(slot);
        });
    }

    void SocketClient::addDisconnectedSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            disconnected.connect(slot);
        });
    }


    void SocketClient::removeDisconnectedSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            disconnected.disconnect(slot);
        });
    }


    void SocketClient::addPostProcessSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            postProcessSignal.connect(slot);
        });
    }


    void SocketClient::removePostProcessSlot(Slot<>& slot)
    {
        mActionQueue.enqueue([this, &slot]()
        {
            postProcessSignal.disconnect(slot);
        });
    }
}
