/*
 * EdgeVPNio
 * Copyright 2020, University of Florida
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef TINCAN_CONTROLLER_COMMS_H_
#define TINCAN_CONTROLLER_COMMS_H_
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <queue>
#include <thread>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <tincan_base.h>
#include "tincan_control.h"
#include "rtc_base/logging.h"

namespace tincan
{
    class CommsChannelMsgHandler
    {
    public:
        virtual ~CommsChannelMsgHandler() = default;
        virtual void OnMsgReceived(
            unique_ptr<vector<char>> msg) = 0;
    };

    class CommsChannel
    {
    public:
        virtual ~CommsChannel() = default;
        virtual void Send(unique_ptr<string> msg) = 0;
        virtual void Deliver(
            TincanControl &ctrl_resp) = 0;

        virtual void Deliver(
            unique_ptr<TincanControl> ctrl_resp) = 0;
        virtual void Close() = 0;
    };

    class CommsServer
    {
    public:
        virtual ~CommsServer() = default;
        virtual void EnableEpollOut(epoll_event &channel_ev) = 0;
        virtual void DisableEpollOut(epoll_event &channel_ev) = 0;
        virtual void EnableEpollIn(epoll_event &channel_ev) = 0;
        virtual void DisableEpollIn(epoll_event &channel_ev) = 0;
    };

    class ControllerCommsChannel : public CommsChannel
    {
    private:
        CommsServer &comms_;
    public:
        const string &socket_name;
        unique_ptr<epoll_event> channel_ev;
        CommsChannelMsgHandler &msg_handler_;
        deque<unique_ptr<string>> sendq_;
        mutex sendq_mutex_;
        unique_ptr<string> wbuf;
        uint16_t rsz;
        unique_ptr<vector<char>> rbuf;

        ControllerCommsChannel(
            const string &socket_name,
            unique_ptr<epoll_event> channel_ev,
            CommsServer &comms,
            CommsChannelMsgHandler &msg_handler);
        ~ControllerCommsChannel();
        void Send(unique_ptr<string> msg);
        void Deliver(
            TincanControl &ctrl_resp);

        void Deliver(
            unique_ptr<TincanControl> ctrl_resp);
        int QuerySocketFd();
        void Close();
    };

    class ControllerComms : public CommsServer
    {
    private:
        int epoll_fd_;
        int signal_fd_;
        // int sigs[] = {SIGHUP, SIGTERM, SIGINT, SIGQUIT, SIGALRM};
        unique_ptr<thread> thread_;
        map<int, shared_ptr<ControllerCommsChannel>> comm_channels_;
        unique_ptr<epoll_event> RegisterEpoll_(int events, int file_desc);
        void RemoveEpoll_(int fd);
        int HandleSignal_();
        void HandleRead_(int fd);
        void HandleWrite_(int fd);
        void ServiceMain_();
        void EnableEpollOut(epoll_event &channel_ev);
        void DisableEpollOut(epoll_event &channel_ev);
        void EnableEpollIn(epoll_event &channel_ev);
        void DisableEpollIn(epoll_event &channel_ev);

    public:
        ControllerComms();
        ~ControllerComms();
        void Run();
        void Shutdown();
        // Client communications interface
        shared_ptr<CommsChannel> ConnectToServer(
            const string &socket_name,
            CommsChannelMsgHandler &msg_handler);

        void CloseChannel(
            shared_ptr<CommsChannel> channel);
    };
} // namespace tincan
#endif // TINCAN_CONTROL_LISTENER_H_
