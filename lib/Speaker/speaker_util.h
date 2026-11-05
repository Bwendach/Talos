#pragma once

#include "speaker_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

typedef void ( *speaker_state_cb_t )( bool playing );

static speaker_state_cb_t speaker_state_cb = NULL;

static void speaker_set_state_cb ( speaker_state_cb_t cb ) {
    speaker_state_cb = cb;
}

// buffer for the samples, times 2 because it's stereo
static int16_t spk_buf[SPK_NUM_FRAMES * 2];

// plays a sine wave at this frequency for this long, volume goes from 0 to 100
static void play_tone ( int freq_hz, int duration_ms, int volume ) {
    if ( volume > 100 ) {
        volume = 100;
    }

    // how loud the wave is and how much the phase moves each sample
    const float amplitude = 32'767.0f * volume / 100.0f;
    const float step = 2.0f * M_PI * freq_hz / SPK_SAMPLE_RATE;
    float phase = 0.0f;

    int total_frames = SPK_SAMPLE_RATE * duration_ms / 1'000;
    bool audible = freq_hz > 0 && volume > 0;

    if ( audible && speaker_state_cb ) {
        speaker_state_cb( true );
    }

    // fill the buffer in chunks and send them until we've played the whole thing
    while ( total_frames > 0 ) {
        int frames = total_frames < SPK_NUM_FRAMES ? total_frames : SPK_NUM_FRAMES;

        // same sample on both left and right
        for ( int i = 0; i < frames; ++i ) {
            int16_t s = ( int16_t )( amplitude * sinf( phase ) );

            spk_buf[2 * i] = s;
            spk_buf[2 * i + 1] = s;

            // wrap the phase so the float doesn't keep growing and lose precision
            phase += step;
            if ( phase >= 2.0f * M_PI ) {
                phase -= 2.0f * M_PI;
            }
        }

        size_t bytes_written = 0;
        i2s_channel_write( tx, spk_buf, frames * 2 * sizeof( int16_t ), &bytes_written, portMAX_DELAY );

        total_frames -= frames;
    }

    if ( audible && speaker_state_cb ) {
        speaker_state_cb( false );
    }
}

// just a tone with 0 volume, basically a pause
static void play_silence ( int duration_ms ) {
    play_tone( 0, duration_ms, 0 );
}

static void play_pcm_mono ( const uint8_t* pcm, size_t count, int volume ) {
    if ( volume > 100 ) {
        volume = 100;
    }

    if ( speaker_state_cb ) {
        speaker_state_cb( true );
    }

    while ( count > 0 ) {
        size_t frames = count < SPK_NUM_FRAMES ? count : SPK_NUM_FRAMES;

        for ( size_t i = 0; i < frames; ++i ) {
            int16_t raw = ( int16_t )( pcm[2 * i] | ( pcm[2 * i + 1] << 8 ) );
            int16_t s = ( int16_t )( ( int32_t )raw * volume / 100 );

            spk_buf[2 * i] = s;
            spk_buf[2 * i + 1] = s;
        }

        size_t bytes_written = 0;
        i2s_channel_write( tx, spk_buf, frames * 2 * sizeof( int16_t ), &bytes_written, portMAX_DELAY );

        pcm += frames * 2;
        count -= frames;
    }

    play_silence( 50 );

    if ( speaker_state_cb ) {
        speaker_state_cb( false );
    }
}

// two quick beeps going up
static void startup_chime () {
    play_tone( 880, 300, 60 );
    play_silence( 100 );
    play_tone( 1'320, 500, 60 );
    play_silence( 100 );
}

// how long to wait between chimes in the test loop
#define CHIME_INTERVAL_MS 1'000

// keeps playing the chime forever, was just for testing the speaker
static void chime_task ( void* arg ) {
    while ( true ) {
        startup_chime();
        vTaskDelay( pdMS_TO_TICKS( CHIME_INTERVAL_MS ) );
    }
}

// starts the test chime task
static void start_chime_loop () {
    xTaskCreate( chime_task, "chime", 4'096, NULL, 5, NULL );
}
