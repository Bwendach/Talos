#pragma once

#include "secrets.h"

#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"

#include <stdbool.h>

// Blynk's MQTT broker, the username is always "device" and the password is the auth token
#define BLYNK_BROKER_URI "mqtts://blynk.cloud:8883"
#define BLYNK_USERNAME "device"
// how often we ping the broker so it knows we're still alive, in seconds
#define BLYNK_KEEPALIVE_S 45

// topics, we subscribe to everything under downlink and send our stuff to ds/<name>
#define BLYNK_DOWNLINK_TOPIC "downlink/#"
#define BLYNK_DOWNLINK_DS_PREFIX "downlink/ds/"
#define BLYNK_UPLINK_DS_PREFIX "ds/"

// buffer sizes for topic names and values
#define BLYNK_TOPIC_LEN 96
#define BLYNK_VALUE_LEN 64

// datastream names, these have to match exactly what's on the Blynk dashboard
#define BLYNK_DS_TEMPERATURE "Temperature"
#define BLYNK_DS_HUMIDITY "Humidity"
#define BLYNK_DS_AUDIO_LEVEL "Noise Level"
#define BLYNK_DS_AUTOMATION "Automation"

// tag for the ESP logs
static const char* BLYNK_TAG = "Blynk";

// callback type for when Blynk sends us a value
typedef void ( *blynk_downlink_cb_t )( const char* datastream, const char* value );

// the MQTT client, whether we're connected, and who to call on downlink
static esp_mqtt_client_handle_t blynk_client = NULL;
static volatile bool mqtt_ok = false;
static blynk_downlink_cb_t downlink_cb = NULL;

// wifi creds, they come from secrets.h
wifi_config_t blynk_wifi_cfg = {
    .sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    },
};

// MQTT config, uses the built in cert bundle so TLS just works
esp_mqtt_client_config_t blynk_mqtt_cfg = {
    .broker.address.uri = BLYNK_BROKER_URI,
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    .credentials.username = BLYNK_USERNAME,
    .credentials.authentication.password = BLYNK_AUTH_TOKEN,
    .session.keepalive = BLYNK_KEEPALIVE_S,
};
