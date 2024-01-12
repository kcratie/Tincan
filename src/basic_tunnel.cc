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
    extern BufferPool<Iob> bp;
    BasicTunnel::BasicTunnel(
        unique_ptr<TunnelDesc> descriptor,
        shared_ptr<ControllerCommsChannel> ctrl_handle) : descriptor_(std::move(descriptor)),
                                                          ctrl_link_(ctrl_handle),
                                                          worker_(make_unique<rtc::Thread>(rtc::SocketServer::CreateDefault())),
                                                          tdev_(make_shared<TapDev>())
    {
    }

    BasicTunnel::~BasicTunnel()
    {
        if (vlink_)
        {
            NetworkThread()->Invoke<void>(RTC_FROM_HERE, [this]()
                                          {vlink_->Disconnect(); vlink_.reset(); });
        }
    }

    int BasicTunnel::Configure(
        unique_ptr<TapDescriptor> tap_desc)
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
        return 0;
    }

    void BasicTunnel::Start()
    {
        tdev_->read_completion = [this](Iob &&iob)
        { TapReadComplete(std::move(iob)); };
        // TD<decltype(tdev_->read_completion)> td;
    }

    rtc::Thread *BasicTunnel::SignalThread()
    {
        return worker_.get();
    }

    rtc::Thread *BasicTunnel::NetworkThread()
    {
        return worker_.get();
    }

    weak_ptr<VirtualLink>
    BasicTunnel::CreateVlink(
        unique_ptr<PeerDescriptor> peer_desc, bool role, const vector<string> &ignored_list)
    {
        if (!vlink_)
        {
            unique_ptr<VlinkDescriptor> vlink_desc = make_unique<VlinkDescriptor>();
            vlink_desc->uid = descriptor_->uid;
            vlink_desc->stun_servers.assign(descriptor_->stun_servers.begin(),
                                            descriptor_->stun_servers.end());

            vlink_desc->turn_descs.assign(descriptor_->turn_descs.begin(),
                                          descriptor_->turn_descs.end());
            NetworkThread()->SetName("NetworkThread", this);
            NetworkThread()->Start();
            vlink_ = make_unique<VirtualLink>(
                std::move(vlink_desc), std::move(peer_desc), SignalThread(), NetworkThread());
            unique_ptr<SSLIdentity> sslid_copy(sslid_->Clone());
            vlink_->Initialize(std::move(sslid_copy),
                               make_unique<rtc::SSLFingerprint>(*local_fingerprint_.get()),
                               role ? cricket::ICEROLE_CONTROLLED : cricket::ICEROLE_CONTROLLING,
                               ignored_list);
            vlink_->SignalMessageReceived.connect(this, &BasicTunnel::VlinkReadComplete);
            vlink_->SignalLinkUp.connect(this, &BasicTunnel::OnVLinkUp);
            vlink_->SignalLinkDown.connect(this, &BasicTunnel::OnVLinkDown);
        }
        return vlink_;
    }

    void BasicTunnel::StartConnections()
    {
        if (NetworkThread()->IsCurrent())
            vlink_->StartConnections();
        else
        {
            NetworkThread()->PostTask(RTC_FROM_HERE, [this]() mutable
                                      { vlink_->StartConnections(); });
        }
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
        if (vlink_)
        {
            vlink_info[TincanControl::LinkId] = vlink_->Id();
            if (vlink_->IceRole() == cricket::ICEROLE_CONTROLLING)
                vlink_info[TincanControl::IceRole] = TincanControl::Controlling;
            else
                vlink_info[TincanControl::IceRole] = TincanControl::Controlled;
            if (vlink_->IsReady())
            {
                NetworkThread()->Invoke<void>(RTC_FROM_HERE, [this, &info = vlink_info[TincanControl::Stats]]()
                                              { vlink_->GetStats(info); });
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

    void BasicTunnel::RemoveLink()
    {
        if (vlink_)
        {
            NetworkThread()->Invoke<void>(RTC_FROM_HERE, [this]()
                                          { vlink_->Disconnect(); vlink_.reset(); });
        }
    }

    void BasicTunnel::VlinkReadComplete(
        const char *data,
        size_t data_len)
    {
        auto frame = bp.get();
        frame.data(data, data_len);
        // tdev_->QueueWrite(std::move(frame));
        tdev_->WriteDirect(data, data_len);
    }

    void BasicTunnel::TapReadComplete(
        Iob &&iob)
    {
        if (!vlink_)
        {
            RTC_LOG(LS_ERROR) << "No vlink for transmit";
            bp.put(std::move(iob));
            return;
        }
        if (NetworkThread()->IsCurrent())
            vlink_->Transmit(std::move(iob));
        else
        {
            NetworkThread()->PostTask(RTC_FROM_HERE, [this, riob = std::move(iob)]() mutable
                                      { vlink_->Transmit(std::move(riob)); });
        }
    }

    void
    BasicTunnel::OnVLinkUp(
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
    BasicTunnel::OnVLinkDown(
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

} // namespace tincan
