#include "oled_init.h"
#include "oled_display.h"

#include "mic_init.h"
#include "mic_util.h"

#include "speaker_init.h"
#include "speaker_util.h"

#include "dht_init.h"
#include "dht_util.h"

#include "blynk_init.h"
#include "blynk_util.h"

#include "ir_init.h"
#include "sharp_ac_util.h"

#include "logger_util.h"

#include "clock_util.h"
#include "automation_util.h"
#include "thingsboard_util.h"
#include "database_util.h"

#include <stdio.h>
#include <string.h>

// if the mic level goes above this we show the audio bars instead of the eyes, got this number by testing it manually irl
#define SOUND_THRESHOLD 60'000

// reads the DHT every few seconds, sends it to Blynk, and beeps if it got hotter or more humid
static void dht_speaker_task ( void* arg ) {
    // give the DHT a sec to wake up first
    vTaskDelay( pdMS_TO_TICKS( 1'000 ) );

    while ( true ) {
        struct DhtReading reading;
        esp_err_t err = read_dht( &reading );

        // bad read, just skip this round and try again later
        if ( err != ESP_OK ) {
            vTaskDelay( pdMS_TO_TICKS( DHT_INTERVAL_MS ) );
            continue;
        }

        // push the readings to the dashboard
        blynk_send_float( BLYNK_DS_TEMPERATURE, reading.temperature );
        blynk_send_float( BLYNK_DS_HUMIDITY, reading.humidity );
        logger_set_dht( reading.temperature, reading.humidity );

        // beep if either one went up compared to the last reading
        // if (
        //     reading.temperature > dht_last.temperature
        //     || reading.humidity > dht_last.humidity
        // ) {
        //     startup_chime();
        // }
        
        dht_last = reading;

        vTaskDelay( pdMS_TO_TICKS( DHT_INTERVAL_MS ) );
    }
}

// gets called whenever Blynk sends something down to us, for now it just prints it
static void on_blynk_downlink ( const char* datastream, const char* value ) {
    printf("Blynk: %s = %s\n", datastream, value);

    if ( strcmp( datastream, BLYNK_DS_AUTOMATION ) == 0 ) {
        automation_set_enabled( value[0] == '1' );
    }
}

// runs the DHT and speaker stuff in its own task so it doesn't block the main loop
static void start_dht_speaker_loop () {
    xTaskCreate( dht_speaker_task, "dht_speaker", 4'096, NULL, 5, NULL );
}

void app_main() {
    // set up the screen first so we can see stuff early
    oled_cfg();
    oled_startup_commands();
    
    mic_cfg();

    speaker_cfg();
    voice_cfg();

    dht_cfg();

    // wifi and Blynk, also tells it what to do when Blynk sends us data
    sharp_ac_lock_init();

    blynk_cfg( on_blynk_downlink );
    clock_cfg();
    automation_cfg();
    thingsboard_cfg( automation_is_enabled, automation_set_enabled );
    speaker_set_state_cb( thingsboard_note_speaker );
    database_cfg();

    // IR stuff, listens for remote frames and opens the serial console for the AC
    ir_cfg();
    start_ir_listen_loop( sharp_ac_on_frame );
    start_sharp_ac_console();

    start_dht_speaker_loop();
    start_logger_loop();

    // main loop, reads the mic and decides what to show on the screen
    while (true) {
        struct AudioLevel level = read_audio();
        logger_add_audio( level.level_left, level.level_right );
        // printf("L: %d  R: %d\n", level.level_left, level.level_right);

        blynk_send_int( BLYNK_DS_AUDIO_LEVEL, level.level_left );

        // if loud enough, show the bars, otherwise just do the blinking eyes
        bool loud = level.level_left > SOUND_THRESHOLD
                || level.level_right > SOUND_THRESHOLD;

        thingsboard_note_oled( loud );

        if ( loud ) {
            audio_display(
                to_bar(level.level_left),
                to_bar(level.level_right)
            );
        } else {
            blinking_eyes();
        }
    }
}