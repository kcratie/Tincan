/*
 * EdgeVPNio
 * Copyright 2023, University of Florida
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
#ifndef TINCAN_EPOLL_ENGINE_H_
#define TINCAN_EPOLL_ENGINE_H_
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

#include <tincan_base.h>
#include "tincan_control.h"
#include "rtc_base/logging.h"

namespace tincan
{
    class EpollChannelMsgHandler
    {
    public:
        virtual ~EpollChannelMsgHandler() = default;
        virtual void operator()(
            unique_ptr<vector<char>> msg) = 0;
    };

    class EpollChannel
    {
    public:
        virtual ~EpollChannel() = default;
        virtual void WriteNext() = 0;
        virtual void ReadNext() = 0;
        virtual bool CanWriteMore() = 0;
        virtual epoll_event &ChannelEvent() = 0;
        virtual void SetChannelEvent(unique_ptr<epoll_event> ev, int event_fd) = 0;
        virtual int FileDesc() = 0;
        virtual bool IsGood() = 0;
        virtual void Close() = 0;
    };
    class EpollEngBase
    {
    public:
        virtual ~EpollEngBase() = default;
        virtual void EnableEpollOut(epoll_event &channel_ev) = 0;
        virtual void DisableEpollOut(epoll_event &channel_ev) = 0;
        virtual void EnableEpollIn(epoll_event &channel_ev) = 0;
        virtual void DisableEpollIn(epoll_event &channel_ev) = 0;
        virtual void Register(shared_ptr<EpollChannel>, int events) = 0;
        virtual void Deregister(int fd) = 0;
    };

    class EpollEngine : virtual public EpollEngBase
    {
    private:
        static const std::array<int, 5> sig_codes;
        int epoll_fd_;
        bool exit_flag_;
        unordered_map<int, shared_ptr<EpollChannel>> comm_channels_;
        bool HandleSignal_();
        void HandleRead_(int fd);
        void HandleWrite_(int fd);
        void SetupSignalHandler_();
        void CheckChannelQueues_();

    public:
        EpollEngine();
        ~EpollEngine();
        void Register(shared_ptr<EpollChannel>, int events);
        void Deregister(int fd);
        void Epoll();
        void Shutdown();
        void EnableEpollOut(epoll_event &channel_ev);
        void DisableEpollOut(epoll_event &channel_ev);
        void EnableEpollIn(epoll_event &channel_ev);
        void DisableEpollIn(epoll_event &channel_ev);
    };
} // namespace tincan
#endif // TINCAN_EPOLL_ENGINE_H_
