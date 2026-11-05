#pragma once

#include "thingsboard_secrets.h"

#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#define TB_BROKER_URI "mqtts://eu.thingsboard.cloud:8883"
#define TB_KEEPALIVE_S 60
#define TB_ROWS_PER_PUBLISH 8

#define TB_TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define TB_ATTRIBUTES_TOPIC "v1/devices/me/attributes"
#define TB_RPC_REQUEST_SUB "v1/devices/me/rpc/request/+"
#define TB_RPC_REQUEST_PREFIX "v1/devices/me/rpc/request/"
#define TB_RPC_RESPONSE_PREFIX "v1/devices/me/rpc/response/"

#define TB_RPC_GET_AUTOMATION "getAutomation"
#define TB_RPC_SET_AUTOMATION "setAutomation"

#define TB_TOPIC_LEN 96
#define TB_PAYLOAD_LEN 192

static const char* TB_TAG = "ThingsBoard";

static esp_mqtt_client_config_t tb_mqtt_cfg = {
    .broker.address.uri = TB_BROKER_URI,
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    .credentials.username = THINGSBOARD_ACCESS_TOKEN,
    .session.keepalive = TB_KEEPALIVE_S,
};
