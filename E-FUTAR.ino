/*
 * E-FUTÁR
 * Beágyazott BKK Futár port ESP32 SoC-ra SSD1306 OLED kijelzővel
 * 
 * Írta: Márkus Balázs - 2017
 * 
 * A program segítségével a mikrokonroller egy OLED kijelzőre kiírja az adott megállóból induló következő buszokat,
 * és indulásig hátralévő idejüket, valamint az aktuális időt. A program több megállót is képes kezelni, ez között
 * a fejlesztőkártya beépített gombjával válthatunk, a loading állapotot a beépített fehér SMD LED jelzi.
 * 
 * Feltöltés előtt WiFi konfiguráció szükséges!
 * Amennyiben másik megállókat szeretnénk beépíteni, a beparsolandó JSON fájlok listáját kell módosítani!
 * 
 * További információkért lásd a ReadMe fájlt!
 * 
 */

#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include <stdlib.h>

bool nyariidoszamitas = false; //nyáron true, télen false, ekkor egy órát hozzáad a lekérdezett UNIX időhöz

enum buszmegallo {Baross, Janos, Varoskozpont}; // A három megállót megkülömböztető enum, ezek között váltunk a vezérlőgomb interruptjával, és a main loopban ez alapján állítódik meg, hogy milyen HTTP GET requestet küldünk a szervernek
enum buszmegallo megallo = Baross; // az alapértelmezett buszmegálló a Pesterzsébet, Baross utca


bool BarossJanosButtonPressed = false; // a pergésmentesítés miatt
//OLED pins to ESP32 GPIOs via this connecthin:
//OLED_SDA -- GPIO4
//OLED_SCL -- GPIO15
//OLED_RST -- GPIO16

SSD1306  display(0x3c, 4, 15); //Az OLED kijelzőt inicializáljuk

//Kommentezd ki a megfelelő wifi adatokat!

char ssid[] = "SSID"; //  your network SSID (name)
char pass[] = "Pass";    // your network password (use for WPA, or use as key for WEP)


int keyIndex = 0;            // your network key Index number (needed only for WEP)

int status = WL_IDLE_STATUS; // a WIFI csatlakozását jelző státuszváltozó

// Initialize the Ethernet client library with the IP address and port of the server that you want to connect to (port 80 is default for HTTP):
WiFiClient client;

// Ha az alapértelmezett megálló: Pesterzsébet, Baross utca, ezt a rész kommentezd ki!
char* stopName = "Baross utca";
char* resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04144&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource

//HTTP kapcsolat adatainak megadása
const char* server = "futar.bkk.hu";  // server's address
const unsigned long BAUD_RATE = 115200;                 // serial connection speed
const unsigned long HTTP_TIMEOUT = 10000;  // max respone time from server
const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response

//A BusData struktúra az alap adattároló egység, egy struktúra egy busz adatait tárolja, és 10 darab ilyen struktúra alkot egy 10-es buszlistát (lásd buszlista tömb)
struct BusData {
    char shortName[16]; //A járat száma
    char stopHeadsign[32]; //A célállomás neve
    char stopHeadsignWithShortName[32]; //A járat száma + a célállomás neve együtt, mint a buszokon kiírva, a kiíratáshoz kell egyben
    char predictedArrivalTime[32]; //A lekérdezett jósolt vagy menetrendszerinti érkezési idő (attól függ hogy van-e aktív GPS kapcsolat a buszon) EPOCHtól számított időformátumban, milliszekundumban
    long predictedArrivalTimeLong; //A jósolt vagy menetrendszerinti érkezési idő (attól függ hogy van-e aktív GPS kapcsolat a buszon) EPOCHtól számított időformátumban, másodpercben, long formátumban
    int predictedArrivalMinutesInt; //Az érkezési idő percben megadva
    char predictedArrivalMinutesString[3]; //Az érkezési idő percben megadva, stringként, végén egy aposztróffal
};

//A long típusban, másodperces felbontásban megkapott érkezési időt átalakítja int típusú, perces felbontásúvá, hogy a predictedArrivalMinutesInt-ban tudjam másolni
int SecondsToMinutes(long secondsLong) {
    long minutesLong;
    int minutesInt;
    minutesLong = secondsLong/60;
    minutesInt = (int)minutesLong;
    return minutesInt;
}

//Az érkezési idő kiíratásához az előbb int-té konvertált időt kell char-rá konvertálni, valamint a végére az aposztrófot tenni, ezt csinálja a függvény
void ArrivalMinutesToString(int arrivalMinutes, char* arrivalString) {
    if(arrivalMinutes < 1) { // ha
        for(int k=0; k<3; k++) arrivalString[k] = ' '; // ki kell clearelni előtte, hogy ha az előző ciklusból bennemaradt valami, az kitörlődjön
        arrivalString[0]= '-';
    }
    else {
        if(arrivalMinutes<10) {
            for(int k=0; k<3; k++) arrivalString[k] = ' '; // ki kell clearelni előtte, hogy ha az előző ciklusból bennemaradt valami, az kitörlődjön
            arrivalString[0]=arrivalMinutes + '0';
            arrivalString[1]= '\'';
            arrivalString[2]= '\0'; // lerövidítjük a stringet mert jobbra záráskor egy vonalba kell legyenek az aposztrófok, így oldjuk meg!
        }
        else {
            for(int k=0; k<3; k++) arrivalString[k] = ' '; // ki kell clearelni előtte
            arrivalString[0]=(arrivalMinutes/10) + '0';
            arrivalString[1]=(arrivalMinutes%10) + '0';
            arrivalString[2]= '\'';
        }
    }
}

struct BusData buszlista[10];
char currentTime[32];
long currentTimeLong,currentTimeHours,currentTimeMinutes;

char clockTimeString[5];

void ConvertTime() {
    if(nyariidoszamitas==true) {
        currentTimeHours = (currentTimeLong % 86400) / 3600;
        currentTimeMinutes = (currentTimeLong % 3600) / 60;
    }
    else {
        currentTimeHours = ((((currentTimeLong % 86400) / 3600)+1)%24); //azért kell a mod24 mert a téli időszámítás miatt 24:05-öt mutatott éjjel
        currentTimeMinutes = (currentTimeLong % 3600) / 60;
    }
    if(currentTimeHours<10) {
        for(int k=0; k<4; k++) clockTimeString[k] = ' '; // ki kell clearelni előtte ha kétjegyűről egyjegyűre vált
        clockTimeString[0] = currentTimeHours + '0'; //konverzió char-rá
        clockTimeString[1] = ':';
        clockTimeString[2] = (currentTimeMinutes/10) + '0';
        clockTimeString[3] = (currentTimeMinutes%10) + '0';
    }
    else {
        for(int k=0; k<4; k++) clockTimeString[k] = ' '; // ki kell clearelni előtte
        clockTimeString[0] = (currentTimeHours/10) + '0'; //konverzió char-rá
        clockTimeString[1] = (currentTimeHours%10) + '0';
        clockTimeString[2] = ':';
        clockTimeString[3] = (currentTimeMinutes/10) + '0';
        clockTimeString[4] = (currentTimeMinutes%10) + '0';
    }
    Serial.print("A pontos ido: ");
    Serial.print(currentTimeHours);
    Serial.print(":");
    Serial.println(currentTimeMinutes);
}

void setupDisplay() {
    pinMode(16,OUTPUT);
    digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high
    // Initialising the UI will init the display too.
    display.init();

    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
}

void setStop() {
    if(megallo==Baross) {
        stopName = "Baross utca";
        resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04144&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource
    }
    if(megallo==Janos) {
        stopName = "János utca";
        resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04126&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource
    }
    if(megallo==Varoskozpont) {
        stopName = "Városközpont";
        resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04122&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource
    }
}

void megallovaltas() {
    digitalWrite(25, HIGH);
    if(megallo==Baross) megallo = Janos;
    else if(megallo==Janos) megallo = Varoskozpont;
    else if(megallo==Varoskozpont) megallo = Baross;
}


// ARDUINO entry point #1: runs once when you press reset or power the board
void setup() {
    Serial.begin(115200);
    pinMode(0, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(0), megallovaltas, FALLING);
    pinMode(25, OUTPUT); //a busy LED-nek az alaplapon
    digitalWrite(25, LOW);
    setupDisplay();
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 0, "E-FUTÁR");
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 28, "Csatlakozás");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 52, "Írta: Márkus Balázs - 2017");
    display.display();
    // attempt to connect to Wifi network:

    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);

    //A BOOT képernyőn a pontok animálva növekednek, ugyanúgy mint a Serial üzenetben, míg a wifihez nem csatlakozik
    int x = 88;
    while (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(ssid, pass);
        delay(300);
        Serial.print(".");
        display.setFont(ArialMT_Plain_16);
        display.drawString(x, 28, ".");
        display.display();
        x=x+4;
        if(x>126) {
            x=88;
            //újrarajzoljuk a kijelzőt, ha a pontok elérték a szélét
            display.clear();
            display.setTextAlignment(TEXT_ALIGN_LEFT);
            display.setFont(ArialMT_Plain_24);
            display.drawString(0, 0, "E-FUTÁR");
            display.setFont(ArialMT_Plain_16);
            display.drawString(0, 28, "Csatlakozás");
            display.setFont(ArialMT_Plain_10);
            display.drawString(0, 52, "Írta: Márkus Balázs - 2017");
            display.display();
        }
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println("Connected to wifi");
    printWifiStatus();

    Serial.println("\nStarting connection to server...");
    // if you get a connection, report back via serial:
    if (client.connect(server, 80)) {
        Serial.println("connected to server");
    }
}


// Main loop
void loop() {

    setStop();
    clearBusList();
    if (connect(server)) {
        if (sendRequest(server, resource) && skipResponseHeaders()) {
            if (readReponseContent()) {
                printBusData();
            }
        }
    }
    disconnect();
    // innentől kezdve a grafikus rész
    drawList();

    //egy kis delay hogy ne pörögjön
    wait();
}

void drawList() {
    //Lista kiiratasa
    // Tovabbi betutipusok a http://oleddisplay.squix.ch/ oldalon keszithetoek
   
    display.clear();

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, stopName);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(128, 0, clockTimeString);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    if(buszlista[0].stopHeadsign[0]=='\0') {
        display.drawString(0, 20, "Nem található indulás");
        display.drawString(0, 34, "60 percen belül.");
    }
    else {
        display.drawString(0, 20, buszlista[0].stopHeadsignWithShortName);
        display.drawString(0, 34, buszlista[1].stopHeadsignWithShortName);
        display.drawString(0, 48, buszlista[2].stopHeadsignWithShortName);
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 20, buszlista[0].predictedArrivalMinutesString); //116nál már a percvessző kilóg szóval 115-höz kell tenni a percszámokat! 88'-vel tesztelve
        display.drawString(128, 34, buszlista[1].predictedArrivalMinutesString);
        display.drawString(128, 48, buszlista[2].predictedArrivalMinutesString);
    }
    display.display();
}

// Open connection to the HTTP server
bool connect(const char* hostName) {
    Serial.print("Connect to ");
    Serial.println(hostName);

    bool ok = client.connect(hostName, 80);

    Serial.println(ok ? "Connected" : "Connection Failed!");
    return ok;
}

// Send the HTTP GET request to the server
bool sendRequest(const char* host, const char* resource) {
    Serial.print("GET ");
    Serial.println(resource);

    client.print("GET ");
    client.print(resource);
    client.println(" HTTP/1.0");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();

    return true;
}

// Skip HTTP headers so that we are at the beginning of the response's body
bool skipResponseHeaders() {
    // HTTP headers end with an empty line
    char endOfHeaders[] = "\r\n\r\n";

    client.setTimeout(HTTP_TIMEOUT);
    bool ok = client.find(endOfHeaders);

    if (!ok) {
        Serial.println("No response or invalid response!");
    }

    return ok;
}

uint16_t maxArraySize=0, ArraySize=0;

void clearBusList() {
    //Előbb kinullázuk a buszlistát, majd belemásoljuk a megfelelő mennyiségű induló buszt, amit a MaxArrySize-ban határoztunk meg, a többi nulla marad
    for(int t=0; t<10; t++) {
        strcpy(buszlista[t].shortName," ");
        buszlista[t].stopHeadsign[0] = '\0';
        buszlista[t].stopHeadsignWithShortName[0] = '\0';
        buszlista[t].predictedArrivalTime[0] = '\0';
        buszlista[t].predictedArrivalTimeLong = 0;
        buszlista[t].predictedArrivalMinutesInt = 0;
        buszlista[t].predictedArrivalMinutesString[0] = '\0';
    }
}

bool readReponseContent() {

    // Compute optimal size of the JSON buffer according to what we need to parse.
    // See https://bblanchon.github.io/ArduinoJson/assistant/
    const size_t BUFFER_SIZE =2*JSON_ARRAY_SIZE(0) + JSON_ARRAY_SIZE(8) + JSON_ARRAY_SIZE(10) + JSON_OBJECT_SIZE(0) + 2*JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(4) + 4*JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(6) + 7*JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(8) + 11*JSON_OBJECT_SIZE(10) + 9*JSON_OBJECT_SIZE(12);

    // Allocate a temporary memory pool
    DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

    JsonObject& root = jsonBuffer.parseObject(client);

    if (!root.success()) {
        Serial.println("JSON parsing failed!");
        return false;
    }

// mikor éjszaka csináltam, rájöttem hogy a stoptimes nem mindig tartalmaz 10 buszt, így át kell kasztolni tömbbé, majd vizgálni a méretére
    JsonArray& nestedArray = root["data"]["entry"]["stopTimes"].asArray();

// mikor éjszaka csináltam, rájöttem hogy a stoptimes nem mindig tartalmaz 10 buszt, csak a fél órán belül indulókat, így vizsgálni kell a tömb méretére, ugyanis ha olyanra hivatkozunk, ami nincs, egyből Guru Error CPU halt következik be!
    Serial.print("A stopTimes mérete: ");
    Serial.println(nestedArray.size());

    ArraySize=nestedArray.size();

    if(ArraySize<3) {
        maxArraySize = ArraySize;
    }
    else {
        maxArraySize=3;
    }

    strncpy(currentTime, root["currentTime"],10);
    currentTimeLong = atol(currentTime);

    ConvertTime();

    for(int i=0; i<maxArraySize; i++) {
        JsonObject& aktualisbusz = root["data"]["entry"]["stopTimes"][i];

        if (aktualisbusz.containsKey("predictedArrivalTime"))
        {
            Serial.println("Predicted");

//mivel milliszekundumban vannak, amit csak long longban lehetne eltárolni de mi nem tudjuk, ezért levágjuk az első 10 számot, neveknél meg az első 25 karaktert hogy kiférjen a kijelzőre biztosan így másodperces felbontást kapunk, de sima long-ban el tudjuk így már tárolni az időket
            strncpy(buszlista[i].stopHeadsign, root["data"]["entry"]["stopTimes"][i]["stopHeadsign"],20
                   );
            strncpy(buszlista[i].predictedArrivalTime, root["data"]["entry"]["stopTimes"][i]["predictedArrivalTime"],10);
            buszlista[i].predictedArrivalTimeLong = atol(buszlista[i].predictedArrivalTime);
            buszlista[i].predictedArrivalMinutesInt = SecondsToMinutes(buszlista[i].predictedArrivalTimeLong-currentTimeLong);  //az aktuális idő kivonása, majd átváltás másodpercről percre
            ArrivalMinutesToString(buszlista[i].predictedArrivalMinutesInt,buszlista[i].predictedArrivalMinutesString);

            char tripId[32];
            char routeId[32];
            char shortName[16];
            strcpy(tripId,root["data"]["entry"]["stopTimes"][i]["tripId"]);
            strcpy(routeId,root["data"]["references"]["trips"][tripId]["routeId"]);
            strcpy(shortName,root["data"]["references"]["routes"][routeId]["shortName"]);
            strcpy(buszlista[i].shortName,shortName);
            strcpy(buszlista[i].stopHeadsignWithShortName,buszlista[i].shortName);
            strcat(buszlista[i].stopHeadsignWithShortName," - ");
            strcat(buszlista[i].stopHeadsignWithShortName,buszlista[i].stopHeadsign);
            Serial.print("tripId=");
            Serial.println(tripId);
            Serial.print("shortName=");
            Serial.println(shortName);
            Serial.print("routeId=");
            Serial.println(routeId);
            Serial.print("shortName=");
            Serial.println(shortName);

            //csak akkor alszik el a LED, ha sikeresen át tudtuk írni
            digitalWrite(25, LOW);
        }
        else if(aktualisbusz.containsKey("arrivalTime")) {
            // predticted time is NOT valid
            Serial.println("Arrival");
            strncpy(buszlista[i].stopHeadsign, root["data"]["entry"]["stopTimes"][i]["stopHeadsign"],20);
            strncpy(buszlista[i].predictedArrivalTime, root["data"]["entry"]["stopTimes"][i]["arrivalTime"],10);
            buszlista[i].predictedArrivalTimeLong = atol(buszlista[i].predictedArrivalTime); //átkasztolás stringről longba
            buszlista[i].predictedArrivalMinutesInt = SecondsToMinutes(buszlista[i].predictedArrivalTimeLong-currentTimeLong);  //az aktuális idő kivonása, majd átváltás másodpercről percre
            ArrivalMinutesToString(buszlista[i].predictedArrivalMinutesInt,buszlista[i].predictedArrivalMinutesString);

            char tripId[32];
            char routeId[32];
            char shortName[16];
            strcpy(tripId,root["data"]["entry"]["stopTimes"][i]["tripId"]);
            strcpy(routeId,root["data"]["references"]["trips"][tripId]["routeId"]);
            strcpy(shortName,root["data"]["references"]["routes"][routeId]["shortName"]);
            strcpy(buszlista[i].shortName,shortName);
            strcpy(buszlista[i].stopHeadsignWithShortName,buszlista[i].shortName);
            strcat(buszlista[i].stopHeadsignWithShortName," - ");
            strcat(buszlista[i].stopHeadsignWithShortName,buszlista[i].stopHeadsign);
            Serial.print("tripId=");
            Serial.println(tripId);
            Serial.print("shortName=");
            Serial.println(shortName);
            Serial.print("routeId=");
            Serial.println(routeId);
            Serial.print("shortName=");
            Serial.println(shortName);

            //csak akkor alszik el a LED, ha sikeresen át tudtuk írni
            digitalWrite(25, LOW);
        }
        else {
            Serial.println("noInfo");
        }
    }
    if(maxArraySize==0) {
        //azért mert ha nem található indulás a következő 60 percben akkor nem alszik el a LED szóval ki kell kapcsolni
        digitalWrite(25, LOW);
    }
    return true;
}


// Print the data extracted from the JSON
void printBusData() {
    for(uint16_t i=0; i<maxArraySize; i++) {
        Serial.print("shortName = ");
        Serial.println(buszlista[i].shortName);
        Serial.print("stopHeadsign = ");
        Serial.println(buszlista[i].stopHeadsign);
        Serial.print("predictedArrivalTime = ");
        Serial.println(buszlista[i].predictedArrivalTime);
        Serial.print("predictedArrivalTimeLong = ");
        Serial.println(buszlista[i].predictedArrivalTimeLong);
        Serial.print("predictedArrivalMinutesInt = ");
        Serial.println(buszlista[i].predictedArrivalMinutesInt);
    }
}

// Close the connection with the HTTP server
void disconnect() {
    Serial.println("Disconnect");
    client.stop();
}

// Pause for a 1 minute
void wait() {
    Serial.println("Wait 2 seconds");
    delay(2000);
}

void printWifiStatus() {
    // print the SSID of the network you're attached to:
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print your WiFi shield's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
}
