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

#include "controller_comms.h"
#include "tincan_exception.h"
namespace tincan
{

    ControllerCommsChannel::ControllerCommsChannel(
        const string &socket_name,
        EpollChannelMsgHandler &msg_handler)
        : socket_name(socket_name),
          rcv_handler_(msg_handler),
          rsz_(0)
    {
    }

    ControllerCommsChannel::~ControllerCommsChannel()
    {
        Close();
    }

    void
    ControllerCommsChannel::ConnectToController()
    {
        shared_ptr<EpollChannel> ch;
        try
        {
            fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (fd_ < 0)
            {
                throw TCEXCEPT("Error: Failed to create Unix Domain Socket");
            }

            // must prepend null byte to server name
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            *addr.sun_path = '\0';
            strncpy(addr.sun_path + 1, socket_name.c_str(), sizeof(addr.sun_path) - 1);
            socklen_t slen = sizeof(sa_family_t) + 1 + socket_name.length();
            int sc = connect(fd_, (struct sockaddr *)&addr, slen);
            if (sc < 0)
            {
                throw TCEXCEPT("Error: Failed to connect Unix Domain Socket");
            }
        }
        catch (const std::exception &e)
        {
            RTC_LOG(LS_INFO) << e.what() << '\n';
            if (fd_ > 0)
                close(fd_);
            ch.reset();
        }
    }

    void ControllerCommsChannel::QueueWrite(const string msg)
    {
        lock_guard<mutex> lg(sendq_mutex_);
        if (!IsGood())
            return;
        sendq_.push_back(msg);
        if (!(channel_ev->events & EPOLLOUT))
        {
            channel_ev->events |= EPOLLOUT;
            epoll_ctl(epfd_, EPOLL_CTL_MOD,
                      channel_ev->data.fd, channel_ev.get());
        }
    }

    void ControllerCommsChannel::WriteNext()
    {
        ssize_t nw = 0;
        if (!wbuf_)
        {
            lock_guard<mutex> lg(sendq_mutex_);
            if (sendq_.empty())
            {
                
                channel_ev->events &= ~EPOLLOUT;
                epoll_ctl(epfd_, EPOLL_CTL_MOD, channel_ev->data.fd, channel_ev.get());
                return;
            }
            wbuf_ = make_unique<string>(sendq_.front());
            sendq_.pop_front();
            uint16_t msg_sz = wbuf_->size();
            nw = send(fd_, &msg_sz, sizeof(msg_sz), 0);
        }
        else
        {
            nw = send(fd_, wbuf_->c_str(), wbuf_->size(), 0);
            wbuf_.reset();
        }
        if (nw < 0)
        {
            RTC_LOG(LS_ERROR) <<"Failed to send data to controller - " << strerror(errno);
        }
    }

    void ControllerCommsChannel::ReadNext()
    {
        ssize_t nr = 0;
        if (rsz_ == 0)
        {
            nr = recv(fd_, &rsz_, sizeof(rsz_), 0);
        }
        else
        {
            rbuf_ = make_unique<vector<char>>((size_t)rsz_ + 1, 0);
            nr = recv(fd_, rbuf_->data(), rsz_, 0);
            if (nr > 0)
            {
                rcv_handler_(std::move(rbuf_));
                rsz_ = 0;
            }
        }
        if (nr < 0)
        {
            RTC_LOG(LS_ERROR) <<"Failed to receive data from controller";
        }
    }

    void ControllerCommsChannel::Deliver(TincanControl &ctrl)
    {
        ctrl.SetRecipient("TincanTunnel");
        ctrl.SetSessionId(getpid());
        auto ctl_str = ctrl.StyledString();
        // RTC_LOG(LS_INFO) << "Sending CONTROL: " << ctl_str;
        QueueWrite(ctl_str);
    }

    void
    ControllerCommsChannel::Deliver(
        unique_ptr<TincanControl> ctrl)
    {
        Deliver(*ctrl.get());
    }

    void
    ControllerCommsChannel::Close()
    {
        if (channel_ev && channel_ev->data.fd != -1)
        {
            shutdown(channel_ev->data.fd, SHUT_RDWR);
            close(channel_ev->data.fd);
            channel_ev->data.fd = fd_ = -1;
        }
    }
} // namespace tincan