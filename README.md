# ESP8266 Weather Station
This is designed to run on a NodeMCU ESP8266 with a screen, connected to a DHT22 sensor.

It'll work out of the box on an Ideaspark T-ESP8266-096OLED, which has the right MCU and screen. For anything else you'll likely need to tweak it.

## Run it

Clone the repo, open it in the Arduino IDE

Rename credentials.example.h to credentials.h and edit it. You'll need to add your WiFi and MQTT credentials.

Edit the main .ino file and make sure the #defines near the top match your setup. Specifically the DHT22 pin and the Button pin. The button pin is D3 by default, which is the flash button.

You'll need an MQTT server. You can just install the HomeAssistant MQTT addon since thats where you'll eventually want the sensor data anyway. Extra config is required on the HA and but that's easily googlable.

## What does it do?

At boot it connects to your WiFi and then registers with MQTT.

Then, it continuously takes measurements from the DHT22.

It averages out the previous 10 readings feom the sensor and, if they differ by some defined threshold (0.25 by default), they are reported to MQTT.

Pressing the flash button (or whatever other button, if you remap it) switches the display between showing an overview, a graph of recent temp measurements, and a graph of recent humidity measurements.

To get the most out of it, configure Home Assistant to _read from_ the MQTT topics this publishes to. That way, you can store the history and set up home automations based on the measurements.

Have a fantastic time knowing how warm you are!