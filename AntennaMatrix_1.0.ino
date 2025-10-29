#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <AsyncTCP.h>

// Define GPIO pins for COM1 and COM2
const int com1_pins[4] = {25, 23, 22, 21}; // ANT1, ANT2, ANT3, ANT4
const int com2_pins[4] = {19, 18, 17, 16}; // ANT1, ANT2, ANT3, ANT4

// Default names
String radioNames[2] = {"COM1 (TX)", "COM2 (RX)"};
String antennaNames[4] = {"ANT1", "ANT2", "ANT3", "ANT4"};

// Current states (0 or 1 for each connection)
bool com1_states[4] = {false, false, false, false};
bool com2_states[4] = {false, false, false, false};

// Allow cross-connection flag
bool allowCross = false;

// Presets (up to 5, each with com1 and com2 states)
#define MAX_PRESETS 5
struct Preset {
  bool com1[4];
  bool com2[4];
  String name;
};
Preset presets[MAX_PRESETS];
int presetCount = 0;

// WiFi settings
String ssid = "AntennaSwitcher";
String password = "password123";
String stationSSID = "";
String stationPass = "";
bool useDHCP = true;
IPAddress staticIP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Preferences for storage
Preferences prefs;

// Async Web Server on port 80
AsyncWebServer server(80);

// Function to update relays based on states
void updateRelays() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(com1_pins[i], com1_states[i] ? HIGH : LOW);
    digitalWrite(com2_pins[i], com2_states[i] ? HIGH : LOW);
  }
}

// Check for dangerous cross-connection
bool isDangerous() {
  for (int i = 0; i < 4; i++) {
    if (com1_states[i] && com2_states[i]) {
      return true;
    }
  }
  return false;
}

// Load settings from preferences
void loadSettings() {
  prefs.begin("antennaSwitch", false);
  
  // Load radio and antenna names
  for (int i = 0; i < 2; i++) {
    radioNames[i] = prefs.getString(("radio" + String(i)).c_str(), radioNames[i]);
  }
  for (int i = 0; i < 4; i++) {
    antennaNames[i] = prefs.getString(("ant" + String(i)).c_str(), antennaNames[i]);
  }
  
  // Load allow cross
  allowCross = prefs.getBool("allowCross", false);
  
  // Load WiFi settings
  stationSSID = prefs.getString("stationSSID", "");
  stationPass = prefs.getString("stationPass", "");
  useDHCP = prefs.getBool("useDHCP", true);
  uint32_t ip = prefs.getUInt("staticIP", staticIP);
  staticIP = IPAddress(ip);
  ip = prefs.getUInt("gateway", gateway);
  gateway = IPAddress(ip);
  ip = prefs.getUInt("subnet", subnet);
  subnet = IPAddress(ip);
  
  // Load presets
  presetCount = prefs.getInt("presetCount", 0);
  for (int p = 0; p < presetCount; p++) {
    presets[p].name = prefs.getString(("pName" + String(p)).c_str(), "Preset " + String(p+1));
    for (int i = 0; i < 4; i++) {
      presets[p].com1[i] = prefs.getBool(("p1_" + String(p) + "_" + String(i)).c_str(), false);
      presets[p].com2[i] = prefs.getBool(("p2_" + String(p) + "_" + String(i)).c_str(), false);
    }
  }
  
  prefs.end();
}

// Save settings to preferences
void saveSettings() {
  prefs.begin("antennaSwitch", false);
  
  // Save names
  for (int i = 0; i < 2; i++) {
    prefs.putString(("radio" + String(i)).c_str(), radioNames[i]);
  }
  for (int i = 0; i < 4; i++) {
    prefs.putString(("ant" + String(i)).c_str(), antennaNames[i]);
  }
  
  // Save allow cross
  prefs.putBool("allowCross", allowCross);
  
  // Save WiFi
  prefs.putString("stationSSID", stationSSID);
  prefs.putString("stationPass", stationPass);
  prefs.putBool("useDHCP", useDHCP);
  prefs.putUInt("staticIP", staticIP);
  prefs.putUInt("gateway", gateway);
  prefs.putUInt("subnet", subnet);
  
  // Save presets
  prefs.putInt("presetCount", presetCount);
  for (int p = 0; p < presetCount; p++) {
    prefs.putString(("pName" + String(p)).c_str(), presets[p].name);
    for (int i = 0; i < 4; i++) {
      prefs.putBool(("p1_" + String(p) + "_" + String(i)).c_str(), presets[p].com1[i]);
      prefs.putBool(("p2_" + String(p) + "_" + String(i)).c_str(), presets[p].com2[i]);
    }
  }
  
  prefs.end();
}

// Setup WiFi AP or Station
void setupWiFi() {
  if (stationSSID != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(stationSSID.c_str(), stationPass.c_str());
    if (!useDHCP) {
      WiFi.config(staticIP, gateway, subnet);
    }
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      // Fallback to AP
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid.c_str(), password.c_str());
    }
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), password.c_str());
  }
}

void setup() {
  // Initialize pins
  for (int i = 0; i < 4; i++) {
    pinMode(com1_pins[i], OUTPUT);
    pinMode(com2_pins[i], OUTPUT);
    digitalWrite(com1_pins[i], LOW);
    digitalWrite(com2_pins[i], LOW);
  }
  
  // Load settings
  loadSettings();
  
  // Setup WiFi
  setupWiFi();
  
  // Web server routes
  
  // Main UI page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Antenna Switcher</title>
      <style>
        body { font-family: Arial; }
        table { border-collapse: collapse; width: 100%; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: center; }
        th { background-color: #f2f2f2; }
        .danger { background-color: red; color: white; }
        .warning::after { content: '!'; font-weight: bold; color: red; }
      </style>
      <script>
        let com1 = [)rawliteral";
    html += String(com1_states[0]) + "," + String(com1_states[1]) + "," + String(com1_states[2]) + "," + String(com1_states[3]) + R"rawliteral(];
        let com2 = [)rawliteral";
    html += String(com2_states[0]) + "," + String(com2_states[1]) + "," + String(com2_states[2]) + "," + String(com2_states[3]) + R"rawliteral(];
        let allowCross = )rawliteral" + String(allowCross ? 1 : 0) + R"rawliteral(;
        let radioNames = [')rawliteral" + radioNames[0] + "','" + radioNames[1] + R"rawliteral('];
        let antNames = [')rawliteral" + antennaNames[0] + "','" + antennaNames[1] + "','" + antennaNames[2] + "','" + antennaNames[3] + R"rawliteral('];

        function checkDanger() {
          for (let i = 0; i < 4; i++) {
            if (com1[i] && com2[i]) return true;
          }
          return false;
        }

        function updateUI() {
          let table = document.getElementById('switchTable');
          let body = table.getElementsByTagName('tbody')[0];
          body.innerHTML = '';
          for (let r = 0; r < 2; r++) {
            let row = body.insertRow();
            let cell = row.insertCell();
            cell.innerText = radioNames[r];
            for (let a = 0; a < 4; a++) {
              let cell = row.insertCell();
              let btn = document.createElement('button');
              btn.innerText = (r == 0 ? com1[a] : com2[a]) ? 'Connected' : 'Connect';
              btn.style.backgroundColor = (r == 0 ? com1[a] : com2[a]) ? 'green' : 'gray';
              btn.onclick = () => toggle(r, a);
              cell.appendChild(btn);
            }
          }
          document.body.className = checkDanger() ? 'danger warning' : '';
        }

        function toggle(r, a) {
          let tempCom1 = [...com1];
          let tempCom2 = [...com2];
          if (r == 0) tempCom1[a] = !tempCom1[a];
          else tempCom2[a] = !tempCom2[a];
          
          let danger = false;
          for (let i = 0; i < 4; i++) {
            if (tempCom1[i] && tempCom2[i]) danger = true;
          }
          
          if (danger && !allowCross) {
            alert('Dangerous connection not allowed!');
            return;
          } else if (danger) {
            if (!confirm('Are you sure? This could damage equipment!')) return;
          }
          
          if (r == 0) com1[a] = tempCom1[a];
          else com2[a] = tempCom2[a];
          
          fetch('/update?com=' + r + '&ant=' + a + '&state=' + (r == 0 ? com1[a] : com2[a]));
          updateUI();
        }

        window.onload = updateUI;
      </script>
    </head>
    <body>
      <h1>Antenna Switcher UI</h1>
      <table id="switchTable">
        <thead>
          <tr>
            <th>Radio</th>)rawliteral";
    for (int i = 0; i < 4; i++) {
      html += "<th>" + antennaNames[i] + "</th>";
    }
    html += R"rawliteral(
          </tr>
        </thead>
        <tbody></tbody>
      </table>
      <h2>Presets</h2>
      <div id="presets">)rawliteral";
    for (int p = 0; p < presetCount; p++) {
      html += "<button onclick=\"loadPreset(" + String(p) + ")\">" + presets[p].name + "</button>";
    }
    html += R"rawliteral(
      </div>
      <button onclick="savePreset()">Save Current as Preset</button>
      <a href="/settings">Settings</a>
      <script>
        function loadPreset(p) {
          fetch('/loadPreset?p=' + p)
            .then(response => response.json())
            .then(data => {
              com1 = data.com1;
              com2 = data.com2;
              updateUI();
            });
        }
        function savePreset() {
          let name = prompt('Preset name:');
          if (name) {
            fetch('/savePreset?name=' + encodeURIComponent(name) + '&com1=' + com1.join(',') + '&com2=' + com2.join(','));
            location.reload();
          }
        }
      </script>
    </body>
    </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });
  
  // Update connection
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    int com = request->getParam("com")->value().toInt();
    int ant = request->getParam("ant")->value().toInt();
    bool state = request->getParam("state")->value().toInt();
    
    if (com == 0) com1_states[ant] = state;
    else com2_states[ant] = state;
    
    updateRelays();
    request->send(200, "text/plain", "OK");
  });
  
  // Load preset
  server.on("/loadPreset", HTTP_GET, [](AsyncWebServerRequest *request){
    int p = request->getParam("p")->value().toInt();
    if (p < presetCount) {
      memcpy(com1_states, presets[p].com1, sizeof(com1_states));
      memcpy(com2_states, presets[p].com2, sizeof(com2_states));
      updateRelays();
      String json = "{\"com1\":[" + String(com1_states[0]) + "," + String(com1_states[1]) + "," + String(com1_states[2]) + "," + String(com1_states[3]) + "],";
      json += "\"com2\":[" + String(com2_states[0]) + "," + String(com2_states[1]) + "," + String(com2_states[2]) + "," + String(com2_states[3]) + "]}";
      request->send(200, "application/json", json);
    } else {
      request->send(400);
    }
  });
  
  // Save preset
  server.on("/savePreset", HTTP_GET, [](AsyncWebServerRequest *request){
    if (presetCount < MAX_PRESETS) {
      String name = request->getParam("name")->value();
      String com1Str = request->getParam("com1")->value();
      String com2Str = request->getParam("com2")->value();
      
      presets[presetCount].name = name;
      for (int i = 0; i < 4; i++) {
        presets[presetCount].com1[i] = com1Str.substring(i*2, i*2+1).toInt();
        presets[presetCount].com2[i] = com2Str.substring(i*2, i*2+1).toInt();
      }
      presetCount++;
      saveSettings();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Max presets reached");
    }
  });
  
  // Settings page
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head><title>Settings</title></head>
    <body>
      <h1>Settings</h1>
      <form action="/saveSettings" method="POST">
        <h2>Radio Names</h2>
        COM1: <input name="radio0" value=")rawliteral" + radioNames[0] + R"rawliteral("><br>
        COM2: <input name="radio1" value=")rawliteral" + radioNames[1] + R"rawliteral("><br>
        
        <h2>Antenna Names</h2>)rawliteral";
    for (int i = 0; i < 4; i++) {
      html += "ANT" + String(i+1) + ": <input name=\"ant" + String(i) + "\" value=\"" + antennaNames[i] + "\"><br>";
    }
    html += R"rawliteral(
        <h2>Allow Cross-Connections</h2>
        <input type="checkbox" name="allowCross" )rawliteral" + String(allowCross ? "checked" : "") + R"rawliteral(><br>
        
        <h2>WiFi Settings</h2>
        Station SSID: <input name="stationSSID" value=")rawliteral" + stationSSID + R"rawliteral("><br>
        Station Password: <input name="stationPass" value=")rawliteral" + stationPass + R"rawliteral("><br>
        Use DHCP: <input type="checkbox" name="useDHCP" )rawliteral" + String(useDHCP ? "checked" : "") + R"rawliteral(><br>
        Static IP: <input name="staticIP" value=")rawliteral" + staticIP.toString() + R"rawliteral("><br>
        Gateway: <input name="gateway" value=")rawliteral" + gateway.toString() + R"rawliteral("><br>
        Subnet: <input name="subnet" value=")rawliteral" + subnet.toString() + R"rawliteral("><br>
        
        <input type="submit" value="Save">
      </form>
      <a href="/">Back to UI</a>
    </body>
    </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });
  
  // Save settings
  server.on("/saveSettings", HTTP_POST, [](AsyncWebServerRequest *request){
    // Update names
    for (int i = 0; i < 2; i++) {
      if (request->hasParam("radio" + String(i), true)) {
        radioNames[i] = request->getParam("radio" + String(i), true)->value();
      }
    }
    for (int i = 0; i < 4; i++) {
      if (request->hasParam("ant" + String(i), true)) {
        antennaNames[i] = request->getParam("ant" + String(i), true)->value();
      }
    }
    
    // Allow cross
    allowCross = request->hasParam("allowCross", true);
    
    // WiFi
    if (request->hasParam("stationSSID", true)) stationSSID = request->getParam("stationSSID", true)->value();
    if (request->hasParam("stationPass", true)) stationPass = request->getParam("stationPass", true)->value();
    useDHCP = request->hasParam("useDHCP", true);
    if (request->hasParam("staticIP", true)) staticIP.fromString(request->getParam("staticIP", true)->value());
    if (request->hasParam("gateway", true)) gateway.fromString(request->getParam("gateway", true)->value());
    if (request->hasParam("subnet", true)) subnet.fromString(request->getParam("subnet", true)->value());
    
    saveSettings();
    setupWiFi();  // Reconfigure WiFi if changed
    
    request->redirect("/");
  });
  
  server.begin();
}

void loop() {
  // Nothing needed here, async server handles it
}