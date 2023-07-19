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

#include "controller_comms.h"
#include "tincan_exception.h"
namespace tincan
{

    ControllerCommsChannel::ControllerCommsChannel(
        const string &socket_name,
        unique_ptr<epoll_event> channel_ev,
        CommsServer &comms,
        CommsChannelMsgHandler &msg_handler)
        : comms_(comms),
          socket_name(socket_name),
          channel_ev(std::move(channel_ev)),
          msg_handler_(msg_handler),
          rsz(0)
    {
    }

    ControllerCommsChannel::~ControllerCommsChannel()
    {
        Close();
    }

    void ControllerCommsChannel::Send(unique_ptr<string> msg)
    {
        lock_guard<mutex> lg(sendq_mutex_);
        sendq_.push_back(std::move(msg));
        // msg_sz_ = msg.length();
        // szq_->push_back(msg.length());
        comms_.EnableEpollOut(*channel_ev.get());
    }

    void ControllerCommsChannel::Deliver(TincanControl &ctrl)
    {
        ctrl.SetRecipient("TincanTunnel");
        std::string msg = ctrl.StyledString();
        // RTC_LOG(LS_INFO) << "Sending CONTROL: " << msg;
        std::cout << "Sending CONTROL: " << msg;
        // Send(msg);
        Send(make_unique<string>(msg));
    }

    void
    ControllerCommsChannel::Deliver(
        unique_ptr<TincanControl> ctrl)
    {
        Deliver(*ctrl.get());
    }

    // void ControllerCommsChannel::UpdateMsgSz()
    // {
    //     msg_sz = 0;
    //     if (!sendq_.empty())
    //     {
    //         auto msg = sendq_.front();
    //         msg_sz_ = msg.length();
    //     }
    // }

    // int ControllerCommsChannel::GetMsgSize()
    // {
    //     lock_guard<mutex> lg(sendq_mutex_);
    //     // int sz = szq_->front();
    //     // szq_->pop_front();
    //     auto sz = msg_sz;
    //     msg_sz = 0;
    //     return sz;
    // }

    // unique_ptr<TincanControl> ControllerCommsChannel::GetMsg()
    // {
    //     lock_guard<mutex> lg(sendq_mutex_);
    //     if (!msg_)
    //     {
    //         auto msg = move(sendq_.front());
    //         sendq_.pop_front();
    //     }
    //     return msg;

    // }
    //     string data = msg.StyledString();
    //     send(channel_ev_->data.fd, data.length(), sizeof(size_t), 0);
    //     send(channel_ev_->data.fd, data.c_str(), data.length(), 0);
    //     if (sendq_.empty())
    //         msg_handler_->DisableEpollOut(channel_ev_)
    // }

    // epoll_event *ControllerCommsChannel::QueryEpollEvent()
    // {
    //     return channel_ev.get();
    // }

    int ControllerCommsChannel::QuerySocketFd()
    {
        if (channel_ev)
            return channel_ev->data.fd;
        return -1;
    }

    void ControllerCommsChannel::Close()
    {
        if (channel_ev && channel_ev->data.fd != -1)
        {
            shutdown(channel_ev->data.fd, SHUT_RDWR);
            close(channel_ev->data.fd);
            channel_ev->data.fd = -1;
            channel_ev.reset();
        }
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////

    ControllerComms::ControllerComms()
    {
        /* set up the epoll instance */
        epoll_fd_ = epoll_create(1);
        if (epoll_fd_ == -1)
        {
            throw TCEXCEPT("Error: Failed to create epoll instance");
        }

        // /* block all signals. we take signals synchronously via signalfd */
        // sigset_t all;
        // sigfillset(&all);
        // sigprocmask(SIG_SETMASK, &all, NULL);

        // /* a few signals we'll accept via our signalfd */
        // sigset_t sw;
        // sigemptyset(&sw);
        // for (n = 0; n < sizeof(sigs) / sizeof(*sigs); n++)
        //     sigaddset(&sw, sigs[n]);
                    
    }

    ControllerComms::~ControllerComms()
    {
        if (epoll_fd_ != -1)
            close(epoll_fd_);
        if (signal_fd_ != -1)
            close(signal_fd_);
        if (thread_ && thread_->joinable())
            thread_->join();
    }

    unique_ptr<epoll_event> ControllerComms::RegisterEpoll_(int events, int fd)
    {
        auto ev = make_unique<epoll_event>();
        memset(ev.get(), 0, sizeof(epoll_event));
        ev->events = events;
        ev->data.fd = fd;
        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, ev.get());
        if (rc == -1)
        {
            // throw TCEXCEPT("Error: epoll ctl add failed");
        }
        return ev;
    }

    void ControllerComms::RemoveEpoll_(int fd)
    {
        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        if (rc == -1)
        {
            // throw TCEXCEPT("Error: epoll ctl del failed");
        }
    }

    // int ControllerComms::HandleSignal_()
    // {
    //     struct signalfd_siginfo info;
    //     ssize_t nr;
    //     int rc = -1;

    //     nr = read(signal_fd_, &info, sizeof(info));
    //     if (nr != sizeof(info))
    //     {
    //         fprintf(stderr, "failed to read signal fd buffer\n");
    //         goto done;
    //     }

    //     switch (info.ssi_signo)
    //     {
    //     case SIGALRM:
    //         alarm(1);
    //         break;
    //     default:
    //         fprintf(stderr, "got signal %d\n", info.ssi_signo);
    //         goto done;
    //         break;
    //     }

    //     rc = 0;

    // done:
    //     return rc;
    // }

    void ControllerComms::HandleWrite_(int fd)
    {
        try
        {
            shared_ptr<ControllerCommsChannel> ch = comm_channels_.at(fd);
            if (!ch->wbuf)
            {

                if (ch->sendq_.empty())
                {
                    DisableEpollOut(*ch->channel_ev.get());
                    return;
                }
                ch->wbuf = std::move(ch->sendq_.front());
                ch->sendq_.pop_front();
                uint16_t msg_sz = ch->wbuf->size();
                ssize_t cnt = send(fd, &msg_sz, sizeof(msg_sz), 0);
                if (cnt == -1)
                {
                    std::cerr << "UDS msg_sz send failed" << endl;
                    return;
                }
                if ((size_t)cnt < sizeof(msg_sz))
                {
                    std::cerr << "UDS msg_sz Incomplete send" << endl;
                    return;
                }
                std::cout << "sent msg size " << endl;
            }
            else
            {
                ssize_t cnt = send(fd, ch->wbuf->c_str(), ch->wbuf->size() + 1, 0);
                if (cnt == -1)
                {
                    std::cerr << "UDS data send failed" << endl;
                    return;
                }
                if ((size_t)cnt < ch->wbuf->size() + 1)
                {
                    std::cerr << "UDS data Incomplete send" << endl;
                    return;
                }
                std::cout << "sent msg ... " << endl;

                ch->wbuf.release();
                if (ch->sendq_.empty())
                {
                    DisableEpollOut(*ch->channel_ev.get());
                }
            }
        }
        catch (const std::exception &e)
        {
            // RTC_LOG(LS_WARNING) << e.what() << '\n';
            std::cerr << e.what() << endl;
            RemoveEpoll_(fd);
            close(fd); // todo:
        }
    }

    void ControllerComms::HandleRead_(int fd)
    {
        int nr;
        try
        {
            auto ch = comm_channels_.at(fd);
            if (ch->rsz == 0)
            {
                nr = recv(fd, &ch->rsz, sizeof(ch->rsz), 0);
                if (nr < 0)
                {
                    throw TCEXCEPT("UDS read for msg size failed");
                }
            }
            else
            {
                ch->rbuf = make_unique<vector<char>>((size_t)ch->rsz, 0);
                nr = recv(fd, ch->rbuf->data(), ch->rsz, 0);
                if (nr < 0)
                {
                    ch->rsz = 0;
                    throw TCEXCEPT("UDS msg read failed");
                }
                else
                {
                    fprintf(stderr, "received %d bytes: %.*s\n",
                            ch->rsz,
                            (int)ch->rsz,
                            ch->rbuf->data());
                    ch->msg_handler_.OnMsgReceived(std::move(ch->rbuf));
                    ch->rsz = 0;
                }
            }
        }
        catch (const exception &e)
        {
            // RTC_LOG(LS_WARNING) << e.what();
            std::cerr << e.what();
            RemoveEpoll_(fd);
            close(fd); // todo:
        }
    }

    void ControllerComms::ServiceMain_()
    {
        struct epoll_event ev;

        // InitializeSignals();
        // alarm(1);
        while (true)
        {
            try
            {
                while (epoll_wait(epoll_fd_, &ev, 1, -1) > 0)
                {
                    std::cout << "Tincan epoll wait cmplt" << endl;
                    if (ev.events & EPOLLIN)
                    {
                        std::cout << "Tincan Handle read" << endl;
                        HandleRead_(ev.data.fd);
                    }
                    else if (ev.events & EPOLLOUT)
                    {
                        std::cout << "Tincan Handle write" << endl;
                        HandleWrite_(ev.data.fd);
                    }
                    else if (ev.events & EPOLLRDHUP)
                    {
                        std::cerr << "EPOLLRDHUP " << ev.data.fd << endl;
                        auto ch = comm_channels_.at(ev.data.fd);
                        DisableEpollIn(*ch->channel_ev.get());
                    }
                    else if (ev.events & EPOLLHUP)
                    {
                        std::cerr << "EPOLLHUP " << ev.data.fd << endl;
                        auto ch = comm_channels_.at(ev.data.fd);
                        ch->Close();
                        RemoveEpoll_(ev.data.fd);
                    }
                }
            }
            catch (const std::exception &e)
            {
                // RTC_LOG(LS_INFO) << e.what() << '\n';
                std::cerr << e.what() << endl;
                //todo: close all comm channels
            }
        }
    }
    void ControllerComms::Run()
    {
        thread_ = make_unique<thread>(&ControllerComms::ServiceMain_, this);
    }

    void ControllerComms::Shutdown() {}

    shared_ptr<CommsChannel> ControllerComms::ConnectToServer(const string &socket_name, CommsChannelMsgHandler &msg_handler)
    {
        int sock_fd = -1;
        shared_ptr<ControllerCommsChannel> ch;
        try
        {
            sock_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (sock_fd < 0)
            {
                // fprintf(stderr, "socket: %s", strerror(errno));
                throw TCEXCEPT("Error: Failed to create Unix Domain Socket");
            }

            /* abstract socket namespace has leading null byte then string */
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            *addr.sun_path = '\0';
            strncpy(addr.sun_path + 1, socket_name.c_str(), sizeof(addr.sun_path) - 1);
            /* the length when connecting to an abstract socket is the
             * initial sa_family_t (2 bytes) plus leading null byte + name
             * (no trailing null in the name, for an autobind socket) */
            socklen_t slen = sizeof(sa_family_t) + 1 + socket_name.length();
            int sc = connect(sock_fd, (struct sockaddr *)&addr, slen);
            if (sc < 0)
            {
                // fprintf(stderr, "connect: %s\n", strerror(errno));
                throw TCEXCEPT("Error: Failed to connect Unix Domain Socket");
            }
            auto ev = RegisterEpoll_(EPOLLIN, sock_fd);
            ch = make_shared<ControllerCommsChannel>(socket_name, std::move(ev), *this, msg_handler);
            comm_channels_[sock_fd] = ch;
        }
        catch (const std::exception &e)
        {
            // RTC_LOG(LS_INFO) << e.what() << '\n';
            std::cerr << e.what() << endl;
            if (sock_fd > 0)
                close(sock_fd);
            ch.reset();
        }
        return ch;
    }

    void ControllerComms::CloseChannel(shared_ptr<CommsChannel> channel)
    {
        ControllerCommsChannel *ch = static_cast<ControllerCommsChannel *>(channel.get());
        comm_channels_.erase(ch->QuerySocketFd());
        ch->Close();
    }

    void ControllerComms::EnableEpollOut(epoll_event &channel_ev)
    {
        if (!(channel_ev.events & EPOLLOUT))
        {
            channel_ev.events |= EPOLLOUT;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void ControllerComms::DisableEpollOut(epoll_event &channel_ev)
    {
        if (channel_ev.events & EPOLLOUT)
        {
            channel_ev.events &= ~EPOLLOUT;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void ControllerComms::EnableEpollIn(epoll_event &channel_ev)
    {
        if (!(channel_ev.events & EPOLLIN))
        {
            channel_ev.events |= EPOLLIN;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void ControllerComms::DisableEpollIn(epoll_event &channel_ev)
    {
        if (channel_ev.events & EPOLLIN)
        {
            channel_ev.events &= ~EPOLLIN;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }
} // namespace tincan