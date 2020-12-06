# E-FUTÁR / E-FUTAR ![BKK logo](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/bkk_icon.png)
**Embedded BKK FUTÁR application port for ESP32 SoC with SSD1306 OLED display**

With the help of this program, the microcontroller displays the next buses departing from a given bus stop and the remaining time until departure, as well as the current time on an OLED display.
The program can handle several stops, you can switch between them with the built-in button of the development card, the loading status is indicated by the built-in white SMD LED.

## Usage & Screenshots
**Boot screen**

![Boot screen](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/screenshot1.jpg)

**Bus list screen**

![Bus list screen](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/screenshot2.jpg)

The program starts immediately after the development board is powered on, connects to the configured WiFi network (see the configuration section), and then displays the next 3 buses starting from the default bus stop. According to the JSON command, the program only scans buses departing in the next 60 minutes. If it finds less than three buses, it displays two or one line, and if no match is found, displays an error message: "No departure found within 60 minutes."

You can switch between each bus stop with the built-in button on the developer card (marked in blue) or by manipulating the corresponding GPIO pin accordingly. When the button is pressed, the white LED integrated on the card (marked in black) flashes to indicate the loading status (because the modified JSON request has to wait for a response), if the stop changes successfully, the LED goes out and the OLED display shows the name of the other stop, and the departing buses just like in the previous cases.

![changeStops button](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/changeStops.jpg)

Three bus stops have been integrated into the example program, in order: 
 - Pesterzsébet, Baross utca
 - Pesterzsébet, Városközpont
 - János utca

**Before use, the SSID and pass for the WiFi as well as the list of stops  must be configured, see the contents of the "Configuration & Setup" part of this documentation!**


## How it works

The program basically follows a round robin architecture with interrupts, there was no need to use a more complex architecture. The stops are displayed on the basis of a simple state machine model, where the current state is always embodied by one stop.

When turned on, the program connects to the WiFi network and then periodically sends HTTP requests to the BKK server, which returns a standardized message in JSON format. This message contains all the information we need, so we can use additional commands to narrow the list,

> for example, if we are only interested in buses departing in the next half hour, we should use the &minutesAfter=60 command in the query.

More information about JSON syntax can be found here: https://www.json.org/ and a list of available commands can be found in the BKK FUTAR Apiary: https://bkkfutar.docs.apiary.io/
After the command is sent, the JSON library uses the returned long string (the HTTP response) to create an understandable, object-oriented, and processable JSON object system. (See the ArduinoJSON Library documentation for more information)
From the data set, the departing buses and the name of the stop are then displayed on the display with various string manipulator functions and graphical processing methods. As an extra, the program also displays the current time, as the BKK API also sends the UNIX epoch time, so just by adding a winter / summer time indicator variable, the exact time can be calculated.
As a result of an interrupt associated with the push of a button, the microcontroller lights up the indicator LED and changes the value of the stop variable. If the stop change is successful, the LED goes out.

## Flowchart

Simplified operation of the program:
![EFUTAR flowchart](https://raw.githubusercontent.com/bazsimarkus/E-FUTAR/master/docs/EFUTAR_flowchart.svg)

## The development board

The program was written and tested on a ESP32 SoC-based WIFI-LoRa-32 development board manufactured by Heltec Automation, but can be ported to any Arduino-compatible microcontroller, as only the standard SSD1306 and WiFi libraries were used during the development.

 ![Heltec development board](https://github.com/bazsimarkus/E-FUTAR/raw/master/docs/Heltec_WIFI-LoRa-32_DiagramPinoutFromTop.jpg)

The program can run natively on the board, no other components or cables are required.

## IDE & Development
The software was developed in the open source **Arduino IDE**.
The development environment can be downloaded here:
https://www.arduino.cc/en/Main/Software
A successful compilation also requires the installation of hardware library files, the process and component files of which can be found in the following repository (see the Installation Guide):
https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series

After the successful installation of the drivers, a board called WIFI_LoRa_32 will appear in the motherboard manager of the Arduino IDE, we need to select this and then compile and upload the code in the usual way.

## Configuration & Setup
**1) WiFi configuration**

Before uploading the code, set your own SSID and password in the ssid and pass global variables:

	char ssid[] = "SSID"; //  your network SSID (name)
	char pass[] = "Pass";    // your network password (use for WPA, or use as key for WEP)

**2) Bus stop configuration**

*Introduction: In the BKK FUTÁR system, each stop has a unique identifier, based on which almost all important data about a given stop or route can be queried in the BKK FUTÁR API, in JSON format. The exact methodology for this can be found in the following semi-official apiary documentation: https://bkkfutar.docs.apiary.io/
To find out the code of the stops, there is a map on the BKK FUTÁR website, where by clicking on a given stop and then selecting any bus line there, the code of the selected stop will appear in the website URL (e.g. Pesterzsébet, Baross utca: F04144)
The page is available here: http://futar.bkk.hu/*

Stops are changed by setting the global variable "currentStop" and then calling the setStop function, which will cause the microcontroller to send the query to the server with the code of the new stop in the next query cycle. Thus, after obtaining the code of the desired stop, we only need to change the value of the JSON request:

	if(currentStop==Baross) {
        stopName = "Baross utca";
        resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04144&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource
    }
Remember to enter the default stop value, which is the first value entered when creating the global variable!

	char* stopName = "Baross utca";
	char* resource = "/bkk-utvonaltervezo-api/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=BKK_F04144&onlyDepartures=onlyDepartures&limit=10&minutesBefore=0&minutesAfter=60";                    // http resource

**After completing these steps, the program can be uploaded to the board.**

## Libraries

*The following libraries were used during the development:*

Arduino WiFi Library - to connect

https://github.com/arduino/Arduino/tree/master/libraries/WiFi

Adafruit SSD1306 Library - for display control

https://github.com/adafruit/Adafruit_SSD1306

ArduinoJSON Library - to parse the http response

https://github.com/bblanchon/ArduinoJson

## Development possibilities

 - client application for WiFi confuguration
 - client application for bus stop configuration
 - alarm with a buzzer, linked to a specific bus service
 - online tracking of bus schedule statistics and delays in a web application

## Sources

György Balássy's blog posts helped me a lot in the project:

https://balassygyorgy.wordpress.com/2016/02/02/bkk-futar-microsoft-bandre-2-bkk-futar-api/

As well as the BKK FUTÁR Apiary:

https://bkkfutar.docs.apiary.io/
