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
String getTimeFirst();
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

void IRAM_ATTR handleButtonPress() {
    buttonInterruptFlag = true;
}

void setup()
{
    Serial.begin(115200);
    WiFi.disconnect();
    delay(500);

    pinMode(resetPin, INPUT);
    pinMode(statusPin, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(resetPin), handleButtonPress, CHANGE);

    initDHT();

    // Initially retrieves for stored credentials in flash.
    preferences.begin("credentials", true);
    String storedSSID = preferences.getString("storedSSID", "");
    String storedPassword = preferences.getString("storedPassword", "");

    String storedFirebaseUser = preferences.getString("storedFBUser", "");
    String storedFirebasePassword = preferences.getString("storedFBPass", "");
    preferences.end();

    // Checks if there are values in stored credentials, if not start SoftAP and start provisioning.
    if (storedSSID == "" && storedPassword == "" && storedFirebaseUser == "" && storedFirebasePassword == "")
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
        initFirebase(API_KEY, storedFirebaseUser, storedFirebasePassword);
        provisioned_status = 1;
        WiFi.mode(WIFI_MODE_STA);
        WiFi.begin(storedSSID, storedPassword);
        digitalWrite(statusPin, HIGH);

        Serial.println("DEBUG_LOG: Stored credentials found! Connecting to WiFi...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntp1Server);
    }
}

void loop()
{
    resetDevice();

    if (provisioned_status != 1)
    {
        WiFiClient client = server.available();
        provisionedStatusLED(1000);
        provisionStart(client);
    }

    if (provisioned_status == 1 && !buttonPressed)
    {
        digitalWrite(statusPin, HIGH);

        tcpKeepAlive();

        app.loop();
        Database.loop();

        String userID = app.getUid();
        databasePath = ((String) "UsersData/" + userID + "/");

        String retrievedTime = getTimeFirst();
        unsigned long time_retrieval_start = millis();

        while (!timeRetrieved)
        {
            if (millis() - time_retrieval_start >= time_retrieval_timeout)
            {
                Serial.println("Timeout reached. Restarting device.");
                ESP.restart();
            }

            retrievedTime = getTimeFirst();


            delay(1000);
        }

        if (app.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0))
        {
            sendDataPrevMillis = millis();

            // Get latest sensor readings
            temperature = dht.readTemperature();
            humidity = dht.readHumidity();

            // Send readings to database:
            Database.set<float>(aClient, databasePath + "temperature", temperature, asyncCB, "tempSetTask");
            Database.set<float>(aClient, databasePath + "humidity", humidity, asyncCB, "humidSetTask");
            Database.set<String>(aClient, databasePath + "timestamp", retrievedTime, asyncCB, "timeSetTask");
        }
    }
}

void initDHT()
{
    dht.begin();
}

void initFirebase(String apiKey, String userEmail, String userPassword)
{
     UserAuth user_auth(apiKey, userEmail, userPassword);

    ssl_client.setClient(&wifi_client);
    ssl_client.setInsecure();
    ssl_client.setDebugLevel(1);
    ssl_client.setSessionTimeout(150);

    initializeApp(aClient, app, getAuth(user_auth), asyncCB, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
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
String getTimeFirst() {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return "";
    }

    timeRetrieved = true;
    char timeYear[5];
    char timeMonth[3];
    char timeDay[3];
    char timeHour[3];
    char timeMinute[3];
    char timeSecond[3];

    strftime(timeYear, 5, "%Y", &timeinfo);
    strftime(timeMonth, 3, "%m", &timeinfo);
    strftime(timeDay, 3, "%d", &timeinfo);
    strftime(timeHour, 3, "%H", &timeinfo);
    strftime(timeMinute, 3, "%M", &timeinfo);
    strftime(timeSecond, 3, "%S", &timeinfo);

    return ((String)timeYear + "-" + timeMonth + "-" + timeDay + " " + timeHour+ ":" + timeMinute + ":" + timeSecond);
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
    const char *firebaseUser;
    const char *firebasePassword;

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
                    const char *receivedFirebaseUser = receivedMessage["FirebaseUser"];
                    const char *receivedFirebasePassword = receivedMessage["FirebasePassword"];

                    WiFi.begin(receivedSSID, receivedPassword);

                    int timeout = 0;

                    Serial.print("Connecting to WiFi ..");
                    while (WiFi.status() != WL_CONNECTED)
                    {
                        provisionedStatusLED(300);
                        timeout++;
                        if (timeout == 5)
                        {
                            break;
                        }
                        delay(1000);
                    }
                    Serial.println(WiFi.localIP());
                    wifiSSID = receivedSSID;
                    wifiPassword = receivedPassword;
                    firebaseUser = receivedFirebaseUser;
                    firebasePassword = receivedFirebasePassword;
                }

                // Checks if WiFi connection is successful
                if (WiFi.status() == WL_CONNECTED)
                {
                    client.println(200);
                    Serial.println("Acknowledgment sent");

                    preferences.begin("credentials", false);

                    preferences.putString("storedSSID", wifiSSID);
                    preferences.putString("storedPassword", wifiPassword);

                    preferences.putString("storedFBUser", firebaseUser);
                    preferences.putString("storedFBPass", firebasePassword);

                    preferences.end();

                    delay(5000);
                    WiFi.mode(WIFI_STA);
                    digitalWrite(statusPin, HIGH);
                    provisioned_status = 1;
                }
                else if (WiFi.status() != WL_CONNECTED)
                {
                    client.println(400);
                    return;
                }
            }
            client.stop();
            Serial.println("Client disconnected");
            ESP.restart();
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

        //200 is 200ms of blinking lights
        provisionedStatusLED(200);

        if (millis() - buttonPressTime >= 5000)
        {
            // Serial.println("Button pressed for 5 seconds. Resetting...");

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