/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

// External includes
#include <nap/device.h>
#include <queue>
#include <mutex>

// NAP includes
#include <utility/threading.h>
#include <concurrentqueue.h>
#include <nap/signalslot.h>
#include <nap/timer.h>

// Local includes
#include "socketadapter.h"
#include "socketpacket.h"

namespace nap
{
    /**
     * SocketClient creates a asio::tcp::socket and tries to connect to an endpoint.
     * Once connected it is able to send and receive data as std::strings
     * SocketClient extends on SocketAdapter, this means the process() function will be called by the SocketThread
     * assigned to the SocketAdapter.
     */
	class NAPAPI SocketClient final : public SocketAdapter
	{
		RTTI_ENABLE(SocketAdapter)
	public:
        /**
         * Send message to server
         * @param message the message
         */
		void send(const SocketPacket& message);

        /**
         * Connect to server
         */
        void connect();

        /**
         * Disconnect from server
         */
        void disconnect();

        /**
         * Returns whether socket is connected
         * @return socket connected
         */
        bool isConnected() const;

        /**
         * Returns whether socket is trying to be established
         * @return socket is connecting
         */
        bool isConnecting() const;

        void enableLog(bool enableLog);

		// slots
        void addMessageReceivedSlot(Slot<const SocketPacket&>& slot);
        void removeMessageReceivedSlot(Slot<const SocketPacket&>& slot);
        void addConnectedSlot(Slot<>& slot);
        void removeConnectedSlot(Slot<>& slot);
        void addDisconnectedSlot(Slot<>& slot);
        void removeDisconnectedSlot(Slot<>& slot);
        void addPostProcessSlot(Slot<>& slot);
        void removePostProcessSlot(Slot<>& slot);

		// properties
		int mPort 							= 13251; 		///< Property: 'Port' the port the client socket binds to
		std::string mRemoteIp 				= "10.8.0.3";	///< Property: 'Endpoint' the ip address the client socket binds to
		bool mConnectOnInit                 = true;         ///< Property: 'Connect on init' whether the client should try to connect after successful initialization
        bool mEnableAutoReconnect           = true;         ///< Property: 'Reconnect On Disconnect' whether the client should try to reconnect after an error or dissconnect
        int  mAutoReconnectIntervalMillis   = 5000;         ///< Property: 'Reconnect Interval' the time interval at which the client should try to reconnect in milliseconds
        bool mEnableLog                     = false;        ///< Property: 'Enable Log' whether the client should log to the console
	    int  mConnectTimeOutMillis          = 5000;
        int  mReadTimeOutMillis             = 200;
        int  mWriteTimeOutMillis            = 200;

    protected:
		/**
		 * Starts the Socket client and creates the socket
		 * @param errorState contains error information
		 * @return true on success
		 */
		bool onStart(utility::ErrorState& errorState) override final;

		/**
		 * Called when socket needs to be closed
		 */
		void onStop() override final;

		/**
		 * The process function
		 */
		void onProcess() override final;

    private:
        // Signals
        Signal<> postProcessSignal;

        /**
         * Message received signal, dispatched on thread assigned to this SocketAdapter
         */
        Signal<const SocketPacket&> dataReceived;

        /**
         * Connected signal, dispatched on thread assigned to this SocketAdapter
         */
        Signal<> connected;

        /**
         * Disconnected signal, dispatched on thread assigned to this SocketAdapter
         */
        Signal<> disconnected;

        /**
         * Handle connect callback
         * @param errorCode any potential errorcode
         */
        void handleConnect(const asio::error_code& errorCode);

        /**
         * Called when an error occurs in process(), closes sockets and initializes reconnect timer if required
         * @param errorCode the errorcode
         * @return whether an error is handled, if errorCode is empty, will return false
         */
        bool handleError(const asio::error_code& errorCode);

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

		// Threading
		moodycamel::ConcurrentQueue<SocketPacket> mQueue;
        std::atomic_bool mSocketReady = { false };
        std::atomic_bool mConnecting = { false };

        // Timers
        SteadyTimer mReconnectTimer;
        SteadyTimer mTimeoutTimer;
        SteadyTimer mWriteResponseTimer;
        SteadyTimer mReadResponseTimer;

        //
        bool mWritingData = false;
        bool mReceivingData = false;

		SocketPacket mWriteBuffer;

        moodycamel::ConcurrentQueue<std::function<void()>> mActionQueue;

		// Client specific ASIO implementation
		class Impl;
		std::unique_ptr<Impl> mImpl;
	};
}
