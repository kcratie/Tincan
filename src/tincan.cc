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

#include "tincan.h"
#include "tincan_exception.h"
#include "turn_descriptor.h"
#include <execinfo.h>
#include <signal.h>

namespace tincan
{
    Tincan::Tincan(const TincanParameters &tp) : tp_(tp),
                                                 dispatch_map_{
                                                     {"ConfigureLogging", &Tincan::ConfigureLogging},
                                                     {"CreateLink", &Tincan::CreateLink},
                                                     {"CreateTunnel", &Tincan::CreateTunnel},
                                                     {"Echo", &Tincan::Echo},
                                                     {"QueryCandidateAddressSet", &Tincan::QueryCandidateAddressSet},
                                                     {"QueryLinkStats", &Tincan::QueryLinkStats},
                                                     {"QueryTunnelInfo", &Tincan::QueryTunnelInfo},
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
        exit_flag_.store(false);
        LogMessage::LogTimestamps();
        LogMessage::LogThreads();
        LogMessage::LogToDebug(LS_WARNING);
        LogMessage::SetLogToStderr(true);
        if (!tp.log_config.empty())
        {
            Json::CharReaderBuilder b;
            unique_ptr<Json::CharReader> parser;
            parser.reset(b.newCharReader());
            Json::String errs;
            auto logcfg = make_unique<Json::Value>();
            if (!parser->parse(tp.log_config.c_str(), tp.log_config.c_str() + tp.log_config.length(), logcfg.get(), &errs))
            {
                string emsg = "Unable to parse logging config - ";
                emsg.append(tp.log_config);
                RTC_LOG(LS_ERROR) << emsg;
            }
            TincanControl ctrl(std::move(logcfg));
            ConfigureLogging(ctrl);
        }
        else
        {
            auto logcfg = make_unique<Json::Value>();
            (*logcfg)["Directory"] = "./";
            (*logcfg)["Filename"] = "tincan";
            (*logcfg)["MaxFileSize"] = 1048576;
            (*logcfg)["MaxArchives"] = 1;
            (*logcfg)["Device"] = "File";
            (*logcfg)["Level"] = "WARNING";
            TincanControl ctrl(std::move(logcfg));
            ConfigureLogging(ctrl);
        }
        // Register signal handlers
        // todo: handle signal in epoll engine
        struct sigaction shutdwm;
        memset(&shutdwm, 0, sizeof(struct sigaction));
        shutdwm.sa_handler = onStopHandler;
        sigemptyset(&shutdwm.sa_mask);
        shutdwm.sa_flags = 0;
        sigaction(SIGQUIT, &shutdwm, NULL);
        sigaction(SIGINT, &shutdwm, NULL);
        sigaction(SIGTERM, &shutdwm, NULL);

        channel_->ConnectToController();

        // TD<decltype(tunnel_.at(""))> BasicTunnelType;
        // int x{0};
        // TD<decltype(x)> xType;
    }

    void
    Tincan::CreateTunnel(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
        try
        {
            CreateTunnel(req, (*resp)[TincanControl::Message]);
            (*resp)[TincanControl::Success] = true;
        }
        catch (exception &e)
        {
            string er_msg = "The CreateTunnel operation failed.";
            RTC_LOG(LS_ERROR) << er_msg << e.what() << ". Control Data=\n"
                              << control.StyledString();
            (*resp)[TincanControl::Message] = er_msg;
            (*resp)[TincanControl::Success] = false;
        }
        control.SetResponse(std::move(resp));
        channel_->Deliver(control);
    }

    void
    Tincan::CreateLink(
        TincanControl &control)
    {
        bool is_resp_ready = false;
        try
        {
            is_resp_ready = CreateVlink(control);
        }
        catch (exception &e)
        {
            string er_msg = "CreateLink failed. Error=";
            er_msg.append(e.what());
            RTC_LOG(LS_ERROR) << er_msg << ". Control Data=\n"
                              << control.StyledString();
            is_resp_ready = true;
            unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
            (*resp)[TincanControl::Message] = er_msg;
            (*resp)[TincanControl::Success] = false;
            control.SetResponse(std::move(resp));
        }
        if (is_resp_ready)
        {
            channel_->Deliver(control);
        } // else respond when CAS is available
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
    Tincan::QueryLinkStats(
        TincanControl &control)
    {
        Json::Value &req = control.GetRequest();
        unique_ptr<Json::Value> resp = make_unique<Json::Value>(Json::objectValue);
        (*resp)[TincanControl::Success] = false;
        try
        {
            QueryLinkStats((*resp)[TincanControl::Message]);
            (*resp)[TincanControl::Message][TincanControl::TunnelId] = req[TincanControl::TunnelId];
            (*resp)[TincanControl::Success] = true;
        }
        catch (exception &e)
        {
            string er_msg = "The QueryLinkStats operation failed. ";
            RTC_LOG(LS_WARNING) << er_msg << e.what() << ". Control Data=\n"
                                << control.StyledString();
            (*resp)[TincanControl::Message] = er_msg;
            (*resp)[TincanControl::Success] = false;
        }
        control.SetResponse(std::move(resp));
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
                ostringstream fn;
                fn << req["Filename"].asString() << "-" << getpid() << ".log";
                size_t max_sz = req["MaxFileSize"].asUInt64();
                size_t num_fls = req["MaxArchives"].asUInt64();
                log_sink_ = make_unique<FileRotatingLogSink>(dir, fn.str(), max_sz, num_fls);
                log_sink_->Init();
                LogMessage::AddLogToStream(log_sink_.get(), log_levels_.at(log_lvl));
            }
            if (req["Device"].asString() == "All" ||
                req["Device"].asString() == "Console")
            {
                if (req["ConsoleLevel"].asString().length() > 0)
                    log_lvl = req["ConsoleLevel"].asString();
                LogMessage::LogToDebug(log_levels_.at(log_lvl));
            }
            // LogMessage::SetLogToStderr(false);
        }
        catch (exception &)
        {
            LogMessage::LogToDebug(LS_INFO);
            LogMessage::SetLogToStderr(true);
            msg = "The configure logging operation failed. Using Console/WARNING";
            RTC_LOG(LS_WARNING) << msg;
            status = false;
        }
    }

    
    ////////////////////////////////////////////////////////////////////////////

    void Tincan::CreateTunnel(
        const Json::Value &tnl_desc,
        Json::Value &tnl_info)
    {
        tunnel_ = make_unique<BasicTunnel>(
            make_unique<TunnelDesc>(tnl_desc),
            channel_);
        unique_ptr<TapDescriptor> tap_desc = make_unique<TapDescriptor>(
            tnl_desc["TapName"].asString(),
            tnl_desc[TincanControl::MTU].asUInt());
        Json::Value network_ignore_list =
            tnl_desc[TincanControl::IgnoredNetInterfaces];
        int count = network_ignore_list.size();
        for (int i = 0; i < count; i++)
        {
            if_list_.push_back(network_ignore_list[i].asString());
        }
        tunnel_->Configure(std::move(tap_desc));
        tunnel_->Start();
        tunnel_->QueryInfo(tnl_info);
        epoll_eng_.Register(tunnel_->TapChannel(), EPOLLIN);
        return;
    }

    bool
    Tincan::CreateVlink(
        TincanControl &control)
    {
        bool role = false;
        auto resp = make_unique<Json::Value>(Json::objectValue);
        Json::Value &tnl_info = (*resp)[TincanControl::Message];
        Json::Value &link_desc = control.GetRequest();
        if (!tunnel_)
        {
            CreateTunnel(link_desc, tnl_info);
            role = true;
        }
        else
        {
            tunnel_->QueryInfo(tnl_info);
        }
        VirtualLink *vlink = tunnel_->Vlink();
        if (!vlink)
        {
            unique_ptr<PeerDescriptor> peer_desc = make_unique<PeerDescriptor>();
            peer_desc->uid =
                link_desc[TincanControl::PeerInfo][TincanControl::UID].asString();
            peer_desc->cas =
                link_desc[TincanControl::PeerInfo][TincanControl::CAS].asString();
            peer_desc->fingerprint =
                link_desc[TincanControl::PeerInfo][TincanControl::FPR].asString();
            peer_desc->mac_address =
                link_desc[TincanControl::PeerInfo][TincanControl::MAC].asString();
            vlink = tunnel_->CreateVlink(std::move(peer_desc), role, if_list_);
            if_list_.clear();
            vlink->SignalLocalCasReady.connect(this, &Tincan::OnLocalCasUpdated);
            unique_ptr<TincanControl> ctrl = make_unique<TincanControl>(control);
            ctrl->SetResponse(std::move(resp));
            std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
            inprogess_controls_[control.GetTransactionId()] = std::move(ctrl);
            vlink->SetCasReadyId(control.GetTransactionId());
            tunnel_->StartConnections();
        }
        else
        {
            vlink->PeerCandidates(
                link_desc[TincanControl::PeerInfo][TincanControl::CAS].asString());
        }
        return false;
    }

    void
    Tincan::QueryLinkCas(
        const Json::Value &link_desc,
        Json::Value &cas_info)
    {
        tunnel_->QueryLinkCas(cas_info);
    }

    void
    Tincan::QueryLinkStats(
        Json::Value &stat_info)
    {
        tunnel_->QueryLinkInfo(stat_info);
    }

    void
    Tincan::QueryTunnelInfo(
        const Json::Value &tnl_desc,
        Json::Value &tnl_info)
    {
        auto tnl_id = tnl_desc[TincanControl::TunnelId].asString();
        tunnel_->QueryInfo(tnl_info);
    }

    void
    Tincan::RemoveVlink(
        const Json::Value &link_desc)
    {
        tunnel_->RemoveLink();
    }

    void
    Tincan::OnLocalCasUpdated(
        uint64_t control_id,
        string lcas)
    {
        unique_ptr<TincanControl> ctrl;
        try
        {
            std::lock_guard<std::mutex> lg(inprogess_controls_mutex_);
            ctrl = std::move(inprogess_controls_.at(control_id));
            inprogess_controls_.erase(control_id);
        }
        catch (exception &e)
        {
            RTC_LOG(LS_WARNING) << e.what();
            return;
        }
        if (lcas.empty())
        {
            const string &link_id = ctrl->GetRequest()[TincanControl::TunnelId].asString();
            lcas = "No local candidates available on vlink: ";
            lcas.append(link_id);
            RTC_LOG(LS_WARNING) << lcas;
        }
        Json::Value &resp = ctrl->GetResponse();
        resp[TincanControl::Message][TincanControl::CAS] = lcas;
        resp[TincanControl::Success] = true;
        ctrl->SetControlType(TincanControl::CTTincanResponse);
        channel_->Deliver(std::move(ctrl));
    }

    ////////////////////////////////////////////////////////////////////////////

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
        req[TincanControl::TunnelId] = tp_.tunnel_id;
        channel_->Deliver(std::move(ctrl));
    }

    void
    Tincan::Run()
    {
        epoll_eng_.Register(channel_, EPOLLIN);
        RegisterDataplane();
        try
        {
            while (!exit_flag_.load(std::memory_order_acquire))
            {
                epoll_eng_.Epoll();
            }
        }
        catch (const std::exception &e)
        {
            RTC_LOG(LS_ERROR) << e.what();
        }
        epoll_eng_.Shutdown();
        tunnel_.reset();
        RTC_LOG(LS_INFO) << "Tincan shutdown completed";
    }

    /*
     * onStopHandler for handling tincan shutdown (SIGINT, SIGTERM)
     */
    void
    Tincan::onStopHandler(int signum)
    {
        exit_flag_.store(true);
    }
}