#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
// #define CAMERA_MODEL_ESP_EYE  // Has PSRAM
#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT  // Has PSRAM
//#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
// #define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// WiFi Configuration
// ===========================
const char *default_ssid = "TP-Link_CEOD";
const char *default_password = "22238665";

// WiFi configuration portal settings
const char *ap_ssid = "ESP32-Camera-Config";
const char *ap_password = "12345678";

// EEPROM addresses for storing WiFi credentials
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 100
#define SSID_MAX_LEN 32
#define PASS_MAX_LEN 64

WebServer configServer(8080);  // Use port 8080 for config to avoid conflicts with camera server
bool configMode = false;
bool cameraInitialized = false;

void startCameraServer();
void setupLedFlash(int pin);
void startConfigPortal();
void handleRoot();
void handleScan();
void handleConnect();
void handleStatus();
String scanNetworks();
bool connectToWiFi(String ssid, String password);
void saveCredentials(String ssid, String password);
bool loadCredentials(String &ssid, String &password);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  
  EEPROM.begin(EEPROM_SIZE);

  // Try to load saved credentials
  String saved_ssid, saved_password;
  bool hasCredentials = loadCredentials(saved_ssid, saved_password);
  
  // Try to connect with saved credentials first, then default
  bool connected = false;
  if (hasCredentials && saved_ssid.length() > 0) {
    Serial.println("Trying saved credentials...");
    connected = connectToWiFi(saved_ssid, saved_password);
  }
  
  if (!connected) {
    Serial.println("Trying default credentials...");
    connected = connectToWiFi(default_ssid, default_password);
  }

  if (connected) {
    // WiFi connected, proceed with camera setup
    setupCamera();
    startCameraServer();
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
  } else {
    // WiFi connection failed, start configuration portal
    Serial.println("WiFi connection failed. Starting configuration portal...");
    startConfigPortal();
  }
}

void loop() {
  if (configMode) {
    configServer.handleClient();
  } else {
    // Check if WiFi is still connected
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, restarting...");
      ESP.restart();
    }
  }
  delay(10);
}

bool connectToWiFi(String ssid, String password) {
  // Ensure WiFi is in station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);
  
  // Optimize WiFi for maximum throughput
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum power

  WiFi.begin(ssid.c_str(), password.c_str());
  WiFi.setSleep(false);  // Disable WiFi sleep for consistent performance

  Serial.print("Connecting to ");
  Serial.print(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Print WiFi performance info
    Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("WiFi Channel: %d\n", WiFi.channel());

    return true;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
    WiFi.disconnect(true);
    delay(1000);
    return false;
  }
}

void setupCamera() {
  if (cameraInitialized) return;

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;  // Start with VGA for better performance
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;  // Use latest frame for streaming
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 15;  // Lower quality for higher speed (10-63, lower = better quality but slower)
  config.fb_count = 2;  // Use 2 frame buffers for double buffering

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  Serial.println("Camera initialized successfully");

  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("Failed to get camera sensor");
    return;
  }

  Serial.printf("Camera sensor PID: 0x%x\n", s->id.PID);
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  cameraInitialized = true;
}

void startConfigPortal() {
  // Completely disconnect from any existing WiFi connection
  WiFi.disconnect(true);
  delay(1000);
  
  // Set WiFi mode to AP only
  WiFi.mode(WIFI_AP);
  delay(1000);
  
  // Start Access Point
  bool apStarted = WiFi.softAP(ap_ssid, ap_password);
  
  if (!apStarted) {
    Serial.println("Failed to start Access Point!");
    // Try again without password
    apStarted = WiFi.softAP(ap_ssid);
  }
  
  if (apStarted) {
    Serial.println("Configuration portal started");
    Serial.print("Connect to WiFi: ");
    Serial.println(ap_ssid);
    Serial.print("Password: ");
    Serial.println(ap_password);
    Serial.print("Open browser to: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println(":8080");  // Show the correct port
    
    // Wait a bit for AP to fully initialize
    delay(2000);
    
    configServer.on("/", handleRoot);
    configServer.on("/scan", handleScan);
    configServer.on("/connect", HTTP_POST, handleConnect);
    configServer.on("/status", handleStatus);
    
    configServer.begin();
    configMode = true;
    
    Serial.println("Web server started successfully");
  } else {
    Serial.println("Failed to start configuration portal!");
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 Camera WiFi Configuration</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".network { background: #f9f9f9; margin: 10px 0; padding: 15px; border-radius: 5px; border-left: 4px solid #007bff; }";
  html += ".network strong { color: #007bff; }";
  html += "button { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; margin: 5px; }";
  html += "button:hover { background: #0056b3; }";
  html += "input[type='password'] { width: 200px; padding: 8px; margin: 5px; border: 1px solid #ddd; border-radius: 3px; }";
  html += ".status { padding: 10px; margin: 10px 0; border-radius: 5px; }";
  html += ".success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }";
  html += ".error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }";
  html += ".loading { text-align: center; margin: 20px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>ESP32 Camera WiFi Configuration</h1>";
  html += "<div id='status'></div>";
  html += "<button onclick='scanNetworks()'>Scan Networks</button>";
  html += "<div id='networks'></div>";
  html += "<script>";
  html += "function scanNetworks() {";
  html += "  document.getElementById('networks').innerHTML = '<div class=\"loading\">Scanning networks...</div>';";
  html += "  fetch('/scan').then(r => r.text()).then(data => {";
  html += "    document.getElementById('networks').innerHTML = data;";
  html += "  }).catch(e => {";
  html += "    document.getElementById('networks').innerHTML = '<div class=\"error\">Error scanning networks</div>';";
  html += "  });";
  html += "}";
  html += "function connectToNetwork(ssid, inputId) {";
  html += "  let password = '';";
  html += "  const passwordInput = document.getElementById(inputId);";
  html += "  if(passwordInput) password = passwordInput.value;";
  html += "  document.getElementById('status').innerHTML = '<div class=\"status\">Connecting to ' + ssid + '...</div>';";
  html += "  fetch('/connect', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  html += "    body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)";
  html += "  }).then(r => r.text()).then(data => {";
  html += "    document.getElementById('status').innerHTML = data;";
  html += "    if(data.includes('success')) {";
  html += "      setTimeout(() => { ";
  html += "        document.getElementById('status').innerHTML = '<div class=\"success\">Connection successful! You can now close this page.</div>';";
  html += "      }, 5000);";
  html += "    }";
  html += "  }).catch(e => {";
  html += "    document.getElementById('status').innerHTML = '<div class=\"error\">Connection failed: ' + e.message + '</div>';";
  html += "  });";
  html += "}";
  html += "window.onload = function() { scanNetworks(); }";
  html += "</script>";
  html += "</div></body></html>";
  
  configServer.send(200, "text/html", html);
}

void handleScan() {
  Serial.println("Scanning networks...");
  
  // Temporarily switch to STA mode for scanning
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  
  String networks = scanNetworks();
  
  // Switch back to AP mode
  WiFi.mode(WIFI_AP);
  delay(100);
  
  configServer.send(200, "text/html", networks);
}

void handleConnect() {
  String ssid = configServer.arg("ssid");
  String password = configServer.arg("password");
  
  if (ssid.length() == 0) {
    configServer.send(400, "text/html", "<div class='error'>SSID cannot be empty</div>");
    return;
  }
  
  Serial.println("Attempting to connect to: " + ssid);
  
  // Switch to STA mode for connection attempt
  WiFi.mode(WIFI_STA);
  delay(1000);
  
  if (connectToWiFi(ssid, password)) {
    saveCredentials(ssid, password);
    
    String response = "<div class='success'>Successfully connected to " + ssid + "!<br>";
    response += "Setting up camera server...<br>";
    response += "Configuration portal will close in 3 seconds...</div>";
    
    configServer.send(200, "text/html", response);
    
    // Stop config server before starting camera server
    delay(3000);
    configMode = false;
    configServer.stop();
    WiFi.softAPdisconnect(true);
    delay(1000);
    
    // Now setup camera and start camera server
    setupCamera();
    startCameraServer();
    
    Serial.println("Camera server started successfully!");
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
    
  } else {
    // Connection failed, switch back to AP mode
    WiFi.mode(WIFI_AP);
    delay(1000);
    WiFi.softAP(ap_ssid, ap_password);
    
    configServer.send(200, "text/html", "<div class='error'>Failed to connect to " + ssid + ". Please check password and try again.</div>");
  }
}

void handleStatus() {
  String status = "Status: ";
  if (WiFi.status() == WL_CONNECTED) {
    status += "Connected to " + WiFi.SSID() + " (IP: " + WiFi.localIP().toString() + ")";
  } else {
    status += "Not connected";
  }
  configServer.send(200, "text/plain", status);
}

String scanNetworks() {
  Serial.println("Starting WiFi scan...");
  
  int n = WiFi.scanNetworks();
  Serial.println("Scan completed, found " + String(n) + " networks");
  
  String html = "";
  
  if (n == 0) {
    html = "<div class='error'>No networks found. Make sure you are in range of WiFi networks.</div>";
  } else {
    html += "<h3>Found " + String(n) + " networks:</h3>";
    
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String security = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
      String ssid_clean = ssid;
      ssid_clean.replace("\"", "&quot;");
      ssid_clean.replace("'", "&#39;");
      
      // Create a safe ID for the password input
      String ssid_id = "net_" + String(i);
      
      html += "<div class='network'>";
      html += "<strong>" + ssid + "</strong> (" + String(rssi) + " dBm, " + security + ")<br>";
      if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
        html += "Password: <input type='password' id='" + ssid_id + "' placeholder='Enter password'> ";
      }
      html += "<button onclick=\"connectToNetwork('" + ssid_clean + "', '" + ssid_id + "')\">Connect</button>";
      html += "</div>";
    }
  }
  
  WiFi.scanDelete();
  return html;
}

void saveCredentials(String ssid, String password) {
  // Clear EEPROM first
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  
  // Write SSID
  for (int i = 0; i < ssid.length() && i < SSID_MAX_LEN; i++) {
    EEPROM.write(SSID_ADDR + i, ssid[i]);
  }
  
  // Write Password
  for (int i = 0; i < password.length() && i < PASS_MAX_LEN; i++) {
    EEPROM.write(PASS_ADDR + i, password[i]);
  }
  
  EEPROM.commit();
  Serial.println("Credentials saved to EEPROM");
}

bool loadCredentials(String &ssid, String &password) {
  ssid = "";
  password = "";
  
  // Read SSID
  for (int i = 0; i < SSID_MAX_LEN; i++) {
    char c = EEPROM.read(SSID_ADDR + i);
    if (c == 0) break;
    ssid += c;
  }
  
  // Read Password
  for (int i = 0; i < PASS_MAX_LEN; i++) {
    char c = EEPROM.read(PASS_ADDR + i);
    if (c == 0) break;
    password += c;
  }
  
  return (ssid.length() > 0);
}