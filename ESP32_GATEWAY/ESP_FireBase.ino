#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <BLEDevice.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ctime>

#define WIFI_SSID     "Redmi"
#define WIFI_PASSWORD "22337788"
#define API_KEY       "AIzaSyAH9vhtMWVc-wdAARuSioNTDKJC2PHwq6I"
#define DATABASE_URL  "https://iotfinal-a8fad-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define TARGET_NAME "HM10_IOT"
static BLEUUID SERVICE_UUID("0000FFE0-0000-1000-8000-00805F9B34FB");
static BLEUUID CHAR_UUID   ("0000FFE1-0000-1000-8000-00805F9B34FB");

BLEAddress* targetAddr = nullptr;
BLEClient* client = nullptr;
BLERemoteCharacteristic* pChar = nullptr;
bool doConnect = false, bleOK = false;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;


String getLocalTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";

  char buf[32];
  strftime(buf, sizeof(buf), "%H:%M:%S %d/%m/%Y", &timeinfo);
  return String(buf);
}

class ScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.haveName() && dev.getName() == TARGET_NAME) {
      Serial.printf("Found HM10: %s\n", dev.getName().c_str());
      dev.getScan()->stop();
      targetAddr = new BLEAddress(dev.getAddress());
      doConnect = true;
    }
  }
};

bool bleConnect(BLEAddress &a) {
  client = BLEDevice::createClient();
  if (!client->connect(a)) return false;
  auto svc = client->getService(SERVICE_UUID);
  if (!svc) return false;
  pChar = svc->getCharacteristic(CHAR_UUID);
  if (!pChar) return false;
  bleOK = true;
  Serial.println("BLE connected!");
  return true;
}


void bleSend(const char *msg) {
  if (bleOK && pChar) {
    pChar->writeValue((uint8_t*)msg, strlen(msg), false);
    Serial.printf("Sent BLE: %s\n", msg);
  }
}


void applyToFirebase(const String &code, const String &id) {
  String path = "/";
  String now = getLocalTimeString();

  if (code == "AC") {
    Firebase.RTDB.setString(&fbdo, path + "new_card", id);
  }
  else if (code == "ODI") {
    Firebase.RTDB.setInt(&fbdo, path + "door", 1);
    Firebase.RTDB.setString(&fbdo, path + "user", id);
    Firebase.RTDB.setString(&fbdo, path + "time", now);

  } else if (code == "ODA") {
    Firebase.RTDB.setInt(&fbdo, path + "door", 1);
    Firebase.RTDB.setString(&fbdo, path + "user", "ADMIN");
    Firebase.RTDB.setString(&fbdo, path + "time", now);

  } else if (code == "CD") {
    Firebase.RTDB.setInt(&fbdo, path + "door", 0);
    if (id != "XXXX")
      Firebase.RTDB.setString(&fbdo, path + "user", id);
    else
      Firebase.RTDB.setString(&fbdo, path + "user", "ADMIN");
    //Firebase.RTDB.setString(&fbdo, path + "time", now);
  }
}


String lastMsg = "";
String bleReadIfChanged() {
  if (!bleOK || !pChar) return "";
  String s = pChar->readValue();
  s.trim();
  if (s.length() > 0 && s != lastMsg) {
    lastMsg = s;
    return s;
  }
  return "";
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Firebase BLE Gateway ===");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(300); }
  Serial.println("\nWiFi connected!");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  BLEDevice::init("ESP32_GATEWAY");
  BLEScan* sc = BLEDevice::getScan();
  sc->setAdvertisedDeviceCallbacks(new ScanCB());
  sc->setActiveScan(true);
  sc->start(5, false);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) signupOK = true;
  else Serial.printf("Signup failed: %s\n", config.signer.signupError.message.c_str());
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase ready!");
}

unsigned long tScan = 0, tCheck = 0, tCmd = 0;

void loop() {
  if (doConnect && targetAddr) {
    if (bleConnect(*targetAddr)) doConnect = false;
    else { bleOK = false; doConnect = false; tScan = millis(); }
  }
  if (!bleOK && millis() - tScan > 3000) {
    BLEDevice::getScan()->start(5, false);
    tScan = millis();
  }

  if (bleOK && millis() - tCheck > 300) {
    tCheck = millis();
    String s = bleReadIfChanged();
    if (s.length() > 0) {
      Serial.print("BLE raw: "); Serial.println(s);
      int p1 = s.indexOf(',');
      if (p1 > 0) {
        String code = s.substring(0, p1);
        String id   = s.substring(p1 + 1);
        code.trim(); id.trim();
        Serial.printf("→ Code=%s | ID=%s\n", code.c_str(), id.c_str());
        if (Firebase.ready() && signupOK) applyToFirebase(code, id);
      }
    }
  }

  if (Firebase.ready() && signupOK && millis() - tCmd > 600) {
    tCmd = millis();

    if (Firebase.RTDB.getString(&fbdo, "cmd_open")) {
      String cmd = fbdo.stringData();
      cmd.trim();
      if (cmd == "toggle" && bleOK) {
        bleSend("O\n");
        Firebase.RTDB.setString(&fbdo, "cmd_open", "none");
        Serial.println("Command: OPEN door (admin)");
      }
    }

    if (Firebase.RTDB.getString(&fbdo, "cmd_add")) {
      String cmd = fbdo.stringData();
      cmd.trim();
      if (cmd == "add" && bleOK) {
        bleSend("R\n");
        Firebase.RTDB.setString(&fbdo, "cmd_add", "none");
        Serial.println("Command: ADD card");
      }
    }
  }
}
