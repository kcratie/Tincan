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
#include "basic_tunnel.h"
#include "rtc_base/third_party/base64/base64.h"
#include "tincan_control.h"
namespace tincan
{
    extern TincanParameters tp;
    BasicTunnel::BasicTunnel(
        unique_ptr<TunnelDesc> descriptor,
        shared_ptr<ControllerCommsChannel> ctrl_handle,
        TunnelThreads *thread_pool) : descriptor_(move(descriptor)),
                                      ctrl_link_(ctrl_handle),
                                      threads_(thread_pool)
    {
        tdev_ = make_shared<TapDev>();
    }

    void
    BasicTunnel::Configure(
        unique_ptr<TapDescriptor> tap_desc,
        const vector<string> &ignored_list)
    {
        tap_desc_ = move(tap_desc);
        tdev_->Open(*tap_desc_.get());

        // create X509 identity for secure connections
        string sslid_name = descriptor_->node_id + descriptor_->uid;
        sslid_ = rtc::SSLIdentity::Create(sslid_name, rtc::KT_RSA);
        if (!sslid_)
            throw TCEXCEPT("Failed to generate SSL Identity");
        local_fingerprint_ = rtc::SSLFingerprint::CreateUnique("sha-512", *sslid_.get());
        if (!local_fingerprint_)
            throw TCEXCEPT("Failed to create the local fingerprint");
        SetIgnoredNetworkInterfaces(ignored_list);
    }

    void
    BasicTunnel::Start()
    {
        tdev_->read_completion_.connect(this, &BasicTunnel::TapReadComplete);
    }

    void
    BasicTunnel::Shutdown()
    {
        tdev_->Down();
    }

    rtc::Thread *BasicTunnel::SignalThread()
    {
        return threads_->LinkThreads().first;
    }

    rtc::Thread *BasicTunnel::NetworkThread()
    {
        return threads_->LinkThreads().second;
    }

    unique_ptr<VirtualLink>
    BasicTunnel::CreateVlink(
        unique_ptr<VlinkDescriptor> vlink_desc,
        unique_ptr<PeerDescriptor> peer_desc,
        cricket::IceRole ice_role)
    {
        vlink_desc->stun_servers.assign(descriptor_->stun_servers.begin(),
                                        descriptor_->stun_servers.end());

        vlink_desc->turn_descs.assign(descriptor_->turn_descs.begin(),
                                      descriptor_->turn_descs.end());
        unique_ptr<VirtualLink> vl = make_unique<VirtualLink>(
            move(vlink_desc), move(peer_desc), SignalThread(), NetworkThread());
        unique_ptr<SSLIdentity> sslid_copy(sslid_->Clone());
        vl->Initialize(net_manager_, move(sslid_copy),
                       make_unique<rtc::SSLFingerprint>(*local_fingerprint_.get()),
                       ice_role);
        vl->SignalMessageReceived.connect(this, &BasicTunnel::VlinkReadComplete);
        vl->SignalLinkUp.connect(this, &BasicTunnel::VLinkUp);
        vl->SignalLinkDown.connect(this, &BasicTunnel::VLinkDown);
        if (vl->PeerCandidates().length() != 0)
            vl->StartConnections();

        return vl;
    }

    void
    BasicTunnel::VLinkUp(
        string vlink_id)
    {
        tdev_->Up();
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
        ctrl->SetControlType(TincanControl::CTTincanRequest);
        Json::Value &req = ctrl->GetRequest();
        req[TincanControl::Command] = TincanControl::LinkStateChange;
        req[TincanControl::TunnelId] = descriptor_->uid;
        req[TincanControl::LinkId] = vlink_id;
        req[TincanControl::Data] = "LINK_STATE_UP";
        ctrl_link_->Deliver(move(ctrl));
    }

    void
    BasicTunnel::VLinkDown(
        string vlink_id)
    {
        unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
        ctrl->SetControlType(TincanControl::CTTincanRequest);
        Json::Value &req = ctrl->GetRequest();
        req[TincanControl::Command] = TincanControl::LinkStateChange;
        req[TincanControl::TunnelId] = descriptor_->uid;
        req[TincanControl::LinkId] = vlink_id;
        req[TincanControl::Data] = "LINK_STATE_DOWN";
        ctrl_link_->Deliver(move(ctrl));
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
            unique_ptr<iob_t> frame = move(((TransmitMsgData *)msg->pdata)->frm);
            shared_ptr<VirtualLink> vl = ((TransmitMsgData *)msg->pdata)->vl;
            vl->Transmit(move(frame));
        }
        break;
        case MSGID_QUERY_NODE_INFO:
        {
            shared_ptr<VirtualLink> vl = ((LinkInfoMsgData *)msg->pdata)->vl;
            vl->GetStats(((LinkInfoMsgData *)msg->pdata)->info);
            ((LinkInfoMsgData *)msg->pdata)->msg_event.Set();
        }
        break;
        case MSGID_DISC_LINK:
        {
            shared_ptr<VirtualLink> vl = ((LinkMsgData *)msg->pdata)->vl;
            vl->Disconnect();
            ((LinkInfoMsgData *)msg->pdata)->msg_event.Set();
        }
        break;
        }
    }

} // namespace tincan
