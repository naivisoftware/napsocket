/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "socketserver.h"
#include "socketservice.h"

// ASIO includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>
#include <asio/streambuf.hpp>

// External includes
#include <nap/logger.h>
#include <thread>
#include <mathutils.h>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketServer)
RTTI_CONSTRUCTOR(nap::SocketService&)
	RTTI_PROPERTY("Port",				&nap::SocketServer::mPort,				nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("IPAddress",			&nap::SocketServer::mIPAddress,	    	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("MaxConnections",		&nap::SocketServer::mMaxConnections,	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("MaxMessageSize",		&nap::SocketServer::mMaxMessageSize,	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("EnableLog",			&nap::SocketServer::mEnableLog,	    	nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// ASIO resources
	//////////////////////////////////////////////////////////////////////////

	class SocketServer::Impl
	{
	public:
		Impl(asio::io_context& context, const asio::ip::address& addr, asio::ip::port_type port) :
			mRemoteEndpoint(addr, port),
			mAcceptor(context),
			mContext(context) {}

		// Opens the acceptor
		bool init(utility::ErrorState& errorState)
		{
			asio::error_code ec;
			mAcceptor.open(mRemoteEndpoint.protocol(), ec);
			if (ec) {
				errorState.fail("Failed to open acceptor: %s", ec.message().c_str());
				return false;
			}

			mAcceptor.bind(mRemoteEndpoint, ec);
			if (ec) {
				errorState.fail("Failed to bind acceptor: %s", ec.message().c_str());
				return false;
			}

			mAcceptor.listen(asio::socket_base::max_listen_connections, ec);
			if (ec) {
				errorState.fail("Failed to listen on acceptor: %s", ec.message().c_str());
				return false;
			}
			return true;
		}

		~Impl()
		{
			mAcceptor.close();
		}

		asio::ip::tcp::endpoint mRemoteEndpoint;
		asio::ip::tcp::acceptor	mAcceptor;
		asio::io_context& mContext;
	};


    //////////////////////////////////////////////////////////////////////////
    // SocketServer
    //////////////////////////////////////////////////////////////////////////

	// Constructor
	SocketServer::SocketServer(SocketService& service) :
		SocketAdapter(service) {}


    bool SocketServer::start(utility::ErrorState &errorState)
    {
		if (!SocketAdapter::start(errorState))
			return false;

        // Try to create ip address. When address property is left empty, bind to any local address
		asio::error_code err_code;
		auto address = !mIPAddress.empty() ?
			asio::ip::make_address(mIPAddress, err_code) :
			asio::ip::address_v4::any();

		if (!handleAsioError(err_code, errorState))
			return false;

		// Create asio implementation
		mImpl = std::make_unique<SocketServer::Impl>(mPool->getContext(), address, mPort);

		// Open the acceptor
		if (!mImpl->init(errorState))
			return false;

        // Async accept new sockets
		if (mMaxConnections > 0)
			acceptNewSocket();

        return true;
    }


    void SocketServer::stop()
    {
		SocketAdapter::stop();

		std::lock_guard lock(mConnectionsMutex);

        // Shutdown and close sockets
        for(auto& it : mConnections)
            it.second->close();

		mConnections.clear();

		// Discard ASIO resources
		mImpl.reset();
    }


    void SocketServer::sendToAll(const SocketPacket& message)
    {
		std::lock_guard lock(mConnectionsMutex);
        for(auto& it : mConnections)
            it.second->enqueue(message);
    }


	void SocketServer::sendToAll(SocketPacket&& message)
	{
		std::lock_guard lock(mConnectionsMutex);
		if (mConnections.size() == 1)
		{
			mConnections.begin()->second->enqueue(std::move(message));
			return;
		}

		for(auto& it : mConnections)
			it.second->enqueue(message);
	}


    void SocketServer::send(const SocketID& id, const SocketPacket& message)
    {
		std::lock_guard lock(mConnectionsMutex);
        auto it = mConnections.find(id);
        if(it == mConnections.end())
        {
			logError(utility::stringFormat("Cannot send message to socket, id %s not found!", id.c_str()));
			return;
        }
		it->second->enqueue(message);
	}


	void SocketServer::send(const SocketID& id, SocketPacket&& message)
	{
		std::lock_guard lock(mConnectionsMutex);
		auto it = mConnections.find(id);
		if(it == mConnections.end())
		{
			logError(utility::stringFormat("Cannot send message to socket, id %s not found!", id.c_str()));
			return;
		}
		it->second->enqueue(std::move(message));
	}


    void SocketServer::acceptNewSocket()
	{
		// Abort if the ASIO resources have been discarded
		if (mImpl == nullptr)
			return;

		// Accept socket asynchronously
		mImpl->mAcceptor.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket)
        {
			if (ec)
			{
				// Report error and accept a new socket
				logError(ec.message());
				acceptNewSocket();
				return;
			}

			if (mNoDelay)
			{
				// Set no delay
				if (socket.set_option(asio::ip::tcp::no_delay(mNoDelay), ec))
				{
					logError(ec.message());
					acceptNewSocket();
					return;
				}
			}

			// Create a new connection to handle this client
			{
				std::lock_guard lock(mConnectionsMutex);

				// Deny connection when capacity is reached
				if (mConnections.size() >= mMaxConnections)
				{
					logInfo(utility::stringFormat("Socket denied. Max connections reached"));

					asio::error_code shutdown_err;
					if (socket.shutdown(asio::socket_base::shutdown_both, shutdown_err))
						logError(shutdown_err.message());

					asio::error_code close_err;
					if (socket.close(close_err))
						logError(close_err.message());

					acceptNewSocket();
					return;
				}

				// Create socket connection
				auto conn = std::make_shared<SocketConnection>(mPool->getContext(), std::move(socket), *this, math::generateUUID());

				// Configure connection
				conn->setMaxMessageSize(mMaxMessageSize);

				// Manage a reference to the connection
				const auto result = mConnections.emplace(conn->getID(), std::move(conn));
				assert(result.second);

				logInfo(utility::stringFormat("Socket accepted | %s", result.first->second->getEndPoint().c_str()));

				// Start reading, posts work to the ASIO context
				result.first->second->readHeader();

				// Dispatch signal
				socketConnected(result.first->second->getID());
			}

			// Create new accepting socket
			acceptNewSocket();
        });
    }


	void SocketServer::onPacketReceived(const SocketID& id, const SocketPacket& packet)
	{
		packetReceived(id, packet);
	}


	void SocketServer::onSocketDisconnected(const SocketID& id)
	{
		std::lock_guard<std::mutex> lock(mConnectionsToRemoveMutex);
		mConnectionsToRemove.emplace(id);
	}


    void SocketServer::process()
    {
		// Destroy connections safely
		std::lock_guard<std::mutex> rm_lock(mConnectionsToRemoveMutex);
		if (mConnectionsToRemove.empty())
			return;

		std::lock_guard<std::mutex> lock(mConnectionsMutex);
		for (const auto& id : mConnectionsToRemove)
			mConnections.erase(id);
    }


    void SocketServer::logError(const std::string& message)
    {
        if (mEnableLog)
        	nap::Logger::error(*this, message);
    }


    void SocketServer::logInfo(const std::string& message)
    {
        if (mEnableLog)
        	nap::Logger::info(*this, message);
    }


	uint SocketServer::getConnectionCount()
	{
		std::lock_guard lock(mConnectionsMutex);
		return static_cast<uint>(mConnections.size());
	}
}
