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

#include "epoll_engine.h"
#include "tincan_exception.h"
namespace tincan
{
    EpollEngine::EpollEngine() : epoll_fd_(1), exit_flag_(false)
    {
        epoll_fd_ = epoll_create(1);
        if (epoll_fd_ == -1)
        {
            throw TCEXCEPT("Error: Failed to create epoll instance");
        }
    }

    EpollEngine::~EpollEngine()
    {
        Shutdown();
    }

    void
    EpollEngine::Register(
        shared_ptr<EpollChannel> ch,
        int events)
    {
        auto ev = make_unique<epoll_event>();
        memset(ev.get(), 0, sizeof(epoll_event));
        ev->events = events;
        ev->data.fd = ch->FileDesc();
        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ch->FileDesc(), ev.get());
        if (rc == -1)
        {
            throw TCEXCEPT("Error: epoll ctl add failed");
        }
        ch->SetChannelEvent(move(ev), epoll_fd_);
        comm_channels_[ch->FileDesc()] = ch;
    }

    void EpollEngine::Deregister(int fd)
    {
        if (fd == -1)
        {
            return;
        }
        int rc = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        if (rc == -1)
        {
            RTC_LOG(LS_WARNING) << "Error: epoll_ctl_del failed. epoll_fd:" << epoll_fd_ << " fd:" << fd;
        }
        comm_channels_.erase(fd);
    }

    void EpollEngine::HandleWrite_(int fd)
    {
        auto ch = comm_channels_.at(fd);
        ch->WriteNext();
    }
    void EpollEngine::HandleRead_(int fd)
    {
        auto ch = comm_channels_.at(fd);
        ch->ReadNext();
    }

    void EpollEngine::Epoll()
    {
        struct epoll_event ev;
        int num_fd = epoll_wait(epoll_fd_, &ev, 1, -1);
        if (exit_flag_)
            return;
        if (num_fd < 0)
            throw TCEXCEPT("Epoll wait failure");

        while (num_fd-- > 0)
        {
            if (ev.events & EPOLLIN)
            {
                HandleRead_(ev.data.fd);
            }
            else if (ev.events & EPOLLOUT)
            {
                HandleWrite_(ev.data.fd);
            }
            else if (ev.events & EPOLLRDHUP)
            {
                auto ch = comm_channels_.at(ev.data.fd);
                DisableEpollIn(ch->ChannelEvent());
            }
            else if (ev.events & EPOLLHUP)
            {
                auto ch = comm_channels_.at(ev.data.fd);
                ch->Close();
                Deregister(ev.data.fd);
            }
        }
    }

    void EpollEngine::Shutdown()
    {
        exit_flag_ = true;
        if (epoll_fd_ == -1)
            return;
        for (const auto &i : comm_channels_)
        {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, i.first, nullptr);
            i.second->Close();
        }
        comm_channels_.clear();
        close(epoll_fd_);
    }

    void EpollEngine::EnableEpollOut(epoll_event &channel_ev)
    {
        if (!(channel_ev.events & EPOLLOUT))
        {
            channel_ev.events |= EPOLLOUT;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void EpollEngine::DisableEpollOut(epoll_event &channel_ev)
    {
        if (channel_ev.events & EPOLLOUT)
        {
            channel_ev.events &= ~EPOLLOUT;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void EpollEngine::EnableEpollIn(epoll_event &channel_ev)
    {
        if (!(channel_ev.events & EPOLLIN))
        {
            channel_ev.events |= EPOLLIN;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

    void EpollEngine::DisableEpollIn(epoll_event &channel_ev)
    {
        if (channel_ev.events & EPOLLIN)
        {
            channel_ev.events &= ~EPOLLIN;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel_ev.data.fd, &channel_ev);
        }
    }

} // namespace tincan