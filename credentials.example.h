// WiFi Connection
//
// Enter the SSID & Password of the network you want to connect to.
// The hostname is what the device shows up as on the network
// e.g. "ESP8266 Bedroom".
//
// Be aware that this bakes the password into your mcu, so you'll
// want to flash it with garbage before disposing.
//
#define WIFI_SSID "MyRouter123"
#define WIFI_PASS "Hunter2"
#define WIFI_HOSTNAME "Attic Temperature"

// MQTT Connection
//
// Recommend just installing the MQTT Add-on into Home Assistant.
// In its configuration, add a user with a password and enter that here.
//
// The client name is what uniquely identifies this board to the MQTT
// server. If you have multiple devices, set each one differently.
#define MQTT_SERVER "homeassistant.local"
#define MQTT_PORT 1883
#define MQTT_USER "username_here"
#define MQTT_PASS "password_here"
#define MQTT_CLIENTNAME "esp8266-attic"

// MQTT Config
//
// These are the channels that readings will be posted to.
// Type what you like - if they don't exist they'll be created on the fly.
//
// You'll need to type the same thing into your homeassistant config yaml
// to tell it what to read from.
#define humidity_topic "sensor/esp8266-attic/humidity"
#define temperature_topic "sensor/esp8266-attic/temperature"