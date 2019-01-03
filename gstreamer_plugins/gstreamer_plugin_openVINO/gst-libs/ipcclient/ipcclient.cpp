/*
 *Copyright (C) 2018 Intel Corporation
 *
 *SPDX-License-Identifier: LGPL-2.1-only
 *
 *This library is free software; you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software Foundation;
 * version 2.1.
 *
 *This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library;
 * if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "Looper.h"
#include "iostream"
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include "AppProtocol.h"
#include <algorithm>

 #include <glib.h>
#include <gst/gst.h>
#include "../ws/wsclient.h"
#include <string.h>

#include "../safestringlib/include/safe_mem_lib.h"
#include "../safestringlib/include/safe_str_lib.h"

using namespace std;
#include <thread>
#include <string>

#ifdef __cplusplus
 extern "C" {
#endif
 
 struct _ipcclient{
      shared_ptr<Transceiver> pTrans;
      Looper* pLooper;
     struct sockaddr_un server;
     int sockfd;
     int id;
     GAsyncQueue *message_queue;
 };
 typedef struct _ipcclient IPCClient;
 
static void item_free_func(gpointer data)
{
       MessageItem *item = ( MessageItem *)data;
      if(item && (item->len>0)){
            g_free(item->data);
       }
       return;
}

 WsClientHandle wsclient_setup(const char *serverUri, int client_id)
 {
     IPCClient *ipcclient = (IPCClient *)g_new0 (IPCClient, 1);
     if(!ipcclient) {
         g_print("Failed to calloc IPCClient!\n");
         return 0;
     }

    if(!serverUri) {
        g_print("Error: invalid uri of socket server!\n");
        return 0;
    }
     ipcclient->sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
     if (ipcclient->sockfd < 0) {
        g_print("Error:  failed to open socket - uri = %s\n", serverUri);
        return 0;
    }

     ipcclient->server.sun_family = AF_UNIX;
     strcpy_s(ipcclient->server.sun_path, strlen(serverUri), serverUri);
     
     if (connect( ipcclient->sockfd, (struct sockaddr *) & ipcclient->server, sizeof(struct sockaddr_un)) < 0) {
         close( ipcclient->sockfd);
         g_print("Error: failed to connect with stream socket");
         return 0;
     }

     ipcclient->id = client_id;
     ipcclient->message_queue = g_async_queue_new_full (item_free_func);

     g_print("Connect to %s, pipe_id = %d\n",  serverUri , client_id);
     ipcclient->pTrans = make_shared<Transceiver>(ipcclient->sockfd);
     ipcclient->pLooper = new Looper(ipcclient->sockfd, ipcclient->pTrans, ipcclient->message_queue);
      ipcclient->pLooper->start();
     std::string sPipeID;
     ipcProtocol tInitMsg;
     tInitMsg.iType = 1;
     //tInitMsg.sPayload = "{ \"type\": 1 \"payload\": " + string(argv[2]) + "}";
     tInitMsg.sPayload = std::string("pipe_id=") + std::to_string(client_id);
     std::string sBuff = "";

     AppProtocol::format(tInitMsg, sBuff);
     ipcclient->pTrans->writeToSendBuffer(sBuff);
     ipcclient->pLooper->notify(ipcclient->pTrans);

     return (WsClientHandle)ipcclient;
 }
 
 void wsclient_send_data(WsClientHandle handle, char *data, int len)
 {
     IPCClient *ipcclient = (IPCClient *)handle;
 
     if(!handle) {
         g_print("Invalid IPCClientHandle!!!\n");
         return;
     }
     char id_info[4];
     g_snprintf(id_info, 4, "%4d",  ipcclient->id);
     std::string sBuff = "";
     std::string message;
     ipcProtocol tMsg;
     tMsg.iType = 0;
     //fill tMsg.sPayload with data
     tMsg.sPayload = std::string(id_info,4) + std::string(data, len);
     AppProtocol::format(tMsg, sBuff);
     ipcclient->pTrans->writeToSendBuffer(sBuff);
     ipcclient->pLooper->notify(ipcclient->pTrans);
 }

void wsclient_set_id(WsClientHandle handle,  int id)
{
    IPCClient *ipcclient = (IPCClient *)handle;

    if(!handle) {
        g_print("%s(): Invalid IPCClientHandle!!!\n",__func__);
        return;
    }
    ipcclient->id = id;
    return;
}

int wsclient_get_id(WsClientHandle handle)
{
    IPCClient *ipcclient = (IPCClient *)handle;

    if(!handle) {
        g_print("%s(): Invalid IPCClientHandle!!!\n",__func__);
        return -1;
    }
    return ipcclient->id;
}

/**
  * Pops data from the queue.
  * This function blocks until data become available.
  **/
MessageItem *wsclient_get_data(WsClientHandle handle)
{
    IPCClient *ipcclient = (IPCClient *)handle;
    if(!handle) {
        g_print("%s(): Invalid IPCClientHandle!!!\n",__func__);
        return NULL;
    }
    MessageItem *item =(MessageItem *)g_async_queue_pop(ipcclient->message_queue);

    return item;
}

 /**
   * Pops data from the queue.
   * If no data is received before end_time, NULL is returned.
   **/
 MessageItem *wsclient_get_data_timed(WsClientHandle handle)
 {
     IPCClient *ipcclient = (IPCClient *)handle;
     MessageItem *item = NULL;
     gint64 timeout_microsec = 400000; //400ms
     if(!handle) {
        g_print("%s(): Invalid IPCClientHandle!!!\n",__func__);
        return NULL;
    }
     item = (MessageItem *)g_async_queue_timeout_pop(ipcclient->message_queue, timeout_microsec);
     return item;
 }

void wsclient_free_item(MessageItem *item)
{
        item_free_func(item);
}
 void wsclient_destroy(WsClientHandle handle)
 {
     IPCClient *ipcclient = (IPCClient *)handle;
     if(!handle)
         return;

      if(ipcclient->pLooper) {
        ipcclient->pLooper->quit();
        delete ipcclient->pLooper;
        ipcclient->pLooper=NULL;
     }

    if(ipcclient->message_queue)
         g_async_queue_unref (ipcclient->message_queue);
 
     g_free(handle);
 }
 
#ifdef __cplusplus
 };
#endif

