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
#ifdef min
#undef min
#endif //
#ifdef max
#undef max
#endif //
#include "rtc_base/ssl_identity.h"
#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/strings/json.h"
#include "tapdev.h"
#include "tincan_exception.h"
#include "tunnel_descriptor.h"
#include "virtual_link.h"
#include "tunnel_threads.h"
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
            unique_ptr<iob_t> frm;
        };
        class LinkInfoMsgData : public MessageData
        {
        public:
            shared_ptr<VirtualLink> vl;
            Json::Value info;
            rtc::Event msg_event;
            LinkInfoMsgData() : info(Json::arrayValue), msg_event(false, false) {}
            ~LinkInfoMsgData() = default;
        };
        class LinkMsgData : public MessageData
        {
        public:
            shared_ptr<VirtualLink> vl;
            rtc::Event msg_event;
            LinkMsgData() : msg_event(false, false)
            {
            }
            ~LinkMsgData() = default;
        };

        class TapMessageData : public MessageData
        {
        public:
            unique_ptr<iob_t> iob_;
            TapMessageData(unique_ptr<iob_t> iob) : iob_(std::move(iob))
            {
            }
            ~TapMessageData() = default;
        };

        BasicTunnel(
            unique_ptr<TunnelDesc> descriptor,
            shared_ptr<ControllerCommsChannel> ctrl_handle,
            TunnelThreads *thread_pool);

        ~BasicTunnel() = default;

        void Configure(
            unique_ptr<TapDescriptor> tap_desc,
            const vector<string> &ignored_list);

        shared_ptr<VirtualLink> CreateVlink(
            unique_ptr<PeerDescriptor> peer_desc);

        TunnelDesc &Descriptor();

        string Fingerprint();

        string Name();

        string MacAddress();

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

        void RemoveLink(
            const string &vlink_id);
        //
        void VlinkReadComplete(
            uint8_t *data,
            uint32_t data_len,
            VirtualLink &vlink);
        //
        void TapReadComplete(
            iob_t *iob_rd);
        // MessageHandler overrides
        void OnMessage(
            Message *msg) override;

    private:
        void SetIgnoredNetworkInterfaces(
            const vector<string> &ignored_list);

        unique_ptr<VirtualLink> CreateVlink(
            unique_ptr<PeerDescriptor> peer_desc,
            cricket::IceRole ice_role);

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
        TunnelThreads *threads_;
        rtc::BasicNetworkManager net_manager_;
        shared_ptr<TapDev> tdev_;
        shared_ptr<VirtualLink> vlink_;
    };
} // namespace tincan
#endif // BASIC_TUNNEL_H_
