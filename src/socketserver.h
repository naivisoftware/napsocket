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
        void handleAccept(const asio::error_code& errorCode);

        bool handleError(const asio::error_code& errorCode);

        // ASIO
        std::unique_ptr<asio::ip::tcp::socket> 		    mSocket;
        std::unique_ptr<asio::ip::tcp::endpoint> 	    mRemoteEndpoint;
        std::unique_ptr<asio::ip::tcp::acceptor>        mAcceptor;

        // Threading
        moodycamel::ConcurrentQueue<std::string> 	mQueue;
        std::atomic_bool mSocketReady = { false };
    };
}
