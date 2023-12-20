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
#ifndef TINCAN_TINCAN_H_
#define TINCAN_TINCAN_H_
#include "tincan_base.h"
#include "rtc_base/event.h"
#include "basic_tunnel.h"
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
        Tincan(const TincanParameters &tp);
        Tincan(const Tincan &) = delete;
        ~Tincan() = default;
        Tincan &operator=(Tincan &) = delete;

        void CreateTunnel(
            const Json::Value &tnl_desc,
            Json::Value &tnl_info);

        bool CreateVlink(
            TincanControl &control);

        void QueryLinkStats(
            Json::Value &stat_info);

        void QueryTunnelInfo(
            const Json::Value &tnl_desc,
            Json::Value &node_info);

        void RemoveVlink(
            const Json::Value &link_desc);

        void QueryLinkCas(
            const Json::Value &link_desc,
            Json::Value &cas_info);

        void OnLocalCasUpdated(
            uint64_t control_id,
            string lcas);

        virtual void operator()(
            unique_ptr<vector<char>> msg) override;
        void Run();

    private:
        void OnStop();
        void RegisterDataplane();
        static void onStopHandler(int signum);
        static void generate_core(int signum);
        // Dispatch Interface
        using TCDSIP = void (Tincan::*)(TincanControl &);
        void CreateTunnel(TincanControl &control);
        void CreateLink(TincanControl &control);
        void QueryTunnelInfo(TincanControl &control);
        void QueryLinkStats(TincanControl &control);
        void Echo(TincanControl &control);
        void QueryCandidateAddressSet(TincanControl &control);
        void RemoveLink(TincanControl &control);
        void ConfigureLogging(TincanControl &control);
        //
        const TincanParameters &tp_;
        static std::atomic_bool exit_flag_;
        unordered_map<string, TCDSIP> dispatch_map_;
        unordered_map<string, LoggingSeverity> log_levels_;
        unique_ptr<FileRotatingLogSink> log_sink_;
        EpollEngine epoll_eng_;
        shared_ptr<ControllerCommsChannel> channel_;
        std::mutex inprogess_controls_mutex_;
        unordered_map<uint64_t, unique_ptr<TincanControl>> inprogess_controls_;
        vector<string> if_list_;
        unique_ptr<BasicTunnel> tunnel_;
    };
} // namespace tincan
#endif // TINCAN_TINCAN_H_
