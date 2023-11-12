# ESP8266 Weather Station
This is designed to run on a NodeMCU ESP8266 with a screen, connected to a DHT22 sensor.

It'll work out of the box on an Ideaspark T-ESP8266-096OLED, which has the right MCU and screen. For anything else you'll likely need to tweak it.

## Run it

Clone the repo, open it in the Arduino IDE

Rename credentials.example.h to credentials.h and edit it. You'll need to add your WiFi and MQTT credentials.

Edit the main .ino file and make sure the #defines near the top match your setup. Specifically the DHT22 pin and the Button pin. The button pin is D3 by default, which is the flash button.

You'll need an MQTT server. You can just install the HomeAssistant MQTT addon since thats where you'll eventually want the sensor data anyway. Extra config is required on the HA and but that's easily googlable.

Have a fantastic time knowing how warm you are!