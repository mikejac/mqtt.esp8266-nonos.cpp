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

#ifndef MQTT_CLIENT_H
#define	MQTT_CLIENT_H

#define WITH_MQTT_STANDALONE

#ifdef	__cplusplus
extern "C" {
#endif

#include <github.com/mikejac/timer.esp8266-nonos.cpp/timer.h>
#include <user_interface.h>
#include <ip_addr.h>
#include <espconn.h>
#include <queue.h>
#include <github.com/mikejac/paho.mqtt.esp8266-nonos.cpp/MQTTPacket/src/MQTTPacket.h>

/******************************************************************************************************************
 * callbacks
 *
 */
typedef void (*MqttConnectCallback)(unsigned char sessionPresent, unsigned char connack_rc, void* args);
typedef void (*MqttDisconnectCallback)(void* args);

typedef void (*MqttPublishCallback)(const char* topic, const unsigned char* payload, int payloadlen, int qos, unsigned char retained, unsigned char dup, void* args);

typedef void (*MqttSubscribeCallback)(unsigned short packetid, int granted_qos, void* args);
typedef void (*MqttUnsubscribeCallback)(unsigned short packetid, void* args);

#if defined(WITH_MQTT_STANDALONE)
typedef int  (*MqttWifiCheck)(void* args);
#endif

/******************************************************************************************************************
 * data
 *
 */
typedef enum {
    mqtt_connect,
    mqtt_publish,
    mqtt_subscribe,
    mqtt_unsubscribe,
    mqtt_ping,
    mqtt_disconnect
} MQTT_MessageType;

typedef struct tag_MQTT_Message MQTT_Message;

struct tag_MQTT_Message {
    STAILQ_ENTRY(tag_MQTT_Message) entries;    // tail queue
            
    MQTT_MessageType    m_MessageType;
    int                 m_Qos;
    unsigned char*      m_Data;
    int                 m_Len;
};

typedef struct tag_MQTT_Notify MQTT_Notify;

struct tag_MQTT_Notify {
    STAILQ_ENTRY(tag_MQTT_Notify) entries;   // tail queue
            
    uint8_t             m_Type;
    unsigned char*      m_Buf;
    int                 m_Len;
};

typedef struct {
	uint8_t                         m_State;
	ip_addr_t                       m_IpAddr;
	int                             m_Port;
        char*                           m_ClientId;
        uint32_t                        m_SendTimeout;
	void*                           m_UserData;
        Timer                           m_TransactionTimer;
        Timer                           m_KeepAliveTimer;
        
        struct espconn                  m_Conn;
        MQTTPacket_connectData          m_Options;
        
        int                             m_WillQoS;
        unsigned char                   m_WillRetain;
        char*                           m_WillTopic;
        char*                           m_WillMsg;
        
        unsigned short                  m_PacketId;
        MqttConnectCallback             m_OnConnectedCb;
        MqttDisconnectCallback          m_OnDisconnectedCb;
        MqttPublishCallback             m_OnPublishCb;
        MqttSubscribeCallback           m_OnSubscribeCb;
        MqttUnsubscribeCallback         m_OnUnsubscribeCb;
        
#if defined(WITH_MQTT_STANDALONE)        
        MqttWifiCheck                   m_WifiCheck;
        void*                           m_WifiCheckArgs;
        ETSTimer                        m_StandaloneTimer;
#endif
        
        STAILQ_HEAD(MQTT_Message_Queue_t, tag_MQTT_Message) m_QueueIngress;
        STAILQ_HEAD(MQTT_Notify_Queue_t,  tag_MQTT_Notify)  m_QueueNotify;
} MQTT_Client;

/******************************************************************************************************************
 * prototypes
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
int MQTT_InitConnection(MQTT_Client* client, const char* client_id, unsigned short keepalive, unsigned char cleansession, void* user_data, int buflen);
/**
 * 
 * @param client
 * @param willTopic
 * @param willMsg
 * @param willQoS
 * @param willRetain
 * @return 
 */
int MQTT_InitLWT(MQTT_Client* client, const char* willTopic, const char* willMsg, int willQoS, unsigned char willRetain);
/**
 * 
 * @param client
 * @param ipaddr
 * @param port
 * @return 
 */
int MQTT_Connect(MQTT_Client* client, const ip_addr_t* ipaddr, int port);
/**
 * 
 * @param client
 * @return 
 */
int MQTT_Disconnect(MQTT_Client* client);
/**
 * 
 * @param client
 * @return 
 */
int MQTT_Shutdown(MQTT_Client* client);
/**
 * 
 * @param client
 * @return 
 */
int MQTT_IsConnected(MQTT_Client* client);
/**
 * 
 * @param client
 * @return 
 */
int MQTT_Run(MQTT_Client* client);
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
int MQTT_Publish(MQTT_Client* client, const char* topic, const char* payload, int payloadlen, int qos, unsigned char retained);
/**
 * 
 * @param client
 * @param topic_filter
 * @param qos
 * @return 
 */
int MQTT_Subscribe(MQTT_Client* client, const char* topic_filter, int qos);
/**
 * 
 * @param client
 * @param topic_filter
 * @return 
 */
int MQTT_Unsubscribe(MQTT_Client* client, const char* topic_filter);
/**
 * 
 * @param client
 * @param connected_cb
 */
void MQTT_OnConnected(MQTT_Client* client, MqttConnectCallback connected_cb);
/**
 * 
 * @param client
 * @param disconnected_cb
 */
void MQTT_OnDisconnected(MQTT_Client* client, MqttDisconnectCallback disconnected_cb);
/**
 * 
 * @param client
 * @param normal_cb
 * @param predefined_cb
 * @param short_cb
 */
void MQTT_OnPublish(MQTT_Client* client, MqttPublishCallback normal_cb);
/**
 * 
 * @param client
 * @param subscribe_cb
 */
void MQTT_OnSubscribe(MQTT_Client* client, MqttSubscribeCallback subscribe_cb);
/**
 * 
 * @param client
 * @param unsubscribe_cb
 */
void MQTT_OnUnsubscribe(MQTT_Client* client, MqttUnsubscribeCallback unsubscribe_cb);

#if defined(WITH_MQTT_STANDALONE)
/**
 * 
 * @param client
 * @param interval_ms
 * @param wifi_func
 * @param wifi_func_args
 * @return 
 */
int MQTT_StartStandalone(MQTT_Client* client, uint32_t interval_ms, MqttWifiCheck wifi_func, void* wifi_func_args);
/**
 * 
 * @param client
 * @return 
 */
int MQTT_StopStandalone(MQTT_Client* client);

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* MQTT_CLIENT_H */

