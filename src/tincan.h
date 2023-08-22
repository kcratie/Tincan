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
#ifndef TINCAN_TINCAN_H_
#define TINCAN_TINCAN_H_
#include "tincan_base.h"
#include "rtc_base/event.h"
#include "single_link_tunnel.h"
#include "tunnel_threads.h"
#include "controller_comms.h"
#include "epoll_engine.h"
#include "rtc_base/logging.h"
#include "rtc_base/log_sinks.h"
#include <signal.h>
namespace tincan
{
    class Tincan : public EpollChannelMsgHandler,
                   public sigslot::has_slots<>
    {
    public:
        Tincan();
        Tincan(Tincan &) = delete;
        ~Tincan() = default;
        Tincan &operator=(Tincan &) = delete;

        void CreateVlink(
            const Json::Value &link_desc,
            const TincanControl &control);

        void CreateTunnel(
            const Json::Value &tnl_desc,
            Json::Value &tnl_info);

        void QueryLinkStats(
            const Json::Value &link_desc,
            Json::Value &node_info);

        void QueryTunnelInfo(
            const Json::Value &tnl_desc,
            Json::Value &node_info);

        void RemoveTunnel(
            const Json::Value &tnl_desc);

        void RemoveVlink(
            const Json::Value &link_desc);

        void QueryLinkCas(
            const Json::Value &link_desc,
            Json::Value &cas_info);

        void OnLocalCasUpdated(
            string link_id,
            string lcas);

        virtual void operator()(
            unique_ptr<vector<char>> msg) override;
        void Run();

    private:
        bool IsTunnelExisit(
            const string &tnl_id);
        void OnStop();
        void Shutdown();
        void RegisterDataplane();
        static void onStopHandler(int signum);
        static void generate_core(int signum);
        // Dispatch Interface
        using TCDSIP = void (Tincan::*)(TincanControl &);
        void ConfigureLogging(TincanControl &control);
        void CreateLink(TincanControl &control);
        void CreateTunnel(TincanControl &control);
        void Echo(TincanControl &control);
        void QueryLinkStats(TincanControl &control);
        void QueryTunnelInfo(TincanControl &control);
        void QueryCandidateAddressSet(TincanControl &control);
        void RemoveLink(TincanControl &control);
        void RemoveTunnel(TincanControl &control);
        //
        std::mutex tunnels_mutex_;
        std::mutex inprogess_controls_mutex_;
        bool exit_flag_;
        TunnelThreads threads_;
        EpollEngine epoll_eng_;
        unordered_map<string, TCDSIP> dispatch_map_;
        unordered_map<string, LoggingSeverity> log_levels_;
        unordered_map<string, shared_ptr<BasicTunnel>> tunnels_;
        shared_ptr<ControllerCommsChannel> channel_;
        unique_ptr<FileRotatingLogSink> log_sink_;
        unordered_map<string, unique_ptr<TincanControl>> inprogess_controls_;

        static Tincan *self_;
    };
} // namespace tincan
#endif // TINCAN_TINCAN_H_
