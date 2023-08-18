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
#ifndef TINCAN_BASE_H_
#define TINCAN_BASE_H_
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <array>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stack>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include "tincan_version.h"
namespace tincan
{
    using MacAddressType = std::array<uint8_t, 6>;
    using IP4AddressType = std::array<uint8_t, 4>;
    using std::array;
    using std::cout;
    using std::deque;
    using std::endl;
    using std::exception;
    using std::hash;
    using std::istringstream;
    using std::list;
    using std::lock_guard;
    using std::make_pair;
    using std::make_shared;
    using std::make_unique;
    using std::map;
    using std::memcpy;
    using std::milli;
    using std::move;
    using std::mutex;
    using std::ostringstream;
    using std::out_of_range;
    using std::pair;
    using std::shared_ptr;
    using std::stack;
    using std::string;
    using std::stringstream;
    using std::thread;
    using std::unique_ptr;
    using std::unordered_map;
    using std::vector;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    using iob_t = vector<char>;

    class InputParser
    {
    public:
        InputParser(int &argc, char **argv)
        {
            for (int i = 1; i < argc; ++i)
                this->tokens.push_back(std::string(argv[i]));
        }

        const std::string &getCmdOption(const std::string &option) const
        {
            std::vector<std::string>::const_iterator itr;
            itr = std::find(this->tokens.begin(), this->tokens.end(), option);
            if (itr != this->tokens.end() && ++itr != this->tokens.end())
            {
                return *itr;
            }
            static const std::string empty_string("");
            return empty_string;
        }

        bool cmdOptionExists(const std::string &option) const
        {
            return std::find(this->tokens.begin(), this->tokens.end(), option) != this->tokens.end();
        }

    private:
        std::vector<std::string> tokens;
    };
    struct TincanParameters
    {
    public:
        TincanParameters()
            : kVersionCheck(false), kNeedsHelp(false), kUdpPort(5800), kLinkConcurrentAIO(2)
        {
        }
        void SetCliOpts(
            int argc,
            char **argv)
        {
            InputParser cli(argc, argv);
            if (cli.cmdOptionExists("-h"))
            {
                kNeedsHelp = true;
                return;
            }
            if (cli.cmdOptionExists("-v"))
            {
                kVersionCheck = true;
                return;
            }
            socket_name = cli.getCmdOption("-s");
            if (socket_name.empty())
            {
                kNeedsHelp = true;
            }
        }
        static const uint16_t kMaxMtuSize = 1500;
        static const uint16_t kTapHeaderSize = 2;
        static const uint16_t kEthHeaderSize = 14;
        static const uint16_t kEthernetSize = kEthHeaderSize + kMaxMtuSize;
        static const uint16_t kTapBufferSize = kMaxMtuSize;
        static const uint8_t kFT_DTF = 0x0A;
        static const uint8_t kFT_FWD = 0x0B;
        static const uint8_t kFT_ICC = 0x0C;
        static const uint16_t kDtfMagic = 0x0A01;
        static const uint16_t kFwdMagic = 0x0B01;
        static const uint16_t kIccMagic = 0x0C01;
        static const char kCandidateDelim = ':';
        const char *const kIceUfrag = "+001EVIOICEUFRAG";
        const char *const kIcePwd = "+00000001EVIOICEPASSWORD";
        const char *const kLocalHost = "127.0.0.1";
        const char *const kLocalHost6 = "::1";
        bool kVersionCheck;
        bool kNeedsHelp;
        uint16_t kUdpPort;
        string socket_name;
        uint8_t kLinkConcurrentAIO;
    };
    ///////////////////////////////////////////////////////////////////////////////
    template <typename InputIter>
    string ByteArrayToString(
        InputIter first,
        InputIter last,
        uint32_t line_breaks = 0,
        bool use_sep = false,
        char sep = ':',
        bool use_uppercase = true)
    {
        assert(sizeof(*first) == 1);
        ostringstream oss;
        oss << std::hex << std::setfill('0');
        if (use_uppercase)
            oss << std::uppercase;
        int i = 0;
        while (first != last)
        {
            oss << std::setw(2) << static_cast<int>(*first++);
            if (use_sep && first != last)
                oss << sep;
            if (line_breaks && !(++i % line_breaks))
                oss << endl;
        }
        return oss.str();
    }
    // Fixme: Doesn't handle line breaks
    template <typename OutputIter>
    size_t StringToByteArray(
        const string &src,
        OutputIter first,
        OutputIter last,
        bool sep_present = false)
    {
        assert(sizeof(*first) == 1);
        size_t count = 0;
        istringstream iss(src);
        char val[3];
        while (first != last && iss.peek() != std::istringstream::traits_type::eof())
        {
            size_t nb = 0;
            iss.get(val, 3);
            (*first++) = (uint8_t)std::stoi(val, &nb, 16);
            count++;
            if (sep_present)
                iss.get();
        }
        return count;
    }

    template <typename T> // declaration only for TD;
    class TD;             // TD == "Type Displayer"
} // namespace tincan
#endif // TINCAN_BASE_H_
