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
#ifndef BASIC_TUNNEL_H_
#define BASIC_TUNNEL_H_
#include "tincan_base.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/strings/json.h"
#include "tapdev.h"
#include "tincan_exception.h"
#include "tunnel_descriptor.h"
#include "virtual_link.h"
#include "controller_comms.h"
namespace tincan
{
    class BasicTunnel : public sigslot::has_slots<>,
                        public MessageHandler
    {
    public:
        enum MSG_ID
        {
            MSGID_TRANSMIT,
            MSGID_SEND_ICC,
            MSGID_QUERY_NODE_INFO,
            MSGID_FWD_FRAME,
            MSGID_FWD_FRAME_RD,
            MSGID_DISC_LINK,
            MSGID_TAP_READ,
            MSGID_TAP_WRITE,
            MSGID_TAP_UP,
            MSGID_TAP_DOWN
        };

        class TransmitMsgData : public MessageData
        {
        public:
            TransmitMsgData()=default;
            TransmitMsgData(const TransmitMsgData &rhs) = delete;
            TransmitMsgData(TransmitMsgData &&rhs) : frm(std::move(rhs.frm)) {}
            virtual ~TransmitMsgData() = default;
            TransmitMsgData &operator=(const TransmitMsgData &rhs) = delete;
            TransmitMsgData &operator=(TransmitMsgData &&rhs)
            {
                if (this != &rhs)
                {
                    frm = std::move(rhs.frm);
                }
                return *this;
            }
            TransmitMsgData &operator=(Iob &&rhs)
            {
                if (&frm != &rhs)
                {
                    frm = std::move(rhs);
                }
                return *this;
            }
            Iob frm;
        };

        class LinkInfoMsgData : public MessageData
        {
        public:
            shared_ptr<VirtualLink> vl;
            Json::Value *info;
            rtc::Event *msg_event;
            LinkInfoMsgData() : info(new Json::Value(Json::arrayValue)), msg_event(new rtc::Event(false, false)) {}
            LinkInfoMsgData(const LinkInfoMsgData &rhs)=delete;
            LinkInfoMsgData(LinkInfoMsgData &&rhs):vl(rhs.vl), info(rhs.info), msg_event(rhs.msg_event)
            {
                rhs.vl.reset();
                rhs.info = nullptr;
                rhs.msg_event=nullptr;
            }
            LinkInfoMsgData &operator=(const LinkInfoMsgData&)=delete;
            LinkInfoMsgData &operator=(LinkInfoMsgData &rhs)
            {
                if( this != &rhs)
                {
                    vl = rhs.vl;
                    rhs.vl.reset();
                    info = rhs.info;
                    rhs.info = nullptr;
                    msg_event = rhs.msg_event;
                    rhs.msg_event=nullptr;
                }
                return *this;
            }  
            virtual ~LinkInfoMsgData()
            {
                delete info;
                delete msg_event;
            }
        };

        BasicTunnel()=delete;
        BasicTunnel(
            unique_ptr<TunnelDesc> descriptor,
            shared_ptr<ControllerCommsChannel> ctrl_handle);
        BasicTunnel(const BasicTunnel &)=delete;
        BasicTunnel& operator=(const BasicTunnel &)=delete;
        BasicTunnel(BasicTunnel &&rhs);
        BasicTunnel& operator=(BasicTunnel &&rhs);
        ~BasicTunnel();

        int Configure(
            unique_ptr<TapDescriptor> tap_desc);

        shared_ptr<VirtualLink> CreateVlink(
            unique_ptr<PeerDescriptor> peer_desc, bool role,
            const vector<string> &ignored_list);

        TunnelDesc &Descriptor();

        string Fingerprint();

        string Name();

        string MacAddress();

        void StartConnections();

        shared_ptr<EpollChannel> TapChannel() { return tdev_; }

        void QueryInfo(
            Json::Value &tnl_info);

        void QueryLinkId(
            string &link_id);

        void QueryLinkInfo(
            Json::Value &vlink_info);

        void QueryLinkCas(
            const string &vlink_id,
            Json::Value &cas_info);

        void Shutdown();

        void Start();

        int RemoveLink(
            const string &vlink_id);
        //
        void VlinkReadComplete(
            const char *data,
            size_t data_len);
        //
        void TapReadComplete(
            Iob iob);
        // MessageHandler overrides
        void OnMessage(
            Message *msg) override;

        shared_ptr<VirtualLink> Vlink() { return vlink_; }

    private:

        void VLinkUp(
            string vlink_id);

        void VLinkDown(
            string vlink_id);

        rtc::Thread *SignalThread();
        rtc::Thread *NetworkThread();

        unique_ptr<TapDescriptor> tap_desc_;
        unique_ptr<TunnelDesc> descriptor_;
        shared_ptr<ControllerCommsChannel> ctrl_link_;
        unique_ptr<rtc::SSLIdentity> sslid_;
        unique_ptr<rtc::SSLFingerprint> local_fingerprint_;
        unique_ptr<rtc::Thread>worker_;
        shared_ptr<TapDev> tdev_;
        shared_ptr<VirtualLink> vlink_;
    };
} // namespace tincan
#endif // BASIC_TUNNEL_H_
