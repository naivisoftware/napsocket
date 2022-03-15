/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/device.h>
#include <thread>
#include <mutex>

// NAP includes
#include <nap/numeric.h>
#include <concurrentqueue.h>
#include <nap/signalslot.h>

// ASIO includes
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <asio/io_service.hpp>
#include <asio/system_error.hpp>
#include <asio/ts/internet.hpp>

// Local includes
#include "socketadapter.h"

namespace nap
{
    //////////////////////////////////////////////////////////////////////////

    /**
     * SocketServer creates a new socket and waits for any incoming connections.
     * You can connect as many clients as you want to the server.
     * Every new connection / socket will get a unique ID.
     */
    class NAPAPI SocketServer final : public SocketAdapter
    {
        RTTI_ENABLE(SocketAdapter)
    public:
        /**
         * initialization
         * @param error contains error information
         * @return true on succes
         */
        virtual bool init(utility::ErrorState& errorState) override;

        /**
         * Called before deconstruction
         */
        virtual void onDestroy() override;

        /**
         * Send message to all connected sockets
         * @param message the message
         */
        void sendToAll(const std::string& message);

        /**
         * Send message to specific socket
         * @param id client id
         * @param message the message
         */
        void send(const std::string& id, const std::string& message);

        /**
         * Returns vector with all id's of connected clients
         * @return vector containing client ids
         */
        std::vector<std::string> getConnectedClientIDs() const;

        /**
         * Returns amount of connected clients
         * @return amount of connected clients
         */
        size_t getConnectedClientsCount() const;
    public:
        // properties
        int mPort 						= 13251;		///< Property: 'Port' the port the server socket binds to
        std::string mIPAddress			= "";	        ///< Property: 'IP Address' local ip address to bind to, if left empty will bind to any local address
        bool mEnableLog                 = false;        ///< Property: 'Enable Log' whether the server should log to the console
    public:
        // Signals
        /**
         * Packet received signal will be dispatched on the thread this SocketAdapter is registered to, see SocketThread
         * First argument is id, second is received message
         */
        Signal<const std::string&, const std::string&> messageReceived;

        /**
         * Socket connected signal, will be dispatched on the thread this SocketAdapter is registered to, see SocketThread
         * Argument is id of socket connected
         */
        Signal<const std::string&> socketConnected;

        /**
         * Socket disconnected signal, will be dispatched on the thread this SocketAdapter is registered to, see SocketThread
         * Argument is id of socket disconnected
         */
        Signal<const std::string&> socketDisconnected;
    protected:
        /**
         * The process function
         */
        void process() override;
    private:
        /**
         * Called when a new socket is connected
         * @param errorCode holds any error generated during connect
         */
        void handleAccept(const asio::error_code& errorCode);

        /**
         * Called when an error occurs in process(), closes socket with given id
         * @param id the id of the socket that generates the error
         * @param errorCode the errorcode
         * @return whether an error is handled, if errorCode is empty, will return false
         */
        bool handleError(const std::string& id, asio::error_code& errorCode);

        /**
         * Clears current message queue
         */
        void clearQueue();

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

        // ASIO
        std::unique_ptr<asio::ip::tcp::socket>                                  mWaitingSocket;
        std::unordered_map<std::string, std::unique_ptr<asio::ip::tcp::socket>> mSockets;
        std::unique_ptr<asio::ip::tcp::endpoint> 	                            mRemoteEndpoint;
        std::unique_ptr<asio::ip::tcp::acceptor>                                mAcceptor;

        // Threading
        std::unordered_map<std::string, moodycamel::ConcurrentQueue<std::string>> 	mMessageQueue;
        std::vector<std::string>                    mSocketsToRemove;
    };
}
