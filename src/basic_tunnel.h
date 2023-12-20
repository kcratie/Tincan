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
    class BasicTunnel : public sigslot::has_slots<>
    {
    public:
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

        VirtualLink* CreateVlink(
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
            Json::Value &cas_info);

        void Start();

        void RemoveLink();
        //
        void VlinkReadComplete(
            const char *data,
            size_t data_len);
        //
        void TapReadComplete(
            Iob&& iob);

        VirtualLink* Vlink() { return vlink_.get(); }

    private:

        void OnVLinkUp(
            string vlink_id);

        void OnVLinkDown(
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
        unique_ptr<VirtualLink> vlink_;
    };
} // namespace tincan
#endif // BASIC_TUNNEL_H_
