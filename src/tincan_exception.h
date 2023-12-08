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
#ifndef TINCAN_EXCEPTION_H_
#define TINCAN_EXCEPTION_H_
#include "tincan_base.h"
#include <errno.h>

namespace tincan
{
    class TincanException : virtual public exception
    {
    protected:
        string emsg;

    public:
        TincanException(const string &arg, const char *file, int line);
        TincanException() = default;
        TincanException(const TincanException &) = default;
        TincanException(TincanException &&) = default;
        TincanException &operator=(const TincanException &) = default;
        TincanException &operator=(TincanException &&) = default;

        virtual ~TincanException() = default;
        virtual const char *what() const noexcept override
        {
            return emsg.c_str();
        }
    };

#define TCEXCEPT(ExtendedErrorInfo) TincanException(ExtendedErrorInfo, __FILE__, __LINE__);
} // namespace tincan
#endif // TINCAN_EXCEPTION_H_
