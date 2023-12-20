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

#define TINCAN_MAIN 1
#include "tincan_base.h"
#include "tincan.h"
namespace tincan
{    
    BufferPool<Iob> bp(1024);
}
using namespace tincan;
std::atomic_bool Tincan::exit_flag_;

int main(int argc, char **argv)
{
    int rv = 0;
    try
    {
        auto const tp = [](int &argc, char **argv) mutable
        {
            InputParser cli(argc, argv);
            return TincanParameters(cli.getCmdOption("-s"),
                                    cli.getCmdOption("-l"),
                                    cli.getCmdOption("-t"),
                                    cli.cmdOptionExists("-v"),
                                    cli.cmdOptionExists("-h"));
        }(argc, argv);
        if (tp.kVersionCheck)
        {
            cout << kTincanVerMjr << "."
                 << kTincanVerMnr << "."
                 << kTincanVerRev << "."
                 << kTincanVerBld << endl;
        }
        else if (tp.kNeedsHelp)
        {
            std::cout << "-v\t\tDisplay version number." << endl
                      << "-s SOCKETNAME\t\tThe controler's Unix Domain Socket name" << endl
                      << "-h\t\tHelp menu" << endl;
        }
        else
        {
            Tincan tc(tp);
            tc.Run();
        }
    }
    catch (exception &e)
    {
        rv = -1;
        std::cerr << e.what() << endl;
    }
    return rv;
}
