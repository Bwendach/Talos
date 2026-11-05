#pragma once

#include "blynk_config.h"

#include <stdio.h>

// are we connected to Blynk or not
static bool blynk_connected () {
    return mqtt_ok;
}

// sends a value to a datastream, does nothing if we're not connected
static void blynk_send_str ( const char* datastream, const char* value ) {
    if ( !mqtt_ok ) {
        return;
    }

    char topic[BLYNK_TOPIC_LEN];
    snprintf( topic, sizeof( topic ), BLYNK_UPLINK_DS_PREFIX "%s", datastream );

    esp_mqtt_client_publish( blynk_client, topic, value, 0, 0, 0 );
}

// same thing but for floats, 2 decimals is enough
static void blynk_send_float ( const char* datastream, float value ) {
    char payload[BLYNK_VALUE_LEN];
    snprintf( payload, sizeof( payload ), "%.2f", value );

    blynk_send_str( datastream, payload );
}

// same thing but for ints
static void blynk_send_int ( const char* datastream, int value ) {
    char payload[BLYNK_VALUE_LEN];
    snprintf( payload, sizeof( payload ), "%d", value );

    blynk_send_str( datastream, payload );
}
