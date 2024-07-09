/* Downloaded Packages Imports*/
#include <FirebaseClient.h>

/* User-defined Imports*/
#include <firebaseDatabaseDefinition.h>
#include <therYgrosInitializations.h>

//                       Initialization Block
/* ====================================================================== */

/* Hardcoded Soft Access Point Credentials */
/* ===================================================== */
#define AP_SSID "TherYgros_53136"
#define AP_PASS "470025906"
/* ===================================================== */

/* Soft Access Point Configurations */
/* ===================================================== */
IPAddress ipAddress(192, 168, 1, 1);
IPAddress subMask(255, 255, 255, 0);
/* ===================================================== */

/* FirebaseClient Initializaton*/
/* ===================================================== */
DefaultNetwork network;

FirebaseApp app;
RealtimeDatabase Database;

ESP_SSLClient ssl_client;
WiFiClient wifi_client;
using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client, getNetwork(network));
/* ===================================================== */

/* Timer Initialization for Database Sending (5000 is 5 seconds) */
/* ===================================================== */
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 5000;
/* ===================================================== */

/* Function Initializations */
/* ===================================================== */
void tcpKeepAlive();
String getTimeFirst();
void statusLED(int time_interval);
void provisionStart(WiFiClient client);
void resetDevice();

void asyncCB(AsyncResult &aResult);
void printResult(AsyncResult &aResult);
/* ===================================================== */

/* Dual-Core Initialization */
/* ===================================================== */
TaskHandle_t Task1;
void Task1code(void *pvParameter);
/* ===================================================== */
/* ====================================================================== */
//                       Initialization Block
void Task1code(void *pvParameter) {
  for (;;) {
    buttonState = digitalRead(resetPin);

    if (buttonState == HIGH) {
      if (!buttonPressed) {
        buttonPressTime = millis();
        buttonPressed = true;
      }

      statusLED(200);

      if (millis() - buttonPressTime >= 5000) {

        preferences.begin("credentials", false);
        preferences.clear();
        preferences.end();

        ESP.restart();
      }
    } else {
      buttonPressed = false;
    }

    delay(10);  // Small delay to debounce the button
  }
}
void setup() {
  delay(3000);
  Serial.begin(115200);
  WiFi.disconnect();
  delay(500);

  pinMode(resetPin, INPUT_PULLDOWN);
  pinMode(statusPin, OUTPUT);

  initComponents();
  initXTask();

  // Initially retrieves for stored credentials in flash.
  preferences.begin("credentials", true);

  String storedSSID = preferences.getString("storedSSID", "");
  String storedPassword = preferences.getString("storedPassword", "");

  String storedFirebaseUser = preferences.getString("storedFBUser", "");
  String storedFirebasePassword = preferences.getString("storedFBPass", "");

  preferences.end();

  // Checks if there are values in stored credentials, if not start SoftAP and start provisioning.
  if (storedSSID == "" && storedPassword == "" && storedFirebaseUser == "" && storedFirebasePassword == "") {
    is_provisioned = false;

    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP(AP_SSID, AP_PASS);

    if (!WiFi.softAPConfig(ipAddress, ipAddress, subMask)) {
      while (1);
    }

    server.begin();
  } else {
    is_provisioned = true;

    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin(storedSSID, storedPassword);

    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);
    }

    digitalWrite(statusPin, HIGH);

    initFirebase(API_KEY, storedFirebaseUser, storedFirebasePassword);
  }
}

void loop()
{
    // resetDevice();

    if (is_provisioned == false)
    {
        WiFiClient client = server.available();

        statusLED(1000);
        provisionStart(client);
    }
    else if (is_provisioned == true)
    {
        digitalWrite(statusPin, HIGH);

        tcpKeepAlive();

        app.loop();

        if (!app.isAuthenticated())
        {
            statusLED(3000);
        }
        else
        {
            digitalWrite(statusPin, HIGH);
        }

        Database.loop();

        String userID = app.getUid();

        databasePath = ((String) "UsersData/" + userID + "/");

        String retrievedTime = getTimeFirst();
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
/* ===================================================== */
//
//
//
//
//
/* Initializes Dual-Core Task */
/* ===================================================== */
void initXTask() {
  xTaskCreatePinnedToCore(
    Task1code, /* Function to implement the task */
    "Task1",   /* Name of the task */
    10000,     /* Stack size in words */
    NULL,      /* Task input parameter */
    0,         /* Priority of the task */
    &Task1,    /* Task handle. */
    1);        /* Core where the task should run */
}
/* ===================================================== */
//
//
//
//
//
/* Initializes the DHT and RTC */
/* ===================================================== */
void initComponents() {
  dht.begin();
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1)
      delay(10);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}
/* ===================================================== */
//
//
//
//
//
/* Initializes the Firebase */
/* ===================================================== */
void initFirebase(String apiKey, String userEmail, String userPassword) {
  UserAuth user_auth(apiKey, userEmail, userPassword);
  // UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

  ssl_client.setClient(&wifi_client);
  ssl_client.setInsecure();
  ssl_client.setDebugLevel(1);
  ssl_client.setSessionTimeout(150);

  initializeApp(aClient, app, getAuth(user_auth), asyncCB, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}
/* ===================================================== */
//
//
//
//
//
/* Function for led status indicator. */
/* ===================================================== */

void statusLED(int time_interval) {
  digitalWrite(statusPin, HIGH);
  delay(time_interval);
  digitalWrite(statusPin, LOW);
  delay(time_interval);
  digitalWrite(statusPin, HIGH);
}
/* ===================================================== */
//
//
//
//
//
/* Contacts the NTP first to get the time */
/* ===================================================== */
String getTimeFirst() {
  DateTime timeNow = rtc.now();

  char timeStr[21];
  snprintf(timeStr, 21, "%d-%02d-%02d %02d:%02d:%02d", timeNow.year(), timeNow.month(), timeNow.day(), timeNow.hour(), timeNow.minute(), timeNow.second());

  return ((String)timeStr);
}
/* ===================================================== */
//
//
//
//
//
/* Function for Keeping the TCP alive (I do not know if it works or consistent though..) */
/* ===================================================== */
void tcpKeepAlive() {
  if (wifi_client.connected()) {
    if (!tcp_keep_alive_set) {
      tcp_keep_alive_set = true;
      wifi_client.setOption(TCP_KEEPALIVE, &keepAlive);
      wifi_client.setOption(TCP_KEEPIDLE, &keepIdle);
      wifi_client.setOption(TCP_KEEPINTVL, &keepInterval);
      wifi_client.setOption(TCP_KEEPCNT, &keepCount);
    }
  } else {
    tcp_keep_alive_set = false;
  }
}
/* ===================================================== */
//
//
//
//
//
/* Function that handles and receives provisioning details from the application. */
/* ===================================================== */
void provisionStart(WiFiClient client) {
  const char *wifiSSID;
  const char *wifiPassword;
  const char *firebaseUser;
  const char *firebasePassword;

  if (client) {
    Serial.println("Client connected");

    while (client.connected()) {
      if (client.available()) {
        String receivedMessage = client.readStringUntil('\n');
        Serial.println("Message received: " + receivedMessage);

        if (receivedMessage.c_str()) {
          DeserializationError error = deserializeJson(decodedMessage, receivedMessage);

          if (decodedMessage["Code"] == "3003") {
            String response3003 = ((String) "{\"Code\":\"200\", \"Body\":" + "\"" + therygrosVersion + "\"" + "}");
            client.println(response3003);
            return;
          } else if (decodedMessage["Code"] == "0913") {
            const char *receivedSSID = decodedMessage["SSID"];
            const char *receivedPassword = decodedMessage["Password"];
            const char *receivedFirebaseUser = decodedMessage["FirebaseUser"];
            const char *receivedFirebasePassword = decodedMessage["FirebasePassword"];

            int timeout = 0;

            WiFi.begin(receivedSSID, receivedPassword);

            while (WiFi.status() != WL_CONNECTED) {
              statusLED(400);

              timeout++;

              delay(1000);

              Serial.print(".");

              if (timeout == 5) {
                break;
              }

            }
            
            if(WiFi.status() == WL_CONNECTED)
                Serial.println(WiFi.localIP());

            wifiSSID = receivedSSID;
            wifiPassword = receivedPassword;
            firebaseUser = receivedFirebaseUser;
            firebasePassword = receivedFirebasePassword;
          }
        }

        // Checks if WiFi connection is successful
        if (WiFi.status() == WL_CONNECTED) {
          client.println(200);
          Serial.println("Acknowledgment sent");

          preferences.begin("credentials", false);

          preferences.putString("storedSSID", wifiSSID);
          preferences.putString("storedPassword", wifiPassword);

          preferences.putString("storedFBUser", firebaseUser);
          preferences.putString("storedFBPass", firebasePassword);

          preferences.end();

          is_provisioned = true;
          delay(5000);
          
        } else if (WiFi.status() != WL_CONNECTED) {
          client.println(400);
          return;
        }
      }
      client.stop();
      Serial.println("DEBUG_LOG: Client disconnected.");

      // Restarts the device if the provision is a success
      if (is_provisioned) {
        ESP.restart();
      }
    }
  }
}
/* ===================================================== */
//
//
//
//
//
/* Firebase Client Functions */
/* ========================================================================= */
/* Callback for asynchronous realtime database. These are permanent debugging logs. (Do not remove or change)*/
/* ========================================================================= */
void asyncCB(AsyncResult &aResult) {
  printResult(aResult);
}

void printResult(AsyncResult &aResult) {
  if (aResult.isEvent()) {
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
  }

  if (aResult.isDebug()) {
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError()) {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
    WiFi.reconnect();
  }

  if (aResult.available()) {
    RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
    if (RTDB.isStream()) {
      Serial.println("----------------------------");
      Firebase.printf("task: %s\n", aResult.uid().c_str());
      Firebase.printf("event: %s\n", RTDB.event().c_str());
      Firebase.printf("path: %s\n", RTDB.dataPath().c_str());
      Firebase.printf("data: %s\n", RTDB.to<const char *>());
      Firebase.printf("type: %d\n", RTDB.type());
    } else {
      Serial.println("----------------------------");
      Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    }

    Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
  }
}
/* ========================================================================= */
