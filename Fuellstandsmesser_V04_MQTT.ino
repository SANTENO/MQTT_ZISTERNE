/*
  Pneumatic Zisternen-Füllstandsmessung.
  Start der Messung und Übertragung der Ergebnise über WLAN.

  Arduino-Bord: "NodeMCU 8766"
  Autor Wolfgang Neußer
  Stand: 15.11.2022

  Adaptierung für ESP32 und Relais:
  Sandro Antenori
  Hardware:
  - ESP32- diymore EPS32 38 pin ESP 32 dev kit
  - 2 Relais,
  - Sensor: SparkFun Qwiic MicroPressure Sensor
  - Druckpumpe an REL1 und
  - Entlueftungsventil REL2 aus Oberarm-Blutdruckmesser

  Messablauf:
  1. Abluftventil schließen, Druckpumpe einschalten
  2. Druck kontinuierlich messen
     Wenn Druckanstieg beendet -> Pumpe ausschalten
  3. Beruhigungszeit
  4. Aktueller Druck - atmosphärischen Druck = Messdruck
     Beispiel: 29810 Pa = 3040 mmH2O = 100% Füllstand
  5. Abluftventil öffnen
*/


#include <WiFi.h>       // Bibliothek WLAN
#include <Wire.h>       // I2C-Schnittstelle
#include <string>       // String handling
// Bibliothek für den Sensor (Im Bibliotheksverwalter unter "MicroPressure" suchen
// oder aus dem GitHub-Repository https://github.com/sparkfun/SparkFun_MicroPressure_Arduino_Library )
#include <SparkFun_MicroPressure.h>
#include <PubSubClient.h>  // MQTT Broker Server/Client lib 


// Konstruktor Sensor initialisieren - Ohne Parameter werden Default Werte verwendet
SparkFun_MicroPressure mpr;

// Zuordnung der Ein- Ausgänge
#define VENTIL 12                // GPIO25 (REL2)
#define PUMPE 14                 // GPIO26 (REL1)
#define SDA            21        // GPIO21 I2C SDA
#define SCL            22        // GPIO22 I2C SCL
#define AUF    HIGH              // Ventil öffnen
#define ZU     LOW               // Ventil schliessen

#define P_EIN  LOW               // Pumpe einschalten
#define P_AUS  HIGH              // Pumpe ausschalten
#define REPORTEACH 17000         // Each X times report athmosph. pressure -17.03 seconds per 1000 cycles
// 17000--> every 5 minutes report average of last 10 measurements--handy for changing conditions on IOBROKER

// WLAN/WIFI setup enter here:
const char* ssid = "WLAN_NAME_Here";
const char* password = "WLAN_passkey";

// MQTT Broker IPAdress is setup dynamically!
// address is assumed to be in same subnet, so it reads the own IP of the ESP (which is obtained via DHCP)
// and then completes it with changing the last value to one of the serverIPs

IPAddress  myIP(0, 0, 0, 0);
IPAddress  serverIPs(107, 84, 46, 53); // use of IPAddress to store 4 different addresses

String str_IP = "192.168.178.107";  // String variable for mqtt server resulting address
// str_IP.c_str() can be used instead of const char*;
int tryIPNr = 0;                    // selector of MQTT server # for str_IP[tryIPNr] - Range:0-3

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

// An eigene Zisterne anpassen (zur Berechnung der Füllmenge)
const int A = 31400;                      // Grundfläche der Zisterne in cm^2 (d * d * 3,14 / 4) 2m -->
const int maxFuellhoehe = 1900;           // Füllhöhe der Zisterne in mm
int i_repeat = 0;                         //counter for each X times of a measurement
int atmDruck, messDruck, vergleichswert;
int messSchritt, wassersaeule;
String str_hoehe = " - - ";
String str_volumen = "- - ";
String str_fuellstand = " - - ";
unsigned long messung, messTakt;
long liter = 0;                          //liter is what is given as value to IOBROKER-
// consider to use only height and calculat back in IOBROKER
//**************************************************************************************
// average function for X number of athmosphere values- thanks to ChatGPT
const int bufferSize = 10;
int values[bufferSize];
int curIndex = 0;

int calculateAverage() {
  int sum = 0;
  for (int i = 0; i < bufferSize; i++) {
    sum += values[i];
  }
  return sum / bufferSize;
}


// **************************************************************************************
// State-Machine Füllstandsmessung
//
void messablauf() {
  switch (messSchritt) {
    case 0:  // aktuellen atmosphärischen Druck erfassen
      if (digitalRead(VENTIL) == AUF && digitalRead(PUMPE) == P_AUS) {
        atmDruck = messDruck;
      }
      break;

    case 1:  // Messung gestartet
      vergleichswert = messDruck;
      digitalWrite(VENTIL, ZU);
      digitalWrite(PUMPE, P_EIN);
      messung = millis() + 2000;
      messSchritt = 2;
      break;

    case 2:  // warten solange Druck steigt
      if (messDruck > vergleichswert + 10) {
        vergleichswert = messDruck;
        messung = millis() + 1000;
      }
      if (wassersaeule > (maxFuellhoehe + 200)) {
        Serial.println("Fehler: Messleitung verstopft!");
        messSchritt = 4;
      }
      else if (messung < millis()) {
        digitalWrite(PUMPE, P_AUS);
        messung = millis() + 100;
        messSchritt = 3;
      }
      break;

    case 3:  // Beruhigungszeit abgelaufen, Messwert ermitteln
      if (messung < millis()) {
        str_hoehe = String(wassersaeule / 10) + "cm";
        str_volumen = String((wassersaeule / 10) * A / 1000) + "L";
        // Umrechnung Wassersäule in 0 - 100%
        str_fuellstand = String(map(wassersaeule, 0, maxFuellhoehe, 0, 100)) + "%";
        liter = (wassersaeule / 10) * A / 1000;  //
        Serial.println("Füllhöhe in cm: " + str_hoehe);
        Serial.println("Volumen: " + str_volumen);
        Serial.println("Füllstand: " + str_fuellstand);
        Serial.println();
        messSchritt = 4;
      }
      break;

    case 4:  // Ablauf beenden - MQTT petzen
      digitalWrite(VENTIL, AUF);
      digitalWrite(PUMPE, P_AUS);
      char ch_hoehe[8];
      dtostrf((wassersaeule / 10), 1, 2, ch_hoehe);
      client.publish("esp32/zisternefuellstand", ch_hoehe);
      messSchritt = 0;
      client.publish("esp32/zisternestart", "0");
      break;

    default:
      messSchritt = 0;
      break;
  }
}

void setup() {
  // Ventil initialization
  pinMode(VENTIL, OUTPUT);
  digitalWrite(VENTIL, AUF);

  // Pumpe initialization
  pinMode(PUMPE, OUTPUT);
  digitalWrite(PUMPE, P_AUS);

  Serial.begin(115200);
  delay(10);
  Serial.println();

  setup_wifi();                   //WLAN setup

  //client.setServer(mqtt_server, 1883); //static definition for one server address only
  //str_IP = String(myIP);
  str_IP = String(String(myIP[0]) + "." \
                  + String(myIP[1]) + "." \
                  + String(myIP[2]) + "." \
                  + String(serverIPs[0]));

  Serial.print(str_IP);
  Serial.println(" wird benutzt fuer MQTT Server");

  client.setServer(str_IP.c_str(), 1883); // use string.c_str() for const char* type
  client.setCallback(callback);


  Wire.begin(SDA, SCL, 400000);         // I2C initialisieren mit 400 kHz

  // Drucksensor initialisieren
  // Die Default-Adresse des Sensors ist 0x18
  // Für andere Adresse oder I2C-Bus: mpr.begin(ADRESS, Wire1)
  if (!mpr.begin()) {
    Serial.println("Keine Verbindung zum Drucksensor.");
    while (1);

  }

  messTakt = 0;
  messSchritt = 0;
  atmDruck = 97400.0;                   // Augangswert Atmosphärendruck in Pa
  for (int i = 0; i < bufferSize; i++) { // init average value
    values[i] = 0;
  }
}
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);           // We start by connecting to a WiFi network
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("my IP address is: ");
  myIP = IPAddress(WiFi.localIP());              //Serial.println(WiFi.localIP());
  Serial.println(myIP);
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Feel free to add more if statements to control more GPIOs with MQTT
  // If a message is received on the topic esp32/zisternestart, you check if the message is either on "1" or off - "0".
  // Changes the output state according to the message
  if (String(topic) == "esp32/zisternestart") {
    Serial.print("Changing output to ");
    if (messageTemp == "1") {
      Serial.println("on received");
      // starte Messung statt digitalWrite(ledPin, HIGH);
      messSchritt = 1;
      //messageTemp == "off" ;
    }
    else if (messageTemp == "0") {
      Serial.println("off");
      // digitalWrite(ledPin, LOW);
    }
  }
}

void reconnect() {
  // Loop until we're reconnected - change Server IP after 10 failed attempts to check if other device is available
  int attempts = 0;
  while (!client.connected()) {
    attempts += 1;
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32_ZISTERNE")) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("esp32/zisternestart");
    } else {
      Serial.print("failed ");
      Serial.print(attempts);
      Serial.print(" times, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      if (attempts >= 10) {
        //tryIPNr is used modulo 4 to setup the IP address
        tryIPNr += 1;
        attempts = 0;
        str_IP = String(String(myIP[0]) + "." \
                        + String(myIP[1]) + "." \
                        + String(myIP[2]) + "." \
                        + String(serverIPs[tryIPNr % 4]));
        Serial.print(str_IP);
        Serial.println(" wird benutzt fuer MQTT Server");

        client.setServer(str_IP.c_str(), 1883); // use string.c_str() for const char* type
        client.setCallback(callback);
      } else {
        // do nothing
      }

    }
  }
}


void loop() {
  static String inputString;
  // Kommandos über serielle Schnittstelle
  if (Serial.available()) {
    char inChar = (char)Serial.read();
    if ((inChar == '\r') || (inChar == '\n')) {
      if (inputString == "?") {
        Serial.println("Kommandos: ");
        Serial.println("p1 = Pumpe EIN");
        Serial.println("p0 = Pumpe AUS");
        Serial.println("v1 = Ventil ZU");
        Serial.println("v0 = Ventil AUF");
        Serial.println("start = Messung starten");
        Serial.println();
      }
      else if (inputString == "p1") {
        Serial.println("Pumpe EIN");
        digitalWrite(PUMPE, P_EIN);
      }
      else if (inputString == "p0") {
        Serial.println("Pumpe AUS");
        digitalWrite(PUMPE, P_AUS);
      }
      else if (inputString == "v1") {
        Serial.println("Ventil ZU");
        digitalWrite(VENTIL, ZU);
      }
      else if (inputString == "v0") {
        Serial.println("Ventil AUF");
        digitalWrite(VENTIL, AUF);
      }
      else if (inputString == "start") {
        if (messSchritt == 0) {
          Serial.println("Messung gestartet");
          messSchritt = 1;
        }
      }
      inputString = "";
    } else inputString += inChar;
  } //if serial available

  // Alle 10 ms Sensorwert auslesen --> not OK, needs to be corrected for overflow problem of millis
  // average out last 10 measurements to avoid peaks

  if (messTakt < millis()) {
    curIndex = i_repeat % bufferSize;
    values[curIndex] = atmDruck;
    int average = calculateAverage();

    i_repeat += 1;    //counter of how many times athmosphere pressure has been read out

    messDruck = ((messDruck * 50) + int(mpr.readPressure(PA))) / 51;   // Messwert in Pascal auslesen und filtern
    wassersaeule = (messDruck - atmDruck) * 10197 / 100000;            // Umrechnung Pa in mmH2O
    if (wassersaeule < 0) wassersaeule = 0;                            // avoid negative values
    if (i_repeat >= REPORTEACH) {                                      // cycle x times for reporting pressure value
      i_repeat = 0;

      //Serial.print("Athm Druck:");
      //Serial.println(atmDruck);
      Serial.print("Average athm Druck:");
      Serial.println(average);
      char ch_atmdruck[8];
      snprintf(ch_atmdruck, 8, "%d", average);
      client.publish("esp32/athmosphere", ch_atmdruck);               // report athmosph pressure to MQTT server
    }

    messTakt = millis() + 10;
  }

  // Sicherheitsabschaltung der Pumpe bei Überdruck
  if ((messSchritt == 0) && (wassersaeule > (maxFuellhoehe + 300))) {
    digitalWrite(PUMPE, P_AUS);
    Serial.println("Überdruck. Messleitung verstopft!");
  }

  // State-Machine
  messablauf();


  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
  }
}
