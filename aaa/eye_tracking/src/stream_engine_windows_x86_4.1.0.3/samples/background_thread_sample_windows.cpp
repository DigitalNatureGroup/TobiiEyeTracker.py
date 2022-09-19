#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>

#pragma warning( push )
#pragma warning( disable: 4548 4265 4255 4668 4355 4625 4571 4626 5026 5027 4365 ) // disable warnings triggered by STL code
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>
#pragma warning( pop )

#include <conio.h>


static auto list_devices( tobii_api_t* api )
{
    std::vector<std::string> result;
    auto error = tobii_enumerate_local_device_urls( api,
        []( char const* url, void* user_data ) // Use a lambda for url receiver function
        {
            // Add url string to the supplied result vector
            auto list = (std::vector<std::string>*) user_data;
            list->push_back( url );
        }, &result );
    if( error != TOBII_ERROR_NO_ERROR ) std::cerr << "Failed to enumerate devices." << std::endl;

    return result;
}


static auto select_device( std::vector<std::string> const& devices )
{
    auto selection = 0u;
    // Present the available devices and loop until user has selected a valid device
    while( selection < 1 || selection > devices.size() )
    {
        std::cout << std::endl << "Select a device" << std::endl << std::endl;
        auto index = 0;
        for( auto t : devices ) std::cout << ++index << ". " << t << std::endl;

        if( !( std::cin >> selection ) )
        {
            // Clean stdin before reading new value
            std::cin.clear();
            std::cin.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
        }
    }

    return devices[ selection - 1 ];
}


static void log( void* log_context, tobii_log_level_t level, char const* text )
{
    // Synchronize access to the log printout
    std::lock_guard<std::mutex> lock(*(std::mutex*)log_context);

    if( level == TOBII_LOG_LEVEL_ERROR )
        std::cerr << "Logged error: " << text << std::endl;
}


static auto device_connect( tobii_api_t *api, std::string url, const unsigned int retries, const unsigned int interval, tobii_device_t **device )
{
    std::cout << "Connecting to " << url << " (trying " << retries << " times with " << interval << " milliseconds intervals)." << std::endl;

    unsigned int retry = 0;
    auto error = TOBII_ERROR_NO_ERROR;
    do
    {
        error = tobii_device_create( api, url.c_str(), TOBII_FIELD_OF_USE_INTERACTIVE, device );
        if( ( error != TOBII_ERROR_CONNECTION_FAILED ) && ( error != TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) ) break;
        std::this_thread::sleep_for( std::chrono::milliseconds( interval ) );
        ++retry;
    } while ( retry < retries );

    if( error != TOBII_ERROR_NO_ERROR )
    {
        std::cerr << "Failed connecting to " << url;
        if( (error == TOBII_ERROR_CONNECTION_FAILED ) || ( error == TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) )
        {
            std::cerr << " (tried " << retries << " times with " << interval << " milliseconds intervals)";
        }
        std::cerr << "." << std::endl;
    }

    return error;
}


static auto device_reconnect( tobii_device_t* device, const unsigned int retries, const unsigned int interval )
{
    std::cout << "Reconnecting (trying " << retries << " times with " << interval << " milliseconds intervals)." << std::endl;

    unsigned int retry = 0;
    auto error = TOBII_ERROR_NO_ERROR;
    do
    {
        error = tobii_device_reconnect( device );
        if( ( error != TOBII_ERROR_CONNECTION_FAILED ) && ( error != TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) ) break;
        std::this_thread::sleep_for( std::chrono::milliseconds( interval ) );
        ++retry;
    } while ( retry < retries );

    if( error != TOBII_ERROR_NO_ERROR )
    {
        std::cerr << "Failed reconnecting";
        if( (error == TOBII_ERROR_CONNECTION_FAILED ) || ( error == TOBII_ERROR_FIRMWARE_UPGRADE_IN_PROGRESS ) )
        {
            std::cerr << " (tried " << retry << " times with " << interval << " milliseconds intervals)";
        }
        std::cerr << "." << std::endl;
    }

    return error;
}


int background_thread_sample_main( void )
{
    // Create log mutex used for thread synchronization in log function
    std::mutex log_mutex;
    tobii_custom_log_t custom_log { &log_mutex, log };

    tobii_api_t* api;
    auto error = tobii_api_create( &api, nullptr, &custom_log );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        std::cerr << "Failed to initialize the Tobii Stream Engine API." << std::endl;
        return 1;
    }

    auto devices = list_devices( api );
    if( devices.size() == 0 )
    {
        std::cerr << "No stream engine compatible device(s) found." << std::endl;
        tobii_api_destroy( api );
        return 1;
    }

    auto selected_device = devices.size() == 1 ? devices[ 0 ] : select_device( devices );
    // Try to connect during 30 seconds with an interval of 100 milliseconds
    const unsigned int retries = 300;
    const unsigned int interval = 100;
    tobii_device_t* device;
    error = device_connect( api, selected_device, retries, interval, &device );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        tobii_api_destroy( api );
        return 1;
    }

    // Create atomic used for inter thread communication
    std::atomic<bool> exit_thread( false );
    // Start the background processing thread before subscribing to data
    std::thread thread(
        [&exit_thread, device, interval, retries]()
        {
            while( !exit_thread )
            {
                // Do a timed blocking wait for new gaze data, will time out after some hundred milliseconds
                auto error = tobii_wait_for_callbacks( 1, &device );

                if( error == TOBII_ERROR_TIMED_OUT ) continue; // If timed out, redo the wait for callbacks call
                else if( error != TOBII_ERROR_NO_ERROR )
                {
                    std::cerr << "tobii_wait_for_callbacks failed: " << tobii_error_message( error ) << "." << std::endl;
                    return;
                }
                // Calling this function will execute the subscription callback functions
                error = tobii_device_process_callbacks( device );

                if( error == TOBII_ERROR_CONNECTION_FAILED )
                {
                    // Block here while attempting reconnect, if it fails, exit the thread
                    error = device_reconnect( device, interval, retries );
                    if( error != TOBII_ERROR_NO_ERROR )
                    {
                        std::cerr << "Connection was lost and reconnection failed." << std::endl;
                        return;
                    }
                    continue;
                }
                else if( error != TOBII_ERROR_NO_ERROR )
                {
                    std::cerr << "tobii_device_process_callbacks failed: " << tobii_error_message( error ) << "." << std::endl;
                    return;
                }
            }
        } );
    // Start subscribing to gaze and supply lambda callback function to handle the gaze point data
    error = tobii_gaze_point_subscribe( device,
        []( tobii_gaze_point_t const* gaze_point, void* user_data )
        {
            (void) user_data; // Unused parameter
            if( gaze_point->validity == TOBII_VALIDITY_VALID )
                std::cout << "Gaze point: " << gaze_point->timestamp_us << " " << gaze_point->position_xy[ 0 ]
                    << ", " << gaze_point->position_xy[ 1 ] << std::endl;
            else
                std::cout << "Gaze point: " << gaze_point->timestamp_us << " INVALID" << std::endl;
        }, nullptr );
    if( error != TOBII_ERROR_NO_ERROR )
    {
        std::cerr << "Failed to subscribe to gaze stream." << std::endl;
        exit_thread = true;
        thread.join();
        tobii_device_destroy( device );
        tobii_api_destroy( api );
        return 1;
    }

    // Main loop
    while( !_kbhit() )
    {
        // Normally, main thread would do things here - simulate this with a sleep
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }

    // Cleanup subscriptions and resources
    error = tobii_gaze_point_unsubscribe( device );
    if( error != TOBII_ERROR_NO_ERROR )
        std::cerr << "Failed to unsubscribe from gaze stream." << std::endl;

    exit_thread = true;
    thread.join();

    error = tobii_device_destroy( device );
    if( error != TOBII_ERROR_NO_ERROR )
        std::cerr << "Failed to destroy device." << std::endl;

    error = tobii_api_destroy( api );
    if( error != TOBII_ERROR_NO_ERROR )
        std::cerr << "Failed to destroy API." << std::endl;

    return 0;
}
