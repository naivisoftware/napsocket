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
         * called on destruction
         */
        virtual void onDestroy() override;

        void send(const std::string& message);
    public:
        // properties
        int mPort 						= 13251;		///< Property: 'Port' the port the server socket binds to
        std::string mIPAddress			= "";	        ///< Property: 'IP Address' local ip address to bind to, if left empty will bind to any local address
        bool mEnableLog                 = false;
    public:
        /**
         * packet received signal will be dispatched on the thread this UDPServer is registered to, see UDPThread
         */
        Signal<const std::string&> messageReceived;
    protected:
        /**
         * The process function
         */
        void process() override;
    private:
        void handleAccept(asio::ip::tcp::socket& socket, const asio::error_code& errorCode);

        bool handleError(asio::ip::tcp::socket& socket, const asio::error_code& errorCode);

        void logError(const std::string& message);

        void logInfo(const std::string& message);

        void clearQueue();

        void createNewSocket();

        // ASIO
        std::vector<std::unique_ptr<asio::ip::tcp::socket>> 		    mSockets;
        std::unique_ptr<asio::ip::tcp::endpoint> 	                    mRemoteEndpoint;
        std::unique_ptr<asio::ip::tcp::acceptor>                        mAcceptor;

        // Threading
        moodycamel::ConcurrentQueue<std::string> 	mQueue;
        std::vector<asio::ip::tcp::socket*>         mSocketsToRemove;
    };
}
