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

#include <inference_engine.hpp>
#include <ie_plugin_dispatcher.hpp>
#include <ie_plugin_config.hpp>
#include <ie_iexecutable_network.hpp>
#include <vpu/vpu_plugin_config.hpp>

#include <gst/gstbuffer.h>
#include <gst/gstpad.h>
#include <gst/gstinfo.h>

#include "ieloader.h"

#ifdef WIN32
#define HDDL_PLUGIN "HDDLPlugin.dll"
#else
#define HDDL_PLUGIN "libHDDLPlugin.so"
#endif

#ifdef __linux__
#include <linux/version.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,20)
#define CV_SPLIT_NO_OPENCL
#endif
#endif

#define IECALLNORET(call)                                       \
if (InferenceEngine::OK != (call)) {                            \
    std::cout << #call " failed: " << resp.msg << std::endl;    \
    std::exit(EXIT_FAILURE);                                    \
}

using namespace InferenceEngine;
using namespace std;

std::mutex requestCreateMutex;

IELoader::IELoader()
{

}

IELoader::~IELoader()
{
    //TODO: how to release IE?
}

GstFlowReturn IELoader::set_device(InferenceEngine::TargetDevice dev)
{
    mTargetDev = dev;
    switch (dev) {
    case InferenceEngine::TargetDevice::eCPU:
        mIEPlugin = InferenceEnginePluginPtr("libMKLDNNPlugin.so");
        break;
    case InferenceEngine::TargetDevice::eGPU:
        mIEPlugin = InferenceEnginePluginPtr("libclDNNPlugin.so");
        break;
    case InferenceEngine::TargetDevice::eMYRIAD:
        mIEPlugin = InferenceEnginePluginPtr(HDDL_PLUGIN);
        break;
    default:
        GST_ERROR("Not support device [ %d ]", (int)dev);
        return GST_FLOW_ERROR;
        break;
    }
    return GST_FLOW_OK;
}
GstFlowReturn IELoader::read_model(std::string strModelXml,
        std::string strModelBin, int modelType)
{
    std::unique_lock<std::mutex> _lock(requestCreateMutex);

    InferenceEngine::ResponseDesc resp;
    InferenceEngine::StatusCode ret = InferenceEngine::StatusCode::OK;

    InferenceEngine::CNNNetReader netReader = InferenceEngine::CNNNetReader();
    netReader.ReadNetwork(strModelXml);
    if (!netReader.isParseSuccess()) {
        GST_ERROR("read model %s fail", strModelXml.c_str());
        return GST_FLOW_ERROR;
    }

    netReader.ReadWeights(strModelBin);
    if (!netReader.isParseSuccess()) {
        GST_ERROR("read model %s fail", strModelBin.c_str());
        return GST_FLOW_ERROR;
    }

    InferenceEngine::CNNNetwork cnnNetwork = netReader.getNetwork();
    InferenceEngine::InputsDataMap networkInputs;
    InferenceEngine::OutputsDataMap networkOutputs;

    // Input data precision
    networkInputs = cnnNetwork.getInputsInfo();
    g_return_val_if_fail(networkInputs.empty()==FALSE, GST_FLOW_ERROR);
    auto firstInputInfo = networkInputs.begin();
    g_return_val_if_fail(firstInputInfo != networkInputs.end(), GST_FLOW_ERROR);
    firstInputInfo->second->setInputPrecision(InferenceEngine::Precision::U8);
    mFirstInputName = firstInputInfo->first;
    firstInputInfo->second->setLayout(Layout::NCHW);//HW: NCHW, SW: NHWC

    // Output data precision
    networkOutputs = cnnNetwork.getOutputsInfo();
    g_return_val_if_fail(!networkOutputs.empty(), GST_FLOW_ERROR);
    auto firstOutputInfo = networkOutputs.begin();
    g_return_val_if_fail(firstOutputInfo != networkOutputs.end(), GST_FLOW_ERROR);
    firstOutputInfo->second->precision = mOutputPrecision;
    mFirstOutputName = firstOutputInfo->first;

    std::map<std::string, std::string> networkConfig;
    networkConfig[InferenceEngine::PluginConfigParams::KEY_LOG_LEVEL]
        = InferenceEngine::PluginConfigParams::LOG_INFO;
    //networkConfig[VPU_CONFIG_KEY(HW_STAGES_OPTIMIZATION)] = CONFIG_VALUE(YES);

    switch(modelType) {
        case IE_MODEL_DETECTION:
#ifdef WIN32
            //IE for windows do not support NETWORK_CONFIG yet
            networkConfig[VPU_CONFIG_KEY(INPUT_NORM)] = "255.0";
#else
            networkConfig[VPU_CONFIG_KEY(NETWORK_CONFIG)] = "data=data,scale=64";
#endif
            break;
        default:
            break;
   }

    // Executable Network for inference engine
    ret = mIEPlugin->LoadNetwork(mExeNetwork, cnnNetwork, networkConfig, &resp);
    if (InferenceEngine::StatusCode::OK != ret) {
        GST_ERROR("mIEPlugin->LoadNetwork FAIL, ret = %d", ret);
        return GST_FLOW_ERROR;
    }

    // First create 4 request for current thread.
    for (int r = 0; r < REQUEST_NUM; r++) {
        ret = mExeNetwork->CreateInferRequest(mInferRequest[r], &resp);
        mRequestEnable[r] = true;
        if (InferenceEngine::OK != ret) {
            std::cout << "CreateInferRequest failed: " << resp.msg << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
    return GST_FLOW_OK;
}

GstFlowReturn IELoader::convert_input_to_blob(const cv::UMat& img,
    InferenceEngine::Blob::Ptr& inputBlobPtr)
{
    if (inputBlobPtr->precision() != mInputPrecision) {
        GST_ERROR("loadImage error: blob must have only U8 precision");
        return GST_FLOW_ERROR;
    }

    cv::Mat src;
    int w = (int)inputBlobPtr->dims()[0];
    int h = (int)inputBlobPtr->dims()[1];
    if (img.cols != w || img.rows != h) {
        GST_ERROR("WARNNING: resize from %dx%d to %dx%d !\n", src.cols, src.rows, w, h);
        cv::resize(img, src, cv::Size(w, h));
    } else {
        src = img.getMat(0);
    }

    auto numBlobChannels = inputBlobPtr->dims()[2];
    size_t numImageChannels = src.channels();
    if (numBlobChannels != numImageChannels && numBlobChannels != 1) {
        GST_ERROR("numBlobChannels != numImageChannels && numBlobChannels != 1");
        return GST_FLOW_ERROR;
    }

    if (InferenceEngine::Precision::U8 == mInputPrecision) {
        InferenceEngine::TBlob<unsigned char>::Ptr inputBlobDataPtr = 
            std::dynamic_pointer_cast<InferenceEngine::TBlob<unsigned char> >(inputBlobPtr);

        if (inputBlobDataPtr != nullptr) {
            unsigned char *inputDataPtr = inputBlobDataPtr->data();

            // Src data has been converted to be BGR planar format
            int nPixels = w * h * numBlobChannels;
            for (int i = 0; i < nPixels; i++)
                inputDataPtr[i] = src.data[i];
        }
    }else{
        GST_ERROR("InferenceEngine::Precision not support: %d", (int)mInputPrecision);
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

int IELoader::get_enable_request()
{
    std::unique_lock<std::mutex> lk(mRequstMutex);
    int target_id = -1;

    mCondVar.wait(lk, [this, &target_id]{
        for(int i = 0; i< REQUEST_NUM; i++) {
            if (mRequestEnable[i]) {
                target_id = i;
                return true;
            }
        }
        return false;
    });

    mRequestEnable[target_id] = false;
    return target_id;
}

GstFlowReturn IELoader::get_input_size(int *w, int *h, int *c)
{
    GstFlowReturn ret = GST_FLOW_ERROR;
    InferenceEngine::ResponseDesc resp;

    int reqestId = get_enable_request();
    if(reqestId >= 0)
    {
        InferenceEngine::IInferRequest::Ptr inferRequestAsyn = mInferRequest[reqestId];
        InferenceEngine::Blob::Ptr inputBlobPtr;
        IECALLNORET(inferRequestAsyn->GetBlob(mFirstInputName.c_str(), inputBlobPtr, &resp));
        *w = (int)inputBlobPtr->dims()[0];
        *h = (int)inputBlobPtr->dims()[1];
        *c = (int)inputBlobPtr->dims()[2];
        ret = GST_FLOW_OK;
    }

    {
        std::unique_lock<std::mutex> lk(mRequstMutex);
        mRequestEnable[reqestId] = true;
        mCondVar.notify_all();
    }
    return ret;
}

GstFlowReturn IELoader::do_inference_async(CvdlAlgoData *algoData, uint64_t frmId, int objId,
                                                  cv::UMat &src, AsyncCallback cb)
{
    InferenceEngine::ResponseDesc resp;

    int reqestId = get_enable_request();
    if(reqestId >= 0)
    {
        InferenceEngine::IInferRequest::Ptr inferRequestAsyn = mInferRequest[reqestId];
        InferenceEngine::Blob::Ptr inputBlobPtr;
        IECALLNORET(inferRequestAsyn->GetBlob(mFirstInputName.c_str(), inputBlobPtr, &resp));

        // Load images.
        if (src.empty()) {
            GST_ERROR("input image empty!!!");
            return GST_FLOW_ERROR;
        }

        convert_input_to_blob(src, inputBlobPtr);

        // send a request
        IECALLNORET(inferRequestAsyn->StartAsync(&resp));

        // Start thread listen to result
        auto WaitAsync = [this, algoData, frmId, objId, cb](InferenceEngine::IInferRequest::Ptr inferRequestAsyn, int reqestId)
        {
            //CvdlAlgoBase *algo;
            InferenceEngine::ResponseDesc resp;
            IECALLNORET(inferRequestAsyn->Wait(InferenceEngine::IInferRequest::WaitMode::RESULT_READY, &resp));
            InferenceEngine::Blob::Ptr resultBlobPtr;
            IECALLNORET(inferRequestAsyn->GetBlob(mFirstOutputName.c_str(), resultBlobPtr, &resp));

            if (this->mOutputPrecision == InferenceEngine::Precision::FP32)
            {
                CvdlAlgoBase *algo = algoData->algoBase;
                GST_LOG("==========WaitAsync - do_inference_async begin: algo = %p(%p), algoData = %p\n",
                    algo, algoData->algoBase, algoData);
                g_usleep(10);
                algo->parse_inference_result(resultBlobPtr, sizeof(float), algoData, objId);
                GST_LOG("==========WaitAsync - do_inference_async finish: algo = %p(%p), algoData = %p\n",
                    algo,algoData->algoBase, algoData);
            } else {
                GST_ERROR("Don't support other output precision except FP32!");
                return;
            }

            GST_LOG("Got inference result: frmId = %ld",frmId);
            // put result into out_queue
            cb(algoData);

            {
                std::unique_lock<std::mutex> lk(mRequstMutex);
                mRequestEnable[reqestId] = true;
                mCondVar.notify_all();
            }
        };

        //WaitAsync(inferRequestAsyn, reqestId);
        std::thread t1(WaitAsync, inferRequestAsyn, reqestId);
        t1.detach();
    }

    return GST_FLOW_OK;
}