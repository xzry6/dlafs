/*
 * Copyright (c) 2018 Intel Corporation
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
#ifndef __ALGO_PIPELINE_H__
#define __ALGO_PIPELINE_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <gst/gstbuffer.h>
#include <gst/gstpad.h>
#include "algoregister.h"
#include "private.h"

typedef void* AlgoHandle;
typedef struct _AlgoPipelineConfig{
    int curId; 
    int curType;/* current algo id */
    int preId; /* previous algo id */
    int nextNum; /* next algo number */
    int nextId[MAX_DOWN_STREAM_ALGO_NUM]; /* next algo id*/
}AlgoPipelineConfig;

// Note: 
//   1. It only has one -1 in the preId field
//   2. For nextId, we will have multiple algo
//                    -1  - the first out algo
//                    -2  -  the second out algo
//      If it is output, all value in nextId are same
//        {1, ALGO_OF_TRACK, 1, 2, {2, 3}}
//        {2, ALGO_OF_TRACK, 1, 1, {-2}}
//
//static AlgoPipelineConfig algoTopologyDefault[] = {
//    {0, ALGO_YOLOV1_TINY, -1,  1, {1}},
//    {1, ALGO_OF_TRACK,        0,  1, {2}},
//    {2, ALGO_GOOGLENETV2,  1,  1, {-1}},
//};

typedef struct _AlgoItem AlgoItem;
struct _AlgoItem{
    int id;
    int type;
    //CvdlAlgoBase *algo;
    void *algo;
    AlgoItem *nextItem[MAX_DOWN_STREAM_ALGO_NUM];
    AlgoItem *preItem;
    AlgoItem *preItemSink[MAX_PRE_SINK_ALGO_NUM]; //only for sinkalgo
};

typedef struct _AlgoPipeline{
    AlgoItem *algo_chain;
    int algo_num;
    void *first;
    void *last;//[MAX_PIPELINE_OUT_NUM];
}AlgoPipeline;

typedef void* AlgoPipelineHandle;


AlgoPipelineHandle algo_pipeline_create(AlgoPipelineConfig* config, int num);
AlgoPipelineHandle algo_pipeline_create_default();
void algo_pipeline_config_destroy(AlgoPipelineConfig *config);
AlgoPipelineConfig *algo_pipeline_config_create(gchar *desc, int *num);

void algo_pipeline_destroy(AlgoPipelineHandle handle);
void algo_pipeline_set_caps(AlgoPipelineHandle handle, int algo_id, GstCaps* caps);
void algo_pipeline_set_caps_all(AlgoPipelineHandle handle, GstCaps* caps);
void algo_pipeline_start(AlgoPipelineHandle handle);
void algo_pipeline_stop(AlgoPipelineHandle handle);
void algo_pipeline_put_buffer(AlgoPipelineHandle handle, GstBuffer *buf, guint w, guint h);
void algo_pipeline_get_buffer(AlgoPipelineHandle handle, GstBuffer **buf);

const char* algo_pipeline_get_name(guint  mAlgoType);

void algo_pipeline_flush_buffer(AlgoPipelineHandle handle);
int algo_pipeline_get_input_queue_size(AlgoPipelineHandle handle);
int algo_pipeline_get_all_queue_size(AlgoPipelineHandle handle);

#ifdef __cplusplus
};
#endif
#endif
