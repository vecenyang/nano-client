/******************************************************************************
 *
 * (c) 2020 Copyright, Real-Time Innovations, Inc. (RTI)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ******************************************************************************/ 

#include "nano/nano_core.h"

#ifdef NANO_HAVE_NET_WIFININA

#if NANO_FEAT_TRANSPORT_IPV4 || \
    NANO_FEAT_TRANSPORT_IPV6

#ifdef IP_ADDR4
    #define set_ip_addr(dst_, src_) \
        IP_ADDR4(&(dst_), (src_)[0], (src_)[1], (src_)[2], (src_)[3])
#elif defined(IP4_ADDR4)
    #define set_ip_addr(dst_, src_) \
        IP4_ADDR4(&(dst_), (src_)[0], (src_)[1], (src_)[2], (src_)[3])
#else
    #define set_ip_addr(dst_, src_) \
        NANO_OSAPI_Memory_copy(&(dst_).addr, (src_), 4)
#endif

#if NANO_FEAT_TRANSPORT_PLUGIN_UDPV4

#include "Arduino.h"
#include "utility/wifi_spi.h"
#include "utility/server_drv.h"
#include "utility/WiFiSocketBuffer.h"

#define WIFININA_UDP_SOCKET_RECV_BUFFER_SIZE   256

NANO_RetCode
NANO_OSAPI_WifiNinaUdpSocket_open(
    NANO_OSAPI_Udpv4Socket *const self,
    const NANO_u8 *const addr,
    const NANO_u16 port,
    const NANO_OSAPI_Udpv4SocketProperties *const properties)
{
    NANO_RetCode rc = NANO_RETCODE_ERROR;

    NANO_LOG_FN_ENTRY

    NANO_PCOND(self != NULL)
    NANO_PCOND(addr != NULL)
    NANO_PCOND(port > 0)

    self->sock = ServerDrv::getSocket();
    if (self->sock == NO_SOCKET_AVAIL)
    {
        NANO_LOG_ERROR_MSG("FAILED to create UDP sock")
        goto done;
    }

    ServerDrv::startServer(port, self->sock, UDP_MODE);
    self->port = port;

    self->recv_buffer = new NANO_u8[WIFININA_UDP_SOCKET_RECV_BUFFER_SIZE];
    self->recv_buffer_size = WIFININA_UDP_SOCKET_RECV_BUFFER_SIZE;

    NANO_MessageBuffer_set_external_data(
            &self->recv_msg, self->recv_buffer, 0);

    rc = NANO_RETCODE_OK;
    
done:
    if (NANO_RETCODE_OK != rc)
    {
        if (self->sock != NO_SOCKET_AVAIL)
        {
            ServerDrv::stopClient(self->sock);
            WiFiSocketBuffer.close(self->sock);
            self->sock = NO_SOCKET_AVAIL;
        }
    }
    NANO_LOG_FN_EXIT_RC(rc)
    return rc;
}

NANO_RetCode
NANO_OSAPI_WifiNinaUdpSocket_send(
    NANO_OSAPI_Udpv4Socket *const self,
    const NANO_u8 *const dest_addr,
    const NANO_u16 dest_port,
    const NANO_MessageBuffer *msg)
{
    NANO_RetCode rc = NANO_RETCODE_ERROR;
    const NANO_MessageBuffer *nxt = NULL;
    NANO_usize i = 0;

    NANO_LOG_FN_ENTRY

    NANO_PCOND(self != NULL)
    NANO_PCOND(dest_addr != NULL)
    NANO_PCOND(dest_port > 0)
    NANO_PCOND(msg != NULL)
    NANO_PCOND(NANO_MessageBuffer_data_ptr(msg) != NULL)

    NANO_PCOND(self->sock != NO_SOCKET_AVAIL)

    IPAddress ip(dest_addr[0], dest_addr[1], dest_addr[2], dest_addr[3]);
    ServerDrv::startClient(uint32_t(ip), dest_port, self->sock, UDP_MODE);

    nxt = msg;
    while (nxt != NULL)
    {
        ServerDrv::insertDataBuf(self->sock, 
                                NANO_MessageBuffer_data_ptr(nxt), 
                                NANO_MessageBuffer_data_len(nxt));

        i += NANO_MessageBuffer_data_len(nxt);
        nxt = NANO_MessageBuffer_next(nxt);
    }

    NANO_LOG_DEBUG("UDP sendto",
        NANO_LOG_USIZE("size", i)
        NANO_LOG_BYTES("dest_addr", dest_addr, sizeof(NANO_Ipv4Addr))
        NANO_LOG_U16("dest_port", dest_port))

    ServerDrv::sendUdpData(self->sock);

#ifdef NANO_HAVE_YIELD
    NANO_OSAPI_Scheduler_yield();
#endif /* NANO_HAVE_YIELD */

    rc = NANO_RETCODE_OK;
    
// done:
//     if (NANO_RETCODE_OK != rc)
//     {
        
//     }
    NANO_LOG_FN_EXIT_RC(rc)
    return rc;
}

NANO_RetCode
NANO_OSAPI_WifiNinaUdpSocket_recv(
    NANO_OSAPI_Udpv4Socket *const self,
    NANO_u8 *const src_addr,
    NANO_u16 *const src_port,
    NANO_MessageBuffer *const msg,
    NANO_usize *const msg_size_out)
{
    NANO_RetCode rc = NANO_RETCODE_ERROR;    

    NANO_LOG_FN_ENTRY

    NANO_PCOND(self != NULL)
    NANO_PCOND(src_addr != NULL)
    NANO_PCOND(src_port != NULL)
    NANO_PCOND(msg != NULL)
    NANO_PCOND(msg_size_out != NULL)

    NANO_OSAPI_Memory_zero(self->recv_buffer, self->recv_buffer_size);
    *msg_size_out = 0;

    NANO_u16 avl_len = ServerDrv::availData(self->sock);
    NANO_LOG_DEBUG("UDP availData",
        NANO_LOG_U16("avl_len", avl_len))
    if(avl_len > self->recv_buffer_size)
    {
        rc = NANO_RETCODE_OUT_OF_RESOURCES;
        goto done;
    }

    *msg_size_out = WiFiSocketBuffer.read(self->sock, self->recv_buffer, self->recv_buffer_size);
    NANO_LOG_DEBUG("UDP read",
        NANO_LOG_USIZE("msg_len", *msg_size_out))

    NANO_OSAPI_Memory_copy(msg->data, self->recv_buffer, *msg_size_out);
    NANO_MessageBuffer_set_data_len(msg, *msg_size_out);

#ifdef NANO_HAVE_YIELD
    NANO_OSAPI_Scheduler_yield();
#endif /* NANO_HAVE_YIELD */


    rc = NANO_RETCODE_OK;
    
done:
    if (NANO_RETCODE_OK != rc)
    {
        
    }
    
    NANO_LOG_FN_EXIT_RC(rc)
    return rc;
}


NANO_RetCode
NANO_OSAPI_WifiNinaUdpSocket_close(NANO_OSAPI_Udpv4Socket *const self)
{
    NANO_LOG_FN_ENTRY

    NANO_PCOND(self != NULL)

    NANO_PCOND(self->sock != NULL)
    NANO_PCOND(self->recv_buffer != NULL)

    ServerDrv::stopClient(self->sock);
    WiFiSocketBuffer.close(self->sock);
	self->sock = NO_SOCKET_AVAIL;
    if (self->recv_buffer)
        delete[] self->recv_buffer;
    self->recv_buffer_size = 0;

    NANO_LOG_FN_EXIT
    return NANO_RETCODE_OK;
}

#endif /* NANO_FEAT_TRANSPORT_PLUGIN_UDPV4 */



#endif /* NANO_FEAT_TRANSPORT_IPV4 || \
            NANO_FEAT_TRANSPORT_IPV6*/

#endif /* NANO_HAVE_NET_WIFININA */
