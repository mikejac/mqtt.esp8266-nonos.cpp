/* 
 * The MIT License (MIT)
 * 
 * ESP8266 Non-OS Firmware
 * Copyright (c) 2015 Michael Jacobsen (github.com/mikejac)
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
 *
 */

#include "mqtt_client.h"
#include <github.com/mikejac/misc.esp8266-nonos.cpp/espmissingincludes.h>
#include <github.com/mikejac/date_time.esp8266-nonos.cpp/system_time.h>
#include <osapi.h>
#include <mem.h>

#if defined(ESP_MESH_NETWORKING)
#include <mesh.h>
#endif

#define DTXT(...)   os_printf(__VA_ARGS__)

#if !defined(MQTT_SEND_TIMOUT)
    #define MQTT_SEND_TIMOUT            5           // seconds (well, not really)
#endif

#if !defined(MQTT_PING_TIMEOUT)
    #define MQTT_PING_TIMEOUT           2           // seconds
#endif

#if !defined(MQTT_CONNECT_TIMEOUT)
    #define MQTT_CONNECT_TIMEOUT 	5           // seconds
#endif

#if !defined(MQTT_TRANSACTION_TIMEOUT)
    #define MQTT_TRANSACTION_TIMEOUT    5
#endif

#if !defined(MQTT_BUF_LEN)
    #define MQTT_BUF_LEN                (4 * 1024) //2048 //1024        // bytes
#endif

/******************************************************************************************************************
 * local var's
 *
 */

// buffer used by serialize and deserialize funtions
static int            mqtt_buflen = MQTT_BUF_LEN;
static unsigned char* mqtt_buf;

static int mqtt_transaction_timeout = MQTT_TRANSACTION_TIMEOUT;
static int mqtt_send_timeout        = MQTT_SEND_TIMOUT;
static int mqtt_ping_timeout        = MQTT_PING_TIMEOUT;
static int mqtt_connect_timeout     = MQTT_CONNECT_TIMEOUT;

/******************************************************************************************************************
 * prototypes
 *
 */

/**
 * 
 * @param arg
 */
static void mqtt_client_connect_cb(void* arg);
/**
 * 
 * @param arg
 * @param err
 */
static void mqtt_client_reconnect_cb(void* arg, sint8 err);
/**
 * 
 * @param arg
 */
static void mqtt_client_sent_cb(void* arg);
/**
 * 
 * @param arg
 * @param pdata
 * @param len
 */
static void mqtt_client_recv_cb(void* arg, char* pdata, unsigned short len);
/**
 * 
 * @param client
 * @return 
 */
static int mqtt_restart_connection(MQTT_Client* client);
/**
 * 
 * @param client
 * @return 
 */
static int mqtt_setup_connection(MQTT_Client* client);
/**
 * 
 * @param client
 * @param msg_type
 * @param data
 * @param len
 * @param qos
 * @return 
 */
/*static*/ int mqtt_add_message(MQTT_Client* client, MQTT_MessageType msg_type, const unsigned char* data, int len, int qos);
/**
 * 
 * @param client
 * @param type
 * @param data
 * @param len
 * @return 
 */
static int mqtt_add_notify(MQTT_Client* client, uint8_t type, const unsigned char* data, int len);
/**
 * 
 * @param client
 * @return 
 */
static int mqtt_clear_queues(MQTT_Client* client);
/**
 * 
 * @param args
 */
#if defined(WITH_MQTT_STANDALONE)
static void mqtt_standalone(void* args);
#endif
/**
 * 
 * @param client
 * @param buf
 * @param len
 * @return 
 */
static inline int ICACHE_FLASH_ATTR mqtt_client_sent(MQTT_Client* client, unsigned char* buf, int len)
{
    client->m_SendTimeout = mqtt_send_timeout;
    
#if defined(ESP_MESH_NETWORKING)
    return espconn_mesh_sent(&client->m_Conn, buf, len);
#else
    return espconn_sent(&client->m_Conn, buf, len);
#endif
}
/**
 * 
 * @param client
 * @return 
 */
static inline int ICACHE_FLASH_ATTR mqtt_client_is_sending(MQTT_Client* client)
{
    return (client->m_SendTimeout == 0) ? 0 : 1;
}
/**
 * 
 * @param client
 * @return 
 */
static inline int ICACHE_FLASH_ATTR mqtt_client_reset_sending(MQTT_Client* client)
{
    client->m_SendTimeout = 0;
    
    return 0;
}

/******************************************************************************************************************
 * public functions
 *
 */

/**
 * 
 * @param client
 * @param client_id
 * @param keepalive
 * @param cleansession
 * @param user_data
 * @param buflen
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_InitConnection(MQTT_Client* client, const char* client_id, unsigned short keepalive, unsigned char cleansession, void* user_data, int buflen)
{
    mqtt_buflen = buflen;
    
    DTXT("MQTT_InitConnection(): mqtt_buflen = %d\n", mqtt_buflen);
    
    mqtt_buf = (unsigned char*) os_malloc(mqtt_buflen);
    if(mqtt_buf == 0) {
        DTXT("MQTT_InitConnection(): mqtt_buf == NULL\n");
        return -1;
    }
    
    os_memset(client, 0, sizeof(MQTT_Client));

    client->m_Conn.proto.tcp = (esp_tcp*) os_zalloc(sizeof(esp_tcp));

    if(client->m_Conn.proto.tcp == NULL) {
        DTXT("MQTT_InitConnection(): m_Conn.proto.tcp == NULL\n");
        return -1;
    }
    
    STAILQ_INIT(&client->m_QueueIngress);
    STAILQ_INIT(&client->m_QueueNotify);

    /******************************************************************************************************************
     * save info for future re-use
     *
     */
    client->m_UserData = user_data;
    
    client->m_ClientId = os_malloc(os_strlen(client_id) + 1);
    
    if(client->m_ClientId == NULL) {
        DTXT("MQTT_InitConnection(): m_ClientId == NULL\n");
        os_free(client->m_Conn.proto.tcp);
        client->m_Conn.proto.tcp = NULL;
        return -1;
    }
    
    os_strcpy(client->m_ClientId, client_id);    
    
    client->m_Options.keepAliveInterval         = keepalive;
    client->m_Options.cleansession              = cleansession;
    client->m_Options.willFlag                  = 0;
    client->m_Options.username.cstring          = NULL;
    client->m_Options.username.lenstring.data   = NULL;
    client->m_Options.username.lenstring.len    = 0;
    client->m_Options.password.cstring          = NULL;
    client->m_Options.password.lenstring.data   = NULL;
    client->m_Options.password.lenstring.len    = 0;
    
    client->m_State                             = (uint8_t) -1;
    
    client->m_OnConnectedCb                     = NULL;
    client->m_OnDisconnectedCb                  = NULL;
    client->m_OnPublishCb                       = NULL;
    client->m_OnSubscribeCb                     = NULL;
    client->m_OnUnsubscribeCb                   = NULL;
            
    return 0;
}
/**
 * 
 * @param client
 * @param willTopic
 * @param willMsg
 * @param willQoS
 * @param willRetain
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_InitLWT(MQTT_Client* client, const char* willTopic, const char* willMsg, int willQoS, unsigned char willRetain)
{
    client->m_WillTopic = os_malloc(os_strlen(willTopic) + 1);
    
    if(client->m_WillTopic == NULL) {
        DTXT("MQTT_InitLWT(): m_WillTopic == NULL\n");
        return -1;
    }
    
    client->m_WillMsg = os_malloc(os_strlen(willMsg) + 1);
    
    if(client->m_WillMsg == NULL) {
        DTXT("MQTT_InitLWT(): m_WillMsg == NULL\n");
        os_free(client->m_WillTopic);
        client->m_WillTopic = NULL;
        return -1;
    }
    
    client->m_WillQoS    = willQoS;
    client->m_WillRetain = willRetain;
    
    os_strcpy(client->m_WillTopic, willTopic);
    os_strcpy(client->m_WillMsg,   willMsg);
    
    return 0;
}
/**
 * 
 * @param client
 * @param ipaddr
 * @param port
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Connect(MQTT_Client* client, const ip_addr_t* ipaddr, int port)
{
    DTXT("MQTT_Connect(): begin\n");
    
    client->m_Port = port;
    os_memcpy(&client->m_IpAddr.addr, &ipaddr->addr, 4);
    
    mqtt_setup_connection(client);

    /******************************************************************************************************************
     * MQTT
     *
     */
    client->m_Options.struct_id[0]              = 'M';
    client->m_Options.struct_id[1]              = 'Q';
    client->m_Options.struct_id[2]              = 'T';
    client->m_Options.struct_id[2]              = 'C';
    client->m_Options.struct_version            = 0;
    client->m_Options.MQTTVersion               = 3;                            // version of MQTT to be used. 3 = 3.1, 4 = 3.1.1
    client->m_Options.clientID.cstring          = client->m_ClientId;
    client->m_Options.clientID.lenstring.data   = NULL;
    client->m_Options.clientID.lenstring.len    = 0;
    client->m_PacketId                          = 0;
    
    if(client->m_WillTopic != NULL) {
        client->m_Options.willFlag                      = 1;
        client->m_Options.will.qos                      = client->m_WillQoS;
        client->m_Options.will.retained                 = client->m_WillRetain;
        client->m_Options.will.struct_id[0]             = 'M';
        client->m_Options.will.struct_id[1]             = 'Q';
        client->m_Options.will.struct_id[2]             = 'T';
        client->m_Options.will.struct_id[3]             = 'W';
        client->m_Options.will.struct_version           = 0;
        client->m_Options.will.topicName.cstring        = client->m_WillTopic;
        client->m_Options.will.topicName.lenstring.data = NULL;
        client->m_Options.will.topicName.lenstring.len  = 0;
        client->m_Options.will.message.cstring          = client->m_WillMsg;
        client->m_Options.will.message.lenstring.data   = NULL;
        client->m_Options.will.message.lenstring.len    = 0;
    }
    
    int len = MQTTSerialize_connect(mqtt_buf, mqtt_buflen, &client->m_Options);

    DTXT("MQTT_Connect(): len = %d\n", len);
    
    int ret = mqtt_add_message(client, mqtt_connect, mqtt_buf, len, 0);
    
    client->m_State = CONNECT;
    
    DTXT("MQTT_Connect(): end\n");
    
    return ret;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Disconnect(MQTT_Client* client)
{
    DTXT("MQTT_Disconnect(): begin\n");
    
    int ret = -1;
    
    if(MQTT_IsConnected(client)) {
        DTXT("MQTT_Disconnect(): connected\n");
        
        int len = MQTTSerialize_disconnect(mqtt_buf, mqtt_buflen);
        
        DTXT("MQTT_Disconnect(): len = %d\n", len);
        
        ret = mqtt_add_message(client, mqtt_disconnect, mqtt_buf, len, 0);
    }
    else {
        DTXT("MQTT_Disconnect(): not connected\n");

        MQTT_Shutdown(client);
    }
    
    DTXT("MQTT_Disconnect(): end\n");

    return ret;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Shutdown(MQTT_Client* client)
{
    DTXT("MQTT_Shutdown(): begin\n");
    
    mqtt_clear_queues(client);

    espconn_delete(&client->m_Conn);
    
    client->m_State = (uint8_t) -1;
    
    DTXT("MQTT_Shutdown(): end\n");
    
    return 0;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_IsConnected(MQTT_Client* client)
{
    switch(client->m_State) {
        case CONNACK:
        case DISCONNECT:
        case PINGREQ:
        case SUBSCRIBE:
        case PUBLISH:
        case PUBREL:
            return 1;
    }
    
    return 0;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Run(MQTT_Client* client)
{
    /******************************************************************************************************************
     * deal with outgoing messages
     *
     */
    if(!mqtt_client_is_sending(client)) {
        switch(client->m_State) {
            case CONNECT:
                {
                    MQTT_Message* msg = STAILQ_FIRST(&client->m_QueueIngress);

                    if(msg != NULL) {
                        DTXT("MQTT_Run(): got message (state CONNECT)\n");

                        if(msg->m_MessageType == mqtt_connect) {
                            DTXT("MQTT_Run(): connect message\n");

                            mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                            countdown(&client->m_TransactionTimer, mqtt_connect_timeout);
                        }

                        if(msg->m_Data != NULL) {
                            os_free(msg->m_Data);
                        }

                        // remove from queue
                        STAILQ_REMOVE(&client->m_QueueIngress, msg, tag_MQTT_Message, entries);            
                        os_free(msg);
                    }
                    else if(expired(&client->m_TransactionTimer)) {
                        DTXT("MQTT_Run(): connect timeout\n");

                        mqtt_restart_connection(client);
                    }
                }
                break;
                
            case CONNACK:
                if(expired(&client->m_KeepAliveTimer)) {
                    DTXT("MQTT_Run(): MQTT_CONNACK keep alive expired\n");

                    int len = MQTTSerialize_pingreq(mqtt_buf, mqtt_buflen);

                    MQTT_Message* msg = os_malloc(sizeof(MQTT_Message));

                    if(msg != NULL) {
                        msg->m_Data = os_malloc(len);

                        if(msg->m_Data != NULL) {
                            os_memcpy(msg->m_Data, mqtt_buf, len);

                            msg->m_Len         = len;
                            msg->m_MessageType = mqtt_ping;
                            msg->m_Qos         = 0;

                            STAILQ_INSERT_HEAD(&client->m_QueueIngress, msg, entries);      // insert at head
                        }
                        else {
                            DTXT("MQTT_Run(): msg->m_Data == NULL\n");
                            os_free(msg);
                        }
                    }
                    else {
                        DTXT("MQTTSN_Run(): msg == NULL\n");
                    }

                    countdown(&client->m_KeepAliveTimer, mqtt_ping_timeout);
                    countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                }
                else {
                    MQTT_Message* msg = STAILQ_FIRST(&client->m_QueueIngress);

                    if(msg != NULL) {
                        DTXT("MQTT_Run(): got message\n");

                        switch(msg->m_MessageType) {
                            case mqtt_connect:
                                DTXT("MQTT_Run(): connect message\n");

                                //client->m_State = UNSUBSCRIBE;
                                
                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_connect_timeout);
                                break;
                                
                            case mqtt_publish:
                                DTXT("MQTT_Run(): publish message\n");

                                if(msg->m_Qos != 0) {
                                    client->m_State = PUBLISH;
                                }

                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                                break;

                            case mqtt_subscribe:
                                DTXT("MQTT_Run(): subscribe message\n");

                                if(msg->m_Qos != 0) {
                                    client->m_State = SUBSCRIBE;
                                }

                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                                break;

                            case mqtt_unsubscribe:
                                DTXT("MQTT_Run(): unsubscribe message\n");

                                client->m_State = UNSUBSCRIBE;
                                
                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                                break;

                            case mqtt_ping:
                                DTXT("MQTT_Run(): ping message\n");

                                client->m_State = PINGREQ;

                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                                break;

                            case mqtt_disconnect:
                                DTXT("MQTT_Run(): disconnect message\n");

                                client->m_State = DISCONNECT;

                                mqtt_client_sent(client, msg->m_Data, msg->m_Len);
                                countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                                break;
                        }

                        if(msg->m_Data != NULL) {
                            os_free(msg->m_Data);
                        }

                        // remove from queue
                        STAILQ_REMOVE(&client->m_QueueIngress, msg, tag_MQTT_Message, entries);            
                        os_free(msg);
                    }
                }
                break;

            case PUBLISH:
            case SUBSCRIBE:
            case PUBREL:
                if(expired(&client->m_TransactionTimer)) {
                    DTXT("MQTT_Run(): transaction timeout\n");

                    mqtt_restart_connection(client);
                }
                break;

            case PINGREQ:
                if(expired(&client->m_KeepAliveTimer)) {
                    DTXT("MQTT_Run(): PING timeout\n");

                    mqtt_restart_connection(client);

                    mqtt_add_notify(client, DISCONNECT, NULL, 0);
                }
                break;
            
            case DISCONNECT:
                DTXT("MQTT_Run(): DISCONNECT\n");
                
                espconn_disconnect(&client->m_Conn);
                
                MQTT_Shutdown(client);
                
                if(client->m_OnDisconnectedCb != NULL) {
                    client->m_OnDisconnectedCb(client->m_UserData);
                }
                break;
                
            default:
                break;
        }
    }
    else {
        //if(client->m_State != CONNECT && expired(&client->m_TransactionTimer)) {
        if(expired(&client->m_TransactionTimer)) {
            DTXT("MQTT_Run(): transaction timeout (client sending)\n");

            mqtt_restart_connection(client);
            
            if(client->m_State != CONNECT) {
                mqtt_add_notify(client, DISCONNECT, NULL, 0);
            }
        }
    }
    
    /******************************************************************************************************************
     * deal with incoming notifications
     *
     */
    MQTT_Notify* notify = STAILQ_FIRST(&client->m_QueueNotify);

    if(notify != NULL) {
        DTXT("MQTT_Run(): got notification\n");
        
        switch(notify->m_Type) {
            case CONNACK:
                {
                    DTXT("MQTT_Run(): CONNACK\n");

                    unsigned char connack_rc = -1;
                    unsigned char sessionPresent;

                    MQTTDeserialize_connack(&sessionPresent, &connack_rc, notify->m_Buf, notify->m_Len);

                    if(client->m_OnConnectedCb != NULL) {
                        client->m_OnConnectedCb(sessionPresent, connack_rc, client->m_UserData);
                    }
                }
                break;
            
            case DISCONNECT:
                DTXT("MQTT_Run(): DISCONNECT\n");
                
                if(client->m_OnDisconnectedCb != NULL) {
                    client->m_OnDisconnectedCb(client->m_UserData);
                }
                break;
                
            case SUBACK:
                {
                    DTXT("MQTT_Run(): SUBACK\n");

                    unsigned short packetid;
                    int            count;
                    int            grantedQoSs[1];
                    
                    MQTTDeserialize_suback(&packetid, sizeof(grantedQoSs), &count, grantedQoSs, notify->m_Buf, notify->m_Len);
                    
                    if(client->m_OnSubscribeCb != NULL) {
                        client->m_OnSubscribeCb(packetid, grantedQoSs[0], client->m_UserData);
                    }
                }
                break;
                
            case UNSUBACK:
                {
                    DTXT("MQTT_Run(): UNSUBACK\n");
                    
                    unsigned short packetid;

                    MQTTDeserialize_unsuback(&packetid, notify->m_Buf, notify->m_Len);
                    
                    if(client->m_OnUnsubscribeCb != NULL) {
                        client->m_OnUnsubscribeCb(packetid, client->m_UserData);
                    }
                }
                break;
                
            case PUBLISH:
                {
                    DTXT("MQTT_Run(): PUBLISH\n");

                    unsigned short packetid;
                    int            qos;
                    int            payloadlen;
                    unsigned char* payload;
                    unsigned char  dup;
                    unsigned char  retained;
                    MQTTString     topicName = MQTTString_initializer;

                    MQTTDeserialize_publish(&dup, &qos, &retained, &packetid, &topicName, &payload, &payloadlen, notify->m_Buf, notify->m_Len);
                    
                    if(client->m_OnPublishCb != NULL) {
                        if(topicName.cstring != NULL) {
                            DTXT("MQTT_Run(): PUBLISH cstring\n");
                            
                            client->m_OnPublishCb(topicName.cstring, payload, payloadlen, qos, retained, dup, client->m_UserData);
                        }
                        else {
                            topicName.lenstring.len = topicName.lenstring.len;
                            
                            DTXT("MQTT_Run(): PUBLISH lenstring; len = %d\n", topicName.lenstring.len);
                            
                            char* mqtt_pub_topic = os_malloc(topicName.lenstring.len + 1);

                            if(mqtt_pub_topic == NULL) {
                                DTXT("MQTT_Run(): mqtt_pub_topic == NULL\n");
                            }
                            else {
                                os_memcpy(mqtt_pub_topic, topicName.lenstring.data, topicName.lenstring.len);
                                mqtt_pub_topic[topicName.lenstring.len] = '\0';
                                        
                                client->m_OnPublishCb(mqtt_pub_topic, payload, payloadlen, qos, retained, dup, client->m_UserData);
                                        
                                os_free(mqtt_pub_topic);
                            }
                        }
                    }
                }   
                break;
                
            default:
                DTXT("MQTT_Run(): unknown notification\n");
                break;
        }
        
        if(notify->m_Buf != NULL) {
            os_free(notify->m_Buf);
        }

        // remove from queue
        STAILQ_REMOVE(&client->m_QueueNotify, notify, tag_MQTT_Notify, entries);            
        os_free(notify);
    }
    
    return 0;
}
/**
 * 
 * @param client
 * @param id
 * @param payload
 * @param payloadlen
 * @param qos
 * @param retained
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Publish(MQTT_Client* client, const char* topic, const char* payload, int payloadlen, int qos, unsigned char retained)
{
    int ret = -1;
    
    if(MQTT_IsConnected(client)) {
        DTXT("MQTT_Publish(): connected\n");
        
        unsigned char  dup = 0;

        MQTTString topicName = MQTTString_initializer;
        topicName.cstring    = (char*) topic;
        
        int len = MQTTSerialize_publish(mqtt_buf, mqtt_buflen, dup, qos, retained, client->m_PacketId++, topicName, (unsigned char*)payload, payloadlen);

        DTXT("MQTT_Publish(): len = %d\n", len);
        
        if(len > 0) {
            ret = mqtt_add_message(client, mqtt_publish, mqtt_buf, len, qos);
        }
    }
    else {
        DTXT("MQTT_Publish(): not connected\n");
    }
    
    return ret;
}
/**
 * 
 * @param client
 * @param topic_filter
 * @param qos
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Subscribe(MQTT_Client* client, const char* topic_filter, int qos)
{
    int ret = -1;
    
    if(MQTT_IsConnected(client)) {
        DTXT("MQTT_Subscribe(): connected\n");
        
        unsigned char  dup = 0;
        MQTTString     topicFilters[1];
        int            requestedQoSs[1];
        
        topicFilters[0].cstring         = (char*) topic_filter;
        topicFilters[0].lenstring.data  = NULL;
        topicFilters[0].lenstring.len   = 0;
        requestedQoSs[0]                = qos;
        
	int len = MQTTSerialize_subscribe(mqtt_buf, mqtt_buflen, dup, client->m_PacketId++, 1, topicFilters, requestedQoSs);
        
        DTXT("MQTT_Subscribe(): len = %d\n", len);
        
        if(len > 0) {
            ret = mqtt_add_message(client, mqtt_subscribe, mqtt_buf, len, 0);
        }
    }
    else {
        DTXT("MQTT_Subscribe(): not connected\n");
    }
    
    return ret;
}
/**
 * 
 * @param client
 * @param topic_filter
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_Unsubscribe(MQTT_Client* client, const char* topic_filter)
{
    int ret = -1;
    
    if(MQTT_IsConnected(client)) {
        DTXT("MQTT_Unsubscribe(): connected\n");
        
        unsigned char  dup = 0;
        MQTTString     topicFilters[1];
        
        topicFilters[0].cstring         = (char*) topic_filter;
        topicFilters[0].lenstring.data  = NULL;
        topicFilters[0].lenstring.len   = 0;
        
	int len = MQTTSerialize_unsubscribe(mqtt_buf, mqtt_buflen, dup, client->m_PacketId++, 1, topicFilters);
        
        DTXT("MQTT_Unsubscribe(): len = %d\n", len);
        
        ret = mqtt_add_message(client, mqtt_unsubscribe, mqtt_buf, len, 0);
    }
    else {
        DTXT("MQTT_Unsubscribe(): not connected\n");
    }
    
    return ret;
}
/**
 * 
 * @param client
 * @param connected_cb
 */
void ICACHE_FLASH_ATTR MQTT_OnConnected(MQTT_Client* client, MqttConnectCallback connected_cb)
{
    client->m_OnConnectedCb = connected_cb;
}
/**
 * 
 * @param client
 * @param disconnected_cb
 */
void ICACHE_FLASH_ATTR MQTT_OnDisconnected(MQTT_Client* client, MqttDisconnectCallback disconnected_cb)
{
    client->m_OnDisconnectedCb = disconnected_cb;
}
/**
 * 
 * @param client
 * @param normal_cb
 * @param predefined_cb
 * @param short_cb
 */
void ICACHE_FLASH_ATTR MQTT_OnPublish(MQTT_Client* client, MqttPublishCallback normal_cb)
{
    client->m_OnPublishCb = normal_cb;
}
/**
 * 
 * @param client
 * @param subscribe_cb
 */
void ICACHE_FLASH_ATTR MQTT_OnSubscribe(MQTT_Client* client, MqttSubscribeCallback subscribe_cb)
{
    client->m_OnSubscribeCb = subscribe_cb;
}
/**
 * 
 * @param client
 * @param unsubscribe_cb
 */
void ICACHE_FLASH_ATTR MQTT_OnUnsubscribe(MQTT_Client* client, MqttUnsubscribeCallback unsubscribe_cb)
{
    client->m_OnUnsubscribeCb = unsubscribe_cb;
}
#if defined(WITH_MQTT_STANDALONE)
/**
 * 
 * @param client
 * @param interval_ms
 * @param wifi_func
 * @param wifi_func_args
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_StartStandalone(MQTT_Client* client, uint32_t interval_ms, MqttWifiCheck wifi_func, void* wifi_func_args)
{
    client->m_WifiCheck     = wifi_func;
    client->m_WifiCheckArgs = wifi_func_args;
    
    os_timer_disarm(&client->m_StandaloneTimer);
    os_timer_setfn(&client->m_StandaloneTimer, (os_timer_func_t*) mqtt_standalone, client);
    os_timer_arm(&client->m_StandaloneTimer, interval_ms, 1);

    return 0;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR MQTT_StopStandalone(MQTT_Client* client)
{
    os_timer_disarm(&client->m_StandaloneTimer);
     
    return 0;
}
#endif

/******************************************************************************************************************
 * local functions
 *
 */

/**
 * 
 * @param arg
 */
void ICACHE_FLASH_ATTR mqtt_client_connect_cb(void *arg)
{
    DTXT("mqtt_client_connect_cb()\n");
    
    struct espconn *server = (struct espconn*) arg;
    
    DTXT("mqtt_client_connect_cb(): remote_ip  = %d.%d.%d.%d\n", server->proto.tcp->remote_ip[0], server->proto.tcp->remote_ip[1], server->proto.tcp->remote_ip[2], server->proto.tcp->remote_ip[3]);
    DTXT("mqtt_client_connect_cb(): local_port = %d\n", server->proto.tcp->local_port);
}
/**
 * 
 * @param arg
 * @param err
 */
void ICACHE_FLASH_ATTR mqtt_client_reconnect_cb(void *arg, sint8 err)
{
    DTXT("mqtt_client_reconnect_cb()\n");
}
/**
 * 
 * @param arg
 */
void ICACHE_FLASH_ATTR mqtt_client_sent_cb(void* arg)
{
    DTXT("mqtt_client_sent_cb()\n");
    
    espconn* conn       = (espconn*) arg;
    MQTT_Client* client = (MQTT_Client*) conn->reverse;
    
    mqtt_client_reset_sending(client);
    
    DTXT("mqtt_client_sent_cb(): state = %d\n", (int) conn->state);
}
/**
 * 
 * @param arg
 * @param pdata
 * @param len
 */
void ICACHE_FLASH_ATTR mqtt_client_recv_cb(void* arg, char* pdata, unsigned short len)
{
    espconn* conn       = (espconn*) arg;
    MQTT_Client* client = (MQTT_Client*) conn->reverse;

    DTXT("mqtt_client_recv_cb(): begin\n");
    
    if(pdata == NULL || len == 0) {
        DTXT("mqtt_client_recv_cb(): end; no data\n");
        return;
    }

    // get packet type
    MQTTHeader     header  = {0};
    unsigned char* curdata = (unsigned char*) pdata;
    header.byte            = readChar(&curdata);

    DTXT("mqtt_client_recv_cb(): len = %d, type = %d\n", len, header.bits.type);
    
    switch(header.bits.type) {             // the packet type
        case CONNACK:
            {
                unsigned char sessionPresent;
                unsigned char connack_rc = -1;
                
                if(MQTTDeserialize_connack(&sessionPresent, &connack_rc, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): unable to connect; connack_rc = %d\n", connack_rc);
                }
                else {
                    DTXT("mqtt_client_recv_cb(): connected; connack_rc = %d\n", connack_rc);
                    
                    client->m_State = CONNACK;

                    countdown(&client->m_KeepAliveTimer, client->m_Options.keepAliveInterval);

                    mqtt_add_notify(client, CONNACK, (unsigned char*) pdata, len);
                }
            }
            break;
            
        case SUBACK:
            {
		unsigned short packetid;
                int            count;
                int            grantedQoSs[1];
                
		if(MQTTDeserialize_suback(&packetid, 1, &count, grantedQoSs, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing SUBACK\n");
                }
                else {
                    DTXT("mqtt_client_recv_cb(): subscribed; packetid = %d\n", packetid);
                    
                    mqtt_add_notify(client, SUBACK, (unsigned char*) pdata, len);
                }
                
                client->m_State = CONNACK;
            }
            break;
            
        case UNSUBACK:
            {
                DTXT("mqtt_client_recv_cb(): UNSUBACK\n");
                
                unsigned short packetid;
                
                if(MQTTDeserialize_unsuback(&packetid, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing UNSUBACK\n");
                }
                else {
                    DTXT("mqtt_client_recv_cb(): unsubscribed; packetid = %d\n", packetid);
                    
                    mqtt_add_notify(client, UNSUBACK, (unsigned char*) pdata, len);
                }
                
                client->m_State = CONNACK;
            }
            break;
            
        case PUBACK:     // QoS 1
            {
                DTXT("mqtt_client_recv_cb(): PUBACK\n");

                unsigned char  packettype;
                unsigned char  dup;
                unsigned short packetid;

                //hexdump(pdata, len);

		if(MQTTDeserialize_ack(&packettype, &dup, &packetid, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing PUBACK\n");
                }
		else {
                    DTXT("mqtt_client_recv_cb(): puback received; packetid = %d, packettype = %d\n", packetid, packettype);            
                }
                
                client->m_State = CONNACK;
            }
            break;
            
        case PUBREC:     // QoS 2 - step 1
            {
                DTXT("mqtt_client_recv_cb(): PUBREC\n");
                
                unsigned char  packettype;
                unsigned char  dup;
                unsigned short packetid;
                
                //hexdump(pdata, len);

		if(MQTTDeserialize_ack(&packettype, &dup, &packetid, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing PUBREC\n");
                }
		else {
                    DTXT("mqtt_client_recv_cb(): pubrec received; packetid = %d, packettype = %d\n", packetid, packettype);
                    int len = MQTTSerialize_pubrel(mqtt_buf, mqtt_buflen, dup, packetid);
                    
                    mqtt_client_sent(client, mqtt_buf, len);
                }
                
                client->m_State = CONNACK;
            }
            break;
            
        case PUBCOMP:    // QoS 2 - step 2
            {
                DTXT("mqtt_client_recv_cb(): PUBCOMP\n");
                
                unsigned char  packettype;
                unsigned char  dup;
                unsigned short packetid;

		if(MQTTDeserialize_ack(&packettype, &dup, &packetid, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing PUBCOMP\n");
                }
		else {
                    DTXT("mqtt_client_recv_cb(): pubcomp received; packetid = %d, packettype = %d\n", packetid, packettype);
                    
                    client->m_State = CONNACK;
                }
            }
            break;
            
        case PUBLISH:
            {
                unsigned short packetid;
                int            qos;
                int            payloadlen;
                unsigned char* payload;
                unsigned char  dup;
                unsigned char  retained;
                MQTTString     topicName = MQTTString_initializer;

                //hexdump(pdata, len);
                
		if(MQTTDeserialize_publish(&dup, &qos, &retained, &packetid, &topicName, &payload, &payloadlen, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing publish\n");
                }
		else {
                    DTXT("mqtt_client_recv_cb(): publish received; packetid = %d, qos = %d, len = %d\n", packetid, qos, topicName.lenstring.len);

                    mqtt_add_notify(client, PUBLISH, (unsigned char*) pdata, len);

                    if(qos == 1) {
                        DTXT("mqtt_client_recv_cb(): publish received; qos 1\n");
                        
                        len = MQTTSerialize_puback(mqtt_buf, mqtt_buflen, packetid);

                        mqtt_client_sent(client, mqtt_buf, len);
                    }
                    else if(qos == 2) {
                        DTXT("mqtt_client_recv_cb(): publish received; qos 2\n");
                        
                        len = MQTTSerialize_pubrec(mqtt_buf, mqtt_buflen, dup, packetid);
                        
                        mqtt_client_sent(client, mqtt_buf, len);
                        client->m_State = PUBREL;
                        
                        countdown(&client->m_TransactionTimer, mqtt_transaction_timeout);
                    }
                    else {
                        DTXT("mqtt_client_recv_cb(): publish received; qos 0\n");
                    }
                }
            }
            break;
            
        case PUBREL:     // QoS 2
            {
                DTXT("mqtt_client_recv_cb(): PUBREL\n");
                
                unsigned char  packettype;
                unsigned char  dup;
                unsigned short packetid;

		if(MQTTDeserialize_ack(&packettype, &dup, &packetid, (unsigned char*) pdata, len) != 1) {
                    DTXT("mqtt_client_recv_cb(): error deserializing PUBREL\n");
                }
		else {
                    DTXT("mqtt_client_recv_cb(): pubrel received; packetid = %d, packettype = %d\n", packetid, packettype);

                    len = MQTTSerialize_pubcomp(mqtt_buf, mqtt_buflen, packetid);
                    
                    mqtt_client_sent(client, mqtt_buf, len);
                    
                    client->m_State = CONNACK;
                }
            }
            break;
            
        case PINGRESP:
            DTXT("mqtt_client_recv_cb(): PINGRESP\n");

            client->m_State = CONNACK;
            countdown(&client->m_KeepAliveTimer, client->m_Options.keepAliveInterval);
            break;
            
        default:
            DTXT("mqtt_client_recv_cb(): packet type = %d\n", header.bits.type);
            break;
    }
    
    DTXT("mqtt_client_recv_cb(): end\n");
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR mqtt_restart_connection(MQTT_Client* client)
{
    int ret = 0;
    
    // empty queues here
    mqtt_clear_queues(client);
    
    espconn_delete(&client->m_Conn);

    if(mqtt_setup_connection(client) == 0) {
        int len = MQTTSerialize_connect(mqtt_buf, mqtt_buflen, &client->m_Options);

        DTXT("mqtt_restart_connection(): len = %d\n", len);

        ret = mqtt_add_message(client, mqtt_connect, mqtt_buf, len, 0);

        client->m_State = CONNECT;
    }
    else {
        ret = -1;
    }
    
    return ret;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR mqtt_setup_connection(MQTT_Client* client)
{
    int ret = 0;
    
    DTXT("mqtt_setup_connection(): begin\n");
    
    countdown(&client->m_TransactionTimer, mqtt_connect_timeout);
    
    if(client->m_Conn.proto.tcp == NULL) {
        DTXT("mqtt_setup_connection(): m_Conn.proto.tcp == NULL\n");
        return -1;
    }
    
    client->m_Conn.type                   = ESPCONN_TCP;
    client->m_Conn.state                  = ESPCONN_NONE;
    client->m_Conn.link_cnt               = 0;
    client->m_Conn.recv_callback          = 0;
    client->m_Conn.sent_callback          = 0;
    client->m_Conn.proto.tcp->local_port  = espconn_port();
    client->m_Conn.proto.tcp->remote_port = client->m_Port;
    
    os_memset(client->m_Conn.proto.tcp->local_ip, 0, sizeof(client->m_Conn.proto.tcp->local_ip));
    os_memcpy(client->m_Conn.proto.tcp->remote_ip, &client->m_IpAddr.addr, 4);
    
    client->m_Conn.reverse = client;

    espconn_regist_recvcb(&client->m_Conn,    mqtt_client_recv_cb);                       // register a tcp packet receiving callback
    espconn_regist_sentcb(&client->m_Conn,    mqtt_client_sent_cb);
    
    // we're not actually using these two, but hey ...
    espconn_regist_connectcb(&client->m_Conn, mqtt_client_connect_cb);
    espconn_regist_reconcb(&client->m_Conn,   mqtt_client_reconnect_cb);
    
#if defined(ESP_MESH_NETWORKING)
    DTXT("mqtt_setup_connection(): using mesh network\n");
    
    if(espconn_mesh_connect(&client->m_Conn)) {
        DTXT("mqtt_setup_connection(): espconn_mesh_connect() failed\n");
        ret = -1;
    }
#else
    espconn_connect(&client->m_Conn);
#endif
    
    mqtt_client_reset_sending(client);
    
    DTXT("mqtt_setup_connection():  remote_ip  = %d.%d.%d.%d\n", client->m_Conn.proto.tcp->remote_ip[0], client->m_Conn.proto.tcp->remote_ip[1], client->m_Conn.proto.tcp->remote_ip[2], client->m_Conn.proto.tcp->remote_ip[3]);
    DTXT("mqtt_setup_connectiont(): local_port = %d\n", client->m_Conn.proto.tcp->local_port);
    
    DTXT("mqtt_setup_connection(): end\n");
    
    return ret;
}
/**
 * 
 * @param client
 * @param msg_type
 * @param data
 * @param len
 * @param qos
 * @return 
 */
int ICACHE_FLASH_ATTR mqtt_add_message(MQTT_Client* client, MQTT_MessageType msg_type, const unsigned char* data, int len, int qos)
{
    int ret = -1;
    
    MQTT_Message* msg = os_malloc(sizeof(MQTT_Message));

    if(msg != NULL) {
        msg->m_Data = os_malloc(len);

        if(msg->m_Data != NULL) {
            os_memcpy(msg->m_Data, data, len);

            msg->m_Len         = len;
            msg->m_MessageType = msg_type;
            msg->m_Qos         = qos;

            STAILQ_INSERT_TAIL(&client->m_QueueIngress, msg, entries);          // insert at end

            ret = 0;
        }
        else {
            DTXT("mqtt_add_message(): msg->m_Data == NULL\n");
            os_free(msg);
        }
    }
    else {
        DTXT("mqtt_add_message(): msg == NULL\n");
    }

    return ret;
}
/**
 * 
 * @param client
 * @param type
 * @param data
 * @param len
 * @return 
 */
int ICACHE_FLASH_ATTR mqtt_add_notify(MQTT_Client* client, uint8_t type, const unsigned char* data, int len)
{
    int ret = -1;
    
    MQTT_Notify* notify = os_malloc(sizeof(MQTT_Notify));

    if(notify != NULL) {
        if(data != NULL && len > 0) {
            notify->m_Buf = os_malloc(len);

            if(notify->m_Buf != NULL) {
                os_memcpy(notify->m_Buf, data, len);

                notify->m_Len  = len;
                notify->m_Type = type;

                STAILQ_INSERT_TAIL(&client->m_QueueNotify, notify, entries);    // insert at end

                ret = 0;
            }
            else {
                DTXT("mqtt_add_notify(): notify->m_Buf == NULL\n");
                os_free(notify);
            }
        }
        else {
            notify->m_Buf  = NULL;
            notify->m_Len  = len;
            notify->m_Type = type;

            STAILQ_INSERT_TAIL(&client->m_QueueNotify, notify, entries);        // insert at end

            ret = 0;
        }
    }
    else {
        DTXT("mqtt_add_notify(): notify == NULL\n");
    }

    return ret;
}
/**
 * 
 * @param client
 * @return 
 */
int ICACHE_FLASH_ATTR mqtt_clear_queues(MQTT_Client* client)
{
    MQTT_Message* msg = STAILQ_FIRST(&client->m_QueueIngress);
    
    while(msg != NULL) {
        if(msg->m_Data != NULL) {
            os_free(msg->m_Data);
        }
                    
        // remove from queue
        STAILQ_REMOVE(&client->m_QueueIngress, msg, tag_MQTT_Message, entries);            
        os_free(msg);
        
        msg = STAILQ_FIRST(&client->m_QueueIngress);
    }
    
    MQTT_Notify* notify = STAILQ_FIRST(&client->m_QueueNotify);
    
    while(notify != NULL) {
        if(notify->m_Buf != NULL) {
            os_free(notify->m_Buf);
        }
                    
        // remove from queue
        STAILQ_REMOVE(&client->m_QueueNotify, notify, tag_MQTT_Notify, entries);            
        os_free(notify);
        
        notify = STAILQ_FIRST(&client->m_QueueNotify);
    }

    return 0;
}
#if defined(WITH_MQTT_STANDALONE)
/**
 * 
 * @param args
 */
void ICACHE_FLASH_ATTR mqtt_standalone(void* args)
{
    static int wifi_connected = 0;
    
    MQTT_Client* client = (MQTT_Client*) args;
    
    MQTT_Run(client);
    
    if(client->m_WifiCheck(client->m_WifiCheckArgs) == 1 && wifi_connected == 0) {
        DTXT("mqtt_standalone(): WiFi has been connected; uptime = %lu\n", (unsigned long) esp_uptime(0));
        
        wifi_connected = 1;
        
        //MQTT_Connect(client);
    }
    else if(client->m_WifiCheck(client->m_WifiCheckArgs) == 0 && wifi_connected == 1) {
        DTXT("mqtt_standalone(): WiFi has been disconnected; uptime = %lu\n", (unsigned long) esp_uptime(0));
        
        wifi_connected = 0;
        
        MQTT_Shutdown(client);
    }
}
#endif