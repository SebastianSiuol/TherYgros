#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <lwip/sockets.h>
#include <Preferences.h>

/* Downloaded Packages */
#include <FirebaseClient.h>
#include <ArduinoJson.h>

/* User-defined*/
#include <firebaseDatabaseDefinition.h>
#include <therYgrosInitializations.h>

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

/* Timer that sends the data to the database (5000 is 5 seconds) */
/* ===================================================== */
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 5000;
/* ===================================================== */

/* Function Initializations */
/* ===================================================== */
void tcpKeepAlive();
int getTimeFirst();
void provisionedStatusLED(int time_interval);
void provisionStart(WiFiClient client);
void resetDevice();

void asyncCB(AsyncResult &aResult);
void printResult(AsyncResult &aResult);
/* ===================================================== */

/* Preferences (Flash Writing) */
/* ===================================================== */
Preferences preferences;
/* ===================================================== */

void initDHT()
{
    dht.begin();
}

void initFirebase()
{
    ssl_client.setClient(&wifi_client);
    ssl_client.setInsecure();
    ssl_client.setDebugLevel(1);
    ssl_client.setSessionTimeout(150);

    initializeApp(aClient, app, getAuth(user_auth), asyncCB, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
}

void setup()
{
    Serial.begin(115200);
    WiFi.disconnect();
    delay(10000);

    pinMode(resetPin, INPUT);
    pinMode(statusPin, OUTPUT);

    initDHT();

    // Initially retrieves for stored credentials in flash.
    preferences.begin("credentials", true);
    String storedSSID = preferences.getString("storedSSID", "");
    String storedPassword = preferences.getString("storedPassword", "");
    preferences.end();

    // Checks if there are values in stored credentials, if not start SoftAP and start provisioning.
    if (storedSSID == "" && storedPassword == "")
    {
        provisioned_status = 0;
        WiFi.mode(WIFI_MODE_APSTA);
        WiFi.softAP(ssid, password);

        if (!WiFi.softAPConfig(ipAddress, ipAddress, subMask))
        {
            while (1)
                ;
        }

        server.begin();
    }
    else
    {
        provisioned_status = 1;
        WiFi.mode(WIFI_MODE_STA);
        WiFi.begin(storedSSID, storedPassword);
        digitalWrite(statusPin, HIGH);

        Serial.println("DEBUG_LOG: Stored credentials found! Connecting to WiFi...");
        initFirebase();
        configTime(gmtOffset_sec, daylightOffset_sec, ntp1Server);
    }
}

void loop()
{
    if (provisioned_status != 1)
    {
        WiFiClient client = server.available();
        provisionedStatusLED(1000);
        provisionStart(client);
    }

    if (provisioned_status == 1 && !buttonPressed)
    {
        tcpKeepAlive();

        app.loop();
        Database.loop();

        String userID = app.getUid();
        databasePath = ((String) "UsersData/" + userID + "/");

        // TODO: Add a minute to reduce timeout session
        int retrievedHour, retrievedMinute;
        unsigned long time_retrieval_start = millis();

        while (!timeRetrieved)
        {
            if (millis() - time_retrieval_start >= time_retrieval_timeout)
            {
                Serial.println("Timeout reached. Restarting device.");
                ESP.restart();
            }

            getTimeFirst(retrievedHour, retrievedMinute);
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
            Database.set<int>(aClient, databasePath + "time/hour", retrievedHour, asyncCB, "timeTask");
            Database.set<int>(aClient, databasePath + "time/minute", retrievedMinute, asyncCB, "timeTask");
        }
    }

    resetDevice();
}

/* Function for led status indicator. */
/* ===================================================== */

void provisionedStatusLED(int time_interval)
{
    digitalWrite(statusPin, HIGH);
    delay(time_interval);
    digitalWrite(statusPin, LOW);
    delay(time_interval);
}
/* ===================================================== */

/* Contacts the NTP first to get the time */
/* ===================================================== */
void getTimeFirst(int &hour, int &minute) {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        hour = 0;
        minute = 0;
        return;
    }

    timeRetrieved = true;
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
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

/* Function that handles and receives provisioning details from the application. */
/* ===================================================== */
void provisionStart(WiFiClient client)
{
    const char *wifiSSID;
    const char *wifiPassword;

    if (client)
    {
        Serial.println("Client connected");

        while (client.connected())
        {
            if (client.available())
            {
                String message = client.readStringUntil('\n');
                Serial.println("Message received: " + message);

                if (message.c_str())
                {
                    DeserializationError error = deserializeJson(receivedMessage, message);

                    const char *receivedSSID = receivedMessage["SSID"];
                    const char *receivedPassword = receivedMessage["Password"];

                    WiFi.begin(receivedSSID, receivedPassword);

                    int timeout = 0;

                    Serial.print("Connecting to WiFi ..");
                    while (WiFi.status() != WL_CONNECTED)
                    {
                        provisionedStatusLED(300);
                        timeout++;
                        if (timeout == 10)
                        {
                            break;
                        }
                    }
                    Serial.println(WiFi.localIP());
                    wifiSSID = receivedSSID;
                    wifiPassword = receivedPassword;
                }

                // Checks if WiFi connection is successful
                if (WiFi.status() == WL_CONNECTED)
                {
                    client.println("Connected!");
                    Serial.println("Acknowledgment sent");

                    preferences.begin("credentials", false);

                    preferences.putString("storedSSID", wifiSSID);
                    preferences.putString("storedPassword", wifiPassword);

                    preferences.end();

                    delay(5000);
                    WiFi.mode(WIFI_STA);
                    digitalWrite(statusPin, HIGH);
                    provisioned_status = 1;
                }
                else if (WiFi.status() != WL_CONNECTED)
                {
                    client.println("Connection failed! try again!");
                }
            }
            client.stop();
            Serial.println("Client disconnected");
        }
    }
}
/* ===================================================== */

/* Function that resets device when pressed for 5 seconds. */
/* ===================================================== */
void resetDevice()
{
    buttonState = digitalRead(resetPin);

    if (buttonState == HIGH)
    {
        if (!buttonPressed)
        {
            buttonPressTime = millis();
            buttonPressed = true;
        }

        provisionedStatusLED(200);

        if (millis() - buttonPressTime >= 5000)
        {
            Serial.println("Button pressed for 5 seconds. Resetting...");

            preferences.begin("credentials", false);
            preferences.clear();
            preferences.end();

            ESP.restart();
        }
    }
    else
    {
        buttonPressed = false;
    }

    delay(10); // Small delay to debounce the button
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