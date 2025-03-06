#include <WiFi.h>
#include <WebServer.h>

#define FLOW_SENSOR_PIN 13  
#define RELAY_PIN 26        
#define LED_PIN1 14         
#define LED_PIN2 27         

volatile int pulseCount = 0;
float flowRate = 0.0;
float totalLiters = 0.0;
unsigned long oldTime = 0;
unsigned long resetTime = 0;
const float calibrationFactor = 450.0;

const float maxWaterUsage = 0.25;  
const unsigned long resetInterval = 120000; // 2 min = 1 day simulation
const unsigned long inputTimeout = 10000;   // 10 sec input timeout

const char *ssid = "YOUR_WIFI_SSID";
const char *pass = "YOUR_WIFI_PASSWORD";
const char *server = "api.thingspeak.com";  
String apiKey = "YOUR_WRITE_API_KEY_OF_THINGSPEAK";

WiFiClient client;
WebServer webServer(80);

bool pumpLocked = false;
bool additionalWaterSupplied = false;
float additionalLiters = 0.0;
float totalAdditionalUsed = 0.0;
bool waitingForInput = false;
bool sensorOnline = false;

// Store user input for additional water
String additionalWaterInput = "0";

void connectWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, pass);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? "\n✅ WiFi Connected!" : "\n❌ WiFi Connection Failed!");
    Serial.print("📡 ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
}

void IRAM_ATTR pulseCounter() {
    pulseCount++;
}

void requestAdditionalWater() {
    Serial.println("💧 Enter additional liters required (or wait 10 sec to skip): ");
    
    unsigned long inputStartTime = millis();
    waitingForInput = true;

    while (millis() - inputStartTime < inputTimeout) {
        if (Serial.available() > 0) {
            float requestedLiters = Serial.parseFloat();
            if (requestedLiters > 0) {
                additionalLiters = requestedLiters;
                Serial.print("✅ Additional Liters Requested: ");
                Serial.println(additionalLiters);

                totalLiters = 0.0;  
                totalAdditionalUsed += additionalLiters;
                pumpLocked = false;
                additionalWaterSupplied = true;

                digitalWrite(RELAY_PIN, HIGH);
                digitalWrite(LED_PIN1, HIGH);
                digitalWrite(LED_PIN2, LOW);
                Serial.println("🟢 Pump ON");
            } 
            waitingForInput = false;
            return;
        }
    }

    // If no input is received, continue normal operation
    Serial.println("⏳ No input received. Continuing normal operation.");
    waitingForInput = false;
}

void resetDailyUsage() {
    Serial.println("🔄 Daily Reset Triggered!");

    totalLiters = 0.0;
    totalAdditionalUsed = 0.0;
    additionalLiters = 0.0;
    pumpLocked = false;
    additionalWaterSupplied = false;

    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_PIN1, HIGH);
    digitalWrite(LED_PIN2, LOW);
    Serial.println("🟢 Pump ON after daily reset");

    resetTime = millis();
}

void handleRoot() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='2'>"; 
    html += "<title>Water Flow Monitor</title>";
    html += "<style>";
    
    // Global Styles
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; background-color: skyblue; color: #333; display: flex; justify-content: center; align-items: center; height: 100vh; flex-direction: row; }";
    
    // Container for both left and right blocks
    html += ".container { display: flex; flex-direction: row; width: 80%; max-width: 1200px; justify-content: space-between; gap: 20px; }";

    // Styling for both left and right containers
    html += ".block { flex: 1; padding: 30px; background: rgba(255, 255, 255, 0.9); border-radius: 15px; box-shadow: 0px 8px 16px rgba(0, 0, 0, 0.3); text-align: center; }";
    
    // ESP32 Status Block (Moved to top-left)
    html += ".esp-status { position: absolute; top: 20px; left: 20px; padding: 15px; background: #222; color: white; border-radius: 10px; font-size: 1.2em; box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.3); }";
    html += ".esp-status.online { background-color: #4CAF50; }";
    html += ".esp-status.offline { background-color: #f44336; }";

    // Info text styling
    html += ".info { font-size: 1.5em; margin-bottom: 15px; color: #333; }";
    
    // Status message styling
    html += ".status { font-size: 1.3em; font-weight: bold; color: white; padding: 15px; border-radius: 5px; transition: background-color 0.3s ease; }";
    html += ".status.on { background-color: #4CAF50; }";
    html += ".status.off { background-color: #f44336; }";
    
    // Additional water info styling
    html += ".additional-info { font-size: 1.3em; margin-bottom: 20px; color: #333; }";
    
    // Progress Bar Styles
    html += ".progress-bar { width: 100%; background: #ddd; border-radius: 10px; margin: 20px 0; height: 25px; }";
    html += ".progress { height: 100%; background-color: #007bff; border-radius: 10px; transition: width 0.5s ease; }";
    
    // Footer styles
    html += ".footer { font-size: 1em; margin-top: 20px; color: #fff; opacity: 0.8; }";

    // Input field and submit button styles
    html += ".form-container { text-align: left; }";
    html += "input[type='text'] { padding: 10px; margin-top: 10px; width: 100%; border-radius: 5px; border: 1px solid #ccc; font-size: 1.2em; }";
    html += "input[type='submit'] { padding: 10px 20px; background-color: #007bff; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 1.2em; margin-top: 15px; }";
    html += "input[type='submit']:hover { background-color: #0056b3; }";

    html += "</style></head><body>";
    
    // ESP32 Status Block (Positioned at the Top-Left)
    html += "<div class='esp-status ";
    html += (WiFi.status() == WL_CONNECTED) ? "online'>ESP32 ONLINE" : "offline'>ESP32 OFFLINE";
    html += "</div>";

    // Content structure
    html += "<div class='container'>";
    
    // Left Block (Details)
    html += "<div class='block'>";
    html += "<h1>Water Flow Monitoring</h1>";
    html += "<p class='info'><strong>Flow Rate:</strong> " + String(flowRate) + " L/min</p>";
    html += "<p class='info'><strong>Total Water Used:</strong> " + String(totalLiters) + " L</p>";
    
    // Display Additional Water Requested
    html += "<p class='additional-info'><strong>Additional Water Requested:</strong> " + String(additionalWaterInput) + " L</p>";
    
    // Progress bar with dynamic width based on water usage
    html += "<div class='progress-bar'><div class='progress' style='width: " + String((totalLiters / maxWaterUsage) * 100) + "%;'></div></div>";
    
    // Pump status (ON/OFF)
    html += "<p class='status " + String(digitalRead(RELAY_PIN) ? "on'> Pump ON" : "off'> Pump OFF") + "</p>";

    // Footer text
    html += "<p class='footer'>Water Flow Monitor - Keep an Eye on Your Consumption</p>";
    html += "</div>"; // End of left block
    
    // Right Block (Form)
    html += "<div class='block form-container'>";
    html += "<h2>Request Additional Water</h2>";
    html += "<form action='/updateWater' method='POST'>";
    html += "<label for='additionalWater'>Enter additional water (L):</label><br>";
    html += "<input type='text' id='additionalWater' name='additionalWater' value='" + additionalWaterInput + "'><br>";
    html += "<input type='submit' value='Submit'>";
    html += "</form>";
    html += "</div>"; // End of right block
    
    html += "</div>"; // End of container
    
    html += "</body></html>";
    
    // Send HTML response
    webServer.send(200, "text/html", html);
}




// Handle the additional water form submission
void handleUpdateWater() {
    if (webServer.hasArg("additionalWater")) {
        additionalWaterInput = webServer.arg("additionalWater");
        additionalLiters = additionalWaterInput.toFloat();
        Serial.print("✅ Additional Liters Requested from Web: ");
        Serial.println(additionalLiters);
        
        // Update the total liters and reset other variables
        totalLiters = 0.0;
        totalAdditionalUsed += additionalLiters;
        pumpLocked = false;
        additionalWaterSupplied = true;

        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(LED_PIN1, HIGH);
        digitalWrite(LED_PIN2, LOW);
        Serial.println("🟢 Pump ON after additional water input from website");
    }
    webServer.send(200, "text/html", "<h1>Water Input Updated!</h1><p>Go back to <a href='/'>monitoring page</a>.</p>");
}

void setup() {
    Serial.begin(115200);
    pinMode(FLOW_SENSOR_PIN, INPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN1, OUTPUT);
    pinMode(LED_PIN2, OUTPUT);
    
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_PIN1, HIGH);
    digitalWrite(LED_PIN2, LOW);
    
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);
    
    connectWiFi();
    
    // Set up routes
    webServer.on("/", HTTP_GET, handleRoot);  // Handle root request
    webServer.on("/updateWater", HTTP_POST, handleUpdateWater);  // Handle form submission for additional water
    
    webServer.begin();
    Serial.println("🌐 Web Server Started!");
    // Print the IP address
    Serial.print("Use this URL to connect: ");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
}

void loop() {
    if (millis() - oldTime > 1000) {
        detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
        flowRate = (pulseCount / calibrationFactor);
        totalLiters += (flowRate / 60.0);

        Serial.print("💧 Flow Rate: ");
        Serial.print(flowRate);
        Serial.print(" L/min\n🌊 Total Water Used: ");
        Serial.print(totalLiters);
        Serial.println(" L");

        pulseCount = 0;
        oldTime = millis();

        // Check if the sensor is functioning correctly
        if (pulseCount == 0) {
            sensorOnline = false;
        } else {
            sensorOnline = true;
        }

        // Pump OFF logic when max water usage is reached
        if (totalLiters >= maxWaterUsage && !pumpLocked) {
            digitalWrite(RELAY_PIN, LOW);
            digitalWrite(LED_PIN1, LOW);
            digitalWrite(LED_PIN2, HIGH);
            Serial.println("🚨 Pump OFF - Maximum Water Usage Reached!");

            pumpLocked = true;
            additionalWaterSupplied = false;

            requestAdditionalWater();
        }

        // Pump OFF logic after additional water is supplied
        if (additionalWaterSupplied && totalLiters >= additionalLiters) {
            digitalWrite(RELAY_PIN, LOW);
            digitalWrite(LED_PIN1, LOW);
            digitalWrite(LED_PIN2, HIGH);
            Serial.println("🚨 Pump OFF - Additional Water Supplied!");
            additionalWaterSupplied = false;
            pumpLocked = true;

            requestAdditionalWater();
        }

        attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);
    }

    // Ensure daily reset happens independently
    if (millis() - resetTime >= resetInterval) {
        resetDailyUsage();
    }
    
    // Handle HTTP requests
    webServer.handleClient();
    
    Serial.printf("Sending to ThingSpeak...\n");
    if (client.connect(server, 80)) {
        String postStr = apiKey + "&field1=" + String(flowRate) + "&field2=" + String(totalLiters) + "&field3=" + digitalRead(LED_PIN1) + "\r\n\r\n";
        client.print("POST /update HTTP/1.1\nHost: api.thingspeak.com\nConnection: close\nX-THINGSPEAKAPIKEY:" + apiKey + "\nContent-Type: application/x-www-form-urlencoded\nContent-Length: " + postStr.length() + "\n\n" + postStr);
        client.stop();
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    delay(2000);
}
