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

// wrapper C++ algo pipeline, so that it can be called by C
//
// This file will be compiled by C++ compiler
//

#include "yolov1tinyalgo.h"
#include "opticalflowtrackalgo.h"
#include "googlenetv2algo.h"
#include "mobilenetssdalgo.h"
#include "tracklpalgo.h"
#include "lprnetalgo.h"
#include "yolov2tinyalgo.h"
#include "reidalgo.h"
#include "genericalgo.h"
#include "sinkalgo.h"
#include "algopipeline.h"

#ifdef __cplusplus
extern "C" {
#endif


static AlgoRegister g_algoRegister;

#if 0
//default algo list
const static char *g_algo_name_str[ALGO_MAX_DEFAULT_NUM] = {
                ALGO_YOLOV1_TINY_NAME,
                ALGO_TRACKING_NAME,
                ALGO_GOOGLENETV2_NAME,
                ALGO_MOBILENET_SSD_NAME,,
                ALGO_TRACK_LP_NAME,
                ALGO_LPRNET_NAME,
                ALGO_YOLOV2_TINY_NAME,
                ALGO_REID_NAME,
                ALGO_SINK_NAME
};
#endif

static AlgoPipelineConfig algoTopologyDefault[] = {
    {0, ALGO_YOLOV1_TINY, -1,  1, {1}},
    {1, ALGO_OF_TRACK,        0,  1, {2}},
    {2, ALGO_GOOGLENETV2,  1,  1, {-1}},
};

static CvdlAlgoBase* algo_create(int type)
{
    CvdlAlgoBase* algo;
    const char *algoName = NULL;
    switch(type) {
        case ALGO_YOLOV1_TINY:
            algo = new Yolov1TinyAlgo;
            break;
        case ALGO_OF_TRACK:
            algo = new OpticalflowTrackAlgo;
            break;
        case ALGO_GOOGLENETV2:
            algo = new GoogleNetv2Algo;
            break;
         case ALGO_MOBILENET_SSD:
            algo = new MobileNetSSDAlgo;
            break;
         case ALGO_TRACK_LP:
            algo = new TrackLpAlgo;
            break;
         case ALGO_LPRNET:
            algo = new LPRNetAlgo;
            break;
         case ALGO_YOLOV2_TINY:
            algo = new Yolov2TinyAlgo;
            break;
         case ALGO_REID:
            algo = new ReidAlgo;
            break;
         case ALGO_SINK:
            algo = new SinkAlgo;
            break;
        default:
            algo =  NULL;
            break;
   };

     // type/id >= 8 is for generic algo
    if(type>=ALGO_MAX_DEFAULT_NUM) {
            algoName =g_algoRegister.get_algo_name(type);
            if(algoName) {
                algo = new GenericAlgo(algoName);
            } else {
                g_print("Error: cannot find algo in algolist, algo_id = %d\n", type);
                exit(1);
            }
   }

    if(algo)
        algo->mAlgoType = type;

   return algo;
}

//TODO: need support one algo link to multiple algos,  specified the algo's src pad 
static void algo_item_link(AlgoItem* from, AlgoItem* to, int index)
{
    CvdlAlgoBase *algoFrom, *algoTo;
    if(!from || !to || !from->algo || !to->algo) {
        GST_ERROR("algo link failed!\n");
        return;
    }
    algoFrom = static_cast<CvdlAlgoBase *>(from->algo);
    algoTo   = static_cast<CvdlAlgoBase *>(to->algo);
    algoFrom->algo_connect_with_index(algoTo, index);
}

static void algo_item_link_sink(AlgoItem *preItem[], int num, AlgoItem *sinkItem)
{
    CvdlAlgoBase *algoPre, *algoSink;
    SinkAlgo *sink;
    int i = 0;
    if(!preItem || !sinkItem || num==0) {
        GST_ERROR("algo link sink failed!\n");
        return;
    }
    algoSink = static_cast<CvdlAlgoBase*>(sinkItem->algo);
    sink = static_cast<SinkAlgo*>(sinkItem->algo);

    for(i=0;i<num;i++) {
          if(preItem[i]==NULL)
            continue;
          algoPre = static_cast<CvdlAlgoBase *>(preItem[i]->algo);
          // sinkalgo did't know how many preItem linked to it
          algoPre->algo_connect(algoSink);
          sink->set_linked_item(algoPre, i);
     }
    return;
}

static int get_str_count(gchar *str, gchar *token, int len)
{
    int count = 0;
    gchar *p = str;

    if(!str || !token || !len)
        return 0;

    p=g_strstr_len(p,len,token);
    while(p){
        count++;
        p++;
        p=g_strstr_len(p,len,token);
    }
    return count;
}

void algo_pipeline_config_destroy(AlgoPipelineConfig *config)
{
    if(config)
        g_free(config);
    return;
}

// create a AlgoPipelineConfig from description string
// The format is:
//  case 1.   "yolov1tiny ! opticalflowtrack ! googlenetv2"
//  case 2.   "detection ! track name=tk ! tk.vehicle_classification  ! tk.person_face_detection ! face_recognication"
AlgoPipelineConfig *algo_pipeline_config_create(gchar *desc, int *num)
{
    AlgoPipelineConfig *config = NULL;
    gchar *p = desc, *pDot, *pName, *pParentName, *descStrip;
    gchar **items = NULL;// **names;
    int count = 0, i, j, index, len, out_index = 1;// nameNum = 0, nameIndex = 0;
    //gboolean newSubBranch = FALSE;
    if(!desc)
        return NULL;

    //register default algo
    g_algoRegister.register_init();
    g_algoRegister.register_dump();

    //TODO: need to support case 2 better

    g_print("algo pipeline congig description: %s\n", desc);
    // delete the blank char
    descStrip = g_strstrip(desc);
    p = descStrip;
    items = g_strsplit(p, "!", 6);/* 6 items at most */

    //nameNum = get_str_count(p, "name=");
    //if(nameNum > 0) {
    //    names = g_new0(gchar *, nameNum);
    //}

    count = g_strv_length(items);
    if(count==0){
        g_print("Invalid algo pipeline description: %s\n",p);
        return NULL;
    }
    for(i=0;i<count;i++)
        items[i] = g_strstrip(items[i]);

    config = g_new0 (AlgoPipelineConfig, count);
    for(i=0;i<count;i++) {
        // get curId
        config[i].curId = i;
        p = items[i];

        // get nextNum
        pName = g_strstr_len(p, strlen(p),"name=");
        if(pName) {
            // get branch algo name list
            //names[nameIndex++] = g_strndup(pName+5,g_strlen(pName+5));
            config[i].nextNum = get_str_count(descStrip, pName+5, strlen(descStrip)) - 1;
            // set nextId
            index = 0;
            pName = pName + 5;
            for(j=0;j<count;j++) {
                if(!strncmp(items[j],pName,strlen(pName)))
                    config[i].nextId[index++] = j;
            }
        } else {
            config[i].nextNum = 1;
            // next is the end or other sub branch, set -1
            if((i==count-1) || (g_strstr_len(items[i+1], strlen(items[i+1]),"."))) {
                config[i].nextId[0] = -1 * out_index;
                out_index ++;
            } else {
                config[i].nextId[0] = i+1;
            }
        }

        // set preId
        // get parent branch name
        pDot = g_strstr_len(p, strlen(items[i]),".");
        if(pDot) {
            // It is a sub branch
            pName = p;
            p = pDot + 1;
            len = pDot - pName;
            // find parent branch id
            for(j=0;j<count;j++) {
                pParentName = g_strstr_len(items[j], strlen(items[j]),"name=");
                if(!pParentName)
                    continue;
                if(!strncmp(pParentName+5,pName, len)) {
                    config[i].preId = j;
                    break;
                }
            }
        } else {
            if(i==0) {
                config[i].preId = -1;
            } else {
                config[i].preId = i - 1;
            }
        }

        // get algo type
 #if 1
        config[i].curType = g_algoRegister.get_algo_id(p);
        if(config[i].curType==-1)
            exit(1);
#else
        for(n=0;n<ALGO_MAX_DEFAULT_NUM-1;n++) {
            if(strlen(p) != strlen(g_algo_name_str[n]))
                continue;
            if(!strncmp(p, g_algo_name_str[n], strlen(g_algo_name_str[n]))) {
                config[i].curType = n;
                break;
            }
         }
#endif
    }
    *num = count;
    g_strfreev(items);
    return config;
}

static void algo_pipeline_print(AlgoPipelineHandle handle)
{
     AlgoPipeline *pipeline = (AlgoPipeline *) handle;
      CvdlAlgoBase* algo = (CvdlAlgoBase *)pipeline->first;
      CvdlAlgoBase* last = (CvdlAlgoBase *)pipeline->last;

      //TODO: print tree-shape algo pipeline
      g_print("algopipeline chain: ");
      while(algo && algo!=last && last) {
                g_print("%s ->  ",g_algoRegister.get_algo_name(algo->mAlgoType));
                algo = algo->mNext[0];//TODO
      }
      if(algo)
        g_print("%s\n",g_algoRegister.get_algo_name(algo->mAlgoType));
}

AlgoPipelineHandle algo_pipeline_create(AlgoPipelineConfig* config, int num)
{
    AlgoPipelineHandle handle = 0;
    AlgoPipeline *pipeline = (AlgoPipeline *)g_new0(AlgoPipeline,1);
    AlgoItem *item = NULL;
    int preId, nextId = 0;
    AlgoItem *preSinkItem[MAX_PIPELINE_OUT_NUM] = {NULL};

    pipeline->algo_chain = (AlgoItem *)g_new0(AlgoItem, num+1);  //add sinkalgo
    pipeline->algo_num   = num+1;
    pipeline->first = NULL;

    int i, j;
    // set id/type/algo/nextItem/preItem for every Item
    for(i=0; i< num; i++) {
        item = pipeline->algo_chain + i;
        item->id = config[i].curId;
        item->type = config[i].curType;
        item->algo = static_cast<void *>(algo_create(config[i].curType));
        preId = config[i].preId;
        if(preId>=0){
            item->preItem = pipeline->algo_chain + preId;
        }else {
            item->preItem = NULL;
        }
        for(j=0;j<config[i].nextNum;j++) {
            nextId = config[i].nextId[j];
            if(nextId>=0){
                item->nextItem[j] = pipeline->algo_chain + nextId;
            }else {
                item->nextItem[j] = NULL;
            }
        }
    }

// link algo and get first and last algo
#if 0
    // link from previous item to current item 
    for(i=0; i< num; i++) {
        item = pipeline->algo_chain + i;
        if(item->preItem){
            algo_item_link(item->preItem, item, 0);
        }else {
            // here preId = -1, means it is the first algo item
            // Note: we should only have one first algo
            pipeline->first = item->algo;
        }
        for(j=0;j<config[i].nextNum;j++) {
            nextId = config[i].nextId[j];
            if(nextId>=0){
                // next algo has not been created
                //algo_item_link(item, item->nextItem[j]);
            }else {
                nextId = (-1 * nextId) - 1;
                if(nextId < MAX_PIPELINE_OUT_NUM)
                     preSinkItem[nextId] = item;
                else
                    g_print("Error when algo pipe create: output algo nextId = %d\n", nextId);
            }
        }
    }
    #else
     // link from current item to next item 
    for(i=0; i< num; i++) {
        item = pipeline->algo_chain + i;
        if(!item->algo)
            continue;
        if(!item->preItem){
            // Note: we should only have one first algo
            pipeline->first = item->algo;
        }
        for(j=0;j<MAX_DOWN_STREAM_ALGO_NUM;j++) {
            if(item->nextItem[j])
                algo_item_link(item, item->nextItem[j], j);
        }
        for(j=0;j<config[i].nextNum;j++) {
            nextId = config[i].nextId[j];
            if(nextId>=0){
                // next algo has not been created
                //algo_item_link(item, item->nextItem[j]);
            }else {
                nextId = (-1 * nextId) - 1;
                if(nextId < MAX_PIPELINE_OUT_NUM)
                     preSinkItem[nextId] = item;
                else
                    g_print("Error when algo pipe create: output algo nextId = %d\n", nextId);
            }
        }
    }
    #endif

     //create sinkalgo
     item = pipeline->algo_chain + num;
     item->id = num;
     item->type = ALGO_SINK;
     item->algo = static_cast<void *>(algo_create(ALGO_SINK));
     algo_item_link_sink(preSinkItem, MAX_PIPELINE_OUT_NUM, item);
     pipeline->last = item->algo;

    handle = (AlgoPipelineHandle)pipeline;
    algo_pipeline_print(handle);
    return handle;
}

AlgoPipelineHandle algo_pipeline_create_default()
{
    return algo_pipeline_create(algoTopologyDefault, 
        sizeof(algoTopologyDefault)/sizeof(AlgoPipelineConfig));
}
void algo_pipeline_destroy(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    int i;
    CvdlAlgoBase* algo;
    if(pipeline==NULL) {
        GST_ERROR("%s - algo pipeline handle is NULL!\n", __func__);
        return;
    }
    for(i=0; i< pipeline->algo_num; i++){
        algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[i].algo);
        delete algo;
    }
    g_free(pipeline->algo_chain);
    g_free(pipeline);
    //register_reset();
}
void algo_pipeline_set_caps(AlgoPipelineHandle handle, int algo_id, GstCaps* caps)
{
     AlgoPipeline *pipeline = (AlgoPipeline *) handle;
     CvdlAlgoBase* algo = NULL;
     if(pipeline==NULL) {
         GST_ERROR("%s - algo pipeline handle is NULL!\n", __func__);
         return;
     }

     if(algo_id>=pipeline->algo_num) {
        GST_ERROR("algo_id is invalid: %d", algo_id);
        return;
     }

    algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[algo_id].algo);
    if(!algo->mCapsInited)
        algo->set_data_caps(caps);
    algo->mCapsInited = true;
}

void algo_pipeline_set_caps_all(AlgoPipelineHandle handle, GstCaps* caps)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;
    int i;
    if(pipeline==NULL) {
        GST_ERROR("%s - algo pipeline handle is NULL!\n", __func__);
        return;
    }
    for(i=0; i< pipeline->algo_num; i++){
        algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[i].algo);
        if(!algo->mCapsInited)
            algo->set_data_caps(caps);
        algo->mCapsInited = true;
    } 
}


void algo_pipeline_start(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;

    int i;
    if(pipeline==NULL) {
        GST_ERROR("%s - algo pipeline handle is NULL!\n", __func__);
        return;
    }
    for(i=0; i< pipeline->algo_num; i++){
        algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[i].algo);
        algo->start_algo_thread();
    }
}
void algo_pipeline_stop(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;
    int i;

    if(pipeline==NULL) {
        GST_ERROR("%s - algo pipeline handle is NULL!\n", __func__);
        return;
    }

    for(i=0; i< pipeline->algo_num; i++){
        algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[i].algo);
        algo->stop_algo_thread();
    }
}
void algo_pipeline_put_buffer(AlgoPipelineHandle handle, GstBuffer *buf,  guint w, guint h)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;

    if(pipeline==NULL) {
        gst_buffer_unref(buf);
        g_print("algo pipeline handle is NULL!\n");
        return;
    }
    algo = static_cast<CvdlAlgoBase *>(pipeline->first);
    if(!algo){
        gst_buffer_unref(buf);
        g_print("failed to put_buffer: algo is NULL");
        return;
    }
    //g_print("%s() - GstBuffer = %p\n",__func__,  buf);
    algo->queue_buffer(buf, w, h);
}


int algo_pipeline_get_input_queue_size(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;

    if(pipeline==NULL) {
        GST_ERROR("algo pipeline handle is NULL!\n");
        return 0;
    }
    algo = static_cast<CvdlAlgoBase *>(pipeline->first);
    if(!algo){
        GST_ERROR("failed to put_buffer: algo is NULL");
        return 0;
    }

    return algo->get_in_queue_size();
}


int algo_pipeline_get_all_queue_size(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;
    int size = 0,i;

    if(pipeline==NULL) {
        GST_ERROR("algo pipeline handle is NULL!\n");
        return 0;
    }
    #if 0
    algo = static_cast<CvdlAlgoBase *>(pipeline->first);
    while(algo) {
        size += algo->get_in_queue_size() + algo->get_out_queue_size();
        size += algo->mInferCnt;
        algo=algo->mNext[0];
    }
    #else
    for(i=0;i<pipeline->algo_num;i++) {
        algo = static_cast<CvdlAlgoBase *>(pipeline->algo_chain[i].algo);
        if(algo) {
            size += algo->get_in_queue_size() + algo->get_out_queue_size();
            size += algo->mInferCnt;
        }
    }
    #endif
    return size;
}

void algo_pipeline_get_buffer(AlgoPipelineHandle handle, GstBuffer **buf)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;

    *buf = NULL;
    //TODO: need support multiple output buffer
    if(pipeline) {
        algo = static_cast<CvdlAlgoBase *>(pipeline->last);
        if(algo)
            *buf = algo->dequeue_buffer();
    }
}

void algo_pipeline_flush_buffer(AlgoPipelineHandle handle)
{
    AlgoPipeline *pipeline = (AlgoPipeline *) handle;
    CvdlAlgoBase* algo = NULL;

    //TODO: need support multiple output buffer
    if(pipeline) {
        algo = static_cast<CvdlAlgoBase *>(pipeline->last);
        g_print("%s() - put EOS buffer!\n",__func__);
        // send a empty buffer to make this get_buffer_task exit 
        algo->queue_out_buffer(NULL);
    }
}

const char* algo_pipeline_get_name(guint  id)
{
        return g_algoRegister.get_algo_name(id);
}

#ifdef __cplusplus
};
#endif

