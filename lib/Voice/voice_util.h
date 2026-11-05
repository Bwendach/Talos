#pragma once

#include "speaker_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VOICE_VOLUME 70
#define VOICE_QUEUE_LEN 4
#define VOICE_SAMPLE_RATE 16000

enum VoiceClip {
    VOICE_AC_OFF,
    VOICE_AC_ON_16,
    VOICE_SET_19,
    VOICE_SET_20,
    VOICE_SET_21,
    VOICE_CLIP_COUNT,
};

extern const uint8_t voice_ac_off_start[] asm( "_binary_ac_off_wav_start" );
extern const uint8_t voice_ac_off_end[] asm( "_binary_ac_off_wav_end" );
extern const uint8_t voice_ac_on_16_start[] asm( "_binary_ac_on_16_wav_start" );
extern const uint8_t voice_ac_on_16_end[] asm( "_binary_ac_on_16_wav_end" );
extern const uint8_t voice_set_19_start[] asm( "_binary_set_19_wav_start" );
extern const uint8_t voice_set_19_end[] asm( "_binary_set_19_wav_end" );
extern const uint8_t voice_set_20_start[] asm( "_binary_set_20_wav_start" );
extern const uint8_t voice_set_20_end[] asm( "_binary_set_20_wav_end" );
extern const uint8_t voice_set_21_start[] asm( "_binary_set_21_wav_start" );
extern const uint8_t voice_set_21_end[] asm( "_binary_set_21_wav_end" );

struct VoiceWav {
    const uint8_t* start;
    const uint8_t* end;
    const char* name;
};

static const struct VoiceWav voice_wavs[VOICE_CLIP_COUNT] = {
    [VOICE_AC_OFF] = { voice_ac_off_start, voice_ac_off_end, "ac_off" },
    [VOICE_AC_ON_16] = { voice_ac_on_16_start, voice_ac_on_16_end, "ac_on_16" },
    [VOICE_SET_19] = { voice_set_19_start, voice_set_19_end, "set_19" },
    [VOICE_SET_20] = { voice_set_20_start, voice_set_20_end, "set_20" },
    [VOICE_SET_21] = { voice_set_21_start, voice_set_21_end, "set_21" },
};

static QueueHandle_t voice_queue = NULL;

static uint32_t voice_u32 ( const uint8_t* p ) {
    return p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( ( uint32_t )p[3] << 24 );
}

static uint16_t voice_u16 ( const uint8_t* p ) {
    return p[0] | ( p[1] << 8 );
}

static bool voice_find_pcm ( const struct VoiceWav* wav, const uint8_t** pcm, size_t* count ) {
    const uint8_t* p = wav->start;
    const uint8_t* end = wav->end;
    bool format_ok = false;

    if ( end - p < 12 || memcmp( p, "RIFF", 4 ) != 0 || memcmp( p + 8, "WAVE", 4 ) != 0 ) {
        return false;
    }

    p += 12;

    while ( end - p >= 8 ) {
        uint32_t size = voice_u32( p + 4 );
        const uint8_t* body = p + 8;

        if ( ( size_t )( end - body ) < size ) {
            return false;
        }

        if ( memcmp( p, "fmt ", 4 ) == 0 && size >= 16 ) {
            format_ok = voice_u16( body ) == 1
                && voice_u16( body + 2 ) == 1
                && voice_u32( body + 4 ) == VOICE_SAMPLE_RATE
                && voice_u16( body + 14 ) == 16;
        } else if ( memcmp( p, "data", 4 ) == 0 ) {
            *pcm = body;
            *count = size / 2;
            return format_ok;
        }

        p = body + size + ( size & 1 );
    }

    return false;
}

static void voice_task ( void* arg ) {
    enum VoiceClip clip;

    while ( true ) {
        if ( xQueueReceive( voice_queue, &clip, portMAX_DELAY ) != pdTRUE ) {
            continue;
        }

        const uint8_t* pcm;
        size_t count;

        if ( !voice_find_pcm( &voice_wavs[clip], &pcm, &count ) ) {
            printf("Voice: %s is not a 16 kHz mono 16-bit WAV\n", voice_wavs[clip].name);
            continue;
        }

        play_pcm_mono( pcm, count, VOICE_VOLUME );
    }
}

static void voice_say ( enum VoiceClip clip ) {
    if ( !voice_queue || clip < 0 || clip >= VOICE_CLIP_COUNT ) {
        return;
    }

    if ( xQueueSend( voice_queue, &clip, 0 ) != pdTRUE ) {
        printf("Voice: queue full, skipped %s\n", voice_wavs[clip].name);
    }
}

static void voice_cfg () {
    voice_queue = xQueueCreate( VOICE_QUEUE_LEN, sizeof( enum VoiceClip ) );
    xTaskCreate( voice_task, "voice", 4'096, NULL, 4, NULL );
}
