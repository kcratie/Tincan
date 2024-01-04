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
    class Iob
    {
    public:
        static const uint16_t kFrameBufferSz = 1500;
        Iob() : iob_(new char[kFrameBufferSz])
        {
        }
        Iob(const char *inp, size_t sz) : iob_(new char[kFrameBufferSz])
        {
            data(inp, sz);
        }
        Iob(const Iob &rhs) = delete;
        // Iob(const Iob &rhs) : iob_(new char[kFrameBufferSz])
        // {
        //     *this = rhs;
        // }
        Iob(Iob &&rhs) noexcept : iob_(nullptr)
        {
            *this = std::move(rhs);
        }
        Iob &operator=(const Iob &rhs) = delete;
        // Iob &operator=(const Iob &rhs)
        // {
        //     if (this != &rhs)
        //     {
        //         len_ = std::min(capacity(), rhs.size());
        //         data(rhs.data(), len_);
        //     }
        //     return *this;
        // }
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
            if (sz < 0 || sz > kFrameBufferSz)
            {
                RTC_LOG(LS_WARNING) << "Iob Resize out of range" << sz;
                return;
            }
            len_ = sz;
        }
        size_t capacity() noexcept { return kFrameBufferSz; }
        char *buf()
        {
            if (!iob_)
                iob_ = new char[kFrameBufferSz];
            return &iob_[0];
        }
        const char *data() const { return &iob_[0]; }
        void data(const char *inp, size_t sz)
        {
            if (sz > capacity())
            {
                RTC_LOG(LS_WARNING) << "Data larger than max buffer size" << sz << "/" << kFrameBufferSz;
            }
            len_ = std::min(sz, capacity());
            if (!iob_)
                iob_ = new char[kFrameBufferSz];
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

template<typename Tb>
class BufferPool
{
public:
    static const uint16_t kPoolCapacity = 256;
	~BufferPool() = default;
	BufferPool() : cap_(kPoolCapacity), pool_(cap_)
	{}
	BufferPool(size_t capacity) : cap_(capacity), pool_(cap_)
	{}
	BufferPool(const BufferPool& rhs) = delete;
	BufferPool& operator=(const BufferPool& rhs) = delete;

	Tb get() noexcept
	{
		lock_guard<mutex> lg(excl_);
		max_used_ = std::max(++sz_, max_used_);
		if (pool_.empty())
		{
            yield();
			return Tb();
		}
		Tb el = std::move(pool_.front());
		pool_.pop_front();
		return el;
	}

	void put(Tb&& iob) noexcept
	{
		lock_guard<mutex> lg(excl_);
		sz_ = sz_ == 0 ? 0 : --sz_;
		if (pool_.size() < cap_)
		{
			iob.size(0);
			pool_.push_back(std::move(iob));
		}
        else
            iob.~Iob();
	}

	void put(Tb& iob) = delete;
    size_t max_used() noexcept {return max_used_;}
private:
	size_t sz_ = { 0 };
	size_t max_used_ = { 0 };
	size_t cap_;
	mutex excl_;
	deque<Tb> pool_;
};
} // namespace tincan
#endif // BUFFER_POOL_H_