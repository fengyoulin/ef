// Copyright (c) 2018-2020 The EFramework Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef _UTIL_HEADER_
#define _UTIL_HEADER_

#define CAST_PARENT_PTR(ptr, parent_type, field_name) \
((parent_type*)((char*)ptr-(char*)&((parent_type*)0)->field_name))

inline size_t ef_resize(size_t size, size_t min) __attribute__((always_inline));

inline size_t ef_resize(size_t size, size_t min)
{
    if (size < min) {
        size = min;
    }

    size -= 1;
    size |= (size >> 1);
    size |= (size >> 2);
    size |= (size >> 4);
    size |= (size >> 8);
    size |= (size >> 16);
#if __x86_64__
    size |= (size >> 32);
#endif

    return size + 1;
}

#endif
