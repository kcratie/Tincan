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

#ifndef TINCAN_TAPDEV_LNX_H_
#define TINCAN_TAPDEV_LNX_H_

#include "tincan_base.h"
#include "epoll_engine.h"
#include "buffer_pool.h"

#include "rtc_base/logging.h"
#include "rtc_base/third_party/sigslot/sigslot.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <net/if.h>
#include <linux/if_tun.h>

namespace tincan
{
    struct TapDescriptor
    {
        TapDescriptor(
            string name,
            uint32_t mtu)
            : name{name}, mtu{mtu} {}
        const string name;
        uint32_t mtu;
    };

    class TapDev : public EpollChannel
    {
    public:
        TapDev();
        TapDev(const TapDev &) = delete;
        TapDev(TapDev &&) = delete;
        ~TapDev() override;
        TapDev &operator=(const TapDev &) = delete;
        TapDev &operator=(TapDev &&) = delete;
        int Open(
            const TapDescriptor &tap_desc);
        uint16_t Mtu();
        void Up();
        void Down();
        MacAddressType MacAddress();
        std::function<void(Iob&&)>read_completion;

        //////////////////////////////////////////////////
        void QueueWrite(Iob&& msg);
        virtual void WriteNext() override;
        virtual void ReadNext() override;
        virtual epoll_event &ChannelEvent() override { return *channel_ev.get(); }
        virtual void SetChannelEvent(unique_ptr<epoll_event> ev, int epoll_fd) override
        {
            channel_ev = std::move(ev);
            epfd_ = epoll_fd;
        }
        virtual int FileDesc() override { return fd_; }
        virtual bool IsGood() override { return FileDesc() != -1; }
        virtual void Close() override;

    private:
        void SetFlags_(short a, short b);
        /////////////////////////////////////////////////////////////////////////////
        int fd_;
        bool is_down_;
        unique_ptr<epoll_event> channel_ev;
        mutex sendq_mutex_;
        deque<Iob> sendq_;
        int epfd_;
        struct ifreq ifr_;
        MacAddressType mac_;
    };
}
#endif
