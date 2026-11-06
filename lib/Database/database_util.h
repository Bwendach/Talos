#pragma once

#include "database_config.h"
#include "logger_util.h"
#include "clock_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

struct DbRow {
    int64_t ts_ms;
    int64_t uptime_ms;
    float temp;
    float hum;
    bool dht_valid;
    int mic_peak;
    char ir[LOG_IR_LEN];
};

static struct DbRow db_rows[DB_BUFFER_ROWS];
static int db_head = 0;
static int db_count = 0;
static int db_dropped = 0;
static portMUX_TYPE db_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_mqtt_client_handle_t db_client = NULL;
static volatile bool db_online = false;
static TaskHandle_t db_task_handle = NULL;

static int64_t db_epoch_ms () {
    struct tm tm;

    if ( !clock_now( &tm ) ) {
        return 0;
    }

    struct timeval tv;
    gettimeofday( &tv, NULL );

    return ( int64_t )tv.tv_sec * 1'000 + tv.tv_usec / 1'000;
}

static void database_on_row ( float temp, float hum, bool dht_valid, int mic_peak, const char* ir ) {
    struct DbRow row = {
        .ts_ms = db_epoch_ms(),
        .uptime_ms = esp_timer_get_time() / 1'000,
        .temp = temp,
        .hum = hum,
        .dht_valid = dht_valid,
        .mic_peak = mic_peak,
    };

    strlcpy( row.ir, ir, sizeof( row.ir ) );

    portENTER_CRITICAL( &db_mux );

    if ( db_count == DB_BUFFER_ROWS ) {
        db_head = ( db_head + 1 ) % DB_BUFFER_ROWS;
        --db_count;
        ++db_dropped;
    }

    db_rows[( db_head + db_count ) % DB_BUFFER_ROWS] = row;
    ++db_count;

    portEXIT_CRITICAL( &db_mux );

    if ( db_task_handle ) {
        xTaskNotifyGive( db_task_handle );
    }
}

static int db_row_json ( char* out, size_t len, const struct DbRow* r ) {
    int n = 0;

    if ( r->ts_ms > 0 ) {
        n += snprintf( out + n, len - n, "{\"ts\":%lld", r->ts_ms );
    } else {
        n += snprintf( out + n, len - n, "{\"ts\":null" );
    }

    n += snprintf( out + n, len - n, ",\"uptime_ms\":%lld", r->uptime_ms );

    if ( r->dht_valid ) {
        n += snprintf( out + n, len - n, ",\"temperature\":%.1f,\"humidity\":%.1f", r->temp, r->hum );
    } else {
        n += snprintf( out + n, len - n, ",\"temperature\":null,\"humidity\":null" );
    }

    n += snprintf( out + n, len - n, ",\"mic_peak\":%d", r->mic_peak );

    if ( strcmp( r->ir, "none" ) != 0 ) {
        n += snprintf( out + n, len - n, ",\"ir_cmd\":\"%s\"}", r->ir );
    } else {
        n += snprintf( out + n, len - n, ",\"ir_cmd\":null}" );
    }

    return n;
}

static bool db_peek ( struct DbRow* out ) {
    bool has = false;

    portENTER_CRITICAL( &db_mux );

    if ( db_count > 0 ) {
        *out = db_rows[db_head];
        has = true;
    }

    portEXIT_CRITICAL( &db_mux );

    return has;
}

static void db_pop () {
    portENTER_CRITICAL( &db_mux );

    if ( db_count > 0 ) {
        db_head = ( db_head + 1 ) % DB_BUFFER_ROWS;
        --db_count;
    }

    portEXIT_CRITICAL( &db_mux );
}

static void database_task ( void* arg ) {
    char json[DB_ROW_JSON_LEN];
    struct DbRow row;

    while ( true ) {
        ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( DB_RETRY_MS ) );

        while ( db_online && esp_mqtt_client_get_outbox_size( db_client ) < DB_OUTBOX_MAX && db_peek( &row ) ) {
            int len = db_row_json( json, sizeof( json ), &row );

            if ( esp_mqtt_client_publish( db_client, DB_TOPIC, json, len, DB_QOS, 0 ) < 0 ) {
                break;
            }

            db_pop();
        }

        portENTER_CRITICAL( &db_mux );
        int dropped = db_dropped;
        db_dropped = 0;
        portEXIT_CRITICAL( &db_mux );

        if ( dropped > 0 ) {
            printf("Database: buffer was full, %d oldest rows dropped\n", dropped);
        }
    }
}

static void db_event_handler ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    switch ( ( esp_mqtt_event_id_t ) id ) {
    case MQTT_EVENT_CONNECTED:
        db_online = true;
        printf("Database: connected to %s\n", ADDRESS);
        esp_mqtt_client_publish( db_client, DB_STATUS_TOPIC, "online", 0, DB_QOS, 1 );

        if ( db_task_handle ) {
            xTaskNotifyGive( db_task_handle );
        }

        break;

    case MQTT_EVENT_DISCONNECTED:
        if ( db_online ) {
            printf("Database: disconnected\n");
        }

        db_online = false;
        break;

    default:
        break;
    }
}

static void db_on_ip ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    if ( db_client ) {
        return;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = ADDRESS,
        .credentials.client_id = DB_CLIENT_ID,
        .session.keepalive = DB_KEEPALIVE_S,
        .session.last_will = {
            .topic = DB_STATUS_TOPIC,
            .msg = "offline",
            .qos = DB_QOS,
            .retain = 1,
        },
    };

    db_client = esp_mqtt_client_init( &cfg );
    esp_mqtt_client_register_event( db_client, ESP_EVENT_ANY_ID, db_event_handler, NULL );
    esp_mqtt_client_start( db_client );
}

static void database_cfg () {
    if ( ADDRESS[0] == '\0' ) {
        printf("Database: ADDRESS not set, upload off\n");
        return;
    }

    xTaskCreate( database_task, "database", DB_TASK_STACK, NULL, 3, &db_task_handle );
    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, db_on_ip, NULL ) );
    logger_add_row_cb( database_on_row );

    printf("Database: MQTT to %s, topic %s\n", ADDRESS, DB_TOPIC);
}
