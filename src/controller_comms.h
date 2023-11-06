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
#include "tincan_base.h"
#include "epoll_engine.h"
#include "tincan_control.h"
#include "rtc_base/logging.h"
namespace tincan
{
    class ControllerCommsChannel : virtual public EpollChannel
    {

    public:
        ControllerCommsChannel(
            const string &socket_name,
            EpollChannelMsgHandler &msg_handler);
        ~ControllerCommsChannel();
        void QueueWrite(const string msg);
        virtual void WriteNext() override;
        virtual void ReadNext() override;
        virtual bool CanWriteMore() override;
        virtual epoll_event &ChannelEvent() override { return *channel_ev.get(); }
        virtual void SetChannelEvent(unique_ptr<epoll_event> ev, int epoll_fd) override
        {
            channel_ev = std::move(ev);
            epfd_ = epoll_fd;
        }
        virtual int FileDesc() override { return fd_; }
        virtual bool IsGood() override {return FileDesc() != -1;}
        virtual void Close() override;

        void ConnectToController();
        void Deliver(
            TincanControl &ctrl_resp);
        void Deliver(
            unique_ptr<TincanControl> ctrl_resp);

    private:
        const string &socket_name;
        unique_ptr<epoll_event> channel_ev;
        EpollChannelMsgHandler &rcv_handler_;
        mutex sendq_mutex_;
        deque<string> sendq_;
        unique_ptr<vector<char>> rbuf_;
        uint16_t rsz_;
        int fd_;
        unique_ptr<string> wbuf_;
        int epfd_;
    };

} // namespace tincan
#endif // TINCAN_CONTROL_LISTENER_H_
