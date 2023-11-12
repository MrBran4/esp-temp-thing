#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <DHT22.h>
#include <PubSubClient.h>

// Credentials for MQTT and WiFi are in this file.
// Make a copy of credentials.example.h and fill it out.
#include "credentials.h"

// DHT22 Config
#define DHT22_PIN D1         // Set this to the DHT's data pin
#define DHT22_INTERVAL 2500  // Interval between readings in ms (dht22 is <0.5hz!)

// Button config
#define BUTTON_PIN D3  // The pin with the button on it (flash button is wired between D3 and gnd)

// Set up display
// This assumes an ideaspark nodemcu with a 128x64 oled.
U8G2_SSD1306_128X64_NONAME_F_SW_I2C
u8g2(U8G2_R0, /*clock=*/14, /*data=*/12, U8X8_PIN_NONE);

// Initialise the dht22, the wifi client, and the mqtt client.
DHT22 dht22(D1);
WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);


// DHT22's can only be pinged (pung?) once per 2 seconds, but our loop needs to
// run as fast as possible to keep the mqtt connection alive.
// This keeps track of the last time we took a reading.
long last_reading_timestamp = 0;

// We don't want to report every reading - only when it changes by a
// significant amount.
float last_published_temp = 0.0;      // Last temp we sent to the MQTT server
float last_published_humidity = 0.0;  // Last humidity we sent to the MQTT server
#define TEMP_THRESHOLD 0.25           // Threshold for submitting a new temp
#define HUMIDITY_THRESHOLD 0.5        // Threshold for submitting new humidity

// Smooth out readings by taking a rolling average of the last N readings.
#define AVG_WINDOW_SIZE 10  // Number of readings to averege

// The display is 128 pixels wide, so as a naiive graph we're just going to store
// the last 128 values and render them as dots :)
//
// For the first 127 readings we won't have a full history to average, so our
// averaging & graphic logic ignores NaN, which is a magic number in this case.
// We store the values newest first.
#define HISTORY_SIZE 128
float historic_temp[HISTORY_SIZE];
float historic_humidity[HISTORY_SIZE];

// We'll be looping through several screens when the button is pressed.
int current_screen = 0;
#define NUM_SCREENS 3
#define SCREEN_0_STATS 0
#define SCREEN_1_TEMP_GRAPH 1
#define SCREEN_2_HUMID_GRAPH 2

// Runs once on startup
// Configures the display and wifi. MQTT connection is done in the loop since it can disconnect.
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setup_display();
  connect_wifi();
  mqtt_client.setServer(MQTT_SERVER, MQTT_PORT);

  // Initialise the histories to NaN
  for (int idx = 0; idx < HISTORY_SIZE; idx++) {
    historic_temp[idx] = NAN;
    historic_humidity[idx] = NAN;
  }
}

// Initialise display stuff
void setup_display() {
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_tenthinnerguys_tf);
}

// If MQTT disconnects for some reason, call this to reconnect it.
// Blocks until reconnected.
void reconnect_mqtt() {
  // Alternate between failure messages so it's clear we're still working.
  bool alt_message = false;

  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    alt_message = !alt_message;

    // Write connecting message
    u8g2.clearBuffer();
    u8g2.drawStr(0, 30, "Wait for MQTT");
    if (alt_message) {
      u8g2.drawStr(0, 44, "Connecting...");  // Even attempts
    } else {
      u8g2.drawStr(0, 44, "Still Connecting...");  // Odd attempts
    }
    u8g2.sendBuffer();

    // Try making the connection. If it's successful, break out.
    if (mqtt_client.connect(MQTT_CLIENTNAME, MQTT_USER, MQTT_PASS)) {
      return;
    }

    // Wait 5 seconds before retrying
    delay(1500);
  }
}

// Connect to WiFi as defined in the credentials.h
// Only returns once WiFi is up.
void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Keep looping until WiFi connects
  while (WiFi.status() != WL_CONNECTED) {

    // Write connecting message to output...
    u8g2.clearBuffer();
    u8g2.drawStr(0, 30, "Wait for WiFi");

    // Determine proper error message if possible, or fall back to error.
    switch (WiFi.status()) {
      case WL_IDLE_STATUS:
        u8g2.drawStr(0, 44, "Waiting");
        break;
      case WL_CONNECT_FAILED:
        u8g2.drawStr(0, 44, "Failed");
        break;
      case WL_WRONG_PASSWORD:
        u8g2.drawStr(0, 44, "Wrong Password!");
        break;
      case WL_CONNECTION_LOST:
        u8g2.drawStr(0, 44, "Dropped");
        break;
      case WL_NO_SSID_AVAIL:
        u8g2.drawStr(0, 44, "SSID Not Found");
        break;
      case WL_DISCONNECTED:
        u8g2.drawStr(0, 44, "Searching...");
        break;
      case WL_SCAN_COMPLETED:
        u8g2.drawStr(0, 44, "Scanned");
        break;
      default:
        u8g2.drawStr(0, 44, "Error");
        break;
    }
    u8g2.sendBuffer();

    // Limit how often we check
    delay(250);
  }

  // When it works, print our info for debugging purposes.
  u8g2.clearBuffer();
  u8g2.drawStr(0, 30, "Connected");
  u8g2.drawStr(0, 44, WiFi.localIP().toString().c_str());
  u8g2.drawStr(0, 58, WiFi.getHostname());
  u8g2.sendBuffer();

  // Delay before returning so the above can be read.
  delay(1000);
}

// Only report values that differ by a sensible amount.
// Returns true if newValue differs from prevValue by more than threshold.
bool compare_with_threshold(float newValue, float prevValue, float threshold) {
  return !isnan(newValue) && (newValue < prevValue - threshold || newValue > prevValue + threshold);
}

bool is_valid_reading(float reading) {
  return reading > -100;
}

// Get the average of the last N readings from the array.
// Since the readings are stored newest first, this is simply an average of
// the first N non-NaN values.
float get_rolling_average(float readings[], int windowSize) {
  float total = 0.0;
  int count = 0;

  // Add non-NaN values up to the window size.
  for (int idx = 0; idx < windowSize; idx++) {
    if (is_valid_reading(readings[idx])) {
      total += readings[idx];
      count++;
    }
  }


  // If we didn't average any elements, don't divide by zero!
  if (count == 0) {
    return 0.0;
  }

  return total / count;
}

// Add a new reading to the start of the array.
void prepend_reading(float readings[], float newReading) {
  // Awkward optimisation: Use memmove to shift len-1 elements to the right by one.
  // Must use memmove because memcpy has undefined behaviour if regions overlap.
  memmove(
    readings + 1,                       // Dest (index 1 of array)
    readings,                           // Src (index 0 of array)
    (HISTORY_SIZE - 1) * sizeof(float)  // Length (all but the last element, so that when copied it fits.)
  );

  // Insert the new value at position 0.
  readings[0] = newReading;
}

// Checks if the button is pressed. If it is, cycles to the next screen.
void check_buttons() {
  int pinValue = digitalRead(BUTTON_PIN);

  // if it's not pressed, don't do anything.
  if (pinValue) {
    return;
  }

  // Poor man's debounce :)
  delay(20);

  // if it's not pressed, it was spurious so don't do anything.
  if (pinValue) {
    return;
  }

  // If we get here, we're convinced the button was pressed.
  current_screen = (current_screen + 1) % NUM_SCREENS;

  // Blank the screen so the user knows to unpress.
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso16_tf);
  switch (current_screen) {
    case SCREEN_0_STATS: u8g2.drawStr(0, 43, "Overview"); break;
    case SCREEN_1_TEMP_GRAPH:
      u8g2.drawStr(0, 34, "Temperature");
      u8g2.drawStr(0, 53, "History");
      break;
    case SCREEN_2_HUMID_GRAPH:
      u8g2.drawStr(0, 34, "Humidity");
      u8g2.drawStr(0, 53, "History");
      break;
  }
  u8g2.drawTriangle(111, 25, 111, 47, 127, 36);
  u8g2.sendBuffer();
  delay(500);
}

// Draws signal bars at the top right of the display and a text header of your choice.
// Does not clear the buffer or flush it afterwards, simply adds them to the current render.
void ui_draw_header(const char* header_text) {
  // Draw the text
  u8g2.setFont(u8g2_font_helvB08_tn);
  u8g2.drawStr(0, 8, header_text);

  int rssi = WiFi.RSSI();
  if (rssi >= -55) {
    u8g2.drawBox(107, 6, 4, 2);
    u8g2.drawBox(112, 4, 4, 4);
    u8g2.drawBox(117, 2, 4, 6);
    u8g2.drawBox(122, 0, 4, 8);
  } else if (rssi< -55 & rssi > - 65) {
    u8g2.drawBox(107, 6, 4, 2);
    u8g2.drawBox(112, 4, 4, 4);
    u8g2.drawBox(117, 2, 4, 6);
    u8g2.drawFrame(122, 0, 4, 8);
  } else if (rssi< -65 & rssi > - 75) {
    u8g2.drawBox(107, 6, 4, 2);
    u8g2.drawBox(112, 4, 4, 4);
    u8g2.drawFrame(117, 2, 2, 6);
    u8g2.drawFrame(122, 0, 4, 8);
  } else if (rssi< -75 & rssi > - 85) {
    u8g2.drawBox(107, 6, 4, 2);
    u8g2.drawFrame(112, 4, 4, 4);
    u8g2.drawFrame(117, 2, 4, 6);
    u8g2.drawFrame(122, 0, 4, 8);
  } else {
    u8g2.drawFrame(107, 6, 4, 2);
    u8g2.drawFrame(112, 4, 4, 4);
    u8g2.drawFrame(117, 2, 4, 6);
    u8g2.drawFrame(122, 0, 4, 8);
  }
}


void loop() {
  // If MQTT has disconnected for some reason, pause until we reconnect.
  if (!mqtt_client.connected()) {
    reconnect_mqtt();
  }
  mqtt_client.loop();

  // Check if the button is pressed and toggle the screen if so.
  check_buttons();

  // If it has note been more than the DHT22's interval, don't try taking a reading.
  long now = millis();
  if (now - last_reading_timestamp < DHT22_INTERVAL) {
    delay(50);
    return;
  }
  last_reading_timestamp = now;


  // Grab new readings and shift them into the start of the readings arrays.
  prepend_reading(historic_temp, dht22.getTemperature());
  prepend_reading(historic_humidity, dht22.getHumidity());

  // Work out rolling average
  float current_temp = get_rolling_average(historic_temp, AVG_WINDOW_SIZE);
  float current_humidity = get_rolling_average(historic_humidity, AVG_WINDOW_SIZE);

  // If the temp changed by more than the threshold, report it
  if (compare_with_threshold(current_temp, last_published_temp, TEMP_THRESHOLD)) {
    last_published_temp = current_temp;
    mqtt_client.publish(temperature_topic, String(current_temp).c_str(), true);
  }

  // If the humidity changed by more than the threshold, report the change.
  if (compare_with_threshold(current_humidity, last_published_humidity, HUMIDITY_THRESHOLD)) {
    last_published_humidity = current_humidity;
    mqtt_client.publish(humidity_topic, String(current_humidity).c_str(), true);
  }

  // Render whichever screen we're meant to be on
  switch (current_screen) {
    case SCREEN_0_STATS:
      ui_draw_stats(current_temp, current_humidity);
      break;
    case SCREEN_1_TEMP_GRAPH:
      ui_draw_temp_graph(current_temp);
      break;
    case SCREEN_2_HUMID_GRAPH:
      ui_draw_humidity_graph(current_humidity);
      break;
    default:
      break;
  }
}

// ---------------------------------------------
// Screens!

// Draw a graph of whatever readings are passed in.
void ui_draw_graph(float readings[HISTORY_SIZE]) {
  // Work out the min and max elements (start with obscene values that will be overwritten.)
  float min = 127;
  float max = -127;

  for (int idx = 0; idx < HISTORY_SIZE; idx++) {
    if (!is_valid_reading(readings[idx])) { continue; }
    if (readings[idx] < min) { min = readings[idx]; }
    if (readings[idx] > max) { max = readings[idx]; }
  }

  // If the min or max are still their defautls, we can't draw the graph and will just return.
  // (it means we have no readings somehow)
  if (min > 125 || max < -125) {
    return;
  }

  // Add some padding to the min and max values (to guarantee there's a range between them, so we don't divide by zero)
  min = floorf(min - 1);
  max = ceilf(max + 1);

  // Draw the min and max on the display
  char minStr[6];
  char maxStr[6];
  sprintf(minStr, "%.0f", min);
  sprintf(maxStr, "%.0f", max);

  u8g2.setFont(u8g2_font_tenthinnerguys_tf);
  u8g2.drawStr(0, 25, maxStr);
  u8g2.drawStr(0, 64, minStr);

  // Draw the line
  // We start at index 1 (the second element) and draw a line from it back to the previous element.
  // We only draw lines if both elements are valid.
  for (int idx = 1; idx < HISTORY_SIZE; idx++) {
    // Skip if both aren't valid
    if (!is_valid_reading(readings[idx]) || !is_valid_reading(readings[idx - 1])) { continue; }

    // The graph is 48px high, so convert to that range.
    // 64 - (some number between 0 and 1) * 48
    int start_y = 64 - floor(to_range(min, max, readings[idx], 48));
    int end_y = 64 - floor(to_range(min, max, readings[idx-1], 48));

    // Now draw a line between those two points.
    // Element idx-1 is the newer element.
    u8g2.drawLine(
     (HISTORY_SIZE - idx) - 1,
     start_y,
     HISTORY_SIZE - idx,
     end_y);
  }
}

// Take the given val and range, return the percentage of the way through the range it is.
// for example (2, 4, 3, 10) = 5 because 3 is halfway between 2 and 4. and the range is 0..10.
double to_range(float rangeMin, float rangeMax, float val, float scaleMax) {
  // Clamp the output - return 0 or 1 if outside of range.
  if (val < rangeMin) { return 0; }
  if (val > rangeMax) { return 1; }

  // Return val as a fraction of the range.
  return ((val - rangeMin) / (rangeMax - rangeMin)) * scaleMax;
}

// Draw the stats screen
// This is the screen with IP address at the top, big temperature in the middle,
// and little humidity at the bottom.
void ui_draw_stats(
  float current_temp,     // Pre-averaged temp
  float current_humidity  // Pre-averaged humidity
) {
  // Convert readings to strings
  char tempstr[6];
  char humstr[6];
  sprintf(tempstr, "%.1f", current_temp);
  sprintf(humstr, "%.1f%%", current_humidity);

  u8g2.clearBuffer();

  // Draw the header with the IP address
  ui_draw_header(WiFi.localIP().toString().c_str());

  // Draw temp
  u8g2.setFont(u8g2_font_logisoso26_tf);
  u8g2.drawStr(0, 43, tempstr);

  // Draw humidity
  u8g2.setFont(u8g2_font_chargen_92_mf);
  u8g2.drawStr(0, 64, humstr);

  // Draw labels
  u8g2.setFont(u8g2_font_tenthinnerguys_tf);
  u8g2.drawStr(97, 43, "Temp");
  u8g2.drawStr(108, 64, "Rel");

  u8g2.sendBuffer();
}


void ui_draw_temp_graph(
  float current_temp  // Historic temps
) {
  u8g2.clearBuffer();

  // Convert reading to string
  char tempstr[16];
  sprintf(tempstr, "Temp %.1f", current_temp);

  // Draw the header
  ui_draw_header(tempstr);

  // Draw the graph
  ui_draw_graph(historic_temp);

  u8g2.sendBuffer();
}

void ui_draw_humidity_graph(
  float current_humidity  // Historic temps
) {
  u8g2.clearBuffer();

  // Convert reading to string
  char humidstr[16];
  sprintf(humidstr, "Rel %.1f%%", current_humidity);

  // Draw the header
  ui_draw_header(humidstr);

  // Draw the graph
  ui_draw_graph(historic_humidity);

  u8g2.sendBuffer();
}
