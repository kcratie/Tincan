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

#include "tincan.h"
#include "tincan_exception.h"
#include "turn_descriptor.h"
namespace tincan
{
    extern TincanParameters tp;
    Tincan::Tincan() : exit_flag_{false},
                       dispatch_map_{
                           {"ConfigureLogging", &Tincan::ConfigureLogging},
                           {"CreateLink", &Tincan::CreateLink},
                           {"CreateTunnel", &Tincan::CreateTunnel},
                           {"Echo", &Tincan::Echo},
                           {"QueryCandidateAddressSet", &Tincan::QueryCandidateAddressSet},
                           {"QueryLinkStats", &Tincan::QueryLinkStats},
                           {"QueryTunnelInfo", &Tincan::QueryTunnelInfo},
                           {"RemoveTunnel", &Tincan::RemoveTunnel},
                           {"RemoveLink", &Tincan::RemoveLink},
                       },
                       log_levels_{
                           {"NONE", rtc::LS_NONE},
                           {"ERROR", rtc::LS_ERROR},
                           {"WARNING", rtc::LS_WARNING},
                           {"INFO", rtc::LS_INFO},
                           {"VERBOSE", rtc::LS_INFO},
                           {"DEBUG", rtc::LS_INFO},
                       },
                       channel_{make_shared<ControllerCommsChannel>(tp.socket_name, *this)}
    {
        LogMessage::LogTimestamps();
        LogMessage::LogThreads();
        LogMessage::LogToDebug(LS_WARNING);
        LogMessage::SetLogToStderr(true);
        channel_->ConnectToController();
        // TD<decltype(tunnels_.at(""))> BasicTunnelType;
        // int x{0};
        // TD<decltype(x)> xType;
    }

    void Tincan::CreateTunnel(
        const Json::Value &tnl_desc,
        Json::Value &tnl_info)
    {
        shared_ptr<BasicTunnel> tnl = make_shared<SingleLinkTunnel>(
            make_unique<TunnelDesc>(tnl_desc),
            channel_,
            &threads_);
        unique_ptr<TapDescriptor> tap_desc = make_unique<TapDescriptor>(
            tnl_desc["TapName"].asString(),
            tnl_desc["IP4"].asString(),
            tnl_desc["IP4PrefixLen"].asUInt(),
            tnl_desc[TincanControl::MTU4].asUInt());
        Json::Value network_ignore_list =
            tnl_desc[TincanControl::IgnoredNetInterfaces];
        int count = network_ignore_list.size();
        vector<string> if_list(count);
        for (int i = 0; i < count; i++)
        {
            if_list[i] = network_ignore_list[i].asString();
        }
        tnl->Configure(move(tap_desc), if_list);
        tnl->Start();
        tnl->QueryInfo(tnl_info);
        lock_guard<mutex> lg(tunnels_mutex_);
        tunnels_.insert(make_pair(tnl->Descriptor().uid, tnl));
        epoll_eng_.Register(tnl->TapChannel(), EPOLLIN);
        return;
    }

    void
    Tincan::CreateVlink(
        const Json::Value &link_desc,
        const TincanControl &control)
    {
        unique_ptr<VlinkDescriptor> vl_desc = make_unique<VlinkDescriptor>();
        vl_desc->uid = link_desc[TincanControl::LinkId].asString();
        unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
        Json::Value &tnl_info = (*resp)[TincanControl::Message];
        string tnl_id = link_desc[TincanControl::TunnelId].asString();
        if (!IsTunnelExisit(tnl_id))
        {
            CreateTunnel(link_desc, tnl_info);
        }
        else
        {
            tunnels_.at(tnl_id)->QueryInfo(tnl_info);
        }
        unique_ptr<PeerDescriptor> peer_desc = make_unique<PeerDescriptor>();
        peer_desc->uid =
            link_desc[TincanControl::PeerInfo][TincanControl::UID].asString();
        peer_desc->vip4 =
            link_desc[TincanControl::PeerInfo][TincanControl::VIP4].asString();
        peer_desc->cas =
            link_desc[TincanControl::PeerInfo][TincanControl::CAS].asString();
        peer_desc->fingerprint =
            link_desc[TincanControl::PeerInfo][TincanControl::FPR].asString();
        peer_desc->mac_address =
            link_desc[TincanControl::PeerInfo][TincanControl::MAC].asString();

        shared_ptr<VirtualLink> vlink =
            tunnels_.at(tnl_id)->CreateVlink(move(vl_desc), move(peer_desc));
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>(control);
        if (!vlink->IsGatheringComplete())
        {
            ctrl->SetResponse(move(resp));
            std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
            inprogess_controls_[link_desc[TincanControl::LinkId].asString()] = move(ctrl);

            vlink->SignalLocalCasReady.connect(this, &Tincan::OnLocalCasUpdated);
        }
        else
        {
            (*resp)["Message"]["CAS"] = vlink->Candidates();
            (*resp)["Success"] = true;
            ctrl->SetResponse(move(resp));
            channel_->Deliver(move(ctrl));
        }
    }

    void
    Tincan::QueryLinkCas(
        const Json::Value &link_desc,
        Json::Value &cas_info)
    {
        const string tnl_id = link_desc[TincanControl::TunnelId].asString();
        const string vlid = link_desc[TincanControl::LinkId].asString();
        tunnels_.at(tnl_id)->QueryLinkCas(vlid, cas_info);
    }

    void
    Tincan::QueryLinkStats(
        const Json::Value &tunnel_ids,
        Json::Value &stat_info)
    {
        for (uint32_t i = 0; i < tunnel_ids["TunnelIds"].size(); i++)
        {
            string lnk_id;
            string tnl_id = tunnel_ids["TunnelIds"][i].asString();
            auto tnl = tunnels_.at(tnl_id);
            tnl->QueryLinkId(lnk_id);
            tnl->QueryLinkInfo(tnl_id, stat_info[tnl_id][lnk_id]);
        }
    }

    void
    Tincan::QueryTunnelInfo(
        const Json::Value &tnl_desc,
        Json::Value &tnl_info)
    {
        auto tnl_id = tnl_desc[TincanControl::TunnelId].asString();
        tunnels_.at(tnl_id)->QueryInfo(tnl_info);
    }

    void
    Tincan::RemoveTunnel(
        const Json::Value &tnl_desc)
    {
        const string tnl_id = tnl_desc[TincanControl::TunnelId].asString();
        if (tnl_id.empty())
            throw TCEXCEPT("No Tunnel ID was specified");

        {
            lock_guard<mutex> lg(tunnels_mutex_);
            auto tc = tunnels_.at(tnl_id)->TapChannel();
            epoll_eng_.Deregister(tc->FileDesc());
            tunnels_.erase(tnl_id);
        }
        RTC_LOG(LS_INFO) << "Tunnel " << tnl_id << " erased from collection ";
    }

    void
    Tincan::RemoveVlink(
        const Json::Value &link_desc)
    {
        const string tnl_id = link_desc[TincanControl::TunnelId].asString();
        const string vlid = link_desc[TincanControl::LinkId].asString();
        if (tnl_id.empty() || vlid.empty())
            throw TCEXCEPT("Required identifier not specified");

        lock_guard<mutex> lg(tunnels_mutex_);
        tunnels_.at(tnl_id)->RemoveLink(vlid);
    }

    void
    Tincan::OnLocalCasUpdated(
        string link_id,
        string lcas)
    {
        if (lcas.empty())
        {
            lcas = "No local candidates available on this vlink";
            RTC_LOG(LS_WARNING) << lcas;
        }
        bool to_deliver = false;
        unique_ptr<TincanControl> ctrl;
        {
            std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
            auto itr = inprogess_controls_.begin();
            for (; itr != inprogess_controls_.end(); itr++)
            {
                if (itr->first == link_id)
                {
                    to_deliver = true;
                    ctrl = move(itr->second);
                    Json::Value &resp = ctrl->GetResponse();
                    resp["Message"]["CAS"] = lcas;
                    resp["Success"] = true;
                    inprogess_controls_.erase(itr);
                    break;
                }
            }
        }
        if (to_deliver)
        {
            channel_->Deliver(move(ctrl));
        }
    }

    void
    Tincan::operator()(
        unique_ptr<vector<char>> msg)
    {
        try
        {
            TincanControl ctrl(msg->data(), msg->size());
            RTC_LOG(LS_INFO) << "Received CONTROL: " << ctrl.StyledString();
            (this->*dispatch_map_.at(ctrl.GetCommand()))(ctrl);
        }
        catch (exception &e)
        {
            RTC_LOG(LS_WARNING) << "A control failed to execute. "
                                << string(msg->data(), msg->size()) << "\n"
                                << e.what();
        }
    }

    void
    Tincan::RegisterDataplane()
    {
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
        ctrl->SetControlType(TincanControl::CTTincanRequest);
        Json::Value &req = ctrl->GetRequest();
        req[TincanControl::Command] = TincanControl::RegisterDataplane;
        req[TincanControl::Data] = "Tincan Dataplane Ready";
        channel_->Deliver(move(ctrl));
    }

    bool
    Tincan::IsTunnelExisit(
        const string &tnl_id)
    {
        lock_guard<mutex> lg(tunnels_mutex_);
        return tunnels_.find(tnl_id) != tunnels_.end();
    }

    void
    Tincan::Shutdown()
    {
        exit_flag_ = true;
        RTC_LOG(LS_INFO) << "Tincan shutdown initiated";
        //    ctrl_listener_->Quit();
        epoll_eng_.Deregister(channel_->FileDesc());
        channel_->Close();
        lock_guard<mutex> lg(tunnels_mutex_);
        for (auto &kv : tunnels_)
        {
            kv.second->Shutdown();
            epoll_eng_.Deregister(kv.second->TapChannel()->FileDesc());
        }
        tunnels_.clear();
        epoll_eng_.Shutdown();
    }

    /*
     * onStopHandler for handling SIGINT,SIGTERM in linux
     * Calls OnStop() for shutdown of tincan
     */
    void
    Tincan::onStopHandler(int signum)
    {
        RTC_LOG(LS_WARNING) << "SIGNUM recv:" << signum;
        if (signum != SIGALRM)
            self_->Shutdown();
    }

    ////////////////////////////////////////////////////////////////////////////

    void
    Tincan::Run()
    {

        // Register signal with handler
        self_ = this;
        struct sigaction newact;
        memset(&newact, 0, sizeof(struct sigaction));
        newact.sa_handler = onStopHandler;
        sigemptyset(&newact.sa_mask);
        newact.sa_flags = 0;
        sigaction(SIGQUIT, &newact, NULL);
        sigaction(SIGINT, &newact, NULL);
        sigaction(SIGTERM, &newact, NULL);
        epoll_eng_.Register(channel_, EPOLLIN);
        RegisterDataplane();
        do
        {
            try
            {
                while (!exit_flag_)
                {
                    epoll_eng_.Epoll();
                }
            }
            catch (const std::exception &e)
            {
                RTC_LOG(LS_INFO) << e.what();
                // todo: close all comm channels
            }
        } while (!exit_flag_);
        RTC_LOG(LS_INFO) << "Tincan shutdown completed";
    }

    void
    Tincan::ConfigureLogging(
        TincanControl &control)
    {

        Json::Value &req = control.GetRequest();
        string log_lvl = req[TincanControl::Level].asString();
        string msg("Tincan logging successfully configured.");
        bool status = true;
        try
        {
            if (req["Device"].asString() == "All" || req["Device"].asString() == "File")
            {
                string dir = req["Directory"].asString();
                string fn = req["Filename"].asString();
                size_t max_sz = req["MaxFileSize"].asUInt64();
                size_t num_fls = req["MaxArchives"].asUInt64();
                log_sink_ = make_unique<FileRotatingLogSink>(dir, fn, max_sz, num_fls);
                log_sink_->Init();
                LogMessage::AddLogToStream(log_sink_.get(), log_levels_.at(log_lvl));
                // LogMessage::SetLogToStderr(false);
            }
            if (req["Device"].asString() == "All" ||
                req["Device"].asString() == "Console")
            {
                if (req["ConsoleLevel"].asString().length() > 0)
                    log_lvl = req["ConsoleLevel"].asString();
                LogMessage::LogToDebug(log_levels_.at(log_lvl));
                LogMessage::SetLogToStderr(false);
            }
        }
        catch (exception &)
        {
            LogMessage::LogToDebug(LS_INFO);
            LogMessage::SetLogToStderr(true);
            msg = "The configure logging operation failed. Using Console/WARNING";
            RTC_LOG(LS_WARNING) << msg;
            status = false;
        }
        control.SetResponse(msg, status);
        channel_->Deliver(control);
    }

    void
    Tincan::CreateLink(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        string msg("Connection to peer node in progress.");
        bool status = false;
        try
        {
            CreateVlink(req, control);
            status = true;
        }
        catch (exception &e)
        {
            msg = "CreateLink failed.";
            RTC_LOG(LS_WARNING) << e.what() << ". Control Data=\n"
                                << control.StyledString();
        }
        if (!status)
        {
            control.SetResponse(msg, status);
            channel_->Deliver(control);
        } // else respond when CAS is available
    }
    void
    Tincan::CreateTunnel(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
        try
        {
            CreateTunnel(req, (*resp)["Message"]);
            (*resp)["Success"] = true;
        }
        catch (exception &e)
        {
            string er_msg = "The CreateTunnel operation failed.";
            RTC_LOG(LS_ERROR) << er_msg << e.what() << ". Control Data=\n"
                              << control.StyledString();
            (*resp)["Message"] = er_msg;
            (*resp)["Success"] = false;
        }
        control.SetResponse(move(resp));
        channel_->Deliver(control);
    }

    void
    Tincan::Echo(TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        string msg = req[TincanControl::Message].asString();
        control.SetResponse(msg, true);
        control.SetControlType(TincanControl::CTTincanResponse);
        channel_->Deliver(control);
    }

    void
    Tincan::QueryCandidateAddressSet(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest(), cas_info;
        string resp;
        bool status = false;
        try
        {
            QueryLinkCas(req, cas_info);
            resp = cas_info.toStyledString();
            status = true;
        }
        catch (exception &e)
        {
            resp = "The QueryCandidateAddressSet operation failed. ";
            RTC_LOG(LS_WARNING) << resp << e.what() << ". Control Data=\n"
                                << control.StyledString();
        }
        control.SetResponse(resp, status);
        channel_->Deliver(control);
    }

    void
    Tincan::QueryLinkStats(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
        (*resp)["Success"] = false;
        try
        {
            QueryLinkStats(req, (*resp)["Message"]);
            (*resp)["Success"] = true;
        }
        catch (exception &e)
        {
            string er_msg = "The QueryLinkStats operation failed. ";
            RTC_LOG(LS_WARNING) << er_msg << e.what() << ". Control Data=\n"
                                << control.StyledString();
            (*resp)["Message"] = er_msg;
            (*resp)["Success"] = false;
        }
        control.SetResponse(move(resp));
        channel_->Deliver(control);
    }

    void
    Tincan::QueryTunnelInfo(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest(), node_info;
        string resp("The QueryTunnelInfo operation succeeded");
        bool status = false;
        try
        {
            QueryTunnelInfo(req, node_info);
            resp = node_info.toStyledString();
            status = true;
        }
        catch (exception &e)
        {
            resp = "The QueryTunnelInfo operation failed. ";
            resp.append(e.what());
            RTC_LOG(LS_WARNING) << resp << e.what() << ". Control Data=\n"
                                << control.StyledString();
        }
        control.SetResponse(resp, status);
        channel_->Deliver(control);
    }

    void
    Tincan::RemoveLink(
        TincanControl &control)
    {
        bool status = false;
        Json::Value &req = control.GetRequest();
        string msg("The RemoveLink operation succeeded");
        try
        {
            RemoveVlink(req);
            status = true;
        }
        catch (exception &e)
        {
            msg = "The RemoveLink operation failed.";
            RTC_LOG(LS_WARNING) << e.what() << ". Control Data=\n"
                                << control.StyledString();
        }
        control.SetResponse(msg, status);
        channel_->Deliver(control);
    }

    void
    Tincan::RemoveTunnel(
        TincanControl &control)
    {
        bool status = false;
        Json::Value &req = control.GetRequest();
        string msg("The RemoveTunnel operation ");
        try
        {
            RemoveTunnel(req);
            status = true;
            msg.append("succeeded.");
        }
        catch (exception &e)
        {
            msg = "failed.";
            RTC_LOG(LS_WARNING) << e.what() << ". Control Data=\n"
                                << control.StyledString();
        }
        control.SetResponse(msg, status);
        channel_->Deliver(control);
    }
}