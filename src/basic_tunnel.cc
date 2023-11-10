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
#include "basic_tunnel.h"
#include "rtc_base/third_party/base64/base64.h"
#include "tincan_control.h"
#include "buffer_pool.h"

namespace tincan
{
    extern TincanParameters tp;
    BasicTunnel::BasicTunnel(
        unique_ptr<TunnelDesc> descriptor,
        shared_ptr<ControllerCommsChannel> ctrl_handle,
        TunnelThreads *thread_pool) : descriptor_(std::move(descriptor)),
                                      ctrl_link_(ctrl_handle),
                                      threads_(thread_pool)
    {
        tdev_ = make_shared<TapDev>();
    }

    int
    BasicTunnel::Configure(
        unique_ptr<TapDescriptor> tap_desc,
        const vector<string> &ignored_list)
    {
        tap_desc_ = std::move(tap_desc);
        if (tdev_->Open(*tap_desc_.get()) == -1)
            return -1;

        // create X509 identity for secure connections
        string sslid_name = descriptor_->node_id + descriptor_->uid;
        sslid_ = rtc::SSLIdentity::Create(sslid_name, rtc::KT_RSA);
        if (!sslid_)
        {
            RTC_LOG(LS_ERROR) << "Failed to generate SSL Identity";
            return -1;
        }
        local_fingerprint_ = rtc::SSLFingerprint::CreateUnique("sha-512", *sslid_.get());
        if (!local_fingerprint_)
        {
            RTC_LOG(LS_ERROR) << "Failed to create the local fingerprint";
            return -1;
        }
        SetIgnoredNetworkInterfaces(ignored_list);
        return 0;
    }

    void
    BasicTunnel::Start()
    {
        tdev_->read_completion_.connect(this, &BasicTunnel::TapReadComplete);
    }

    void
    BasicTunnel::Shutdown()
    {
        if (vlink_ && vlink_->IsReady())
        {
            LinkInfoMsgData md;
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_DISC_LINK, &md);
            md.msg_event.Wait(Event::kForever);
        }
        vlink_.reset();
        NetworkThread()->Stop();
        SignalThread()->Stop();
    }

    rtc::Thread *BasicTunnel::SignalThread()
    {
        return threads_->LinkThreads().first;
    }

    rtc::Thread *BasicTunnel::NetworkThread()
    {
        return threads_->LinkThreads().second;
    }

    shared_ptr<VirtualLink>
    BasicTunnel::CreateVlink(
        unique_ptr<PeerDescriptor> peer_desc, bool role)
    {
        if (!vlink_)
        {
            unique_ptr<VlinkDescriptor> vlink_desc = make_unique<VlinkDescriptor>();
            vlink_desc->uid = descriptor_->uid;
            vlink_desc->stun_servers.assign(descriptor_->stun_servers.begin(),
                                            descriptor_->stun_servers.end());

            vlink_desc->turn_descs.assign(descriptor_->turn_descs.begin(),
                                          descriptor_->turn_descs.end());
            unique_ptr<VirtualLink> vl = make_unique<VirtualLink>(
                std::move(vlink_desc), std::move(peer_desc), SignalThread(), NetworkThread());
            unique_ptr<SSLIdentity> sslid_copy(sslid_->Clone());
            vl->Initialize(net_manager_, std::move(sslid_copy),
                           make_unique<rtc::SSLFingerprint>(*local_fingerprint_.get()),
                           role ? cricket::ICEROLE_CONTROLLED : cricket::ICEROLE_CONTROLLING);
            vl->SignalMessageReceived.connect(this, &BasicTunnel::VlinkReadComplete);
            vl->SignalLinkUp.connect(this, &BasicTunnel::VLinkUp);
            vl->SignalLinkDown.connect(this, &BasicTunnel::VLinkDown);
            vlink_ = std::move(vl);
        }
        return vlink_;
    }

    void BasicTunnel::StartConnections()
    {
        vlink_->StartConnections();
    }
    void BasicTunnel::QueryInfo(
        Json::Value &tnl_info)
    {
        tnl_info[TincanControl::TunnelId] = descriptor_->uid;
        tnl_info[TincanControl::FPR] = Fingerprint();
        tnl_info[TincanControl::TapName] = tap_desc_->name;
        tnl_info[TincanControl::MAC] = MacAddress();
        tnl_info["LinkIds"] = Json::Value(Json::arrayValue);
        if (vlink_)
        {
            tnl_info["LinkIds"].append(vlink_->Id());
        }
    }

    void BasicTunnel::QueryLinkCas(
        const string &vlink_id,
        Json::Value &cas_info)
    {
        if (vlink_)
        {
            if (vlink_->IceRole() == cricket::ICEROLE_CONTROLLING)
                cas_info[TincanControl::IceRole] = TincanControl::Controlling.c_str();
            else if (vlink_->IceRole() == cricket::ICEROLE_CONTROLLED)
                cas_info[TincanControl::IceRole] = TincanControl::Controlled.c_str();

            cas_info[TincanControl::CAS] = vlink_->Candidates();
        }
    }

    void BasicTunnel::QueryLinkId(string &link_id)
    {
        if (vlink_)
            link_id = vlink_->Id();
    }

    void BasicTunnel::QueryLinkInfo(
        Json::Value &vlink_info)
    {
        vlink_info[TincanControl::LinkId] = vlink_->Id();
        if (vlink_)
        {
            if (vlink_->IceRole() == cricket::ICEROLE_CONTROLLING)
                vlink_info[TincanControl::IceRole] = TincanControl::Controlling;
            else
                vlink_info[TincanControl::IceRole] = TincanControl::Controlled;
            if (vlink_->IsReady())
            {
                LinkInfoMsgData md;
                NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_QUERY_NODE_INFO, &md);
                md.msg_event.Wait(Event::kForever);
                vlink_info[TincanControl::Stats].swap(md.info);
                vlink_info[TincanControl::Status] = "ONLINE";
            }
            else
            {
                vlink_info[TincanControl::Status] = "OFFLINE";
                vlink_info[TincanControl::Stats] = Json::Value(Json::objectValue);
            }
        }
        else
        {
            vlink_info[TincanControl::Status] = "UNKNOWN";
            vlink_info[TincanControl::Stats] = Json::Value(Json::objectValue);
        }
    }

    int BasicTunnel::RemoveLink(
        const string &vlink_id)
    {
        if (!vlink_)
            return 0;
        if (vlink_->Id() != vlink_id)
        {
            RTC_LOG(LS_ERROR) << "The specified VLink ID does not match this Tunnel";
            return -1;
        }
        if (vlink_->IsReady())
        {
            LinkInfoMsgData md;
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_DISC_LINK, &md);
            md.msg_event.Wait(Event::kForever);
        }
        vlink_.reset();
        return 0;
    }

    void BasicTunnel::VlinkReadComplete(
        const char *data,
        size_t data_len)
    {
        auto frame = make_unique<Iob>(data, data_len);
        tdev_->QueueWrite(std::move(frame));
    }

    void BasicTunnel::TapReadComplete(
        Iob *iob)
    {
        if (!vlink_)
        {
            RTC_LOG(LS_ERROR) << "No vlink for transmit";
            delete iob;
            return;
        }
        if (NetworkThread()->IsCurrent())
            vlink_->Transmit(unique_ptr<Iob>(iob));
        else
        {
            TransmitMsgData *md = new TransmitMsgData;
            md->frm.reset(iob);
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_TRANSMIT, md);
        }
    }

    void
    BasicTunnel::VLinkUp(
        string vlink_id)
    {
        tdev_->Up();
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
        ctrl->SetControlType(TincanControl::CTTincanRequest);
        Json::Value &req = ctrl->GetRequest();
        req[TincanControl::Command] = TincanControl::LinkConnected;
        req[TincanControl::TunnelId] = descriptor_->uid;
        req[TincanControl::LinkId] = vlink_id;
        ctrl_link_->Deliver(std::move(ctrl));
    }

    void
    BasicTunnel::VLinkDown(
        string vlink_id)
    {
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
        ctrl->SetControlType(TincanControl::CTTincanRequest);
        Json::Value &req = ctrl->GetRequest();
        req[TincanControl::Command] = TincanControl::LinkDisconnected;
        req[TincanControl::TunnelId] = descriptor_->uid;
        req[TincanControl::LinkId] = vlink_id;
        ctrl_link_->Deliver(std::move(ctrl));
    }

    TunnelDesc &
    BasicTunnel::Descriptor()
    {
        return *descriptor_.get();
    }

    string
    BasicTunnel::Name()
    {
        return descriptor_->uid;
    }

    string
    BasicTunnel::MacAddress()
    {
        MacAddressType mac = tdev_->MacAddress();
        return ByteArrayToString(mac.begin(), mac.end(), 0);
    }

    string
    BasicTunnel::Fingerprint()
    {
        return local_fingerprint_->ToString();
    }

    void
    BasicTunnel::SetIgnoredNetworkInterfaces(
        const vector<string> &ignored_list)
    {
        net_manager_.set_network_ignore_list(ignored_list);
    }

    void BasicTunnel::OnMessage(Message *msg)
    {
        switch (msg->message_id)
        {
        case MSGID_TRANSMIT:
        {
            if (vlink_)
                vlink_->Transmit(std::move(((TransmitMsgData *)msg->pdata)->frm));
        }
        break;
        case MSGID_QUERY_NODE_INFO:
        {
            if (vlink_)
                vlink_->GetStats(((LinkInfoMsgData *)msg->pdata)->info);
            ((LinkInfoMsgData *)msg->pdata)->msg_event.Set();
        }
        break;
        case MSGID_DISC_LINK:
        {
            if (vlink_)
                vlink_->Disconnect();
            ((LinkInfoMsgData *)msg->pdata)->msg_event.Set();
        }
        break;
        }
        //msg is deleted elsewhere
    }

} // namespace tincan
