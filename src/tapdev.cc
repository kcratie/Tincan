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
    static const char *const TUN_PATH = "/dev/net/tun";
    extern BufferPool<Iob> bp;

    TapDev::TapDev() : fd_(-1), is_down_(true), epfd_(-1)
    {
        memset(&ifr_, 0x0, sizeof(ifr_));
        memset(&mac_, 0x0, sizeof(mac_));
    }

    TapDev::~TapDev()
    {
        Close();
    }

    int TapDev::Open(
        const TapDescriptor &tap_desc)
    {
        string emsg("The Tap device open operation failed - ");
        if ((fd_ = open(TUN_PATH, O_RDWR)) < 0)
        {
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
            return -1;
        }
        ifr_.ifr_flags = IFF_TAP | IFF_NO_PI;
        size_t len = std::min(tap_desc.name.length(), (size_t)IFNAMSIZ);
        strncpy(ifr_.ifr_name, tap_desc.name.c_str(), len);
        ifr_.ifr_name[tap_desc.name.length()] = 0;
        // create the device
        if (ioctl(fd_, TUNSETIFF, (void *)&ifr_) < 0)
        {
            emsg.append("the device could not be created.");
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
            return -1;
        }
        int cfg_skt;
        if ((cfg_skt = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            emsg.append("a socket bind failed.");
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
            return -1;
        }
        if ((ioctl(cfg_skt, SIOCGIFHWADDR, &ifr_)) < 0)
        {
            emsg.append("retrieving the device mac address failed");
            close(cfg_skt);
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
            return -1;
        }
        memcpy(mac_.data(), ifr_.ifr_hwaddr.sa_data, 6);
        if (ioctl(cfg_skt, SIOCGIFFLAGS, &ifr_) < 0)
        {
            close(cfg_skt);
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
            return -1;
        }
        close(cfg_skt);
        return 0;
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
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
        }
        ifr_.ifr_flags |= enable;
        ifr_.ifr_flags &= ~disable;
        // write back the modified flag states
        if (ioctl(cfg_skt, SIOCSIFFLAGS, &ifr_) < 0)
        {
            close(cfg_skt);
            RTC_LOG(LS_ERROR) << emsg << " - " << strerror(errno);
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

    void TapDev::WriteDirect(
        const char *data,
        size_t data_len)
    {
        int remain = data_len;
        while (remain > 0)
        {
            auto nw = write(fd_, data, remain);
            if (nw >= 0)
            {
                remain = -nw;
            }
            else
            {
                RTC_LOG(LS_WARNING) << "TAP write failed. "
                                    << "data len: " << data_len << " "
                                    << " written: " << data_len - remain << ". " << strerror(errno);
                return;
            }
        }
    }
    ///////////////////////////////////////////////////////////////////////////
    // EpollChannel interface
    void TapDev::QueueWrite(Iob &&msg)
    {
        lock_guard<mutex> lg(sendq_mutex_);
        if (is_down_ || !IsGood())
        {
            return;
        }
        sendq_.push_back(std::move(msg));
        if ((channel_ev->events & EPOLLOUT) == 0)
        {
            channel_ev->events |= EPOLLOUT;
            epoll_ctl(epfd_, EPOLL_CTL_MOD,
                      channel_ev->data.fd, channel_ev.get());
        }
    }

    void TapDev::WriteNext()
    {
        lock_guard<mutex> lg(sendq_mutex_);
        while (!sendq_.empty())
        {
            ssize_t nw = 0;
            Iob &wiob = sendq_.front();
            nw = write(fd_, wiob.data(), wiob.size());
            if (nw < 0)
            {
                RTC_LOG(LS_WARNING) << "TAP write failed. "
                                    << "iob sz:" << wiob.size() << " " << strerror(errno);
            }
            if ((size_t)nw == wiob.size())
            {
                sendq_.pop_front();
            }
            else if ((size_t)nw < wiob.size())
            {
                // less than requested was written
                // resize and leave on sendq
                wiob.size(wiob.size() - nw);
            }
        }
        channel_ev->events &= ~EPOLLOUT;
        epoll_ctl(epfd_, EPOLL_CTL_MOD, channel_ev->data.fd, channel_ev.get());
    }

    void TapDev::ReadNext()
    {
        ssize_t nr = 0;
        Iob riob = bp.get();
        nr = read(fd_, riob.buf(), riob.capacity());
        if (nr > 0)
        {
            riob.size(nr);
            read_completion(std::move(riob));
        }
        else if (nr < 0)
        {
            RTC_LOG(LS_WARNING) << "TAP read failed";
        }
    }

    void
    TapDev::Close()
    {
        Down();
        if (fd_ != -1)
        {
            close(fd_);
            fd_ = -1;
        }
        if (channel_ev)
        {
            channel_ev->data.fd = -1;
        }
    }
} // tincan
