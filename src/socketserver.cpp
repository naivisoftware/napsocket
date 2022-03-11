/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "socketserver.h"
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

RTTI_BEGIN_CLASS(nap::SocketServer)
        RTTI_PROPERTY("Port",			&nap::SocketServer::mPort,			nap::rtti::EPropertyMetaData::Default)
        RTTI_PROPERTY("IP Address",		&nap::SocketServer::mIPAddress,	    nap::rtti::EPropertyMetaData::Default)
        RTTI_PROPERTY("Enable Log",		&nap::SocketServer::mEnableLog,	    nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
    //////////////////////////////////////////////////////////////////////////
    // SocketServer
    //////////////////////////////////////////////////////////////////////////

    bool SocketServer::init(utility::ErrorState &errorState)
    {
        // when asio error occurs, init_success indicates whether initialization should fail or succeed
        bool init_success = false;
        asio::error_code asio_error_code;

        // try to create ip address
        // when address property is left empty, bind to any local address
        asio::ip::address address;
        if (mIPAddress.empty())
        {
            address = asio::ip::address_v4::any();
        } else
        {
            address = asio::ip::make_address(mIPAddress, asio_error_code);
            if (handleAsioError(asio_error_code, errorState, init_success))
                return init_success;
        }



        // create endpoint
        mRemoteEndpoint = std::make_unique<tcp::endpoint>(address, mPort);

        // create acceptor and attach the acceptor callback
        mAcceptor = std::make_unique<tcp::acceptor>(getIOService(), *mRemoteEndpoint.get());

        //
        createNewSocket();

        // init the adapter
        if(!SocketAdapter::init(errorState))
            return false;

        return true;
    }


    void SocketServer::handleAccept(asio::ip::tcp::socket& socket, const asio::error_code& errorCode)
    {
        if(!errorCode)
        {
            // log status
            logInfo("Socket connected");

            // read all available bytes, this is to make sure socket stream is empty before we start receiving new data
            size_t available = socket.available();
            asio::streambuf receivedStreamBuffer;
            asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
            asio::error_code err;
            socket.receive(bufs, asio::socket_base::message_end_of_record, err);
            if(err)
            {
                logError(err.message());
            }

            //
            createNewSocket();
        }else
        {
            // log error
            logError(errorCode.message());

            mSocketsToRemove.emplace_back(&socket);

            createNewSocket();
        }
    }


    void SocketServer::onDestroy()
    {
        SocketAdapter::onDestroy();

        // close socket
        for(auto& socket : mSockets)
        {
            asio::error_code asio_error_code;
            socket->close(asio_error_code);

            // log any errors
            if (asio_error_code)
            {
                logError(asio_error_code.message());
            }
        }

        mSockets.clear();
    }


    void SocketServer::send(const std::string &message)
    {
        mQueue.try_enqueue(message);
    }


    bool SocketServer::handleError(asio::ip::tcp::socket& socket, const asio::error_code& errorCode)
    {
        // has an error occured, close socket and re-attach acceptor callback
        if(errorCode)
        {
            // log any errors or info
            logError(utility::stringFormat("Error occured, %s", errorCode.message().c_str()));
            logInfo("Socket disconnected");

            // close the socket
            asio::error_code err;
            socket.close(err);
            if (err)
            {
                logError(err.message());
            }

            mSocketsToRemove.emplace_back(&socket);

            return true;
        }

        return false;
    }


    void SocketServer::createNewSocket()
    {
        // create socket
        mSockets.emplace_back(std::make_unique<tcp::socket>(getIOService()));
        auto* socket_ptr = mSockets[mSockets.size()-1].get();
        mAcceptor->async_accept(*socket_ptr, [this, socket_ptr](const asio::error_code& errorCode)
        {
            handleAccept(*socket_ptr, errorCode);
        });
    }

    void SocketServer::process()
    {
        std::vector<std::string> message_queue;
        std::string message;
        while (mQueue.try_dequeue(message))
        {
            message_queue.emplace_back(message);
        }

        // first remove obsolete sockets
        for(auto* socket_to_remove : mSocketsToRemove)
        {
            auto itr = std::find_if(mSockets.begin(), mSockets.end(), [socket_to_remove](const std::unique_ptr<tcp::socket>& socket)
            {
                return socket.get() == socket_to_remove;
            });

            if(itr!=mSockets.end());
                mSockets.erase(itr);
        }
        mSocketsToRemove.clear();

        for(auto& socket : mSockets)
        {
            if(socket->is_open())
            {
                // error code
                asio::error_code err;

                // let the socket send queued messages
                std::string message;
                for(auto& message : message_queue)
                {
                    socket->send(asio::buffer(message), asio::socket_base::message_end_of_record, err);

                    if (handleError(*socket.get(), err))
                        break;
                }

                // if we had a
                if(err)
                    continue;

                // get available bytes
                size_t available = socket->available(err);

                // bail on error
                if (handleError(*socket.get(), err))
                    continue;

                // receive incoming messages
                asio::streambuf receivedStreamBuffer;
                asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
                socket->receive(bufs, asio::socket_base::message_end_of_record, err);

                // bail on error
                if (handleError(*socket.get(), err))
                    continue;

                if (bufs.size() > 0)
                {
                    std::string received_message(asio::buffers_begin(bufs), asio::buffers_begin(bufs) + bufs.size());
                    messageReceived.trigger(received_message);
                }
            }
        }
    }


    void SocketServer::logError(const std::string& message)
    {
        if(mEnableLog)
        {
            nap::Logger::error(*this, message);
        }
    }


    void SocketServer::logInfo(const std::string& message)
    {
        if(mEnableLog)
        {
            nap::Logger::info(*this, message);
        }
    }


    void SocketServer::clearQueue()
    {
        while(mQueue.size_approx()>0)
        {
            std::string message;
            mQueue.try_dequeue(message);
        }
    }
}
