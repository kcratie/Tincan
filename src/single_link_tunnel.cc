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
#include "single_link_tunnel.h"
#include "tincan_exception.h"
#include "tincan_control.h"

namespace tincan
{
    SingleLinkTunnel::SingleLinkTunnel(
        unique_ptr<TunnelDesc> descriptor,
        shared_ptr<ControllerCommsChannel> ctrl_handle,
        TunnelThreads *thread_pool) : BasicTunnel(move(descriptor), ctrl_handle, thread_pool)
    {
    }

    shared_ptr<VirtualLink>
    SingleLinkTunnel::CreateVlink(
        unique_ptr<VlinkDescriptor> vlink_desc,
        unique_ptr<PeerDescriptor> peer_desc)
    {
        if (vlink_)
        {
            vlink_->PeerCandidates(peer_desc->cas);
            vlink_->StartConnections();
            RTC_LOG(LS_INFO) << "Added remote CAS to vlink w/ peer "
                             << vlink_->PeerInfo().uid;
        }
        else
        {
            cricket::IceRole ir = cricket::ICEROLE_CONTROLLED;
            if (descriptor_->node_id < peer_desc->uid)
                ir = cricket::ICEROLE_CONTROLLING;
            string roles[] = {"CONTROLLING", "CONTROLLED"};
            RTC_LOG(LS_INFO) << "Creating " << roles[ir] << " vlink w/ peer " << peer_desc->uid;
            vlink_ = BasicTunnel::CreateVlink(move(vlink_desc), move(peer_desc), ir);
        }
        return vlink_;
    }

    void SingleLinkTunnel::QueryInfo(
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

    void SingleLinkTunnel::QueryLinkCas(
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

    void SingleLinkTunnel::QueryLinkId(string &link_id)
    {
        if (vlink_)
            link_id = vlink_->Id();
    }

    void SingleLinkTunnel::QueryLinkInfo(
        const string &vlink_id,
        Json::Value &vlink_info)
    {
        if (vlink_)
        {
            if (vlink_->IceRole() == cricket::ICEROLE_CONTROLLING)
                vlink_info[TincanControl::IceRole] = TincanControl::Controlling;
            else
                vlink_info[TincanControl::IceRole] = TincanControl::Controlled;
            if (vlink_->IsReady())
            {
                LinkInfoMsgData md;
                md.vl = vlink_;
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

    void SingleLinkTunnel::Shutdown()
    {
        if (vlink_ && vlink_->IsReady())
        {
            LinkInfoMsgData md;
            md.vl = vlink_;
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_DISC_LINK, &md);
            md.msg_event.Wait(Event::kForever);
        }
        vlink_.reset();
        BasicTunnel::Shutdown();
    }
    void SingleLinkTunnel::RemoveLink(
        const string &vlink_id)
    {
        if (!vlink_)
            return;
        if (vlink_->Id() != vlink_id)
            throw TCEXCEPT("The specified VLink ID does not match this Tunnel");
        if (vlink_->IsReady())
        {
            LinkInfoMsgData md;
            md.vl = vlink_;
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_DISC_LINK, &md);
            md.msg_event.Wait(Event::kForever);
        }
        vlink_.reset();
    }

    void SingleLinkTunnel::VlinkReadComplete(
        uint8_t *data,
        uint32_t data_len,
        VirtualLink &vlink)
    {
        auto frame = make_unique<iob_t>(data, data + data_len);
        tdev_->QueueWrite(move(frame));
    }

    void SingleLinkTunnel::TapReadComplete(
        iob_t *iob_rd)
    {
        unique_ptr<iob_t> iob(iob_rd);
        if (NetworkThread()->IsCurrent())
            vlink_->Transmit(move(iob));
        else
        {
            TransmitMsgData *md = new TransmitMsgData;
            md->frm = move(iob);
            md->vl = vlink_;
            NetworkThread()->Post(RTC_FROM_HERE, this, MSGID_TRANSMIT, md);
        }
    }

} // end namespace tincan
