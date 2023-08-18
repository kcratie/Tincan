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

#include "tapdev.h"
#include "tincan_exception.h"
#include <sys/types.h>
#include <sys/socket.h>

namespace tincan
{

    extern TincanParameters tp;
    static const char *const TUN_PATH = "/dev/net/tun";

    TapDevLnx::TapDevLnx() : fd_(-1), epfd_(-1)
    {
        memset(&ifr_, 0x0, sizeof(ifr_));
        memset(&mac_, 0x0, sizeof(mac_));
    }

    TapDevLnx::~TapDevLnx()
    {
        Close();
    }

    void TapDevLnx::Open(
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

    /**
     * Given some flags to enable and disable, reads the current flags for the
     * network device, and then ensures the high bits in enable are also high in
     * ifr_flags, and the high bits in disable are low in ifr_flags. The results are
     * then written back. For a list of valid flags, read the "SIOCGIFFLAGS,
     * SIOCSIFFLAGS" section of the 'man netdevice' page. You can pass `(short)0` if
     * you don't want to enable or disable any flags.
     */

    void TapDevLnx::SetFlags_(
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

        // set or unset the right flags
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

    uint16_t TapDevLnx::TapDevLnx::Mtu()
    {
        return ifr_.ifr_mtu;
    }

    MacAddressType TapDevLnx::MacAddress()
    {
        return mac_;
    }

    void TapDevLnx::Up()
    {
        SetFlags_(IFF_UP, 0);
    }

    void TapDevLnx::Down()
    {
        SetFlags_(0, IFF_UP);
        RTC_LOG(LS_INFO) << "TAP device state set to DOWN";
    }
    ///////////////////////////////////////////////////////////////////////////
    // EpollChannel interface
    void TapDevLnx::QueueWrite(unique_ptr<iob_t> msg)
    {
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

    void TapDevLnx::WriteNext()
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
            // RTC_LOG(LS_INFO) << "TAP Channel write size: " << nw << "/" << wiob.size();
            if (nw < 0)
            {
                // std::cerr << "Tap Channel data send failed. ERRNO: " << errno << endl;
                RTC_LOG(LS_WARNING) << "Tap Channel data send failed. ERRNO: " << errno;
                // Close(); Todo: handle failure
            }
        }
    }

    bool TapDevLnx::CanWriteMore()
    {
        lock_guard<mutex> lg(sendq_mutex_);
        return !sendq_.empty();
    }

    void TapDevLnx::ReadNext()
    {
        ssize_t nr = 0;
        unique_ptr<iob_t> riob = make_unique<iob_t>((size_t)tp.kTapBufferSize, 0);
        nr = read(fd_, riob->data(), riob->size());
        // RTC_LOG(LS_INFO) << "TAP Channel read size: " << nr;
        if (nr > 0)
        {
            riob->resize(nr);
            read_completion_(riob.release());
        }
        else if (nr < 0)
        {
            // std::cerr << "TAP Channel data recv failed. ERRNO: " << errno << endl;
            RTC_LOG(LS_WARNING) << "TAP Channel data recv failed. ERRNO: " << errno;
            // Close(); Todo: handle failure
        }
    }

    void
    TapDevLnx::Close()
    {
        if (channel_ev && channel_ev->data.fd != -1)
        {
            close(channel_ev->data.fd);
            channel_ev->data.fd = -1;
            channel_ev.reset();
        }
    }
} // tincan
