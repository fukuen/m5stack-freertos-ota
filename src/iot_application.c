/*
 * Amazon FreeRTOS V201912.00
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Copyright (C) 2020 fukuen.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file aws_iot_demo_shadow.c
 * @brief Demonstrates usage of the Thing Shadow library.
 *
 * This program demonstrates the using Shadow documents to toggle a state called
 * "powerOn" in a remote device.
 */

/* The config header is always included first. */
#include "iot_config.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* MQTT include. */
#include "iot_mqtt.h"

/* Shadow include. */
#include "aws_iot_shadow.h"

/* JSON utilities include. */
#include "iot_json_utils.h"

#include "iot_network_manager_private.h"
#include "iot_https_client.h"
#include "iot_https_utils.h"

#include "time.h"
#include <sys/time.h>

#include "aws_application_version.h"

/* Required to get the broker address and port. */
#include "aws_clientcredential.h"

/* Amazon FreeRTOS OTA agent includes. */
#include "aws_iot_ota_agent.h"

/**
 * @cond DOXYGEN_IGNORE
 * Doxygen should ignore this section.
 *
 * Provide default values for undefined configuration settings.
 */
#ifndef AWS_IOT_DEMO_SHADOW_UPDATE_COUNT
    #define AWS_IOT_DEMO_SHADOW_UPDATE_COUNT        ( 2 )
#endif
#ifndef AWS_IOT_DEMO_SHADOW_UPDATE_PERIOD_MS
    #define AWS_IOT_DEMO_SHADOW_UPDATE_PERIOD_MS    ( 3000 )
#endif
/** @endcond */

/* Validate Shadow demo configuration settings. */
#if AWS_IOT_DEMO_SHADOW_UPDATE_COUNT <= 0
    #error "AWS_IOT_DEMO_SHADOW_UPDATE_COUNT cannot be 0 or negative."
#endif
#if AWS_IOT_DEMO_SHADOW_UPDATE_PERIOD_MS <= 0
    #error "AWS_IOT_DEMO_SHADOW_UPDATE_PERIOD_MS cannot be 0 or negative."
#endif

/**
 * @brief The keep-alive interval used for this demo.
 *
 * An MQTT ping request will be sent periodically at this interval.
 */
#define KEEP_ALIVE_SECONDS    ( 60 )

/**
 * @brief The timeout for Shadow and MQTT operations in this demo.
 */
#define TIMEOUT_MS            ( 5000 )

/*-----------------------------------------------------------*/

#define otaDemoCONN_TIMEOUT_MS            ( 2000UL )

#define otaDemoCONN_RETRY_INTERVAL_MS     ( 5000 )

#define otaDemoCONN_RETRY_LIMIT           ( 100 )

#define otaDemoKEEPALIVE_SECONDS          ( 1200 )

#define myappONE_SECOND_DELAY_IN_TICKS    pdMS_TO_TICKS( 1000UL )

#define otaDemoNETWORK_TYPES              ( AWSIOT_NETWORK_TYPE_ALL )

IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;
static void App_OTACompleteCallback( OTA_JobEvent_t eEvent );


/**
 * @brief Format string representing a Shadow document with a "reported" state.
 *
 * Note the client token, which is required for all Shadow updates. The client
 * token must be unique at any given time, but may be reused once the update is
 * completed. For this demo, a timestamp is used for a client token.
 */
#define SHADOW_REPORTED_JSON        \
    "{"                             \
    "\"state\":{"                   \
    "\"reported\":{"                \
    "\"appVersion\":\"%d.%d.%d\","  \
    "\"lastConnect\":\"%24.24s\""   \
    "}"                             \
    "},"                            \
    "\"clientToken\":\"%06lu\""     \
    "}"

/**
 * @brief The expected size of #SHADOW_REPORTED_JSON.
 *
 * Because all the format specifiers in #SHADOW_REPORTED_JSON include a length,
 * its full size is known at compile-time.
 */
#define EXPECTED_REPORTED_JSON_SIZE    ( sizeof( SHADOW_REPORTED_JSON ) - 3 )

/*-----------------------------------------------------------*/

/* Declaration of demo function. */
int RunShadowDemo( bool awsIotMqttMode,
                   const char * pIdentifier,
                   void * pNetworkServerInfo,
                   void * pNetworkCredentialInfo,
                   const IotNetworkInterface_t * pNetworkInterface );

#define MAXITEM 20

struct timeval tv_now;

/*-----------------------------------------------------------*/
/**
 * @brief Split string by delimiter.
 *
 * @param[in] str Input string.
 * @param[in] delim Delimiter.
 * @param[out] outlist Output array.
 *
 * @return count of splited strings.
 */
int split( char *str, const char *delim, char *outlist[] )
{
    char    *tk;
    int     cnt = 0;

    tk = strtok( str, delim );
    while( tk != NULL && cnt < MAXITEM )
    {
        outlist[cnt++] = tk;
        tk = strtok( NULL, delim );
    }
    return cnt;
}

/*-----------------------------------------------------------*/
/**
 * @brief Set clock.
 *
 * @param[in] str Ntp time retrieved from ntp server.
 *
 * @return void
 */
void set_clock( char *str )
{
    int len = strlen( str );
    if ( len <= 0 )
    {
        return;
    }

    char *out[ MAXITEM ];
    int cnt = split( str, "." , out );
    unsigned long ntptime = 0UL;
    long ms = 0L;
    if ( cnt == 0 )
    {
        return;
    }
    else if ( cnt == 1 )
    {
        ntptime = strtoul( out[0], NULL, 0 );
    }
    else
    {
        ntptime = strtoul( out[0], NULL, 0 );
        ms      = strtoul( out[1], NULL, 0 );
        if ( strlen( out[1] ) == 1 )
        {
            ms = ms * 100;
        }
        else if ( strlen( out[1] ) == 2 )
        {
            ms = ms * 10;
        }
    }

	long posixtime = (long)(ntptime - 2208988800UL);
    tv_now.tv_sec = posixtime;
    tv_now.tv_usec = 0;
    configPRINTF( ( "ntptime: %u\n", ntptime ) );
    configPRINTF( ( "posixtime: %ld\n", posixtime ) );
    configPRINTF( ( "ms: %ld\n", ms ) );

    settimeofday( &tv_now, NULL );

    gettimeofday( &tv_now, NULL );
    configPRINTF( ( "%s\n", ctime(&tv_now.tv_sec) ) );
}

const char HTTPS_SERVER_ADDRESS[18] = "ntp-a1.nict.go.jp";
const int HTTP_TIMEOUT_MS = 10000;

IotHttpsConnectionInfo_t connInfo = IOT_HTTPS_CONNECTION_INFO_INITIALIZER;
IotHttpsConnectionHandle_t connHandle = IOT_HTTPS_CONNECTION_HANDLE_INITIALIZER;

/*-----------------------------------------------------------*/
/**
 * @brief HTTP connect.
 *
 * @param[in] pNetworkInterface Network Interface.
 *
 * @return void.
 */
static void prvHTTPSConnect( IotNetworkInterface_t* pNetworkInterface )
{
    IotHttpsClient_Init();

// An initialized network interface.
//    IotNetworkInterface_t* pNetworkInterface;


// Parameters to HTTPS Client connect.
    uint8_t* pConnUserBuffer = (uint8_t*)malloc(connectionUserBufferMinimumSize + 512);
    // Set the connection configuration information.
    connInfo.pAddress = HTTPS_SERVER_ADDRESS;
    connInfo.addressLen = strlen(HTTPS_SERVER_ADDRESS);
    connInfo.port = 80;
    connInfo.flags = IOT_HTTPS_IS_NON_TLS_FLAG;
    connInfo.pAlpnProtocols = NULL;
    connInfo.alpnProtocolsLen = 0;
    connInfo.pCaCert = NULL;
    connInfo.caCertLen = 0;
    connInfo.userBuffer.pBuffer = pConnUserBuffer;
    connInfo.userBuffer.bufferLen = connectionUserBufferMinimumSize + 512;
    connInfo.pClientCert = NULL;
    connInfo.clientCertLen = 0;
    connInfo.pPrivateKey = NULL;
    connInfo.privateKeyLen = 0;
    connInfo.pNetworkInterface = pNetworkInterface;
    IotHttpsReturnCode_t returnCode = IotHttpsClient_Connect(&connHandle, &connInfo);
    if( returnCode == IOT_HTTPS_OK )
    {
        configPRINTF( ( "Connected.\r\n" ) );
    }
    else
    {
        configPRINTF( ( "Connection error.\r\n" ) );
        configPRINTF( ( "Return code: %d\r\n", returnCode ) );
    }
}

/*-----------------------------------------------------------*/
/**
 * @brief HTTP request.
 *
 * @param[in] xMethod Request method.
 * @param[in] pcPath Request path.
 * @param[in] ulPathLength Request path length.
 *
 * @return void.
 */
static void prvHTTPRequest( IotHttpsMethod_t xMethod, 
                                   const char *pcPath, 
                                   uint32_t ulPathLength )
{
    IotHttpsReturnCode_t xHTTPClientResult;
    IotHttpsRequestInfo_t xHTTPRequestInfo = IOT_HTTPS_REQUEST_INFO_INITIALIZER;
    IotHttpsResponseInfo_t xHTTPResponseInfo = IOT_HTTPS_RESPONSE_INFO_INITIALIZER;
    IotHttpsRequestHandle_t xHTTPRequest = IOT_HTTPS_REQUEST_HANDLE_INITIALIZER;
    IotHttpsResponseHandle_t xHTTPResponse = IOT_HTTPS_RESPONSE_HANDLE_INITIALIZER;
    IotHttpsSyncInfo_t xHTTPSyncRequestInfo = IOT_HTTPS_SYNC_INFO_INITIALIZER;
    IotHttpsSyncInfo_t xHTTPSyncResponseInfo = IOT_HTTPS_SYNC_INFO_INITIALIZER;
//    char usResponseStatus = 0;

    uint8_t ucHTTPRequestUserBuffer[ requestUserBufferMinimumSize + 512 ];
    uint8_t cHTTPRequestBodyBuffer[ 512 ];
    uint8_t ucHTTPResponseUserBuffer[ responseUserBufferMinimumSize + 512 ];
    uint8_t ucHTTPResponseBodyBuffer[ 256 ];

	/************************** HTTP request setup. ***************************/

    if( ( xMethod == IOT_HTTPS_METHOD_PUT ) || ( xMethod == IOT_HTTPS_METHOD_POST ) )
    {
        xHTTPSyncRequestInfo.pBody = ( uint8_t* )cHTTPRequestBodyBuffer;
        xHTTPSyncRequestInfo.bodyLen = sizeof( cHTTPRequestBodyBuffer );
    }
    else
    {
        xHTTPSyncRequestInfo.pBody = NULL;
        xHTTPSyncRequestInfo.bodyLen = 0;
    }

    xHTTPRequestInfo.pHost = HTTPS_SERVER_ADDRESS;
    xHTTPRequestInfo.hostLen = sizeof( HTTPS_SERVER_ADDRESS ) - 1;
    xHTTPRequestInfo.pPath = pcPath;
    xHTTPRequestInfo.pathLen = ulPathLength;
    xHTTPRequestInfo.method = xMethod;
    xHTTPRequestInfo.isNonPersistent = false;
    xHTTPRequestInfo.userBuffer.pBuffer = ucHTTPRequestUserBuffer;
    xHTTPRequestInfo.userBuffer.bufferLen = sizeof( ucHTTPRequestUserBuffer ) - 1;
    xHTTPRequestInfo.isAsync = false;
    xHTTPRequestInfo.u.pSyncInfo = &xHTTPSyncRequestInfo;

    xHTTPClientResult = IotHttpsClient_InitializeRequest( &xHTTPRequest, &xHTTPRequestInfo );

    if( xHTTPClientResult == IOT_HTTPS_OK )
    {
        configPRINTF( ( "Initialize ok.\r\n" ) );
    }
    else
    {
        configPRINTF( ( "Initialize error.\r\n" ) );
        configPRINTF( ( "Result: %d\r\n", xHTTPClientResult ) );
    }

    /************************** HTTP response setup. **************************/

    memset( ucHTTPResponseBodyBuffer, 0, sizeof( ucHTTPResponseBodyBuffer ) );

    if( xMethod == IOT_HTTPS_METHOD_HEAD )
    {
        xHTTPSyncResponseInfo.pBody = NULL;
        xHTTPSyncResponseInfo.bodyLen = 0;
    }
    else
    {
        xHTTPSyncResponseInfo.pBody = ucHTTPResponseBodyBuffer;
        xHTTPSyncResponseInfo.bodyLen = sizeof( ucHTTPResponseBodyBuffer );
    }

    xHTTPResponseInfo.userBuffer.pBuffer = ucHTTPResponseUserBuffer;
    xHTTPResponseInfo.userBuffer.bufferLen = sizeof( ucHTTPResponseUserBuffer );
    xHTTPResponseInfo.pSyncInfo = &xHTTPSyncResponseInfo;

    /*************************** Send HTTP request. ***************************/

    /* This synchronous send function blocks until the full response is received
     * from the network. */
    xHTTPClientResult = IotHttpsClient_SendSync( connHandle, 
                                       xHTTPRequest, 
                                       &xHTTPResponse, 
                                       &xHTTPResponseInfo, 
                                       HTTP_TIMEOUT_MS );

    char    *lines[ MAXITEM ];
    int cnt = split( (char*)&ucHTTPResponseBodyBuffer, "\n" , lines );
    for( int i = 0; i < cnt; i++ ) {
        if( lines[i][0] != '<' )
        {
        	set_clock( lines[i] );
        }
    }

}

/*-----------------------------------------------------------*/
/**
 * @brief HTTP disconnect.
 *
 * @return void.
 */
static void prvHTTPSDisconnect( void )
{
    IotHttpsReturnCode_t xHTTPSClientResult;

    /* The application must always explicitly disconnect from the server with
     * this API if the last request sent on the connection was a persistent
     * request. */
    xHTTPSClientResult = IotHttpsClient_Disconnect( connHandle );
}

/*-----------------------------------------------------------*/

/**
 * @brief Parses the "state" key from the "previous" or "current" sections of a
 * Shadow updated document.
 *
 * @param[in] pUpdatedDocument The Shadow updated document to parse.
 * @param[in] updatedDocumentLength The length of `pUpdatedDocument`.
 * @param[in] pSectionKey Either "previous" or "current". Must be NULL-terminated.
 * @param[out] pState Set to the first character in "state".
 * @param[out] pStateLength Length of the "state" section.
 *
 * @return `true` if the "state" was found; `false` otherwise.
 */
static bool _getUpdatedState( const char * pUpdatedDocument,
                              size_t updatedDocumentLength,
                              const char * pSectionKey,
                              const char ** pState,
                              size_t * pStateLength )
{
    bool sectionFound = false, stateFound = false;
    const size_t sectionKeyLength = strlen( pSectionKey );
    const char * pSection = NULL;
    size_t sectionLength = 0;

    /* Find the given section in the updated document. */
    sectionFound = IotJsonUtils_FindJsonValue( pUpdatedDocument,
                                               updatedDocumentLength,
                                               pSectionKey,
                                               sectionKeyLength,
                                               &pSection,
                                               &sectionLength );

    if( sectionFound == true )
    {
        /* Find the "state" key within the "previous" or "current" section. */
        stateFound = IotJsonUtils_FindJsonValue( pSection,
                                                 sectionLength,
                                                 "state",
                                                 5,
                                                 pState,
                                                 pStateLength );
    }
    else
    {
        IotLogWarn( "Failed to find section %s in Shadow updated document.",
                    pSectionKey );
    }

    return stateFound;
}

/*-----------------------------------------------------------*/

/**
 * @brief Initialize the the MQTT library and the Shadow library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
static int _initializeDemo( void )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t mqttInitStatus = IOT_MQTT_SUCCESS;
    AwsIotShadowError_t shadowInitStatus = AWS_IOT_SHADOW_SUCCESS;

    /* Flags to track cleanup on error. */
    bool mqttInitialized = false;

    /* Initialize the MQTT library. */
    mqttInitStatus = IotMqtt_Init();

    if( mqttInitStatus == IOT_MQTT_SUCCESS )
    {
        mqttInitialized = true;
    }
    else
    {
        status = EXIT_FAILURE;
    }

    /* Initialize the Shadow library. */
    if( status == EXIT_SUCCESS )
    {
        /* Use the default MQTT timeout. */
        shadowInitStatus = AwsIotShadow_Init( 0 );

        if( shadowInitStatus != AWS_IOT_SHADOW_SUCCESS )
        {
            status = EXIT_FAILURE;
        }
    }

    /* Clean up on error. */
    if( status == EXIT_FAILURE )
    {
        if( mqttInitialized == true )
        {
            IotMqtt_Cleanup();
        }
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief Clean up the the MQTT library and the Shadow library.
 */
static void _cleanupDemo( void )
{
    AwsIotShadow_Cleanup();
    IotMqtt_Cleanup();
}

/*-----------------------------------------------------------*/

/**
 * @brief Establish a new connection to the MQTT server for the Shadow demo.
 *
 * @param[in] pIdentifier NULL-terminated MQTT client identifier. The Shadow
 * demo will use the Thing Name as the client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[out] pMqttConnection Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
static int _establishMqttConnection( const char * pIdentifier,
                                     void * pNetworkServerInfo,
                                     void * pNetworkCredentialInfo,
                                     const IotNetworkInterface_t * pNetworkInterface,
                                     IotMqttConnection_t * pMqttConnection )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t connectStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
    IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;

    if( pIdentifier == NULL )
    {
        IotLogError( "Shadow Thing Name must be provided." );

        status = EXIT_FAILURE;
    }

    if( status == EXIT_SUCCESS )
    {
        /* Set the members of the network info not set by the initializer. This
         * struct provided information on the transport layer to the MQTT connection. */
        networkInfo.createNetworkConnection = true;
        networkInfo.u.setup.pNetworkServerInfo = pNetworkServerInfo;
        networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
        networkInfo.pNetworkInterface = pNetworkInterface;

        #if ( IOT_MQTT_ENABLE_SERIALIZER_OVERRIDES == 1 ) && defined( IOT_DEMO_MQTT_SERIALIZER )
            networkInfo.pMqttSerializer = IOT_DEMO_MQTT_SERIALIZER;
        #endif

        /* Set the members of the connection info not set by the initializer. */
        connectInfo.awsIotMqttMode = true;
        connectInfo.cleanSession = true;
        connectInfo.keepAliveSeconds = KEEP_ALIVE_SECONDS;

        /* AWS IoT recommends the use of the Thing Name as the MQTT client ID. */
        connectInfo.pClientIdentifier = pIdentifier;
        connectInfo.clientIdentifierLength = ( uint16_t ) strlen( pIdentifier );

        IotLogInfo( "Shadow Thing Name is %.*s (length %hu).",
                    connectInfo.clientIdentifierLength,
                    connectInfo.pClientIdentifier,
                    connectInfo.clientIdentifierLength );

        /* Establish the MQTT connection. */
        connectStatus = IotMqtt_Connect( &networkInfo,
                                         &connectInfo,
                                         TIMEOUT_MS,
                                         pMqttConnection );

        if( connectStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "MQTT CONNECT returned error %s.",
                         IotMqtt_strerror( connectStatus ) );

            status = EXIT_FAILURE;
        }
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief Send the Shadow updates that will trigger the Shadow callbacks.
 *
 * @param[in] pDeltaSemaphore Used to synchronize Shadow updates with the delta
 * callback.
 * @param[in] mqttConnection The MQTT connection used for Shadows.
 * @param[in] pThingName The Thing Name for Shadows in this demo.
 * @param[in] thingNameLength The length of `pThingName`.
 *
 * @return `EXIT_SUCCESS` if all Shadow updates were sent; `EXIT_FAILURE`
 * otherwise.
 */
static int _sendShadowUpdates( IotSemaphore_t * pDeltaSemaphore,
                               IotMqttConnection_t mqttConnection,
                               const char * const pThingName,
                               size_t thingNameLength )
{
    int status = EXIT_SUCCESS;
    int32_t i = 0, desiredState = 0;
    AwsIotShadowError_t updateStatus = AWS_IOT_SHADOW_STATUS_PENDING;
    AwsIotShadowDocumentInfo_t updateDocument = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;

    /* A buffer containing the update document. It has static duration to prevent
     * it from being placed on the call stack. */
    static char pUpdateDocument[ 100 ] = { 0 };

    /* Set the common members of the Shadow update document info. */
    updateDocument.pThingName = pThingName;
    updateDocument.thingNameLength = thingNameLength;
    updateDocument.u.update.pUpdateDocument = pUpdateDocument;
    updateDocument.u.update.updateDocumentLength = EXPECTED_REPORTED_JSON_SIZE;

    /* Publish Shadow updates at a set period. */
    for( i = 1; i <= AWS_IOT_DEMO_SHADOW_UPDATE_COUNT; i++ )
    {
        /* Toggle the desired state. */
        desiredState = !( desiredState );

        /* Generate a Shadow desired state document, using a timestamp for the client
         * token. To keep the client token within 6 characters, it is modded by 1000000. */
        status = sprintf( pUpdateDocument,
                          SHADOW_REPORTED_JSON,
                          APP_VERSION_MAJOR,
                          APP_VERSION_MINOR,
                          APP_VERSION_BUILD,
                          ctime(&tv_now.tv_sec),
                          ( long unsigned ) ( IotClock_GetTimeMs() % 1000000 ) );
        updateDocument.u.update.updateDocumentLength = status;
        status = EXIT_SUCCESS;

        IotLogInfo( "Sending Shadow update %d of %d: %s",
                    i,
                    AWS_IOT_DEMO_SHADOW_UPDATE_COUNT,
                    pUpdateDocument );

        /* Send the Shadow update. Because the Shadow is constantly updated in
         * this demo, the "Keep Subscriptions" flag is passed to this function.
         * Note that this flag only needs to be passed on the first call, but
         * passing it for subsequent calls is fine.
         */
        updateStatus = AwsIotShadow_TimedUpdate( mqttConnection,
                                                 &updateDocument,
                                                 AWS_IOT_SHADOW_FLAG_KEEP_SUBSCRIPTIONS,
                                                 TIMEOUT_MS );

        /* Check the status of the Shadow update. */
        if( updateStatus != AWS_IOT_SHADOW_SUCCESS )
        {
            IotLogError( "Failed to send Shadow update %d of %d, error %s.",
                         i,
                         AWS_IOT_DEMO_SHADOW_UPDATE_COUNT,
                         AwsIotShadow_strerror( updateStatus ) );

            status = EXIT_FAILURE;
            break;
        }
        else
        {
            IotLogInfo( "Successfully sent Shadow update %d of %d.",
                        i,
                        AWS_IOT_DEMO_SHADOW_UPDATE_COUNT );

            /* Wait for the delta callback to change its state before continuing. */
//            if( IotSemaphore_TimedWait( pDeltaSemaphore, TIMEOUT_MS ) == false )
//            {
//                IotLogError( "Timed out waiting on delta callback to change state." );

//                status = EXIT_FAILURE;
//                break;
//            }
        }

        IotClock_SleepMs( AWS_IOT_DEMO_SHADOW_UPDATE_PERIOD_MS );
    }

    return status;
}

/*-----------------------------------------------------------*/

static const char * pcStateStr[ eOTA_AgentState_All ] =
{
    "Init",
    "Ready",
    "RequestingJob",
    "WaitingForJob",
    "CreatingFile",
    "RequestingFileBlock",
    "WaitingForFileBlock",
    "ClosingFile",
    "ShuttingDown",
    "Stopped"
};

void vRunOTAUpdateDemo( IotMqttConnection_t mqttConnection,
                        const IotNetworkInterface_t * pNetworkInterface,
                        void * pNetworkCredentialInfo )
{
//    IotMqttConnectInfo_t xConnectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;
    OTA_State_t eState;
    OTA_ConnectionContext_t xOTAConnectionCtx = { 0 };

    configPRINTF( ( "OTA demo version %u.%u.%u\r\n",
                    xAppFirmwareVersion.u.x.ucMajor,
                    xAppFirmwareVersion.u.x.ucMinor,
                    xAppFirmwareVersion.u.x.usBuild ) );
    configPRINTF( ( "Creating MQTT Client...\r\n" ) );

    xOTAConnectionCtx.pvControlClient = mqttConnection;
    xOTAConnectionCtx.pxNetworkInterface = ( void * ) pNetworkInterface;
    xOTAConnectionCtx.pvNetworkCredentials = pNetworkCredentialInfo;

    OTA_AgentInit( ( void * ) ( &xOTAConnectionCtx ), ( const uint8_t * ) ( clientcredentialIOT_THING_NAME ), App_OTACompleteCallback, ( TickType_t ) ~1 );

//    int cnt = 0;
//    while( ( eState = OTA_GetAgentState() ) != eOTA_AgentState_Stopped && cnt < 10 )
    while( ( eState = OTA_GetAgentState() ) != eOTA_AgentState_Stopped )
    {
        /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
        vTaskDelay( myappONE_SECOND_DELAY_IN_TICKS );
//        cnt++;
        configPRINTF( ( "State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcStateStr[ eState ],
                        OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped() ) );
    }

}


/* The OTA agent has completed the update job or determined that we're in
 * self test mode. If it was accepted, we want to activate the new image.
 * This typically means we should reset the device to run the new firmware.
 * If now is not a good time to reset the device, it may be activated later
 * by your user code. If the update was rejected, just return without doing
 * anything and we'll wait for another job. If it reported that we should
 * start test mode, normally we would perform some kind of system checks to
 * make sure our new firmware does the basic things we think it should do
 * but we'll just go ahead and set the image as accepted for demo purposes.
 * The accept function varies depending on your platform. Refer to the OTA
 * PAL implementation for your platform in aws_ota_pal.c to see what it
 * does for you.
 */

static void App_OTACompleteCallback( OTA_JobEvent_t eEvent )
{
    OTA_Err_t xErr = kOTA_Err_Uninitialized;


    /* OTA job is completed. so delete the MQTT and network connection. */
    if( eEvent == eOTA_JobEvent_Activate )
    {
        configPRINTF( ( "Received eOTA_JobEvent_Activate callback from OTA Agent.\r\n" ) );
        IotMqtt_Disconnect( mqttConnection, 0 );
//        #if defined( CONFIG_OTA_UPDATE_DEMO_ENABLED )
//            vMqttDemoDeleteNetworkConnection( &xConnection );
//        #endif
        OTA_ActivateNewImage();
    }
    else if( eEvent == eOTA_JobEvent_Fail )
    {
        configPRINTF( ( "Received eOTA_JobEvent_Fail callback from OTA Agent.\r\n" ) );
        /* Nothing special to do. The OTA agent handles it. */
    }
    else if( eEvent == eOTA_JobEvent_StartTest )
    {
        /* This demo just accepts the image since it was a good OTA update and networking
         * and services are all working (or we wouldn't have made it this far). If this
         * were some custom device that wants to test other things before calling it OK,
         * this would be the place to kick off those tests before calling OTA_SetImageState()
         * with the final result of either accepted or rejected. */
        configPRINTF( ( "Received eOTA_JobEvent_StartTest callback from OTA Agent.\r\n" ) );
        xErr = OTA_SetImageState( eOTA_ImageState_Accepted );

        if( xErr != kOTA_Err_None )
        {
            OTA_LOG_L1( " Error! Failed to set image state as accepted.\r\n" );
        }
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief The function that runs the Shadow demo, called by the demo runner.
 *
 * @param[in] awsIotMqttMode Ignored for the Shadow demo.
 * @param[in] pIdentifier NULL-terminated Shadow Thing Name.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection for Shadows.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection for Shadows.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return `EXIT_SUCCESS` if the demo completes successfully; `EXIT_FAILURE` otherwise.
 */
int RunApplication( bool awsIotMqttMode,
                    const char * pIdentifier,
                    void * pNetworkServerInfo,
                    void * pNetworkCredentialInfo,
                    const IotNetworkInterface_t * pNetworkInterface )
{
    /* Return value of this function and the exit status of this program. */
    int status = 0;

    /* Handle of the MQTT connection used in this demo. */
//    IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

    /* Length of Shadow Thing Name. */
    size_t thingNameLength = 0;

    /* Allows the Shadow update function to wait for the delta callback to complete
     * a state change before continuing. */
    IotSemaphore_t deltaSemaphore;

    /* Flags for tracking which cleanup functions must be called. */
    bool librariesInitialized = false, connectionEstablished = false, deltaSemaphoreCreated = false;

    /* The first parameter of this demo function is not used. Shadows are specific
     * to AWS IoT, so this value is hardcoded to true whenever needed. */
    ( void ) awsIotMqttMode;

    /* Determine the length of the Thing Name. */
    if( pIdentifier != NULL )
    {
        thingNameLength = strlen( pIdentifier );

        if( thingNameLength == 0 )
        {
            IotLogError( "The length of the Thing Name (identifier) must be nonzero." );

            status = EXIT_FAILURE;
        }
    }
    else
    {
        IotLogError( "A Thing Name (identifier) must be provided for the Shadow demo." );

        status = EXIT_FAILURE;
    }

    /* Initialize the libraries required for this demo. */
    if( status == EXIT_SUCCESS )
    {
        status = _initializeDemo();
    }

    /* HTTP connect to ntp server. */
    if( status == EXIT_SUCCESS )
    {
        prvHTTPSConnect( pNetworkInterface );
    }

    /* HTTP request to ntp server. */
    if( status == EXIT_SUCCESS )
    {
        char pcPath[13] = "/cgi-bin/ntp";
        uint32_t pcPathLen = strlen(pcPath);
        prvHTTPRequest( IOT_HTTPS_METHOD_GET, pcPath, pcPathLen );
    }

    if( status == EXIT_SUCCESS )
    {
        /* Mark the libraries as initialized. */
        librariesInitialized = true;

        /* Establish a new MQTT connection. */
        status = _establishMqttConnection( pIdentifier,
                                           pNetworkServerInfo,
                                           pNetworkCredentialInfo,
                                           pNetworkInterface,
                                           &mqttConnection );
    }

    if( status == EXIT_SUCCESS )
    {
        /* Mark the MQTT connection as established. */
        connectionEstablished = true;

        /* Create the semaphore that synchronizes with the delta callback. */
        deltaSemaphoreCreated = IotSemaphore_Create( &deltaSemaphore, 0, 1 );

        if( deltaSemaphoreCreated == false )
        {
            status = EXIT_FAILURE;
        }
    }

	if( status == EXIT_SUCCESS )
    {
        /* Clear the Shadow document so that this demo starts with no existing
         * Shadow. */
//        _clearShadowDocument( mqttConnection, pIdentifier, thingNameLength );

        /* Send Shadow updates. */
        status = _sendShadowUpdates( &deltaSemaphore,
                                     mqttConnection,
                                     pIdentifier,
                                     thingNameLength );

        /* Delete the Shadow document created by this demo to clean up. */
//        _clearShadowDocument( mqttConnection, pIdentifier, thingNameLength );
    }

    // OTA
    if( status == EXIT_SUCCESS )
    {
        vRunOTAUpdateDemo( mqttConnection, pNetworkInterface, pNetworkCredentialInfo );
    }

    /* Disconnect the MQTT connection if it was established. */
    if( connectionEstablished == true )
    {
        IotMqtt_Disconnect( mqttConnection, 0 );
    }

    /* Clean up libraries if they were initialized. */
    if( librariesInitialized == true )
    {
        _cleanupDemo();
    }

    /* Destroy the delta semaphore if it was created. */
    if( deltaSemaphoreCreated == true )
    {
        IotSemaphore_Destroy( &deltaSemaphore );
    }

    return status;
}

/*-----------------------------------------------------------*/
