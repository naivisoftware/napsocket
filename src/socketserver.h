/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#pragma once

// External includes
#include <thread>
#include <mutex>

// NAP includes
#include <nap/device.h>
#include <nap/numeric.h>
#include <nap/signalslot.h>

// Local includes
#include "socketconnection.h"
#include "socketadapter.h"
#include "socketpacket.h"
#include "socketpool.h"
#include "socketid.h"

namespace nap
{
    /**
     * SocketServer creates a new socket and waits for any incoming connections.
     * You can connect as many clients as you want to the server.
     * Every new connection / socket will get a unique ID.
     */
    class NAPAPI SocketServer final : public SocketAdapter
    {
        RTTI_ENABLE(SocketAdapter)
    public:
		// Constructor
		SocketServer(SocketService& service);

        /**
         * Send message to all connected sockets
         * @param message the message
         */
        void sendToAll(const SocketPacket& message);

		/**
		 * Send message to all connected sockets
		 * @param message the message
		 */
		void sendToAll(SocketPacket&& message);

        /**
         * Send message to specific socket
         * @param id client id
         * @param message the message
         */
        void send(const socket::ID& id, const SocketPacket& message);

		/**
		 * Send message to specific socket
		 * @param id client id
		 * @param message the message
		 */
		void send(const socket::ID& id, SocketPacket&& message);

        /**
         * Packet received signal will be dispatched on the thread this SocketAdapter is registered to, see SocketPool
         * First argument is id, second is received message
         */
        Signal<const socket::ID&, const SocketPacket&> packetReceived;

        /**
         * Socket connected signal, will be dispatched on the thread this SocketAdapter is registered to, see SocketPool
         * Argument is id of socket connected
         */
        Signal<const socket::ID&> socketConnected;

        /**
         * Socket disconnected signal, will be dispatched on the thread this SocketAdapter is registered to, see SocketPool
         * Argument is id of socket disconnected
         */
        Signal<const socket::ID&> socketDisconnected;

		int mPort = 13251;				///< Property: 'Port' the port the server socket binds to
		std::string mIPAddress;			///< Property: 'IP Address' local ip address to bind to, if left empty will bind to any local address
		uint mMaxConnections = 4;		///< Property: 'MaxConnections' the maximum number of clients that can be connected at one time
		uint mMaxMessageSize = 1 << 20;	///< Property: 'MaxMessageSize' the maximum size of messages in bytes
		bool mEnableLog = false;        ///< Property: 'Enable Log' whether the server should log to the console

	protected:
		/**
		 * Called when server socket needs to be created
		 * @param errorState The error state
		 * @return true on success
		 */
		virtual bool start(utility::ErrorState& errorState) override final;

		/**
		 * Called when socket needs to be closed
		 */
		virtual void stop() override final;

		/**
		 *
		 */
		virtual void process() override;

    private:
        /**
         * Log an error to the console
         * @param message the message to log
         */
        void logError(const std::string& message);

        /**
         * Log a message to console
         * @param message the message to log
         */
        void logInfo(const std::string& message);

		/**
         * Creates a new socket and tells the acceptor to wait for new connections
         */
        void acceptNewSocket();

		/**
		 *
		 * @param id
		 * @param packet
		 */
		virtual void onPacketReceived(const socket::ID& id, const SocketPacket& packet) override;

		/**
		 *
		 * @param id
		 */
		virtual void onSocketDisconnected(const socket::ID& id) override;

		// Server specific ASIO resources
		class Impl;
		std::unique_ptr<Impl> mImpl;

		// Connections
		std::unordered_map<socket::ID, std::shared_ptr<SocketConnection>> mConnections;

        // Threading
		std::mutex mConnectionsMutex;
	};

	// Object creator
	using SocketServerObjectCreator = rtti::ObjectCreator<SocketServer, SocketService>;
}
