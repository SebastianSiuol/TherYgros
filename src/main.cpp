#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <lwip/sockets.h>
#include <Preferences.h>

/* Downloaded Packages */
#include <FirebaseClient.h>
#include <DHT.h>
#include <ArduinoJson.h>


/* User-defined*/
#include <firebaseDatabaseDefinition.h>

//                       Initialization Block
/* ====================================================================== */

/* Hardcoded Soft Access Point Credentials */
/* ===================================================== */
const char *ssid = "TherYgros_53136";
const char *password = "470025906";
/* ===================================================== */

/* Soft Access Point Configurations */
/* ===================================================== */
IPAddress ipAddress(192, 168, 1, 1);
IPAddress subMask(255, 255, 255, 0);
/* ===================================================== */

/* TCP Initialization */
/* ===================================================== */
WiFiServer server(80);
/* ===================================================== */

/* Initializes JSON Doc */
/* ===================================================== */
StaticJsonDocument<50> receivedMessage;
/* ===================================================== */

/* TCP KeepAlive */
/* ===================================================== */
bool tcp_keep_alive_set = false;
int keepAlive = 1000; // Milliseconds
int keepIdle = 5;     // Seconds
int keepInterval = 5; // Seconds
int keepCount = 1;
/* ===================================================== */

/* FirebaseClient Initializaton*/
/* ===================================================== */
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
RealtimeDatabase Database;

DefaultNetwork network;
WiFiClient wifi_client;
ESP_SSLClient ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client, getNetwork(network));
/* ===================================================== */

/* DHT variables and pins initialization */
/* ===================================================== */
#define DHTPIN 4
DHT dht(DHTPIN, DHT22);
float temperature;
float humidity;
String databasePath;
/* ===================================================== */

/* NTP Time Initializaton*/
/* ===================================================== */
const char *ntp1Server = "time.nist.gov";
// const char *ntp2Server = "pool.ntp.org";
// const char *ntp3Server = "1.asia.pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 3600;
bool timeRetrieved = false;
/* ===================================================== */

/* Timer that sends the data to the database (5000 is 5 seconds) */
/* ===================================================== */
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 5000;
/* ===================================================== */

/* Function Initializations */
/* ===================================================== */
void tcpKeepAlive();
int getTimeFirst();

void asyncCB(AsyncResult &aResult);
void printResult(AsyncResult &aResult);
/* ===================================================== */


void initDHT()
{
    dht.begin();
}

void setup()
{
    WiFi.disconnect();
    delay(10000);

    initDHT();

    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }

    configTime(gmtOffset_sec, daylightOffset_sec, ntp1Server);

    ssl_client.setClient(&wifi_client);
    ssl_client.setInsecure();
    ssl_client.setDebugLevel(1);
    ssl_client.setSessionTimeout(150);

    initializeApp(aClient, app, getAuth(user_auth), asyncCB, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
}

void loop()
{
    tcpKeepAlive();

    app.loop();
    Database.loop();

    String userID = app.getUid();
    databasePath = ((String)"UsersData/" + userID + "/" );

    //TODO: Add a minute to reduce timeout session
    int retrievedHour = getTimeFirst();

    while (!timeRetrieved)
    {
        retrievedHour = getTimeFirst();
        delay(1000);
    }

    if (app.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0))
    {
        sendDataPrevMillis = millis();

        // Get latest sensor readings
        temperature = dht.readTemperature();
        humidity = dht.readHumidity();

        // Send readings to database:
        Database.set<float>(aClient, databasePath + "temperature", temperature, asyncCB, "tempTask");
        Database.set<float>(aClient, databasePath + "humidity", humidity, asyncCB, "humidTask");
        Database.set<int>(aClient, databasePath + "time", retrievedHour, asyncCB, "timeTask");
    }
}

/* Contacts the NTP first to get the time */
/* ===================================================== */
int getTimeFirst()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
        return 0;
    }

    timeRetrieved = true;
    return ((int)timeinfo.tm_hour);
}
/* ===================================================== */


/* Function for Keeping the TCP alive (I do not know if it works or consistent though..) */
/* ===================================================== */
void tcpKeepAlive()
{
    if (wifi_client.connected())
    {
        if (!tcp_keep_alive_set)
        {
            tcp_keep_alive_set = true;
            wifi_client.setOption(TCP_KEEPALIVE, &keepAlive);
            wifi_client.setOption(TCP_KEEPIDLE, &keepIdle);
            wifi_client.setOption(TCP_KEEPINTVL, &keepInterval);
            wifi_client.setOption(TCP_KEEPCNT, &keepCount);
        }
    }
    else
    {
        tcp_keep_alive_set = false;
    }
}
/* ===================================================== */


/* Callback for asynchronous realtime database. These are permanent debugging logs. (Do not remove)*/
/* ===================================================== */
void asyncCB(AsyncResult &aResult)
{
    printResult(aResult);
}

void printResult(AsyncResult &aResult)
{
    if (aResult.isEvent())
    {
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
    }

    if (aResult.isDebug())
    {
        Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
    }

    if (aResult.isError())
    {
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
        WiFi.reconnect();
    }

    if (aResult.available())
    {
        RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
        if (RTDB.isStream())
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s\n", aResult.uid().c_str());
            Firebase.printf("event: %s\n", RTDB.event().c_str());
            Firebase.printf("path: %s\n", RTDB.dataPath().c_str());
            Firebase.printf("data: %s\n", RTDB.to<const char *>());
            Firebase.printf("type: %d\n", RTDB.type());
        }
        else
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
        }

        Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
    }
}
/* ===================================================== */