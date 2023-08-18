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
#ifndef TINCAN_TAPDEV_LNX_H_
#define TINCAN_TAPDEV_LNX_H_

#include "tincan_base.h"
#include "epoll_engine.h"

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
            string ip4,
            uint32_t prefix4,
            uint32_t mtu4)
            : name{name}, ip4{ip4}, prefix4{prefix4}, mtu4{mtu4}, prefix6{0}, mtu6{0} {}
        const string name;
        string ip4;
        uint32_t prefix4;
        uint32_t mtu4;
        string ip6;
        uint32_t prefix6;
        uint32_t mtu6;
    };

    class TapDevLnx : public EpollChannel
    {
    public:
        TapDevLnx();
        TapDevLnx(TapDevLnx &&) = delete;
        ~TapDevLnx() override;
        TapDevLnx &operator=(TapDevLnx &&) = delete;
        sigslot::signal1<iob_t *> read_completion_;
        void Open(
            const TapDescriptor &tap_desc);
        uint16_t Mtu();
        void Up();
        void Down();
        MacAddressType MacAddress();

        //////////////////////////////////////////////////
        void QueueWrite(unique_ptr<iob_t> msg);
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
        virtual void Close() override;
    private:
        void SetFlags_(short a, short b);
        /////////////////////////////////////////////////////////////////////////////
        int fd_;
        unique_ptr<epoll_event> channel_ev;
        unique_ptr<iob_t> wbuf_;
        mutex sendq_mutex_;
        deque<iob_t> sendq_;
        int epfd_;
        struct ifreq ifr_;
        MacAddressType mac_;
    };
}
#endif
