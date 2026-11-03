#include "sharp_ac_config.h"
#include "ir_util.h"
#include "logger_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// writes a value into some bits of a byte without messing up the other bits
static void sharp_ac_set_bits ( uint8_t* frame, int byte, int offset, int size, uint8_t value ) {
    uint8_t mask = ( ( 1 << size ) - 1 ) << offset;
    frame[byte] = ( frame[byte] & ~mask ) | ( ( value << offset ) & mask );
}

// reads some bits out of a byte
static uint8_t sharp_ac_get_bits ( const uint8_t* frame, int byte, int offset, int size ) {
    return ( frame[byte] >> offset ) & ( ( 1 << size ) - 1 );
}

// Sharp's checksum, XOR everything together then fold it down to 4 bits
static uint8_t sharp_ac_checksum ( const uint8_t* frame ) {
    uint8_t x = 0;

    for ( int i = 0; i < SHARP_AC_STATE_LEN - 1; ++i ) {
        x ^= frame[i];
    }

    // the low half of the last byte counts too, the high half is where the checksum goes
    x ^= frame[SHARP_AC_BYTE_CHECKSUM] & 0x0f;
    x ^= x >> 4;

    return x & 0x0f;
}

// prints the frame in hex
static void sharp_ac_print_frame ( const char* label, const uint8_t* frame ) {
    printf("%s:", label);

    for ( int i = 0; i < SHARP_AC_STATE_LEN; ++i ) {
        printf(" %02X", frame[i]);
    }

    printf("\n");
}

// mode as text for printing
static const char* sharp_ac_mode_name ( enum SharpAcMode mode ) {
    switch ( mode ) {
    case SHARP_AC_AUTO: return "auto";
    case SHARP_AC_HEAT: return "heat";
    case SHARP_AC_COOL: return "cool";
    case SHARP_AC_DRY: return "dry";
    }

    return "?";
}

// fan speed as text for printing
static const char* sharp_ac_fan_name ( enum SharpAcFan fan ) {
    switch ( fan ) {
    case SHARP_AC_FAN_AUTO: return "auto";
    case SHARP_AC_FAN_MIN: return "min";
    case SHARP_AC_FAN_MED: return "med";
    case SHARP_AC_FAN_HIGH: return "high";
    case SHARP_AC_FAN_MAX: return "max";
    }

    return "?";
}

// prints everything we know about the AC right now
static void sharp_ac_print_state () {
    printf(
        "Sharp AC: %s, mode %s, %d C, fan %s, swing %s, turbo %s, econo %s, ion %s\n",
        sharp_ac.power ? "ON" : "OFF",
        sharp_ac_mode_name( sharp_ac.mode ),
        sharp_ac.temp,
        sharp_ac_fan_name( sharp_ac.fan ),
        sharp_ac.swing ? "on" : "off",
        sharp_ac.turbo ? "on" : "off",
        sharp_ac.econo ? "on" : "off",
        sharp_ac.ion ? "on" : "off"
    );
}

// builds the frame from the current state, starting from the default one
static void sharp_ac_build_frame (
    enum SharpAcPower power,
    enum SharpAcSpecial special,
    enum SharpAcSwing swing
) {
    memcpy( sharp_ac_frame, SHARP_AC_DEFAULT_FRAME, SHARP_AC_STATE_LEN );

    // auto and dry don't let you pick the temp so we send 0 there
    bool fixed_temp = sharp_ac.mode == SHARP_AC_AUTO || sharp_ac.mode == SHARP_AC_DRY;
    uint8_t temp = fixed_temp ? 0 : sharp_ac.temp - SHARP_AC_MIN_TEMP;

    // put every setting where it belongs
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_TEMP, 0, 4, temp );
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_POWER, 4, 4, power );
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_MODE, 0, 2, sharp_ac.mode );
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_MODE, 4, 3, sharp_ac.fan );
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_SWING, 0, 3, swing );
    sharp_ac_frame[SHARP_AC_BYTE_SPECIAL] = special;
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_ION, 2, 1, sharp_ac.ion );

    // checksum goes last since it depends on everything else
    sharp_ac_set_bits( sharp_ac_frame, SHARP_AC_BYTE_CHECKSUM, 4, 4, sharp_ac_checksum( sharp_ac_frame ) );
}

// builds the frame and fires it at the AC
static esp_err_t sharp_ac_send (
    enum SharpAcPower power,
    enum SharpAcSpecial special,
    enum SharpAcSwing swing
) {
    sharp_ac_build_frame( power, special, swing );
    sharp_ac_print_frame( "Sharp AC TX", sharp_ac_frame );

    return ir_send_pulse_distance( &SHARP_AC_TIMING, sharp_ac_frame, SHARP_AC_BITS );
}

// for changing settings, if the AC is off we just save it and don't send anything
static esp_err_t sharp_ac_send_setting ( enum SharpAcSpecial special, enum SharpAcSwing swing ) {
    if ( !sharp_ac.power ) {
        printf("Sharp AC: off, setting saved for next power on\n");
        return ESP_OK;
    }

    return sharp_ac_send( SHARP_AC_POWER_ON, special, swing );
}

// turns the AC on or off, uses the from off code if it was off before
static esp_err_t sharp_ac_set_power ( bool on ) {
    bool was_on = sharp_ac.power;
    sharp_ac.power = on;

    enum SharpAcPower power = SHARP_AC_POWER_OFF;

    if ( on ) {
        power = was_on ? SHARP_AC_POWER_ON : SHARP_AC_POWER_ON_FROM_OFF;
    }

    return sharp_ac_send( power, SHARP_AC_SPECIAL_POWER, SHARP_AC_SWING_IGNORE );
}

// sets the temp, keeps it within 15 to 30
static esp_err_t sharp_ac_set_temp ( int temp ) {
    if ( temp < SHARP_AC_MIN_TEMP ) {
        temp = SHARP_AC_MIN_TEMP;
    }

    if ( temp > SHARP_AC_MAX_TEMP ) {
        temp = SHARP_AC_MAX_TEMP;
    }

    sharp_ac.temp = temp;

    return sharp_ac_send_setting( SHARP_AC_SPECIAL_POWER, SHARP_AC_SWING_IGNORE );
}

// changes the mode
static esp_err_t sharp_ac_set_mode ( enum SharpAcMode mode ) {
    sharp_ac.mode = mode;

    return sharp_ac_send_setting( SHARP_AC_SPECIAL_POWER, SHARP_AC_SWING_IGNORE );
}

// changes the fan speed, this also turns turbo off like the real remote does
static esp_err_t sharp_ac_set_fan ( enum SharpAcFan fan ) {
    sharp_ac.fan = fan;
    sharp_ac.turbo = false;

    return sharp_ac_send_setting( SHARP_AC_SPECIAL_FAN, SHARP_AC_SWING_IGNORE );
}

// turns swing on or off
static esp_err_t sharp_ac_set_swing ( bool on ) {
    sharp_ac.swing = on;

    return sharp_ac_send_setting(
        SHARP_AC_SPECIAL_SWING,
        on ? SHARP_AC_SWING_TOGGLE : SHARP_AC_SWING_OFF
    );
}

// turns the ion thing on or off
static esp_err_t sharp_ac_set_ion ( bool on ) {
    sharp_ac.ion = on;

    return sharp_ac_send_setting( SHARP_AC_SPECIAL_POWER, SHARP_AC_SWING_IGNORE );
}

// turbo on or off, turbo also means max fan
static esp_err_t sharp_ac_set_turbo ( bool on ) {
    sharp_ac.turbo = on;

    if ( on ) {
        sharp_ac.fan = SHARP_AC_FAN_MAX;
    }

    if ( !sharp_ac.power ) {
        return sharp_ac_send_setting( SHARP_AC_SPECIAL_TURBO, SHARP_AC_SWING_IGNORE );
    }

    // turbo uses the special power codes instead of the normal on code
    return sharp_ac_send(
        on ? SHARP_AC_POWER_SPECIAL_ON : SHARP_AC_POWER_SPECIAL_OFF,
        SHARP_AC_SPECIAL_TURBO,
        SHARP_AC_SWING_IGNORE
    );
}

// econo on or off, works the same way as turbo
static esp_err_t sharp_ac_set_econo ( bool on ) {
    sharp_ac.econo = on;

    if ( !sharp_ac.power ) {
        return sharp_ac_send_setting( SHARP_AC_SPECIAL_ECONO, SHARP_AC_SWING_IGNORE );
    }

    return sharp_ac_send(
        on ? SHARP_AC_POWER_SPECIAL_ON : SHARP_AC_POWER_SPECIAL_OFF,
        SHARP_AC_SPECIAL_ECONO,
        SHARP_AC_SWING_IGNORE
    );
}

// gets called for every IR frame we receive, if it's from the Sharp remote we update our state to match
static void sharp_ac_on_frame ( const rmt_symbol_word_t* symbols, size_t count ) {
    uint8_t frame[SHARP_AC_STATE_LEN];

    int bits = ir_decode_pulse_distance(
        &SHARP_AC_TIMING,
        symbols,
        count,
        frame,
        SHARP_AC_BITS
    );

    // not enough bits so it's some other remote, dump the raw timings in case we want them
    if ( bits != SHARP_AC_BITS ) {
        printf(
            "IR RX: not a Sharp AC frame, decoded %d of %d bits, type 'replay' to resend it\n",
            bits,
            SHARP_AC_BITS
        );
        ir_print_raw( symbols, count );
        logger_set_ir( "unknown" );
        return;
    }

    sharp_ac_print_frame( "Sharp AC RX", frame );

    // make sure the frame isn't corrupted
    uint8_t expected = sharp_ac_checksum( frame );
    uint8_t received = sharp_ac_get_bits( frame, SHARP_AC_BYTE_CHECKSUM, 4, 4 );

    if ( expected != received ) {
        printf("Sharp AC RX: checksum mismatch (got %X, expected %X)\n", received, expected);
        logger_set_ir( "sharp_bad_checksum" );
        return;
    }

    // only change power for the normal on and off codes, the special ones are turbo or econo stuff
    uint8_t power = sharp_ac_get_bits( frame, SHARP_AC_BYTE_POWER, 4, 4 );

    if ( power == SHARP_AC_POWER_OFF ) {
        sharp_ac.power = false;
    } else if ( power == SHARP_AC_POWER_ON || power == SHARP_AC_POWER_ON_FROM_OFF ) {
        sharp_ac.power = true;
    }

    // copy the rest of the settings from the frame
    sharp_ac.mode = sharp_ac_get_bits( frame, SHARP_AC_BYTE_MODE, 0, 2 );
    sharp_ac.fan = sharp_ac_get_bits( frame, SHARP_AC_BYTE_MODE, 4, 3 );
    sharp_ac.ion = sharp_ac_get_bits( frame, SHARP_AC_BYTE_ION, 2, 1 );

    // temp only means something in cool or heat
    if ( sharp_ac.mode == SHARP_AC_COOL || sharp_ac.mode == SHARP_AC_HEAT ) {
        sharp_ac.temp = sharp_ac_get_bits( frame, SHARP_AC_BYTE_TEMP, 0, 4 ) + SHARP_AC_MIN_TEMP;
    }

    sharp_ac_print_state();

    char command[LOG_IR_LEN];
    snprintf(
        command,
        sizeof( command ),
        "%s/%s/%dC/fan_%s",
        sharp_ac.power ? "on" : "off",
        sharp_ac_mode_name( sharp_ac.mode ),
        sharp_ac.temp,
        sharp_ac_fan_name( sharp_ac.fan )
    );
    logger_set_ir( command );
}

// turns "on" or "off" into a bool, returns false if it's neither
static bool sharp_ac_parse_on_off ( const char* arg, bool* out ) {
    if ( strcmp( arg, "on" ) == 0 ) {
        *out = true;
        return true;
    }

    if ( strcmp( arg, "off" ) == 0 ) {
        *out = false;
        return true;
    }

    return false;
}

// list of commands you can type in the console
static void sharp_ac_print_help () {
    printf(
        "Sharp AC commands:\n"
        "  on | off\n"
        "  temp <15-30>\n"
        "  mode auto|cool|heat|dry\n"
        "  fan auto|min|med|high|max\n"
        "  swing on|off\n"
        "  turbo on|off\n"
        "  econo on|off\n"
        "  ion on|off\n"
        "  status | replay | help\n"
    );
}

// takes one typed line and does whatever it says
static void sharp_ac_run_command ( char* line ) {
    // split it into the command and its argument
    char* cmd = strtok( line, " " );
    char* arg = strtok( NULL, " " );
    bool on = false;
    esp_err_t err = ESP_OK;

    if ( !cmd ) {
        return;
    }

    // power
    if ( strcmp( cmd, "on" ) == 0 ) {
        err = sharp_ac_set_power( true );
    } else if ( strcmp( cmd, "off" ) == 0 ) {
        err = sharp_ac_set_power( false );
    // temp
    } else if ( strcmp( cmd, "temp" ) == 0 && arg ) {
        err = sharp_ac_set_temp( atoi( arg ) );
    // mode
    } else if ( strcmp( cmd, "mode" ) == 0 && arg ) {
        if ( strcmp( arg, "auto" ) == 0 ) err = sharp_ac_set_mode( SHARP_AC_AUTO );
        else if ( strcmp( arg, "cool" ) == 0 ) err = sharp_ac_set_mode( SHARP_AC_COOL );
        else if ( strcmp( arg, "heat" ) == 0 ) err = sharp_ac_set_mode( SHARP_AC_HEAT );
        else if ( strcmp( arg, "dry" ) == 0 ) err = sharp_ac_set_mode( SHARP_AC_DRY );
        else { sharp_ac_print_help(); return; }
    // fan
    } else if ( strcmp( cmd, "fan" ) == 0 && arg ) {
        if ( strcmp( arg, "auto" ) == 0 ) err = sharp_ac_set_fan( SHARP_AC_FAN_AUTO );
        else if ( strcmp( arg, "min" ) == 0 ) err = sharp_ac_set_fan( SHARP_AC_FAN_MIN );
        else if ( strcmp( arg, "med" ) == 0 ) err = sharp_ac_set_fan( SHARP_AC_FAN_MED );
        else if ( strcmp( arg, "high" ) == 0 ) err = sharp_ac_set_fan( SHARP_AC_FAN_HIGH );
        else if ( strcmp( arg, "max" ) == 0 ) err = sharp_ac_set_fan( SHARP_AC_FAN_MAX );
        else { sharp_ac_print_help(); return; }
    // all the on and off toggles
    } else if ( strcmp( cmd, "swing" ) == 0 && arg && sharp_ac_parse_on_off( arg, &on ) ) {
        err = sharp_ac_set_swing( on );
    } else if ( strcmp( cmd, "turbo" ) == 0 && arg && sharp_ac_parse_on_off( arg, &on ) ) {
        err = sharp_ac_set_turbo( on );
    } else if ( strcmp( cmd, "econo" ) == 0 && arg && sharp_ac_parse_on_off( arg, &on ) ) {
        err = sharp_ac_set_econo( on );
    } else if ( strcmp( cmd, "ion" ) == 0 && arg && sharp_ac_parse_on_off( arg, &on ) ) {
        err = sharp_ac_set_ion( on );
    // stuff that doesn't send a Sharp frame
    } else if ( strcmp( cmd, "status" ) == 0 ) {
        sharp_ac_print_state();
        return;
    } else if ( strcmp( cmd, "replay" ) == 0 ) {
        err = ir_replay_last();
        printf("IR: replay %s\n", err == ESP_OK ? "sent" : "failed (nothing captured yet?)");
        return;
    // anything else just shows the help
    } else {
        sharp_ac_print_help();
        return;
    }

    if ( err != ESP_OK ) {
        printf("Sharp AC: send failed (%s)\n", esp_err_to_name( err ));
        return;
    }

    sharp_ac_print_state();
}

// reads what you type in the serial monitor one char at a time and runs it when you hit enter
static void sharp_ac_console_task ( void* arg ) {
    char line[SHARP_AC_CONSOLE_LINE_LEN];
    int len = 0;

    sharp_ac_print_help();

    while ( true ) {
        int c = getchar();

        // nothing typed yet, wait a bit so we don't hog the CPU
        if ( c == EOF ) {
            clearerr( stdin );
            vTaskDelay( pdMS_TO_TICKS( 20 ) );
            continue;
        }

        // enter was pressed, so run the line if there's anything in it
        if ( c == '\r' || c == '\n' ) {
            if ( len == 0 ) {
                continue;
            }

            printf("\n");
            line[len] = '\0';
            len = 0;

            sharp_ac_run_command( line );
            continue;
        }

        // add the char and echo it back so you can see what you typed
        if ( len < SHARP_AC_CONSOLE_LINE_LEN - 1 ) {
            line[len++] = ( char )c;
            putchar( c );
            fflush( stdout );
        }
    }
}

// starts the console task
static void start_sharp_ac_console () {
    xTaskCreate( sharp_ac_console_task, "sharp_ac_console", 4'096, NULL, 5, NULL );
}
