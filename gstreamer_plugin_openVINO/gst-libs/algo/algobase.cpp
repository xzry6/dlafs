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
#include <gst/gst.h>
#include <gst/video/video.h>
#include "algobase.h"
#include "algopipeline.h"

using namespace std;
using namespace cv;

#define QUEUE_ELEMENT_MAX_NUM 100
#define LOG_DIR "/home/hddls_log/"

static void algo_enter_thread (GstTask * task, GThread * thread, gpointer user_data)
{
    GST_DEBUG("enter algo thread.");
}

static void algo_leave_thread (GstTask * task, GThread * thread, gpointer user_data)
{
    GST_DEBUG("leave algo thread.");
}

static void try_process_algo_data(CvdlAlgoData *algoData)
{
    bool allObjDone = true;
    CvdlAlgoBase *hddlAlgo = algoData->algoBase;

    // check if this frame has been done, which contains multiple objects
    for(unsigned int i=0; i<algoData->mObjectVecIn.size();i++)
        if(!(algoData->mObjectVecIn[i].flags & CVDL_OBJECT_FLAG_DONE))
            allObjDone = false;

    // if all objects are done, then push it into output queue
    if(allObjDone) {
        // clear input objectData
        algoData->mObjectVecIn.clear();
        if(hddlAlgo->postCb)
               hddlAlgo->postCb(algoData);

        std::vector<ObjectData> &objectVec = algoData->mObjectVec;
        if(objectVec.size()>0) {
            //put algoData;
            GST_LOG("algo %d - output GstBuffer = %p(%d)\n",
                   hddlAlgo->mAlgoType, algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT(algoData->mGstBuffer));
            hddlAlgo->mNext->mInQueue.put(*algoData);
        } else {
            GST_LOG("algo %d - unref GstBuffer = %p(%d)\n",
                hddlAlgo->mAlgoType, algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT(algoData->mGstBuffer));
            gst_buffer_unref(algoData->mGstBuffer);
            delete algoData;
        }
    }
}

 void process_one_object(CvdlAlgoData *algoData, ObjectData &objectData, int objId)
{
    GstFlowReturn ret = GST_FLOW_OK;
    GstBuffer *ocl_buf = NULL;
    CvdlAlgoBase *hddlAlgo = algoData->algoBase;
    gint64 start, stop;

   VideoRect crop = { (uint32_t)objectData.rectROI.x, 
                      (uint32_t)objectData.rectROI.y,
                      (uint32_t)objectData.rectROI.width,
                      (uint32_t)objectData.rectROI.height};

    if(crop.width<=0 || crop.height<=0 || crop.x<0 || crop.y<0) {
        GST_ERROR("classfication: crop = (%d,%d) %dx%d", crop.x, crop.y, crop.width, crop.height);
        objectData.flags |= CVDL_OBJECT_FLAG_DONE;
        try_process_algo_data(algoData);
        return;
    }
    start = g_get_monotonic_time();
    hddlAlgo->mImageProcessor.process_image(algoData->mGstBuffer,NULL,&ocl_buf,&crop);
    stop = g_get_monotonic_time();
    hddlAlgo->mImageProcCost += stop - start;
    if(ocl_buf==NULL) {
        g_print("Failed to do image process!");
        objectData.flags |= CVDL_OBJECT_FLAG_DONE;
        try_process_algo_data(algoData);
        return;
    }
    GST_LOG("algo %d - get Ocl GstBuffer = %p(%d)\n",
             hddlAlgo->mAlgoType, ocl_buf, GST_MINI_OBJECT_REFCOUNT(ocl_buf));

    OclMemory *ocl_mem = NULL;
    ocl_mem = ocl_memory_acquire (ocl_buf);
    if(ocl_mem==NULL){
        g_print("Failed get ocl_mem after image process!");
        if(ocl_buf)
            gst_buffer_unref(ocl_buf);
        objectData.flags |= CVDL_OBJECT_FLAG_DONE;
        try_process_algo_data(algoData);
        return;
    }
    objectData.oclBuf = ocl_buf;
    //test
    //hddlAlgo->save_buffer(ocl_mem->frame.getMat(0).ptr(), hddlAlgo->mInputWidth,
    //    hddlAlgo->mInputHeight,3,algoData->mFrameId*1000 + objId, 1,
    //    lgo_pipeline_get_name(hddlAlgo->mAlgoType));

    // result callback function
    auto onHddlResult = [&objectData](void* data)
    {
        CvdlAlgoData *algoData = static_cast<CvdlAlgoData*> (data);
        CvdlAlgoBase *hddlAlgo = algoData->algoBase;

        hddlAlgo->mInferCnt--;
        objectData.flags |= CVDL_OBJECT_FLAG_DONE;

        // check and process algoData
        try_process_algo_data(algoData);
    };

    // ASync detect, directly return after pushing request.
    start = g_get_monotonic_time();
    ret = hddlAlgo->mIeLoader.do_inference_async((void *)algoData, algoData->mFrameId,objId,
                                                        ocl_mem->frame, onHddlResult);

    // this ocl will not use, free it here
    GST_LOG("algo %d - unref Ocl GstBuffer = %p(%d)\n",
                hddlAlgo->mAlgoType, objectData.oclBuf, GST_MINI_OBJECT_REFCOUNT(objectData.oclBuf));
    gst_buffer_unref(objectData.oclBuf);
    objectData.oclBuf  = NULL;

    stop = g_get_monotonic_time();
    hddlAlgo->mInferCost += (stop - start);
    hddlAlgo->mInferCnt++;
    hddlAlgo->mInferCntTotal++;

    if (ret!=GST_FLOW_OK) {
        GST_ERROR("IE: detect FAIL");
    }
    return;
}

/*
 * This is the main function of cvdl task:
 *     1. NV12 --> BGR_Planar
 *     2. async inference
 *     3. parse inference result and put it into in_queue of next algo
 */
static void base_hddl_algo_func(gpointer userData)
{
    CvdlAlgoBase *hddlAlgo = static_cast<CvdlAlgoBase*> (userData);
    CvdlAlgoData *algoData = new CvdlAlgoData;
    const char*algo_name = algo_pipeline_get_name(hddlAlgo->mAlgoType);
    GST_LOG("\n%s:%s - new an algoData = %p\n", __func__, algo_name, algoData);

    if(!hddlAlgo->mNext) {
        GST_LOG("The %s algo's next algo is NULL", algo_name);
    }

    if(!hddlAlgo->mInQueue.get(*algoData)) {
        GST_WARNING("InQueue is empty!");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    if(algoData->mGstBuffer==NULL) {
        GST_WARNING("Invalid buffer!!!");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    // bind algoTask into algoData, so that can be used when sync callback
    algoData->algoBase = static_cast<CvdlAlgoBase *>(hddlAlgo);

    if(algoData->mGstBuffer==NULL) {
        GST_ERROR("%s() - get null buffer\n", __func__);
        return;
    }
    GST_LOG("%s() - algo = %p, algoData->mFrameId = %ld\n", __func__,
            hddlAlgo, algoData->mFrameId);
    GST_LOG("%s() - algo  %d, get one buffer, GstBuffer = %p, refcout = %d, queueSize = %d,"\
            "algoData = %p, algoBase = %p\n",
        __func__,hddlAlgo->mAlgoType, algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT (algoData->mGstBuffer),
        hddlAlgo->mInQueue.size(), algoData, algoData->algoBase);

    // get input data and process it here, put the result into algoData
    // NV12-->BGR_Plannar
    algoData->mObjectVecIn = algoData->mObjectVec;
    algoData->mObjectVec.clear();
    for(unsigned int i=0; i< algoData->mObjectVecIn.size(); i++) {
        algoData->mObjectVecIn[i].flags =0;
        process_one_object(algoData, algoData->mObjectVecIn[i], i);
    }

    hddlAlgo->mFrameDoneNum++;
}


void push_algo_data(CvdlAlgoData* &algoData)
{
    std::vector<ObjectData> &objectVec = algoData->mObjectVec;
    CvdlAlgoBase *cvAlgo = algoData->algoBase;

    // Must release intermedia buffer
    gst_buffer_unref(algoData->mGstBufferOcl);

    // Not tracking data need to pass to next algo component
    if(objectVec.size()==0) {
        GST_LOG("push_algo_data - unref GstBuffer = %p(%d)\n",
            algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT(algoData->mGstBuffer));
        gst_buffer_unref(algoData->mGstBuffer);
        delete algoData;
        return;
    }

    GST_LOG("push_algo_data - pass down GstBuffer = %p(%d)\n",
         algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT(algoData->mGstBuffer));

    //debug
    for(size_t i=0; i< objectVec.size(); i++) {
        GST_LOG("%d - cv_output-%ld-%ld: prob = %f, label = %s, rect=(%d,%d)-(%dx%d), score = %f\n",
            cvAlgo->mFrameDoneNum, algoData->mFrameId, i, objectVec[i].prob, objectVec[i].label.c_str(),
            objectVec[i].rect.x, objectVec[i].rect.y,
            objectVec[i].rect.width, objectVec[i].rect.height, objectVec[i].score);
    }
    cvAlgo->mNext->mInQueue.put(*algoData);
}

/*
 * This is the main function of cv task:
 *     1. NV12 --> Gray/BGR
 *     2. cv(track) processsing
 *     3. queue algoData into in_queue of next algo
 */
static void base_cv_algo_func(gpointer userData)
{
    CvdlAlgoBase *cvAlgo = static_cast<CvdlAlgoBase*> (userData);
    CvdlAlgoData *algoData = new CvdlAlgoData;
    gint64 start, stop;

    const char*algo_name = algo_pipeline_get_name(cvAlgo->mAlgoType);
    GST_LOG("\n%s:%s - new an algoData = %p\n", __func__, algo_name, algoData);

    if(!cvAlgo->mNext) {
        GST_WARNING("Algo %d: the next algo is NULL, return!", cvAlgo->mAlgoType);
        delete algoData;
        return;
    }

    if(!cvAlgo->mInQueue.get(*algoData)) {
        GST_WARNING("Algo %d:InQueue is empty!", cvAlgo->mAlgoType);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        delete algoData;
        return;
    }
    if(algoData->mGstBuffer==NULL) {
        GST_WARNING("Algo %d: Invalid buffer!!!", cvAlgo->mAlgoType);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        delete algoData;
        return;
    }
    start = g_get_monotonic_time();

    // bind algoTask into algoData, so that can be used when sync callback
    algoData->algoBase = cvAlgo;
    cvAlgo->mInferCnt=1;

    GST_LOG("%s() - Algo = %p, algoData->mFrameId = %ld\n",
            __func__, cvAlgo, algoData->mFrameId);
    GST_LOG("%s() - algo %d get one buffer, GstBuffer = %p, refcout = %d, queueSize = %d, algoData = %p, algoBase = %p\n",
        __func__, cvAlgo->mAlgoType, algoData->mGstBuffer, GST_MINI_OBJECT_REFCOUNT (algoData->mGstBuffer),
        cvAlgo->mInQueue.size(), algoData, algoData->algoBase);


    // get input data and process it here, put the result into algoData
    // NV12-->BGR
    GstBuffer *ocl_buf = NULL;
    VideoRect crop = {0,0, (unsigned int)cvAlgo->mImageProcessorInVideoWidth,
                           (unsigned int)cvAlgo->mImageProcessorInVideoHeight};
    cvAlgo->mImageProcessor.process_image(algoData->mGstBuffer,NULL, &ocl_buf, &crop);
    if(ocl_buf==NULL) {
        GST_WARNING("Failed to do image process!");
        cvAlgo->mInferCnt=0;
        gst_buffer_unref(algoData->mGstBuffer);
        delete algoData;
        return;
    }
    OclMemory *ocl_mem = NULL;
    ocl_mem = ocl_memory_acquire (ocl_buf);
    if(ocl_mem==NULL){
        GST_WARNING("Failed get ocl_mem after image process!");
        cvAlgo->mInferCnt=0;
        gst_buffer_unref(algoData->mGstBuffer);
        delete algoData;
        return;
    }
    algoData->mGstBufferOcl = ocl_buf;

    //test
    //cvAlgo->save_buffer(ocl_mem->frame.getMat(0).ptr(), trackAlgo->mInputWidth,
    //                   cvAlgo->mInputHeight,1,cvAlgo->mFrameId, 1, algo_name);


    // Tracking every object, and get predicts.
   if(cvAlgo->postCb)
        cvAlgo->postCb(algoData);

    stop = g_get_monotonic_time();
    cvAlgo->mImageProcCost += (stop - start);

    // push data if possible
    push_algo_data(algoData);
    cvAlgo->mInferCnt=0;
    cvAlgo->mInferCntTotal++;
    cvAlgo->mFrameDoneNum++;
}

CvdlAlgoBase::CvdlAlgoBase(PostCallback  cb, guint cvdlType )
    :mCapsInited(false), mCvdlType(cvdlType), mTask(NULL), mIeInited(false),
     mInputWidth(0), mInputHeight(0), mImageProcessorInVideoWidth(0),
     mImageProcessorInVideoHeight(0), mInCaps(NULL), mOclCaps(NULL), 
     mNext(NULL), mPrev(NULL), postCb(cb), mInferCnt(0), mInferCntTotal(0),
     mFrameIndex(0), mFrameDoneNum(0), mImageProcCost(1), mInferCost(1),
     mFrameIndexLast(0), mObjIndex(0),  fpOclResult(NULL)
{
    g_rec_mutex_init (&mMutex);

    if(cvdlType == CVDL_TYPE_NONE) {
        mTask = NULL;
        return;
    }

    /* Create task for this algo */
    if(cvdlType==CVDL_TYPE_DL) {
        mTask = gst_task_new (base_hddl_algo_func, this, NULL);
     } else {
         mTask = gst_task_new (base_cv_algo_func, this, NULL);
     }
     gst_task_set_lock (mTask, &mMutex);
     gst_task_set_enter_callback (mTask, algo_enter_thread, NULL, NULL);
     gst_task_set_leave_callback (mTask, algo_leave_thread, NULL, NULL);
}


CvdlAlgoBase::~CvdlAlgoBase()
{
    wait_work_done();
    if(mTask) {
        if((gst_task_get_state(mTask) == GST_TASK_STARTED) ||
          (gst_task_get_state(mTask) == GST_TASK_PAUSED)) {
                gst_task_set_state(mTask, GST_TASK_STOPPED);
                mInQueue.flush();
                gst_task_join(mTask);
        }
        gst_object_unref(mTask);
        mTask = NULL;
    }
    mInQueue.close();
    mNext = mPrev = NULL;
    if(mInCaps)
        gst_caps_unref(mInCaps);

    if(fpOclResult)
        fclose(fpOclResult);
    //gst_object_unref(mPool);
}

void CvdlAlgoBase::algo_connect(CvdlAlgoBase *algoTo)
{
    this->mNext = algoTo;
    algoTo->mPrev = this;
}

void CvdlAlgoBase::start_algo_thread()
{
    if(mTask)
        gst_task_start(mTask);
}

void CvdlAlgoBase::stop_algo_thread()
{
    if(mTask)
        gst_task_set_state(mTask, GST_TASK_STOPPED);
    // remove all intem in the Queue
    while(mInQueue.size()>0) {
            CvdlAlgoData algoData;
            algoData.mGstBuffer=NULL;
            if(mInQueue.get(algoData)) {
                   if(algoData.mGstBuffer)
                        gst_buffer_unref(algoData.mGstBuffer);
            }
    }
    mInQueue.flush();
    wait_work_done();
    if(mTask)
        gst_task_join(mTask);
}

void CvdlAlgoBase::queue_buffer(GstBuffer *buffer, guint w, guint h)
{
    CvdlAlgoData algoData(buffer);
    algoData.mFrameId = mFrameIndex++;
    if(buffer)
        algoData.mPts = GST_BUFFER_TIMESTAMP (buffer);

    ObjectData objData;
    objData.rect = cv::Rect(0,0, w,h);
    objData.rectROI =  cv::Rect(0,0, w,h);
    algoData.mObjectVec.push_back(objData);
    mInQueue.put(algoData);
    GST_LOG("InQueue size = %d\n", mInQueue.size());
}

void CvdlAlgoBase::queue_out_buffer(GstBuffer *buffer)
{
    CvdlAlgoData algoData(buffer);
    algoData.mFrameId = mFrameIndex++;
    if(buffer)
        algoData.mPts = GST_BUFFER_TIMESTAMP (buffer);
    
    mInQueue.put(algoData);
    GST_LOG("InQueue size = %d\n", mInQueue.size());
}
int CvdlAlgoBase::get_in_queue_size()
{
    return mInQueue.size();
}

int CvdlAlgoBase::get_out_queue_size()
{
    //we did't have mOutQueue currently.
    return 0;
}

GstFlowReturn CvdlAlgoBase::init_ieloader(const char* modeFileName, guint ieType)
{
    GstFlowReturn ret = GST_FLOW_OK;
    if(mIeInited)
        return ret;
    mIeInited = true;

    ret = mIeLoader.set_device(InferenceEngine::TargetDevice::eMYRIAD);
    if(ret != GST_FLOW_OK){
        GST_ERROR("IE failed to set device be eMYRIAD!");
        return GST_FLOW_ERROR;
    }
    // Load different Model based on different device.
    std::string strModelXml(modeFileName);
    std::string tmpFn = strModelXml.substr(0, strModelXml.rfind("."));
    std::string strModelBin = tmpFn + ".bin";
    GST_DEBUG("Algo %d: Model bin = %s", mAlgoType, strModelBin.c_str());
    GST_DEBUG("Algo %d: Model xml = %s", mAlgoType, strModelXml.c_str());
    ret = mIeLoader.read_model(strModelXml, strModelBin, ieType);
    
    if(ret != GST_FLOW_OK){
        g_print("IELoder failed to read model!");
        return GST_FLOW_ERROR;
    }

    int w, h, c;
    ret = mIeLoader.get_input_size(&w, &h, &c);
    if(ret==GST_FLOW_OK) {
        g_print("Algo %d: parse out the input size whc= %dx%dx%d\n", mAlgoType, w, h, c);
        mInputWidth = w;
        mInputHeight = h;
    }
   return ret;
}

void  CvdlAlgoBase::init_dl_caps(GstCaps* incaps)
{
    if(mInCaps)
        gst_caps_unref(mInCaps);
    mInCaps = gst_caps_copy(incaps);

    if(!mInputWidth || !mInputHeight) {
          int c=0;
          mIeLoader.get_input_size(&mInputWidth, &mInputHeight, &c);
    }

    //Supposed that IE only accept BGR_Plannar input format
    //int oclSize = mInputWidth * mInputHeight * 3;
    mOclCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "BGR", NULL);
    gst_caps_set_simple (mOclCaps, "width", G_TYPE_INT, mInputWidth, "height",
                         G_TYPE_INT, mInputHeight, NULL);

    mImageProcessor.ocl_init(incaps, mOclCaps, IMG_PROC_TYPE_OCL_CRC, CRC_FORMAT_BGR_PLANNAR);
    mImageProcessor.get_input_video_size(&mImageProcessorInVideoWidth,
                                         &mImageProcessorInVideoHeight);
    gst_caps_unref (mOclCaps);
}

void CvdlAlgoBase::save_buffer(unsigned char *buf, int w, int h, int p, int id, int bPlannar, char *info)
{
    char filename[128];
    sprintf(filename, "%s/%s-%dx%dx%d-%d.rgb",LOG_DIR, info,w,h,p,id);

    if(bPlannar) {
        int size = w*h;
        char *rgb = (char *)g_new0(char, w*h*p);
        for(int i=0;i<size;i++) {
            for(int j=0; j<p; j++)
                rgb[3*i + j] = buf[size * j + i];
        }
        FILE *fp = fopen (filename, "wb");
        if (fp) {
            fwrite (rgb, 1, w*h*p, fp);
            fclose (fp);
        }
        g_free(rgb);
    } else {
        FILE *fp = fopen (filename, "wb");
        if (fp) {
            fwrite (buf, 1, w*h*p, fp);
            fclose (fp);
        }
    }
}
void CvdlAlgoBase::save_image(unsigned char *buf, int w, int h, int p, int bPlannar, char *info)
{
    char filename[128];
    if(fpOclResult==NULL){
        sprintf(filename, "%s/%s-%dx%dx%d.rgb",LOG_DIR, info,w,h,p);
        fpOclResult = fopen (filename, "wb");
    }
     if(fpOclResult==NULL)
        return;

    if(bPlannar) {
        int size = w*h;
        char *rgb = (char *)g_new0(char, w*h*p);
        for(int i=0;i<size;i++) {
            for(int j=0; j<p; j++)
                rgb[3*i + j] = buf[size * j + i];
        }
         fwrite (rgb, 1, w*h*p, fpOclResult);
         g_free(rgb);
    } else {
         fwrite (buf, 1, w*h*p, fpOclResult);
    }
}


void CvdlAlgoBase::print_objects(std::vector<ObjectData> &objectVec) {
    for(size_t i=0; i< objectVec.size(); i++) {
        g_print("objId = %d: prob = %f, label = %s, rect=(%d,%d)-(%dx%d), ROI=(%d,%d)-(%dx%d)\n",
            objectVec[i].id, objectVec[i].prob, objectVec[i].label.c_str(),
            objectVec[i].rect.x, objectVec[i].rect.y,
            objectVec[i].rect.width, objectVec[i].rect.height,
            objectVec[i].rectROI.x, objectVec[i].rectROI.y,
            objectVec[i].rectROI.width, objectVec[i].rectROI.height);
    }
}
