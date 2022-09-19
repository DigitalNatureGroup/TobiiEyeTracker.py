#include <tobii/tobii.h>
#include <tobii/tobii_wearable.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#pragma warning( push )
#pragma warning( disable: 4255 4668 )
#include <windows.h>
#pragma warning( pop )


static void wearable_callback( tobii_wearable_consumer_data_t const* wearable_data, void* user_data )
{
    // Store the latest wearable data in the supplied storage
    tobii_wearable_consumer_data_t* wearable_data_storage = (tobii_wearable_consumer_data_t*) user_data;
    *wearable_data_storage = *wearable_data;
}

struct url_receiver_context_t
{
    char** urls;
    int capacity;
    int count;
};

static void url_receiver( char const* url, void* user_data )
{
    // The memory context is passed through the user_data void pointer
    struct url_receiver_context_t* context = (struct url_receiver_context_t*) user_data;
    // Allocate more memory if maximum capacity has been reached
    if( context->count >= context->capacity )
    {
        context->capacity *= 2;
        char** urls = (char**) realloc( context->urls, sizeof( char* ) * context->capacity );
        if( !urls )
        {
            fprintf( stderr, "Allocation failed\n" );
            return;
        }
        context->urls = urls;
    }
    // Copy the url string input parameter to allocated memory
    size_t url_length = strlen( url ) + 1;
    context->urls[ context->count ] = malloc( url_length );
    if( !context->urls[ context->count ] )
    {
        fprintf( stderr, "Allocation failed\n" );
        return;
    }
    memcpy( context->urls[ context->count++ ], url, url_length );
}


struct device_list_t
{
    char** urls;
    int count;
};

static struct device_list_t list_devices( tobii_api_t* api )
{
    struct device_list_t list = { NULL, 0 };
    // Create a memory context that can be used by the url receiver callback
    struct url_receiver_context_t url_receiver_context;
    url_receiver_context.count = 0;
    url_receiver_context.capacity = 16;
    url_receiver_context.urls = (char**) malloc( sizeof( char* ) * url_receiver_context.capacity );
    if( !url_receiver_context.urls )
    {
        fprintf( stderr, "Allocation failed\n" );
        return list;
    }

    // Enumerate the connected devices, connected devices will be stored in the supplied memory context
    tobii_error_t error = tobii_enumerate_local_device_urls( api, url_receiver, &url_receiver_context );
    if( error != TOBII_ERROR_NO_ERROR ) fprintf( stderr, "Failed to enumerate devices.\n" );

    list.urls = url_receiver_context.urls;
    list.count = url_receiver_context.count;
    return list;
}


static void free_device_list( struct device_list_t* list )
{
    for( int i = 0; i < list->count; ++i )
        free( list->urls[ i ] );

    free( list->urls );
}


static char const* select_device( struct device_list_t* devices )
{
    int tmp = 0, selection = 0;

    // Present the available devices and loop until user has selected a valid device
    while( selection < 1 || selection > devices->count)
    {
        printf( "\nSelect a device\n\n" );
        for( int i = 0; i < devices->count; ++i ) printf( "%d. %s\n",  i + 1, devices->urls[ i ] ) ;
        if( scanf( "%d", &selection ) <= 0 ) while( ( tmp = getchar() ) != '\n' && tmp != EOF );
    }

    return devices->urls[ selection - 1 ];
}


struct thread_context_t
{
    tobii_device_t* device;
    HANDLE reconnect_event; // Used to signal that a reconnect is needed
    HANDLE timesync_event; // Timer event used to signal that time synchronization is needed
    HANDLE exit_event; // Used to signal that the background thead should exit
    volatile LONG is_reconnecting;
};


static DWORD WINAPI reconnect_and_timesync_thread( LPVOID param )
{
    struct thread_context_t* context = (struct thread_context_t*) param;

    HANDLE objects[ 3 ];
    objects[ 0 ] = context->reconnect_event;
    objects[ 1 ] = context->timesync_event;
    objects[ 2 ] = context->exit_event;

    for( ; ; )
    {
        // Block here, waiting for one of the three events.
        DWORD result = WaitForMultipleObjects( 3, objects, FALSE, INFINITE );
        if( result == WAIT_OBJECT_0 ) // Handle reconnect event
        {
            fprintf( stdout, "Reconnecting.\n" );
            tobii_error_t error;
            // Run reconnect loop until connection succeeds or the exit event occurs
            while( ( error = tobii_device_reconnect( context->device ) ) != TOBII_ERROR_NO_ERROR )
            {
                if( ( error != TOBII_ERROR_CONNECTION_FAILED ) && ( error != TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) )
                    fprintf( stderr, "Reconnection failed: %s.\n", tobii_error_message( error ) );
                // Blocking waiting for exit event, timeout after 250 ms and retry reconnect
                result = WaitForSingleObject( context->exit_event, 250 ); // Retry every quarter of a second
                if( result == WAIT_OBJECT_0 )
                    return 0; // exit thread
            }
            InterlockedExchange( &context->is_reconnecting, 0L );
        }
        else if( result == WAIT_OBJECT_0 + 1 ) // Handle timesync event
        {
            tobii_error_t error = tobii_update_timesync( context->device );
            LARGE_INTEGER due_time;
            LONGLONG const timesync_update_interval = 300000000LL; // Time sync every 30 s
            LONGLONG const timesync_retry_interval = 1000000LL; // Retry time sync every 100 ms
            due_time.QuadPart = ( error == TOBII_ERROR_NO_ERROR ) ? -timesync_update_interval : -timesync_retry_interval;
            // Re-schedule timesync event
            SetWaitableTimer( context->timesync_event, &due_time, 0, NULL, NULL, FALSE );
        }
        else if( result == WAIT_OBJECT_0 + 2 ) // Handle exit event
        {
            // Exit requested, exiting the thread
            return 0;
        }
    }
}


static void log_func( void* log_context, tobii_log_level_t level, char const* text )
{
    CRITICAL_SECTION* log_mutex = (CRITICAL_SECTION*)log_context;

    EnterCriticalSection( log_mutex );
    if( level == TOBII_LOG_LEVEL_ERROR )
        fprintf( stderr, "Logged error: %s\n", text );
    LeaveCriticalSection( log_mutex );
}


static tobii_error_t device_connect( tobii_api_t *api, const char *url, const unsigned int retries, const unsigned int interval, tobii_device_t **device )
{
    fprintf( stdout, "Connecting to %s (trying %d times with %d milliseconds intervals).\n", url, retries, interval );

    unsigned int retry = 0;
    tobii_error_t error = TOBII_ERROR_NO_ERROR;
    do
    {
        error = tobii_device_create( api, url, TOBII_FIELD_OF_USE_INTERACTIVE, device );
        if( ( error != TOBII_ERROR_CONNECTION_FAILED ) && ( error != TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) ) break;
        Sleep( interval );
        ++retry;
    } while ( retry < retries );

    if( error != TOBII_ERROR_NO_ERROR )
    {
        fprintf( stderr, "Failed connecting to %s", url );
        if( (error == TOBII_ERROR_CONNECTION_FAILED ) || ( error == TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) )
        {
            fprintf( stderr, " (tried %d times with %d milliseconds intervals)", retries, interval );
        }
        fprintf( stderr, ".\n" );
    }

    return error;
}


int wearable_game_loop_sample_main( void )
{
    // Initialize critical section, used for thread synchronization in the log function
    static CRITICAL_SECTION log_mutex;
    InitializeCriticalSection( &log_mutex );
    tobii_custom_log_t custom_log = { &log_mutex, log_func };

    tobii_api_t* api;
    tobii_error_t error = tobii_api_create( &api, NULL, &custom_log );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        fprintf( stderr, "Failed to initialize the Tobii Stream Engine API.\n" );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    struct device_list_t devices = list_devices( api );
    if( devices.count == 0 )
    {
        fprintf( stderr, "No stream engine compatible device(s) found.\n" );
        free_device_list( &devices );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }
    char const* selected_device = devices.count == 1 ? devices.urls[ 0 ] : select_device( &devices );
    
    // Try to connect during 30 seconds with an interval of 100 milliseconds
    const unsigned int retries = 300;
    const unsigned int interval = 100;
    tobii_device_t* device;
    error = device_connect( api, selected_device, retries, interval, &device );
    free_device_list( &devices );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        fprintf( stderr, "Failed to initialize the device with url %s.\n", selected_device );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    tobii_wearable_consumer_data_t latest_wearable_data;
    latest_wearable_data.timestamp_us = 0LL;
    latest_wearable_data.gaze_direction_combined_validity = TOBII_VALIDITY_INVALID;
    latest_wearable_data.gaze_origin_combined_validity = TOBII_VALIDITY_INVALID;
    // Start subscribing to wearable data, in this sample we supply a tobii_wearable_data_t variable to store
    // latest value.
    error = tobii_wearable_consumer_data_subscribe( device, wearable_callback, &latest_wearable_data );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        fprintf( stderr, "Failed to subscribe to gaze stream.\n" );

        tobii_device_destroy( device );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    // Create event objects used for inter thread signaling
    HANDLE reconnect_event = CreateEvent( NULL, FALSE, FALSE, NULL );
    HANDLE timesync_event = CreateWaitableTimer( NULL, TRUE, NULL );
    HANDLE exit_event = CreateEvent( NULL, FALSE, FALSE, NULL );
    if( reconnect_event == NULL || timesync_event == NULL || exit_event == NULL )
    {
        fprintf( stderr, "Failed to create event objects.\n" );

        tobii_device_destroy( device );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    struct thread_context_t thread_context;
    thread_context.device = device;
    thread_context.reconnect_event = reconnect_event;
    thread_context.timesync_event = timesync_event;
    thread_context.exit_event = exit_event;
    thread_context.is_reconnecting = 0;
    // Create and run the reconnect and timesync thread
    HANDLE thread_handle = CreateThread( NULL, 0U, reconnect_and_timesync_thread, &thread_context, 0U, NULL );
    if( thread_handle == NULL )
    {
        fprintf( stderr, "Failed to create reconnect_and_timesync thread.\n" );

        tobii_device_destroy( device );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    LARGE_INTEGER due_time;
    LONGLONG const timesync_update_interval = 300000000LL; // time sync every 30 s
    due_time.QuadPart = -timesync_update_interval;
    // Schedule the time synchronization event
    BOOL timer_error = SetWaitableTimer( thread_context.timesync_event, &due_time, 0, NULL, NULL, FALSE );
    if( timer_error == FALSE )
    {
        fprintf( stderr, "Failed to schedule timer event.\n" );

        tobii_device_destroy( device );
        tobii_api_destroy( api );
        DeleteCriticalSection( &log_mutex );
        return 1;
    }

    // Main loop
    while( GetAsyncKeyState( VK_ESCAPE ) == 0 )
    {
        Sleep( 10 ); // Perform work i.e game loop code here - let's emulate it with a sleep

        // Only enter the next code section if not in reconnecting state
        if( !InterlockedCompareExchange( &thread_context.is_reconnecting, 0L, 0L ) )
        {
            error = tobii_device_process_callbacks( device );
            // TODO: Handle device_error

            if( error == TOBII_ERROR_CONNECTION_FAILED )
            {
                // Change state and signal that reconnect is needed.
                InterlockedExchange( &thread_context.is_reconnecting, 1L );
                SetEvent( thread_context.reconnect_event );
            }
            else if( error != TOBII_ERROR_NO_ERROR )
            {
                fprintf( stderr, "tobii_device_process_callbacks failed: %s.\n", tobii_error_message( error ) );
                break;
            }

            printf( "Gaze Direction: T %" PRIu64, latest_wearable_data.timestamp_us );
            if( latest_wearable_data.gaze_direction_combined_validity == TOBII_VALIDITY_VALID )
            {
                printf( "\tCombined {x:% 2.2f, y:% 2.2f, z:% 2.2f} ",
                    latest_wearable_data.gaze_direction_combined_normalized_xyz[ 0 ],
                    latest_wearable_data.gaze_direction_combined_normalized_xyz[ 1 ],
                    latest_wearable_data.gaze_direction_combined_normalized_xyz[ 2 ] );
            }
            else
            {
                printf( "\tCombined INVALID\t\t\t" );
            }

            printf( "\n" );
        }

        Sleep( 6 ); // Perform work which needs eye tracking data and the rest of the game loop
    }

    // Signal reconnect and timesync thread to exit and clean up event objects.
    SetEvent( thread_context.exit_event );
    WaitForSingleObject( thread_handle, INFINITE );
    CloseHandle( thread_handle );
    CloseHandle( timesync_event );
    CloseHandle( exit_event );
    CloseHandle( reconnect_event );

    error = tobii_wearable_consumer_data_unsubscribe( device );
    if( error != TOBII_ERROR_NO_ERROR )
        fprintf( stderr, "Failed to unsubscribe from wearable data stream.\n" );

    error = tobii_device_destroy( device );
    if( error != TOBII_ERROR_NO_ERROR )
        fprintf( stderr, "Failed to destroy device.\n" );

    error = tobii_api_destroy( api );
    if( error != TOBII_ERROR_NO_ERROR )
        fprintf( stderr, "Failed to destroy API.\n" );

    DeleteCriticalSection( &log_mutex );
    return 0;
}
