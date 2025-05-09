#include <ConfigAssist.h>  // Config assist class
#include <ConfigAssistHelper.h>  // Config assist helper class

#include "appPMem.h"

#ifndef LED_BUILTIN
  #define LED_BUILTIN 22
#endif
#if defined(ESP32)
  WebServer server(80);
#else
  ESP8266WebServer  server(80);
#endif

#define INI_FILE "/NtpTimeSync.ini" // Define SPIFFS storage file

// Config class
ConfigAssist conf(INI_FILE, VARIABLES_DEF_YAML);

// Define a ConfigAssist helper class
ConfigAssistHelper confHelper(conf);

// Setup internal led variable if not set
String s = (conf("led_buildin") == "") ? conf["led_buildin"] = LED_BUILTIN : "";

time_t tnow;
unsigned long pingMillis = millis();  // Ping

// Handler function for Home page
void handleRoot() {

  String out("<h2>Hello from {name}</h2>");
  out += "<h4>Device time: " + conf.getLocalTime() +"</h4>";
  out += "<a href='/cfg'>Edit config</a>";

#if defined(ESP32)
    out.replace("{name}", "ESP32");
#else
    out.replace("{name}", "ESP8266!");
#endif

#if (CA_USE_TIMESYNC)
  // Send a web browser sync java script to client
  out += "<script>" + conf.getTimeSyncScript() + "</script>";
#endif
  server.send(200, "text/html", out);
}

// Handler for page not found
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void onConnectionResult(ConfigAssistHelper::WiFiResult result, const String& msg){

  switch (result) {
      case ConfigAssistHelper::WiFiResult::SUCCESS:
          LOG_D("Connected to Wi-Fi! IP: %s\n", msg.c_str());
          confHelper.startMDNS();
          conf.setupConfigPortalHandlers(server);
          // Start web server
          server.begin();
          LOG_I("HTTP server started\n");
          LOG_I("Device started. Visit http://%s\n", WiFi.localIP().toString().c_str());

          // Setup time synchronization, Wait max 10 sec
          LOG_I("Start time sync..\n");
          confHelper.syncTime(10000);
          pingMillis = millis();
          tnow = time(nullptr);
          break;

      case ConfigAssistHelper::WiFiResult::INVALID_CREDENTIALS:
          LOG_D("Invalid credentials: %s\n", msg.c_str());
          // Append config assist handlers to web server, setup ap on no connection
          conf.setup(server, true);
          break;

      case ConfigAssistHelper::WiFiResult::CONNECTION_TIMEOUT:
          LOG_D("Connection fail: %s\n", msg.c_str());
          conf.setup(server, true);
          break;

      case ConfigAssistHelper::WiFiResult::DISCONNECTION_ERROR:
          LOG_D("Disconnect: %s\n", msg.c_str());
          WiFi.setAutoReconnect(true);
          confHelper.setReconnect(true);
          break;

      default:
          LOG_D("Unknown result: %s\n", msg.c_str());
          break;
  }
}

// Setup function
void setup(void) {

  Serial.begin(115200);
  Serial.print("\n\n\n\n");
  Serial.flush();

  LOG_I("Starting.. \n");
  if(false) conf.deleteConfig(); //Set to true to remove old ini file and re-built it fron dictionary

  //Active seperator open/close, All others closed
  conf.setDisplayType(ConfigAssistDisplayType::AccordionToggleClosed);

  // Register handlers for web server
  server.on("/", handleRoot);
  server.on("/d", []() {              // Append dump handler
    conf.dump(&server);
  });
  server.onNotFound(handleNotFound);  // Append not found handler
  confHelper.setWiFiResultCallback(onConnectionResult);
  // Connect to any available network
  confHelper.connectToNetworkAsync(15000, conf("led_buildin").toInt());
}

int syncType = 0;


// App main loop
void loop(void) {
  server.handleClient();
  // Update led, print dots when connecting, check connection on connected
  confHelper.loop();

  if(!WiFi.isConnected()) return;
  // Display info
  if (millis() - pingMillis >= 20000){
    tnow = time(nullptr);
    LOG_I("Time in sync: %i clock: %s", confHelper.isTimeSync(), ctime(&tnow));
    syncType++;
    pingMillis = millis();
  }else{
    return;
  }

  if(syncType == 1){
    // Force time synchronization,
    // syncTime will not wait if already time is set
    // and time will be automatically sync in background.
    LOG_I("* * * Starting soft resync time...\n");
    confHelper.syncTime(conf("sync_timeout").toInt() * 1000L, false);
    tnow = time(nullptr);
    LOG_I("Soft resync time: %i clock: %s", confHelper.isTimeSync(), ctime(&tnow));

  }else if(syncType == 2){
    // Force time synchronization,
    // Clock will be reseted and wait for max 20 sec to sync
    // If fail clock will be restored
    LOG_I("* * * Starting hard resync time...\n");
    confHelper.syncTime(conf("sync_timeout").toInt() * 1000L, true);
    tnow = time(nullptr);
    LOG_I("Hard resynced time: %i clock: %s", confHelper.isTimeSync(), ctime(&tnow));
  }else if(syncType == 3){
    // Force time synchronization and don't wait for Synchronization
    LOG_I("* * * Starting ASYNC hard resync time...\n");
    confHelper.syncTimeAsync(conf("sync_timeout").toInt() * 1000L, true);
    tnow = time(nullptr);
    LOG_N("ASYNC Hard resynced time: %i clock: %s", confHelper.isTimeSync(), ctime(&tnow));
  }
  if(syncType > 3) syncType = 0;
  // Allow the cpu to switch to other tasks
  delay(2);
}
