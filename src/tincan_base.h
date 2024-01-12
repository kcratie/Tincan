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
#ifndef TINCAN_BASE_H_
#define TINCAN_BASE_H_
#include <atomic>
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
#include <random>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>
#include "tincan_version.h"
namespace tincan
{
    using MacAddressType = std::array<uint8_t, 6>;
    using IP4AddressType = std::array<uint8_t, 4>;
    using std::array;
    using std::atomic_bool;
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
    using std::unique_ptr;
    using std::unordered_map;
    using std::vector;
    using std::weak_ptr;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    using std::this_thread::yield;
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
        TincanParameters(const string &socket_name,
                         const string &log_config,
                         const string &tunnel_id,
                         const bool verchk,
                         bool needs_help) : socket_name(socket_name), tunnel_id(tunnel_id), log_config(log_config), kVersionCheck(verchk), kNeedsHelp(needs_help || socket_name.empty() || tunnel_id.empty())
        {
        }
        const string socket_name;
        const string tunnel_id;
        const string log_config;
        const bool kVersionCheck;
        const bool kNeedsHelp;
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
    // TD (Type Displayer) decl only to cause syntax error 
    template <typename T> class TD;
} // namespace tincan
#endif // TINCAN_BASE_H_
