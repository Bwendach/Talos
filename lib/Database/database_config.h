#pragma once

#define ADDRESS "mqtt://192.168.0.102:4092"

#define DB_CLIENT_ID "talos"
#define DB_TOPIC "talos/readings"
#define DB_STATUS_TOPIC "talos/status"
#define DB_QOS 1
#define DB_KEEPALIVE_S 30
#define DB_BUFFER_ROWS 300
#define DB_OUTBOX_MAX 8'192
#define DB_ROW_JSON_LEN 192
#define DB_RETRY_MS 2'000
#define DB_TASK_STACK 4'096
