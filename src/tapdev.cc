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

#include "tapdev.h"
#include "tincan_exception.h"
#include <sys/types.h>
#include <sys/socket.h>

namespace tincan
{

    extern TincanParameters tp;
    static const char *const TUN_PATH = "/dev/net/tun";

    TapDev::TapDev() : fd_(-1), is_down_(true), epfd_(-1)
    {
        memset(&ifr_, 0x0, sizeof(ifr_));
        memset(&mac_, 0x0, sizeof(mac_));
    }

    TapDev::~TapDev()
    {
        Down();
        Close();
    }

    void TapDev::Open(
        const TapDescriptor &tap_desc)
    {
        string emsg("The Tap device open operation failed - ");
        if ((fd_ = open(TUN_PATH, O_RDWR)) < 0)
            throw TCEXCEPT(emsg.c_str());
        ifr_.ifr_flags = IFF_TAP | IFF_NO_PI;
        if (tap_desc.name.length() >= IFNAMSIZ)
        {
            emsg.append("the name length is longer than maximum allowed.");
            throw TCEXCEPT(emsg.c_str());
        }
        strncpy(ifr_.ifr_name, tap_desc.name.c_str(), tap_desc.name.length());
        ifr_.ifr_name[tap_desc.name.length()] = 0;
        // create the device
        if (ioctl(fd_, TUNSETIFF, (void *)&ifr_) < 0)
        {
            emsg.append("the device could not be created.");
            throw TCEXCEPT(emsg.c_str());
        }
        int cfg_skt;
        if ((cfg_skt = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            emsg.append("a socket bind failed.");
            throw TCEXCEPT(emsg.c_str());
        }

        if ((ioctl(cfg_skt, SIOCGIFHWADDR, &ifr_)) < 0)
        {
            emsg.append("retrieving the device mac address failed");
            close(cfg_skt);
            throw TCEXCEPT(emsg.c_str());
        }
        memcpy(mac_.data(), ifr_.ifr_hwaddr.sa_data, 6);

        if (ioctl(cfg_skt, SIOCGIFFLAGS, &ifr_) < 0)
        {
            close(cfg_skt);
            throw TCEXCEPT(emsg.c_str());
        }

        close(cfg_skt);
    }

    void TapDev::SetFlags_(
        short enable,
        short disable)
    {
        int cfg_skt;
        string emsg("The TAP device set flags operation failed");
        if ((cfg_skt = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            emsg.append("a socket bind failed.");
            // throw TCEXCEPT(emsg.c_str());
            RTC_LOG(LS_ERROR) << emsg;
        }
        ifr_.ifr_flags |= enable;
        ifr_.ifr_flags &= ~disable;
        // write back the modified flag states
        if (ioctl(cfg_skt, SIOCSIFFLAGS, &ifr_) < 0)
        {
            close(cfg_skt);
            // throw TCEXCEPT(emsg.c_str());
            RTC_LOG(LS_ERROR) << emsg;
        }
        close(cfg_skt);
    }

    uint16_t TapDev::TapDev::Mtu()
    {
        return ifr_.ifr_mtu;
    }

    MacAddressType TapDev::MacAddress()
    {
        return mac_;
    }

    void TapDev::Up()
    {
        if (is_down_)
        {
            SetFlags_(IFF_UP, 0);
            is_down_ = false;
        }
    }

    void TapDev::Down()
    {
        if (!is_down_)
        {
            SetFlags_(0, IFF_UP);
            is_down_ = true;
            RTC_LOG(LS_INFO) << ifr_.ifr_name << " is now DOWN";
        }
    }
    ///////////////////////////////////////////////////////////////////////////
    // EpollChannel interface
    void TapDev::QueueWrite(unique_ptr<iob_t> msg)
    {
        if (is_down_)
            return;
        {
            lock_guard<mutex> lg(sendq_mutex_);
            sendq_.push_back(*msg.release());
        }
        if (!(channel_ev->events & EPOLLOUT))
        {
            channel_ev->events |= EPOLLOUT;
            epoll_ctl(epfd_, EPOLL_CTL_MOD,
                      channel_ev->data.fd, channel_ev.get());
        }
    }

    void TapDev::WriteNext()
    {
        lock_guard<mutex> lg(sendq_mutex_);
        if (sendq_.empty())
        {
            // disable EPOLLOUT
            if (channel_ev->events & EPOLLOUT)
            {
                channel_ev->events &= ~EPOLLOUT;
                epoll_ctl(epfd_, EPOLL_CTL_MOD, channel_ev->data.fd, channel_ev.get());
            }
        }
        else
        {
            ssize_t nw = 0;
            auto wiob = sendq_.front();
            sendq_.pop_front();
            nw = write(fd_, wiob.data(), wiob.size());
            if (nw < 0)
            {
                throw TCEXCEPT("TAP write failed");
            }
        }
    }

    bool TapDev::CanWriteMore()
    {
        lock_guard<mutex> lg(sendq_mutex_);
        return !sendq_.empty();
    }

    void TapDev::ReadNext()
    {
        ssize_t nr = 0;
        unique_ptr<iob_t> riob = make_unique<iob_t>((size_t)tp.kTapBufferSize, 0);
        nr = read(fd_, riob->data(), riob->size());
        if (nr > 0)
        {
            riob->resize(nr);
            read_completion_(riob.release());
        }
        else if (nr < 0)
        {
            throw TCEXCEPT("TAP read failed");
        }
    }

    void
    TapDev::Close()
    {
        if (channel_ev && channel_ev->data.fd != -1)
        {
            close(channel_ev->data.fd);
            channel_ev->data.fd = -1;
            channel_ev.reset();
        }
    }
} // tincan
