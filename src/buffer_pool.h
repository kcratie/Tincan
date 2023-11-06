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
#ifndef BUFFER_POOL_H_
#define BUFFER_POOL_H_
#include "tincan_base.h"
#include "rtc_base/logging.h"
namespace tincan
{
    extern TincanParameters tp;
    class Iob
    {
    public:
        Iob() : iob_(new char[tp.kFrameBufferSz])
        {
        }
        Iob(const char *inp, size_t sz) : iob_(new char[tp.kFrameBufferSz])
        {
            data(inp, sz);
        }
        Iob(const Iob &rhs) : iob_(new char[tp.kFrameBufferSz])
        {
            *this = rhs;
        }
        Iob(Iob &&rhs) noexcept : iob_(nullptr)
        {
            *this = std::move(rhs);
        }
        Iob &operator=(const Iob &rhs)
        {
            if (this != &rhs)
            {
                len_ = std::min(capacity(), rhs.size());
                data(rhs.data(), len_);
            }
            return *this;
        }
        Iob &operator=(Iob &&rhs) noexcept
        {
            if (this != &rhs)
            {
                delete[] iob_;
                len_ = std::min(capacity(), rhs.size());
                iob_ = rhs.iob_;
                rhs.iob_ = nullptr;
                rhs.len_ = 0;
            }
            return *this;
        }
        ~Iob()
        {
            delete[] iob_;
        }
        size_t size() const noexcept { return len_; }
        void size(size_t sz) noexcept
        {
            if (sz < 0 || sz > tp.kFrameBufferSz)
            {
                RTC_LOG(LS_WARNING) << "Iob Resize out of range" << sz;
                return;
            }
            len_ = sz;
        }
        size_t capacity() noexcept { return tp.kFrameBufferSz; }
        char *buf()
        {
            if (!iob_)
                iob_ = new char[tp.kFrameBufferSz];
            return &iob_[0];
        }
        const char *data() const { return &iob_[0]; }
        void data(const char *inp, size_t sz)
        {
            if (sz > capacity())
            {
                RTC_LOG(LS_WARNING) << "Data larger than max buffer size" << sz << "/" << tp.kFrameBufferSz;
            }
            len_ = std::min(sz, capacity());
            if (!iob_)
                iob_ = new char[tp.kFrameBufferSz];
            std::copy(inp, inp + len_, buf());
        }

        char operator[](
            size_t pos)
        {
            return iob_[pos];
        }

    private:
        char *iob_;
        size_t len_ = {0};
    };

    // class BufferPool
    // {
    // public:
    //     ~BufferPool() {
    //         cout << "BufferPool dtor:" << this << endl;
    //     }

    //     BufferPool(size_t size = 1, size_t capacity = 2) : sz_(size), cap_(capacity)
    //     {
    //         cout << "BufferPool ctor:" << this << endl;
    //         if (sz_ > cap_)
    //             throw out_of_range("Initial size greater then max capacity");
    //         pool_ = make_unique<deque<Iob>>(sz_);
    //     }
    //     Iob get()
    //     {
    //         if (pool_->empty())
    //         {
    //             auto ccc = sz_ / 2 +1;
    //             size_t sz = std::min(ccc, cap_ - sz_);
    //             extend(sz);
    //         }
    //         if (pool_->empty())
    //             throw std::bad_alloc();
    //         Iob el = std::move(pool_->front());
    //         pool_->pop_front();
    //         return el;
    //     }

    //     void put(Iob&& iob)
    //     {
    //         if (pool_->size() < cap_)
    //         {

    //             iob.size(0);
    //             pool_->push_back(std::move(iob));
    //         }
    //     }

    //     void put(Iob& iob)
    //     {
    //         if (pool_->size() >= cap_)
    //             delete &iob;

    //         iob.size(0);
    //         pool_->push_back(iob);
    //     }

    // private:
    //     void extend(size_t sz)
    //     {
    //         for (size_t i = 0; i < sz; i++)
    //             pool_->emplace_back(Iob());
    //     }
    //     size_t sz_;
    //     size_t cap_;
    //     unique_ptr<deque<Iob>> pool_;
    // };

} // namespace tincan
#endif // BUFFER_POOL_H_