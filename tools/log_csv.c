#include <windows.h>
#include <stdio.h>
#include <string.h>

#define LINE_LEN 512
#define CSV_PREFIX "CSV,"
#define CSV_HEADER "uptime_ms,temp_c,humidity_pct,mic_peak,ir_cmd"

static volatile LONG running = 1;

static BOOL WINAPI on_ctrl ( DWORD type ) {
    InterlockedExchange( &running, 0 );
    return TRUE;
}

static HANDLE open_port ( const char* port ) {
    char path[64];
    snprintf( path, sizeof( path ), "\\\\.\\%s", port );

    HANDLE h = CreateFileA( path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return h;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof( dcb );
    GetCommState( h, &dcb );
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 200;

    if ( !SetCommState( h, &dcb ) || !SetCommTimeouts( h, &timeouts ) ) {
        CloseHandle( h );
        return INVALID_HANDLE_VALUE;
    }

    return h;
}

static FILE* open_csv ( const char* path ) {
    FILE* f = fopen( path, "ab" );

    if ( !f ) {
        return NULL;
    }

    fseek( f, 0, SEEK_END );

    if ( ftell( f ) == 0 ) {
        fprintf( f, "pc_time," CSV_HEADER "\r\n" );
        fflush( f );
    }

    return f;
}

static void handle_line ( FILE* out, const char* line ) {
    size_t prefix_len = strlen( CSV_PREFIX );

    if ( strncmp( line, CSV_PREFIX, prefix_len ) != 0 ) {
        return;
    }

    const char* row = line + prefix_len;

    if ( strcmp( row, CSV_HEADER ) == 0 ) {
        return;
    }

    SYSTEMTIME t;
    GetLocalTime( &t );

    char stamp[32];
    snprintf(
        stamp,
        sizeof( stamp ),
        "%04d-%02d-%02d %02d:%02d:%02d",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond
    );

    fprintf( out, "%s,%s\r\n", stamp, row );
    fflush( out );
    printf( "%s,%s\n", stamp, row );
}

int main ( int argc, char** argv ) {
    const char* port = argc > 1 ? argv[1] : "COM8";
    const char* csv_path = argc > 2 ? argv[2] : "data.csv";

    HANDLE h = open_port( port );

    if ( h == INVALID_HANDLE_VALUE ) {
        fprintf( stderr, "Could not open %s (error %lu). Is the serial monitor still open?\n", port, GetLastError() );
        return 1;
    }

    FILE* out = open_csv( csv_path );

    if ( !out ) {
        fprintf( stderr, "Could not open %s for writing\n", csv_path );
        CloseHandle( h );
        return 1;
    }

    SetConsoleCtrlHandler( on_ctrl, TRUE );
    printf( "Logging %s to %s, press Ctrl+C to stop\n", port, csv_path );

    char line[LINE_LEN];
    size_t len = 0;
    char buf[256];

    while ( running ) {
        DWORD got = 0;

        if ( !ReadFile( h, buf, sizeof( buf ), &got, NULL ) ) {
            fprintf( stderr, "Read failed (error %lu), was the board unplugged?\n", GetLastError() );
            break;
        }

        for ( DWORD i = 0; i < got; ++i ) {
            char c = buf[i];

            if ( c == '\r' ) {
                continue;
            }

            if ( c == '\n' ) {
                line[len] = '\0';
                handle_line( out, line );
                len = 0;
                continue;
            }

            if ( len < LINE_LEN - 1 ) {
                line[len++] = c;
            }
        }
    }

    fclose( out );
    CloseHandle( h );
    printf( "Stopped\n" );

    return 0;
}
