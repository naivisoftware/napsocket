/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "socketserver.h"
#include "socketservice.h"

// External includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>
#include <asio/streambuf.hpp>
#include <nap/logger.h>
#include <nap/assert.h>

#include <thread>
#include <mathutils.h>

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::SocketServer)
RTTI_CONSTRUCTOR(nap::SocketService&)
	RTTI_PROPERTY("Port",			&nap::SocketServer::mPort,			nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("IPAddress",		&nap::SocketServer::mIPAddress,	    nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("EnableLog",		&nap::SocketServer::mEnableLog,	    nap::rtti::EPropertyMetaData::Default)
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
			mAcceptor(context, mRemoteEndpoint)	//< Opens the acceptor
		{ }

		~Impl()
		{
			mAcceptor.close();
		}

		asio::ip::tcp::endpoint mRemoteEndpoint;
		asio::ip::tcp::acceptor	mAcceptor;
	};


    //////////////////////////////////////////////////////////////////////////
    // SocketServer
    //////////////////////////////////////////////////////////////////////////

	// Constructor
	SocketServer::SocketServer(SocketService& service) :
		SocketAdapter(service) {}


    bool SocketServer::start(utility::ErrorState &errorState)
    {
        // Try to create ip address. When address property is left empty, bind to any local address
		asio::error_code err_code;
		auto address = !mIPAddress.empty() ?
			asio::ip::make_address(mIPAddress, err_code) :
			asio::ip::address_v4::any();

		if (!handleAsioError(err_code, errorState))
			return false;

		// Create asio implementation
		mImpl = std::make_unique<SocketServer::Impl>(mPool->getContext(), address, mPort);

        // Async accept new sockets
        acceptNewSocket();

        return true;
    }


    void SocketServer::stop()
    {
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


    void SocketServer::send(const socket::ID& id, const SocketPacket& message)
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


	void SocketServer::send(const socket::ID& id, SocketPacket&& message)
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


    bool SocketServer::handleProcessError(const socket::ID& id, asio::error_code& errorCode)
    {
		// On error, close socket and re-attach acceptor callback
		bool is_error = errorCode.operator bool();
		if (!is_error)
			return false;

		// Log any errors or info
		logError(errorCode.message());
		logError("Socket disconnected");

		{
			std::lock_guard lock(mConnectionsMutex);

			// Close the socket
			auto it = mConnections.find(id); assert(it != mConnections.end());
			it->second->close();

			socketDisconnected.trigger(it->first);
		}
		return true;
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

			// Set no delay
			if (socket.set_option(asio::ip::tcp::no_delay(mNoDelay), ec))
			{
				logError(ec.message());
				acceptNewSocket();
				return;
			}

			// Create a new connection to handle this client
			{
				auto conn = std::make_shared<SocketConnection>(mPool->getContext(), std::move(socket), *this, math::generateUUID());
				std::lock_guard lock(mConnectionsMutex);

				// Manage a reference to the connection
				const auto result = mConnections.emplace(conn->getID(), std::move(conn));
				assert(result.second);

				logInfo(utility::stringFormat("Socket accepted | %s", result.first->second->getEndPoint().c_str()));

				// Start reading
				result.first->second->readHeader();

				// Dispatch signal
				socketConnected(result.first->second->getID());
			}

			// Create new accepting socket
			acceptNewSocket();
        });
    }


	void SocketServer::onPacketReceived(const socket::ID& id, const SocketPacket& packet)
	{
		packetReceived(id, packet);
	}


	void SocketServer::onSocketDisconnected(const socket::ID& id)
	{
		std::lock_guard lock(mConnectionsMutex);
		auto it = mConnections.find(id);
		assert(it != mConnections.end());

		// TODO: test this
		mConnections.erase(it);
	}


    void SocketServer::process()
    {
		// TODO: see if this is useful
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
}
