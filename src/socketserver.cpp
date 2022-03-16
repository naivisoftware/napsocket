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
#include <mathutils.h>


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

        // create new accepting socket
        acceptNewSocket();

        // init the adapter
        if(!SocketAdapter::init(errorState))
            return false;

        return true;
    }


    void SocketServer::handleAccept(const asio::error_code& errorCode)
    {
        bool error = errorCode.operator bool();
        asio::error_code error_code = errorCode;

        if(!error)
        {
            // log status
            logInfo("Socket connected");

            // set no delay
            mWaitingSocket->set_option(tcp::no_delay(mNoDelay), error_code);
            bool error = error_code.operator bool();
            if(!error)
            {
                // read all available bytes, this is to make sure socket stream is empty before we start receiving new data
                size_t available = mWaitingSocket->available();
                asio::streambuf receivedStreamBuffer;
                asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
                asio::error_code err;
                mWaitingSocket->receive(bufs, asio::socket_base::message_end_of_record, err);
                if (err)
                {
                    logError(err.message());
                }

                // create new message queue
                std::string socket_id = math::generateUUID();
                mMessageQueue.emplace(socket_id, moodycamel::ConcurrentQueue<std::string>());
                mSockets.emplace(socket_id, std::move(mWaitingSocket));

                // create new accepting socket
                acceptNewSocket();

                // dispatch signal
                socketConnected.trigger(socket_id);
            }
        }

        if(error)
        {
            // log error
            logError(error_code.message());

            // create new accepting socket
            acceptNewSocket();
        }
    }


    void SocketServer::onDestroy()
    {
        SocketAdapter::onDestroy();

        // shutdown sockets
        for(auto& pair : mSockets)
        {
            asio::error_code asio_error_code;
            pair.second->shutdown(asio::socket_base::shutdown_both,asio_error_code);

            // log any errors
            if (asio_error_code)
            {
                logError(asio_error_code.message());
            }
        }

        mSockets.clear();
    }


    void SocketServer::sendToAll(const std::string &message)
    {
        for(auto& pair : mMessageQueue)
        {
            pair.second.enqueue(message);
        }
    }


    void SocketServer::send(const std::string &id, const std::string &message)
    {
        auto itr = mMessageQueue.find(id);
        if(itr!=mMessageQueue.end())
        {
            itr->second.enqueue(message);
        }else
        {
            logError(utility::stringFormat("Cannot send message to socket, id %s not found!", id.c_str()));
        }
    }


    bool SocketServer::handleError(const std::string& id, asio::error_code& errorCode)
    {
        // has an error occured, close socket and re-attach acceptor callback
        if(errorCode)
        {
            // log any errors or info
            logError(utility::stringFormat("Error occured, %s", errorCode.message().c_str()));
            logInfo("Socket disconnected");

            // close the socket
            asio::error_code err;
            auto itr = mSockets.find(id);
            assert(itr!=mSockets.end());
            itr->second->shutdown(asio::socket_base::shutdown_both, err);
            if (err)
            {
                logError(err.message());
            }

            mSocketsToRemove.emplace_back(itr->first);
            socketDisconnected.trigger(itr->first);

            return true;
        }

        return false;
    }


    void SocketServer::acceptNewSocket()
    {
        // create socket
        mWaitingSocket = std::make_unique<tcp::socket>(getIOService());
        mAcceptor->async_accept(*mWaitingSocket, [this](const asio::error_code& errorCode)
        {
            handleAccept(errorCode);
        });
    }

    void SocketServer::process()
    {
        // first remove obsolete sockets
        for(const auto& socket_to_remove : mSocketsToRemove)
        {
            mSockets.erase(socket_to_remove);
            mMessageQueue.erase(socket_to_remove);
        }
        mSocketsToRemove.clear();

        for(auto& pair : mSockets)
        {
            const auto& socket_id = pair.first;
            auto& socket = *pair.second;
            if(socket.is_open())
            {
                // error code
                asio::error_code err;

                // let the socket send queued messages
                std::string message;
                auto message_queue_itr = mMessageQueue.find(socket_id);
                assert(message_queue_itr!=mMessageQueue.end());
                auto& message_queue = message_queue_itr->second;
                while(message_queue.try_dequeue(message))
                {
                    socket.send(asio::buffer(message), asio::socket_base::message_end_of_record, err);

                    if(err)
                        break;
                }

                // bail on error
                if (handleError(socket_id, err))
                    continue;

                // get available bytes
                size_t available = socket.available(err);

                // bail on error
                if (handleError(socket_id, err))
                    continue;

                // receive incoming messages
                asio::streambuf receivedStreamBuffer;
                asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);
                socket.receive(bufs, asio::socket_base::message_end_of_record, err);

                // bail on error
                if (handleError(socket_id, err))
                    continue;

                // dispatch any received messages
                for(auto& buf : bufs)
                {
                    if(buf.size()>0)
                    {
                        std::string received_message(static_cast<char*>(buf.data()), buf.size());
                        messageReceived.trigger(socket_id, received_message);
                    }
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
        for(auto& pair : mMessageQueue)
        {
            while(pair.second.size_approx()>0)
            {
                std::string message;
                pair.second.try_dequeue(message);
            }
        }
    }


    std::vector<std::string> SocketServer::getConnectedClientIDs() const
    {
        std::vector<std::string> clients;
        for(const auto& pair : mSockets)
        {
            clients.emplace_back(pair.first);
        }
        return clients;
    }


    size_t SocketServer::getConnectedClientsCount() const
    {
        return mSockets.size();
    }
}
