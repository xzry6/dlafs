/*
 * Copyright (c) 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VPP_DEFS_H_
#define _VPP_DEFS_H_

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum CRCFormat{
    CRC_FORMAT_BGR = 0,
    CRC_FORMAT_BGR_PLANNAR = 1,
    CRC_FORMAT_GRAY = 2,
};

typedef enum {
    VPP_CRC_PARAM,
    VPP_BLEND_PARAM,
} VppParamType;

typedef struct {
    VppParamType type;
    guint32 src_w;
    guint32 src_h;
    guint32 crop_x;
    guint32 crop_y;
    guint32 crop_w;
    guint32 crop_h;
    guint32 dst_w;
    guint32 dst_h;
} VppCrcParam;

typedef struct {
    VppParamType type;
    guint32 x;
    guint32 y;
    guint32 w;
    guint32 h;
} VppBlendParam;


#ifdef __cplusplus
}
#endif

#endif /*  _VPP_DEFS_H_ */