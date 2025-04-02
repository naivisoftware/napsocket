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

RTTI_BEGIN_CLASS(nap::SocketServer)
	RTTI_PROPERTY("Port",			&nap::SocketServer::mPort,			nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("IP Address",		&nap::SocketServer::mIPAddress,	    nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("Enable Log",		&nap::SocketServer::mEnableLog,	    nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SocketServerASIO
	//////////////////////////////////////////////////////////////////////////

	class SocketServer::Impl
	{
	public:
		Impl(asio::io_context& context) : mIOContext(context){}

		asio::io_context& 			mIOContext;
		asio::ip::tcp::endpoint 	mRemoteEndpoint;

		asio::ip::tcp::acceptor		mAcceptor{ mIOContext };
		asio::ip::tcp::socket		mWaitingSocket{ mIOContext };
		std::unordered_map<std::string, asio::ip::tcp::socket> mSockets;
	};


    //////////////////////////////////////////////////////////////////////////
    // SocketServer
    //////////////////////////////////////////////////////////////////////////

    bool SocketServer::onStart(utility::ErrorState &errorState)
    {
		// create asio implementation
		mImpl = std::make_unique<SocketServer::Impl>(getIOContext());

        // when asio error occurs, init_success indicates whether initialization should fail or succeed
        bool init_success = false;

        // try to create ip address
        // when address property is left empty, bind to any local address
		asio::error_code asio_error_code;
		asio::ip::address address;
        if (mIPAddress.empty())
        {
            address = asio::ip::address_v4::any();
        }
		else
        {
            address = asio::ip::make_address(mIPAddress, asio_error_code);
            if (handleAsioError(asio_error_code, errorState, init_success))
                return init_success;
        }

        // create endpoint
        mImpl->mRemoteEndpoint = asio::ip::tcp::endpoint(address, mPort);

        // create acceptor and attach the acceptor callback
        mImpl->mAcceptor = asio::ip::tcp::acceptor(getIOContext(), mImpl->mRemoteEndpoint);

        // create new accepting socket
        acceptNewSocket();

        return true;
    }


    void SocketServer::handleAccept(const asio::error_code& errorCode)
    {
        bool is_error = errorCode.operator bool();
        asio::error_code error_code = errorCode;

		if(is_error)
		{
			// report is_error and accept a new socket
			logError(error_code.message());
			acceptNewSocket();
			return;
		}

		// log status
		logInfo("Socket connected");

		// set no delay
		mImpl->mWaitingSocket.set_option(asio::ip::tcp::no_delay(mNoDelay), error_code);
		is_error = error_code.operator bool();

		if(is_error)
		{
			logError(error_code.message());
			return;
		}

		// read all available bytes, this is to make sure socket stream is empty before we start receiving new data
		size_t available = mImpl->mWaitingSocket.available();
		asio::streambuf receivedStreamBuffer;
		asio::streambuf::mutable_buffers_type bufs = receivedStreamBuffer.prepare(available);

		mImpl->mWaitingSocket.receive(bufs, asio::socket_base::message_end_of_record, error_code);
		is_error = error_code.operator bool();

		if (is_error)
		{
			logError(error_code.message());
		}

		// create new message queue
		std::string socket_id = math::generateUUID();
		mMessageQueueMap.emplace(socket_id, moodycamel::ConcurrentQueue<SocketPacket>());
		mImpl->mSockets.emplace(socket_id, std::move(mImpl->mWaitingSocket));

		// create new accepting socket
		acceptNewSocket();

		// dispatch signal
		socketConnected.trigger(socket_id);
    }


    void SocketServer::onStop()
    {
        SocketAdapter::onDestroy();

        // shutdown sockets
        for(auto& pair : mImpl->mSockets)
        {
            asio::error_code error_code;
            pair.second.shutdown(asio::socket_base::shutdown_both, error_code);

            // log any errors
			bool is_error = error_code.operator bool();
            if (is_error)
                logError(error_code.message());
        }

		mImpl->mSockets.clear();
    }


    void SocketServer::sendToAll(const SocketPacket& message)
    {
        for(auto& pair : mMessageQueueMap)
            pair.second.enqueue(message);
    }


	void SocketServer::sendToAll(SocketPacket&& message)
	{
		for(auto& pair : mMessageQueueMap)
			pair.second.enqueue(std::move(message));
	}


    void SocketServer::send(const std::string &id, const SocketPacket& message)
    {
        auto it = mMessageQueueMap.find(id);
        if(it == mMessageQueueMap.end())
        {
			logError(utility::stringFormat("Cannot send message to socket, id %s not found!", id.c_str()));
			return;
        }
		it->second.enqueue(message);
	}


	void SocketServer::send(const std::string &id, SocketPacket&& message)
	{
		auto it = mMessageQueueMap.find(id);
		if(it == mMessageQueueMap.end())
		{
			logError(utility::stringFormat("Cannot send message to socket, id %s not found!", id.c_str()));
			return;
		}
		it->second.enqueue(std::move(message));
	}


    bool SocketServer::handleError(const std::string& id, asio::error_code& errorCode)
    {
		// has an error occured, close socket and re-attach acceptor callback
		bool is_error = errorCode.operator bool();
		if (!is_error)
			return false;

		// log any errors or info
		logError(utility::stringFormat("Error occured, %s", errorCode.message().c_str()));
		logInfo("Socket disconnected");

		// close the socket
		asio::error_code err;
		auto it = mImpl->mSockets.find(id);
		assert(it != mImpl->mSockets.end());
		it->second.shutdown(asio::socket_base::shutdown_both, err);
		if (err)
			logError(err.message());

		mSocketsToRemove.emplace_back(it->first);
		socketDisconnected.trigger(it->first);
		return true;
    }


    void SocketServer::acceptNewSocket()
    {
        // create socket
        mImpl->mWaitingSocket = asio::ip::tcp::socket(getIOContext());
		mImpl->mAcceptor.async_accept(mImpl->mWaitingSocket, [this](const asio::error_code& errorCode)
        {
            handleAccept(errorCode);
        });
    }


    void SocketServer::onProcess()
    {
        // first remove obsolete sockets
		{
			std::lock_guard<std::mutex> lock(mMutex);
			for (const auto &socket_to_remove: mSocketsToRemove)
			{
				mImpl->mSockets.erase(socket_to_remove);
				mMessageQueueMap.erase(socket_to_remove);
			}
		}
        mSocketsToRemove.clear();

        for(auto& pair : mImpl->mSockets)
        {
            const auto& socket_id = pair.first;
            auto& socket = pair.second;
            if(!socket.is_open())
            	continue;

			// error code
			asio::error_code err;

			// let the socket send queued messages
			{
				std::lock_guard<std::mutex> lock(mMutex);
				auto msg_queue_it = mMessageQueueMap.find(socket_id);
				assert(msg_queue_it != mMessageQueueMap.end());
				auto& msg_queue = msg_queue_it->second;

				SocketPacket msg;
				while(msg_queue.try_dequeue(msg))
				{
					socket.send(asio::buffer(msg.data()), asio::socket_base::message_end_of_record, err);
					if(err)
						break;
				}
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
			asio::streambuf rec_stream_buf;
			asio::streambuf::mutable_buffers_type bufs = rec_stream_buf.prepare(available);
			socket.receive(bufs, asio::socket_base::message_end_of_record, err);

			// bail on error
			if (handleError(socket_id, err))
				continue;

			// dispatch any received messages
			for(const auto& buf : bufs)
			{
				if(buf.size()<=0)
					continue;

				SocketPacket msg(static_cast<const uint8*>(buf.data()), buf.size());
				packetReceived.trigger(socket_id, msg);
			}
        }
    }


    void SocketServer::logError(const std::string& message)
    {
        if(!mEnableLog)
        	return;

		nap::Logger::error(*this, message);
    }


    void SocketServer::logInfo(const std::string& message)
    {
        if(!mEnableLog)
        	return;

		nap::Logger::info(*this, message);
    }


    void SocketServer::clearQueue()
    {
        for(auto& pair : mMessageQueueMap)
        {
            while(pair.second.size_approx()>0)
            {
                SocketPacket msg;
                pair.second.try_dequeue(msg);
            }
        }
    }


    std::vector<std::string> SocketServer::getConnectedClientIDs() const
    {
        std::vector<std::string> clients;
        for(const auto& pair : mImpl->mSockets)
            clients.emplace_back(pair.first);

        return clients;
    }


    size_t SocketServer::getConnectedClientsCount() const
    {
        return mImpl->mSockets.size();
    }
}
