//#define CONFIG_ASYNC_TCP_RUNNING_CORE -1
// #define CONFIG_ASYNC_TCP_USE_WDT 0
#include "esp32-common.h"

#include "math.h"
#ifdef __AVR__
#include <avr/power.h>
#endif

#include <vector>
#include <queue>
#include <array>

//#include "OLEDDisplay.h"
#include "SSD1306Wire.h"

#include "time.h"

#include <chrono>

// circular buffer, from https://github.com/martinmoene/ring-span-lite
#include "ring_span.hpp"

#define ARDIUNOJSON_TAB "  "
#include <ArduinoJson.h>
#include "PubSubClient.h"

#include "light_utils.h"
#include "Pushbutton.h"

#include "ota_update.h" // for OTA update

const char * pref_do_ota_on_boot = "ota_firmware";
const char * pref_do_ota_spiffs_on_boot = "ota_spiffs";

bool enable_pixel_update = false;


template< typename T, size_t N >
inline size_t dim( T (&arr)[N] ) { return N; }

union Color {
    Color() {
        num = 0;
    }
    Color(int32_t c) {
        num = c;
    }
    Color(uint8_t r, uint8_t g, uint8_t b) {
        this->r = r;
        this->b = b;
        this->g = g;
    }
    operator uint32_t() const  {
        return num;
    }
    struct __attribute__ ((packed)){
        uint8_t b; 
        uint8_t g;
        uint8_t r; 
        uint8_t w;
    };
    uint32_t num;
};





// globals
const int pin_strand_1 = 5 ;
const int pin_white_led = 25;
const int max_led_count = 1000;
int led_count = 10;
int max_current = 500;
String device_name="nerdlights";
bool lights_on = true; // true if lights are currently turned on
bool is_tree = false;

const int pin_command_button = 0; // built in command button
Pushbutton command_button(pin_command_button, 0);

std::array<Color, max_led_count> leds;



float speed = 1.0;
float cycles = 1.0;
#include <FastLED.h>

WiFiClient wifi_client;
PubSubClient mqtt(wifi_client);
StaticJsonDocument<3000> shared_json_input_doc;
StaticJsonDocument<3000> shared_json_output_doc;
char mqtt_client_id[20];

// Observable pattern state tracking
String last_actual_power_id = "";
String last_actual_program_id = "";
bool initial_sync_complete = false;
bool pending_power_sync = true;

// Scheduled restart state
static bool restart_pending = false;
static unsigned long restart_scheduled_ms = 0;
bool pending_program_sync = true;
unsigned long sync_start_time = 0;
const unsigned long retained_message_timeout = 5000; // 5 seconds

// Forward declarations
void set_program(JsonDocument & doc);
void cmd_pattern_list(CommandEnvironment &env);
void cmd_pattern_info(CommandEnvironment &env);
void cmd_pattern_discover(CommandEnvironment &env);
void cmd_pattern_set(CommandEnvironment &env);
void init_pattern_system();
void publish_pattern_definitions();

//#define use_fastled
#if defined(use_fastled)
#include <FastLED.h>
CRGB fast_leds[1000];
#else
#include "esp32_digital_led_lib.h"
strand_t STRANDS[] = {{.rmtChannel = 2,
                       .gpioNum = pin_strand_1,
                       .ledType = LED_WS2812B_V3,
                       .brightLimit = 24,
                       .numPixels = max_led_count}};
const int STRANDCNT = sizeof(STRANDS) / sizeof(STRANDS[0]);

strand_t *strands[8];
#endif


////////////////////////////////////////////
// helpers
////////////////////////////////////////////
uint32_t clock_millis() {
  //return millis();
    auto start = std::chrono::system_clock::now();
    auto since_epoch = start.time_since_epoch();
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
    return ms & 0xffffffff;// millis();
}

// returns a random double [0,1)
double frand() {
  return double(rand()) / (double(RAND_MAX) + 1.0);
}

template<class T> T clamp(T v, T min, T max) {
  if (v<min) return min;
  if (v>max) return max;
  return v;

}

Color mix_colors(Color c1, Color c2, float part2) {
  part2 = clamp<float>(part2, 0., 1.);
  float part1 = 1-part2;
  return Color(c1.r*part1+c2.r*part2+0.5, c1.g*part1+c2.g*part2+0.5, c1.b*part1+c2.b*part2+0.5);
}




// current lighting pattern selected for display
enum LightMode {
  mode_explosion,
  mode_pattern1,
  mode_gradient,
  mode_rainbow,
  mode_strobe,
  mode_twinkle,
  mode_stripes,
  mode_normal,
  mode_flicker,
  mode_meteor,
  mode_confetti,
  mode_breathe,
  mode_solid,
  mode_last = mode_solid
};

LightMode string_to_light_mode(const char * s) {
  if(strcmp(s, "explosion") == 0) {
    return mode_explosion;
  } else if(strcmp(s, "pattern1") == 0) {
    return mode_pattern1;
  } else if(strcmp(s, "gradient") == 0) {
    return mode_gradient;
  } else if(strcmp(s, "rainbow") == 0) {
    return mode_rainbow;
  } else if(strcmp(s, "strobe") == 0) {
    return mode_strobe;
  } else if(strcmp(s, "twinkle") == 0) {
    return mode_twinkle;
  } else if(strcmp(s, "stripes") == 0) {
    return mode_stripes;
  } else if(strcmp(s, "normal") == 0) {
    return mode_normal;
  } else if(strcmp(s, "solid") == 0) {
    return mode_solid;
  } else if(strcmp(s, "flicker") == 0) {
    return mode_flicker;
  } else if(strcmp(s, "meteor") == 0) {
    return mode_meteor;
  } else if(strcmp(s, "confetti") == 0) {
    return mode_confetti;
  } else if(strcmp(s, "breathe") == 0) {
    return mode_breathe;
  }
  return mode_rainbow;
}

LightMode light_mode = mode_rainbow;
uint8_t saturation = 200;
uint8_t brightness = 50;

// breathe pattern variables
uint32_t breathe_cycle_start_ms = 0;
uint32_t breathe_duration_ms = 6000;

// solid pattern variables
uint16_t pattern_spacing = 1;

std::vector<Color> unscaled_colors = {Color(15, 15, 15)};
std::vector<Color> current_colors = {Color(15, 15, 15)};

////////////////////////////////
// define commands
////////////////////////////////


const char * light_mode_name(LightMode mode) {
  switch (mode) {
    case mode_explosion:
      return "explosion";
    
    case mode_pattern1:
      return "pattern1";

    case mode_gradient:
      return "gradient";

    case mode_meteor:
      return "meteor";

    case mode_rainbow:
      return "rainbow";

    case mode_strobe:
      return "strobe";

    case mode_twinkle:
      return "twinkle";

    case mode_stripes:
      return "stripes";

    case mode_normal:
      return "normal";

    case mode_flicker:
      return "flicker";

    case mode_confetti:
      return "confetti";

    case mode_breathe:
      return "breathe";

    case mode_solid:
      return "solid";
  }
  return "mode_not_found";
}


void update_pixels() {
  #if defined(use_fastled)
    for(int i=0;i<led_count;++i) {
        fast_leds[i] = CRGB(leds[i].r, leds[i].g, leds[i].b);
    }
    FastLED.show();
  #else
    for(int i=0;i<led_count;++i) {
        auto &p = strands[0]->pixels[i];
        p.num = leds[i];
        std::swap(p.g, p.b);
        std::swap(p.r, p.b);
    }
    digitalLeds_drawPixels(strands, STRANDCNT);
  #endif

  delay(20);
}

void off() { 
  for(int i=0; i < led_count; ++i) {
    leds[i]=Color(0,0,0);
  }
}


void blacken_pixels() {
  lights_on = false;
  off();
  update_pixels();
}

static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

void mdns_print_results(mdns_result_t * results){
    mdns_result_t * r = results;
    mdns_ip_addr_t * a = NULL;
    int i = 1, t;
    while(r){
        Serial.printf("%d: Interface: %s, Type: %s\n", i++, if_str[r->tcpip_if], ip_protocol_str[r->ip_protocol]);
        if(r->instance_name){
            Serial.printf("  PTR : %s\n", r->instance_name);
        }
        if(r->hostname){
            Serial.printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        }

        if(r->txt_count){
            Serial.printf("  TXT : [%u] ", r->txt_count);
            for(t=0; t<r->txt_count; t++){
                Serial.printf("%s=%s; ", r->txt[t].key, r->txt[t].value);
            }
            Serial.printf("\n");
        }
        a = r->addr;
        while(a){
            if(a->addr.type == IPADDR_TYPE_V6){
                Serial.printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
            } else {
                Serial.printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
            }
            a = a->next;
        }
        r = r->next;
    }

}

void find_mdns_service(const char * service_name, const char * proto)
{
    Serial.printf("Query PTR: %s.%s.local\n", service_name, proto);

    mdns_result_t * results = NULL;
    esp_err_t err = mdns_query_ptr(service_name, proto, 1000, 20,  &results);
    if(err){
        Serial.println("Query Failed");
        return;
    }
    if(!results){
        Serial.println("No results found!");
        return;
    }

    mdns_print_results(results);
    mdns_query_results_free(results);
}


void cmd_get_nearby_devices(CommandEnvironment & env) {
  const int doc_capacity = 5000;
  auto & doc = shared_json_output_doc;
  mdns_result_t * results = nullptr;

  esp_err_t err = mdns_query_ptr("_nerd_lights", "_tcp", 3000, 20,  &results);
  if(err) {
     env.cerr.println("failed querying mdns");
     doc["success"]=0;
  } else {
    doc["success"]=1;
    mdns_result_t * r = results;
    mdns_ip_addr_t * a = NULL;
    JsonArray device_array = doc.createNestedArray("devices");
    while(r){
      auto device_json = device_array.createNestedObject();
      device_json["interface"]=if_str[r->tcpip_if];
      device_json["protocol"]=ip_protocol_str[r->ip_protocol];

      if(r->instance_name){
        device_json["instance_name"] = r->instance_name;
      }
      if(r->hostname){
        device_json["hostname"] = r->hostname;
        device_json["port"] = r->port;
      }

      if(r->txt_count){
          for(int t=0; t<r->txt_count; t++){
            device_json[r->txt[t].key]=r->txt[t].value;
          }
      }
      a = r->addr;
      if(a){
          char address_string[20];
          if(a->addr.type == IPADDR_TYPE_V6){
            snprintf(address_string, 19, IPV6STR,IPV62STR(a->addr.u_addr.ip6));
            device_json["ipv6"]= address_string;
          } else {
            snprintf(address_string, 19, IPSTR,IP2STR(&(a->addr.u_addr.ip4)));
            device_json["ip_address"] =  address_string;
          }
          // a = a->next; // could be multiple, but only want one
      }
      r = r->next;
    }
    mdns_query_results_free(results);
  }
  serializeJson(doc, env.cout);
}

void schedule_restart(unsigned long delay_ms = 100) {
  restart_pending = true;
  restart_scheduled_ms = millis() + delay_ms;
  Serial.printf("Restart scheduled in %lu ms\n", delay_ms);
}

void cmd_restart (CommandEnvironment & env) {
  env.cout.printf("Restarting");
  blacken_pixels();

  display.clear();
  display.drawString(0,0,"Restarting");
  display.display();

  schedule_restart(500);
}

void cmd_do_ota_upgrade(CommandEnvironment & env) {
  env.cout.printf("Starting firmware upgrade");
  
  preferences.begin("main");
  preferences.putBool(pref_do_ota_on_boot,true);
  preferences.end();

  blacken_pixels();

  display.clear();
  display.drawString(0,0,"Restarting to");
  display.drawString(0,10,"firmware upgrade");
  display.display();

  schedule_restart(500);
}

void cmd_do_spiffs_upgrade(CommandEnvironment & env) {
  env.cout.printf("Starting SPIFFS upgrade");
  
  preferences.begin("main");
  preferences.putBool(pref_do_ota_spiffs_on_boot,true);
  preferences.end();

  blacken_pixels();

  schedule_restart(500);
}

void get_program_json(ArduinoJson::JsonObject & program)
{
  program["light_mode"] = light_mode_name(light_mode);
  program["brightness"] = brightness;
  program["saturation"] = saturation;
  program["cycles"] = cycles;
  program["speed"] = speed;

  JsonArray colors_array = program.createNestedArray("colors");
  for (int i = 0; i < unscaled_colors.size(); ++i)
  {
    Color c = unscaled_colors[i];
    auto color_json = colors_array.createNestedObject();
    color_json["r"] = c.r;
    color_json["g"] = c.g;
    color_json["b"] = c.b;
  }
}


void cmd_get_program(CommandEnvironment & env) {
  Serial.println("cmd_get_program");
  Stream & o = env.cout;
  auto & doc = shared_json_output_doc;
  doc.clear();
  ArduinoJson::JsonObject program = doc.to<ArduinoJson::JsonObject>();
  get_program_json(program);

  //serializeJsonPretty(doc, o);
  serializeJson(doc, o);
}


void cmd_status(CommandEnvironment &env)
{
  Stream & o = env.cout;

  o.println("{");

  o.print("\"device_name\":\"");
  o.print(device_name);
  o.println("\",");

  o.print("\"firmware_version\":\"" __DATE__ " " __TIME__ );
  o.println("\",");


  o.print("\"light_mode\":\"");
  o.print(light_mode_name(light_mode));
  o.println("\",");

  o.print("\"brightness\":");
  o.print(brightness);
  o.println(",");

  o.print("\"cycles\":");
  o.print(cycles);
  o.println(",");

  o.print("\"saturation\":");
  o.print(saturation);
  o.println(",");

  o.print("\"speed\":");
  o.print(speed,4);
  o.println(",");

  o.print("\"lights_on\":");
  o.print(lights_on ? "true" : "false");
  o.println(",");

  o.print("\"is_tree\":");
  o.print(is_tree ? "true" : "false");
  o.println(",");

  o.print("\"ssid\":\"");
  o.print(wifi_task.ssid);
  o.println("\",");

  o.print("\"ip_address\":\"");
  o.print(WiFi.localIP().toString());
  o.println("\",");

  o.print("\"led_count\":");
  o.print(led_count);
  o.println(",");

  o.print("\"max_current\":");
  o.print(max_current);
  o.println(",");

  o.print("\"bytes_free\":");
  o.print(ESP.getFreeHeap());
  o.println(",");

  o.print("\"colors\":[");
  for(uint32_t i = 0; i < unscaled_colors.size(); ++i) {
    if(i > 0) {
      o.print(", ");
    }
    Color p = unscaled_colors[i];
    o.print("{");
    o.print("\"r\":");
    o.print(p.r);
    o.print(",");


    o.print("\"g\":");
    o.print(p.g);

    o.print(",\"b\":");
    o.print(p.b);
    o.print("}");

  }
  o.println("]");

  o.println("}");
}

void cmd_name(CommandEnvironment &env) {
  if (env.args.getParamCount() != 1) {
    env.cout.printf("Failed - requires a single parameter for name");
    return;
  }
  String name = env.args.getCmdParam(1);

  if (name.length() > 32) {
    env.cout.printf(
        "Failed - device name must be 32 or fewer characters");
    return;
  }
  preferences.begin("main");
  preferences.putString("bt_name", name);
  preferences.end();
  
  env.cout.printf("name = %s", name.c_str());
  schedule_restart();
}

inline uint8_t mul_div(uint8_t number, uint32_t numerator, uint32_t denominator) {
    int32_t ret = number;
    ret *= numerator;
    ret /= denominator;
    return (uint8_t) ret;
}

void calculate_scaled_colors() {
  uint8_t max_c = 0;
  // calculate max of all components
  Color p;
  for(auto & c : unscaled_colors){

    max_c = max(max_c, c.r);
    max_c = max(max_c, c.g);
    max_c = max(max_c, c.b);
  }

  current_colors.clear();
  for(auto & c : unscaled_colors){
    if(max_c > 0) {
      p.r = mul_div(c.r, brightness, max_c);
      p.g = mul_div(c.g, brightness, max_c);
      p.b = mul_div(c.b, brightness, max_c);
    }
    current_colors.push_back(p);
  }
}

void publish_json(const char * topic, JsonDocument & doc) {
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic, payload.c_str(), true); // fails silently if not connected, not a problem
}

// sends current program to controllers/{device_id}/status/program
void publish_program() {
  auto & doc = shared_json_output_doc;
  doc.clear();
  auto program = doc.createNestedObject("program");
  get_program_json(program);

  // send it
  char topic[100];
  snprintf(topic, 99, "controllers/%s/status/program", mqtt_client_id);
  publish_json(topic, doc);
} 

// sends device statistics to controllers/{device_id}/status/statistics
void publish_statistics() {
  auto & doc = shared_json_output_doc;
  doc.clear();
  auto statistics = doc.createNestedObject("statistics");

  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  statistics["timestamp"] = timestamp;

  statistics["bytes_free"] = ESP.getFreeHeap();
  statistics["ip_address"] = WiFi.localIP().toString();
  statistics["firmware_version"] = __DATE__ " " __TIME__;
  statistics["wifi_rssi"] = WiFi.RSSI();
  statistics["uptime_seconds"] = millis()/1000;

  // send it
  char topic[100];
  snprintf(topic, 99, "controllers/%s/status/statistics", mqtt_client_id);
  publish_json(topic, doc);
}

// sends device settings to controllers/{device_id}/status/settings
void publish_settings() {

  auto & doc = shared_json_output_doc;
  doc.clear();

  auto settings = doc.createNestedObject("settings");
  settings["device_name"] = device_name;
  settings["is_tree"] = is_tree;
  settings["led_count"] = led_count;
  settings["max_current"] = max_current;
  settings["ssid"] = wifi_task.ssid;


  // send it
  char topic[100];
  snprintf(topic, 99, "controllers/%s/status/settings", mqtt_client_id);
  publish_json(topic, doc);
}

// Observable Pattern: Publish setpoint power (for controller-initiated changes)
void publish_setpoint_power(String message_id) {
  auto & doc = shared_json_output_doc;
  doc.clear();
  doc["id"] = message_id;
  doc["timestamp"] = millis();
  doc["lights_on"] = lights_on;
  
  char topic[100];
  snprintf(topic, 99, "controllers/%s/setpoints/power", mqtt_client_id);
  
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic, payload.c_str(), true); // retained=true
}

// Observable Pattern: Publish actual power state
void publish_actual_power(String message_id, String status, String error = "") {
  auto & doc = shared_json_output_doc;
  doc.clear();
  doc["id"] = message_id;
  doc["status"] = status;
  doc["timestamp"] = millis();
  doc["lights_on"] = lights_on;
  
  if (error.length() > 0) {
    doc["error"] = error;
  }
  
  char topic[100];
  snprintf(topic, 99, "controllers/%s/actuals/power", mqtt_client_id);
  
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic, payload.c_str(), true); // retained=true
}

// Observable Pattern: Publish actual program state
void publish_actual_program(String message_id, String status, String error = "") {
  auto & doc = shared_json_output_doc;
  doc.clear();
  doc["id"] = message_id;
  doc["status"] = status;
  doc["timestamp"] = millis();
  doc["lights_on"] = lights_on;
  
  // Include full program state
  auto program = doc.createNestedObject("program");
  get_program_json(program);
  
  if (error.length() > 0) {
    doc["error"] = error;
  }
  
  char topic[100];
  snprintf(topic, 99, "controllers/%s/actuals/program", mqtt_client_id);
  
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic, payload.c_str(), true); // retained=true
}

// Observable Pattern: Check if initial sync is complete
void check_initial_sync_complete() {
  if (!initial_sync_complete && !pending_power_sync && !pending_program_sync) {
    initial_sync_complete = true;
    Serial.println("Initial MQTT sync complete");
    
    // Publish current actual state if we have message IDs
    if (last_actual_power_id.length() > 0) {
      publish_actual_power(last_actual_power_id, "applied");
    }
    if (last_actual_program_id.length() > 0) {
      publish_actual_program(last_actual_program_id, "applied");
    }
  }
}

// Observable Pattern: Handle power setpoint messages
void handle_power_setpoint(byte* message, unsigned int length) {
  pending_power_sync = false;
  
  // Parse JSON message
  auto & doc = shared_json_input_doc;
  doc.clear();
  DeserializationError error = deserializeJson(doc, message, length);
  if (error) {
    Serial.print("handle_power_setpoint deserializeJson() failed: ");
    Serial.println(error.c_str());
    check_initial_sync_complete();
    return;
  }
  
  String message_id = doc["id"];
  
  if (message_id != last_actual_power_id) {
    bool requested_state = doc["lights_on"];
    
    // Apply the power setting
    lights_on = requested_state;
    Serial.printf("Applied power setpoint: lights_on=%s, id=%s\n", 
                  lights_on ? "true" : "false", message_id.c_str());
    
    // Report what we applied
    last_actual_power_id = message_id;
    publish_actual_power(message_id, "applied");
  }
  
  check_initial_sync_complete();
}

// Observable Pattern: Handle program setpoint messages
void handle_program_setpoint(byte* message, unsigned int length) {
  pending_program_sync = false;
  
  // Parse JSON message
  auto & doc = shared_json_input_doc;
  doc.clear();
  DeserializationError error = deserializeJson(doc, message, length);
  if (error) {
    Serial.print("handle_program_setpoint deserializeJson() failed: ");
    Serial.println(error.c_str());
    check_initial_sync_complete();
    return;
  }
  
  String message_id = doc["id"];
  
  if (message_id != last_actual_program_id) {
    // Extract program data and apply using existing set_program function
    if (doc.containsKey("program")) {
      Serial.printf("Applying program setpoint, id=%s\n", message_id.c_str());
      
      // Create a temporary document with just the program data for set_program
      StaticJsonDocument<2000> program_doc;
      program_doc.set(doc["program"]);
      set_program(program_doc);
      
      last_actual_program_id = message_id;
      publish_actual_program(message_id, "applied");
    } else {
      Serial.println("Program setpoint missing 'program' field");
      last_actual_program_id = message_id;
      publish_actual_program(message_id, "error", "missing_program_field");
    }
  }
  
  check_initial_sync_complete();
}

// Command Console: Handle console command messages
void handle_console_command(byte* message, unsigned int length) {
  // Convert message to string
  char command_string[length + 1];
  memcpy(command_string, message, length);
  command_string[length] = '\0';
  
  Serial.printf("Received console command: %s\n", command_string);
  
  // Parse command using existing infrastructure
  CmdParser parser;
  parser.setOptIgnoreQuote();
  parser.parseCmd(command_string);
  
  // Find command
  Command *command = get_command_by_name(parser.getCommand());
  
  String output_string;
  output_string.reserve(2000);
  StringStream output_stream(output_string);
  
  if (command == nullptr) {
    output_stream.printf("ERROR: Command not found - %s\n", parser.getCommand());
  } else {
    // Execute command
    CommandEnvironment env(parser, output_stream, output_stream);
    command->execute(env);
  }
  
  // Publish result
  char resultTopic[100];
  snprintf(resultTopic, 99, "controllers/%s/commands/result", mqtt_client_id);
  mqtt.publish(resultTopic, output_string.c_str());
  
  Serial.printf("Command result published (%d bytes)\n", output_string.length());
}

void turn_on() {
  if (lights_on)
  {
    return;
  }
  
  lights_on = true;
  // NOTE: Power state now handled through observable pattern (actuals/setpoints)
}

void turn_off() {
  if(!lights_on) {
    return;
  }
  lights_on = false;
  // NOTE: Power state now handled through observable pattern (actuals/setpoints)
} 

void toggle_on_off() {
  if (lights_on) {
    turn_off();
  } else {
    turn_on();
  }
}

void set_light_mode(LightMode mode) {
  Serial.printf("set_light_mode %d\n", mode);
  light_mode = mode;
  preferences.begin("main");
  preferences.putInt("light_mode", (int)light_mode);
  preferences.end();
}

void set_brightness(uint8_t new_brightness) {
  brightness = new_brightness;
  preferences.begin("main");
  preferences.putInt("brightness", new_brightness);
  preferences.end();
  calculate_scaled_colors();
}

void set_saturation(uint8_t new_saturation) {
  saturation = new_saturation;
  preferences.begin("main");
  preferences.putInt("saturation", saturation);
  preferences.end();
}

void set_cycles(float new_cycles) {
  if (new_cycles < 0.0001 || new_cycles > 1000) {
    Serial.println("invalid cycle value");
    return;
  }
  cycles = new_cycles;
  preferences.begin("main");
  preferences.putFloat("cycles", cycles);
  preferences.end();
}

void set_speed(float new_speed) {
  if(fabs(new_speed) > 10 ) {
    Serial.println("invalid speed");
  }
  speed = new_speed;
  preferences.begin("main");
  preferences.putFloat("speed", speed);
  preferences.end();
}

void add_color(Color color) {
  unscaled_colors.push_back(color);
  uint16_t color_index = unscaled_colors.size()-1; 
  String key = String("color")+color_index;

  preferences.begin("main");
  preferences.putUInt("color_count", unscaled_colors.size());
  preferences.putUInt(key.c_str(), unscaled_colors[color_index]);
  preferences.end();
  calculate_scaled_colors();
}

void set_program(JsonDocument & doc) {
  auto new_light_mode = doc["light_mode"];
  if(new_light_mode.is<int>()) {
    set_light_mode((LightMode)new_light_mode.as<int>());
  }

  if(new_light_mode.is<const char *>()) {
    set_light_mode(string_to_light_mode(new_light_mode.as<const char *>()));
  }

  auto new_brightness = doc["brightness"];
  if(new_brightness.is<float>()) {
    set_brightness(new_brightness.as<float>());
  }

  auto new_saturation = doc["saturation"];
  if(new_brightness.is<int>()) {
    set_saturation(new_saturation.as<int>());
  } else {
    set_saturation(255);
  }

  auto new_cycles = doc["cycles"];
  if(new_cycles.is<float>()) {
    set_cycles(new_cycles.as<float>());
  }

  auto new_speed = doc["speed"];
  if(new_speed.is<float>()) {
    set_speed(new_speed.as<float>());
  } else {
    set_speed(0.0);
  }


  // print out doc["colors"]
  // Serial.printf("doc[\"colors\"] = %s\n", doc["colors"].as<String>.c_str());

  auto new_colors = doc["colors"].as<JsonArray>();
  if(!new_colors.isNull() && new_colors.size() > 0) {
    Serial.printf("setting %d colors\n", new_colors.size());
    unscaled_colors.clear();
    for(JsonObject o : new_colors) {
      Color c;
      c.r = o["r"]; // will default to zero if doesn't exist
      c.g = o["g"]; // will default to zero if doesn't exist
      c.b = o["b"]; // will default to zero if doesn't exist
      add_color(c);
    }
  }
}


void cmd_set_program(CommandEnvironment & env) {
  if (env.args.getParamCount() != 1) {
    env.cout.printf("Failed - requires a single parameter for json document without whitespace");
    return;
  }

  auto & doc = shared_json_input_doc;
  doc.clear();
  String doc_string = env.args.getCmdParam(1);
  auto error = deserializeJson(doc, doc_string);

  if(error) {
    env.cerr.println("could not parse json");
    env.cerr.println(doc_string);
    env.cerr.println(error.c_str());
  } else {
    set_program(doc);

    serializeJsonPretty(doc, env.cout);
    publish_program();
  }
}

void cmd_explosion(CommandEnvironment &env) { set_light_mode(mode_explosion); turn_on();}
void cmd_confetti(CommandEnvironment &env) { set_light_mode(mode_confetti); turn_on();}
void cmd_pattern1(CommandEnvironment &env) { set_light_mode(mode_pattern1); turn_on();}
void cmd_gradient(CommandEnvironment &env) { set_light_mode(mode_gradient); turn_on();}
void cmd_meteor(CommandEnvironment &env) { set_light_mode(mode_meteor); turn_on();}
void cmd_rainbow(CommandEnvironment &env) { set_light_mode(mode_rainbow); turn_on();}
void cmd_strobe(CommandEnvironment &env) { set_light_mode(mode_strobe); turn_on();}
void cmd_twinkle(CommandEnvironment &env) { set_light_mode(mode_twinkle); turn_on();}
void cmd_normal(CommandEnvironment &env) { set_light_mode(mode_normal); turn_on();}
void cmd_solid(CommandEnvironment &env) { 
  set_light_mode(mode_solid); 
  turn_on();
  pattern_registry.set_active_pattern("Solid");
}
void cmd_flicker(CommandEnvironment &env) { set_light_mode(mode_flicker); turn_on();}
void cmd_breathe(CommandEnvironment &env) { 
  breathe_cycle_start_ms = 0;  // Reset cycle on mode change
  set_light_mode(mode_breathe); 
  turn_on();
  pattern_registry.set_active_pattern("Breathe");
}
void cmd_off(CommandEnvironment &env) { turn_off(); }
void cmd_on(CommandEnvironment &env) { turn_on(); }





void cmd_color(CommandEnvironment &env) {
  auto n_params = env.args.getParamCount();
  if (n_params == 0 || n_params %3  != 0) {
    env.cerr.printf("failed - requires three parameters per color");
    return;
  }

  unscaled_colors.clear();
  for(int i = 0; i < n_params/3; ++i) {
    Color color = Color(atoi(env.args.getCmdParam(i*3+1)), atoi(env.args.getCmdParam(i*3+2)),
                    atoi(env.args.getCmdParam(i*3+3)));
    add_color(color);
  }
}

void cmd_stripes(CommandEnvironment & env) {
  set_light_mode(mode_stripes);
}

void cmd_brightness(CommandEnvironment &env) {
  env.cout.print("current brighness: ");
  env.cout.println(brightness);
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if(env.args.getParamCount() == 1) {
    int new_brightess = atoi(env.args.getCmdParam(1));
    if (new_brightess < 1 || new_brightess > 255) {
      env.cerr.printf("failed - brightness must be between 1 and 255");
      return;
    }
    set_brightness(new_brightess);
    env.cout.print("new brighness: ");
    env.cout.println(brightness);
  }
}



void cmd_cycles(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto new_cycles = atof(env.args.getCmdParam(1));
    if (!(new_cycles > 0.0001 && new_cycles < 1000)) {
      env.cerr.printf("failed - cycles should be between 0.0001 and 1000");
      return;
    }
    set_cycles(new_cycles);
  }
  env.cout.print("cycles = ");
  env.cout.println(cycles);
}

void cmd_speed(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto new_speed = atof(env.args.getCmdParam(1));
    if (!(fabs(new_speed) <= 10)) {
      env.cerr.printf("failed - cycles speed should be less than 10");
      return;
    }
    set_speed(new_speed);
  }
  env.cout.print("speed = ");
  env.cout.println(speed);
}

void cmd_duration(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto new_duration_ms = atoi(env.args.getCmdParam(1));
    if (new_duration_ms < 100 || new_duration_ms > 60000) {
      env.cerr.printf("failed - duration must be between 100 and 60000 milliseconds");
      return;
    }
    breathe_duration_ms = new_duration_ms;
    breathe_cycle_start_ms = 0;  // Reset cycle when duration changes
    
    // Also configure active Breathe pattern if it's active
    PatternBase* active = pattern_registry.get_active_pattern();
    if (active && strcmp(active->get_name(), "Breathe") == 0) {
      active->set_parameter_int("duration", new_duration_ms);
    }
  }
  env.cout.print("duration = ");
  env.cout.print(breathe_duration_ms);
  env.cout.println(" ms");
}

void cmd_spacing(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto new_spacing = atoi(env.args.getCmdParam(1));
    if (new_spacing < 1 || new_spacing > 100) {
      env.cerr.printf("failed - spacing must be between 1 and 100 pixels");
      return;
    }
    pattern_spacing = (uint16_t)new_spacing;
    preferences.begin("main");
    preferences.putInt("spacing", pattern_spacing);
    preferences.end();
    
    // Also configure active Solid pattern if it's active
    PatternBase* active = pattern_registry.get_active_pattern();
    if (active && strcmp(active->get_name(), "Solid") == 0) {
      active->set_parameter_int("spacing", new_spacing);
    }
  }
  env.cout.print("spacing = ");
  env.cout.println(pattern_spacing);
}

void cmd_saturation(CommandEnvironment &env) {
  if (env.args.getParamCount() != 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  int new_saturation = atoi(env.args.getCmdParam(1));
  if (new_saturation < 0 || new_saturation > 255) {
    env.cerr.printf("failed - saturation must be between 0 and 255");
    return;
  }
  set_saturation(new_saturation);
}

void cmd_add_color(CommandEnvironment &env) {
  if (env.args.getParamCount() != 3) {
    env.cerr.printf("failed - requires three parameters");
    return;
  }

  uint32_t color =
      Color(atoi(env.args.getCmdParam(1)), atoi(env.args.getCmdParam(2)),
                  atoi(env.args.getCmdParam(3)));
  add_color(color);
}

void cmd_set_tree_mode(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto v = atoi(env.args.getCmdParam(1));
    is_tree = (v == 1);
    preferences.begin("main");
    preferences.putBool("is_tree", is_tree);
    preferences.end();
  }
  env.cout.print("is_tree: ");
  env.cout.println(is_tree);
  publish_settings();
}


void cmd_set_led_count(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto v = atoi(env.args.getCmdParam(1));
    if (v < 1) {
      env.cerr.printf("failed - led_count must be one or more");
      return;
    }

    if (v >= max_led_count) {
      env.cerr.printf("failed - led_count must not be more that %d", max_led_count);
      return;
    }
    
    // clear existing leds
    for(auto & led : leds) {
      led = Color(0,0,0);
    }
    update_pixels();
    led_count = v;
    preferences.begin("main");
    preferences.putInt("led_count", led_count);
    preferences.end();
#if defined(use_fastled)
    FastLED.addLeds<WS2812, pin_strand_1, RGB>(fast_leds, led_count);
#else
    digitalLeds_resetPixels(strands, STRANDCNT);
    digitalLeds_removeStrands(strands, STRANDCNT);
    STRANDS[0].numPixels = led_count;
    digitalLeds_addStrands(strands, STRANDCNT);

#endif
    // strip.updateLength(led_count);
  }
  env.cout.print("ledcount = ");
  env.cout.println(led_count);
  publish_settings();
}

void cmd_set_max_current(CommandEnvironment &env) {
  if (env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if (env.args.getParamCount() == 1) {
    auto v = atoi(env.args.getCmdParam(1));
    if (v < 200) {
      env.cerr.printf("failed - max current must be greater than 200 ma");
      return;
    }
    max_current = v;
    preferences.begin("main");
    preferences.putInt("max_current", max_current);
    preferences.end();
  }
  env.cout.print("max_current = ");
  env.cout.println(max_current);
  publish_settings();
}

void cmd_set_enable_wifi(CommandEnvironment &env) {
  bool enable_wifi = String(env.args.getCmdParam(1)) == "1";
  wifi_task.set_enable(enable_wifi);
  preferences.begin("main");
  preferences.putBool("enable_wifi", enable_wifi);
  preferences.end();
}

void cmd_next(CommandEnvironment &env) {
  LightMode mode = (LightMode)(light_mode + 1);
  if (mode > mode_last) {
    mode = (LightMode)0;
  }
  set_light_mode(mode);
}

void cmd_previous(CommandEnvironment &env) {
  LightMode mode =
      (light_mode == 0) ? light_mode = mode_last : (LightMode)(light_mode - 1);
  set_light_mode(mode);
}

void cmd_scan_networks(CommandEnvironment & env) {

  int n = WiFi.scanNetworks();
  env.cout.print(R"({"networks":[)");
  for (int i = 0; i < n; ++i) {
    if(i>0) {
      env.cout.print(",");
    }
    env.cout.println();
    env.cout.printf(R"({"ssid": "%s", "rssi": %d, "encryption":%d})",WiFi.SSID(i).c_str(), WiFi.RSSI(i), (int)WiFi.encryptionType(i));
  }
  env.cout.println("]}");
}



void do_ota_upgrade(bool upgrade_spiffs = false) {
  init_display();
  // prepare oled display
  display.clear();
  display.drawString(0, 0, "performing ota upgrade");
  display.display();



  Serial.println("Performing OTA upgrade");
  
  preferences.begin("main", true);
  String ssid = preferences.getString("ssid","");
  String password = preferences.getString("password","");
  preferences.end();

  WiFi.begin(ssid.c_str(), password.c_str());

  // Wait for connection to establish
  const uint32_t connect_timeout_ms = 30000;
  auto start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("."); // Keep the serial monitor lit!
    auto elapsed = millis()-start_time;
    if(elapsed > connect_timeout_ms) {
      display.clear();
      display.drawString(0,0,"Upgrade failed WiFi");
      display.drawString(0,10,"rebooting...");
      display.display();
      delay(2000);
      ESP.restart();
    }
    delay(500);
  }
  Serial.println("connected, doing upgrade");
  if(upgrade_spiffs) {
    display.clear();
    display.drawString(0,0,"Upgrading SPIFFS");
    display.display();
    Serial.println("Upgrading SPIFFS");
    do_ota_upgrade("nerdlights.net", 80, "/updates/heltec-esp32/spiffs.bin",U_SPIFFS);
  } else {
    Serial.println("Upgrading firmware");
    display.clear();
    display.drawString(0,0,"Upgrading Firmware");
    display.display();
    do_ota_upgrade("nerdlights.net", 80, "/updates/heltec-esp32/firmware.bin");

    preferences.begin("main");
    preferences.putBool(pref_do_ota_spiffs_on_boot,true);
    preferences.end();
    Serial.println("Restarting to perform SPIFFS upgrade");
    delay(500);
  }

  display.clear();
  display.drawString(0,0,"restarting");
  display.display();
  delay(100);

  ESP.restart();
}

const uint8_t gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

void setup() {
  Serial.begin(921600);
  Serial.println();
  Serial.println("booting Nerdlights");


  // do OTA upgrade if indicated
  {
    preferences.begin("main");
    bool do_upgrade = preferences.getBool(pref_do_ota_on_boot);
    preferences.putBool(pref_do_ota_on_boot, false); // set to false so we only try once
    preferences.end();
    if(do_upgrade) {
      do_ota_upgrade();
    }
  }

  {
    preferences.begin("main");
    bool do_upgrade = preferences.getBool(pref_do_ota_spiffs_on_boot);
    preferences.putBool(pref_do_ota_spiffs_on_boot, false); // set to false so we only try once
    preferences.end();
    if(do_upgrade) {
      do_ota_upgrade(true);
    }
  }

  pinMode(pin_white_led, OUTPUT);
  digitalWrite(pin_white_led, LOW);

  esp32_common_setup();
  // read preferences
  preferences.begin("main");

  is_tree = preferences.getBool("is_tree", false);
  led_count = preferences.getInt("led_count", 50);
#if defined(use_fastled)
  FastLED.addLeds<WS2812, pin_strand_1, RGB>(fast_leds, led_count);
#else
  STRANDS[0].numPixels = led_count;  
#endif

  max_current = preferences.getInt("max_current", 500);
  light_mode = (LightMode)preferences.getInt("light_mode", mode_rainbow);
  saturation = preferences.getInt("saturation", 200);
  brightness = preferences.getInt("brightness", 30);
  speed = preferences.getFloat("speed", 1.0);
  cycles = preferences.getFloat("cycles", 1.0);
  pattern_spacing = preferences.getInt("spacing", 1);
  device_name = preferences.getString("bt_name", "ledlights");
  preferences.putString("bt_name", device_name);

  unscaled_colors.clear();
  auto color_count = preferences.getUInt("color_count", 1);
  for(int i=0;i<color_count; ++i) {
    String key = (String)"color"+i;
    uint32_t color = preferences.getUInt(key.c_str(), 0x300000);
    unscaled_colors.push_back(color);
  }

  preferences.end();
  calculate_scaled_colors();

  commands.emplace_back(
      Command{"status", cmd_status, "displays current device status information"});
  commands.emplace_back(Command{"enablewifi", cmd_set_enable_wifi, "on/off"});

  commands.emplace_back(
      Command{"explosion", cmd_explosion, "colored explosions"});
  commands.emplace_back(
    Command{"confetti", cmd_confetti, "confetti like effect"});
  commands.emplace_back(
      Command{"pattern1", cmd_pattern1, "lights chase at different rates"});
  commands.emplace_back(
      Command{"gradient", cmd_gradient, "gradual blending of chosen colors"});
  commands.emplace_back(
      Command{"meteor", cmd_meteor, "blended pairs of colors, look like meteors when chasing"});
  commands.emplace_back(Command{"rainbow", cmd_rainbow, "rainbow road"});
  commands.emplace_back(Command{"strobe", cmd_strobe, "flashes whole strand at once"});
  commands.emplace_back(Command{"twinkle", cmd_twinkle, "twinkle random lights"});
  commands.emplace_back(Command{"saturation", cmd_saturation,
                                "set saturation 0-255 for rainbow effect"});
  commands.emplace_back(Command{"brightness", cmd_brightness,
                                "set brightness 1-255 for rainbow effect"});
  commands.emplace_back(Command{"name", cmd_name, "set device name"});
  commands.emplace_back(Command{
      "cycles", cmd_cycles,
      "the number of times the current pattern will fit on the light strand, "
      "if less than one, it will only fit part of the pattern"});
  commands.emplace_back(
      Command{"speed", cmd_speed,
              "speed of effect,  1.0 would mean moving entire strip once per "
              "second, 2.0 would do it twice per second"});
  commands.emplace_back(Command{"is_tree", cmd_set_tree_mode, "set to true if lights are on a tree, makes stripes same width bottom to top"});
  commands.emplace_back(Command{"normal", cmd_normal, "colors are repeated through the strand"});
  commands.emplace_back(Command{"solid", cmd_solid, "solid colors with configurable spacing (alias for normal)"});
  commands.emplace_back(Command{"flicker", cmd_flicker, "flicker like a candle"});
  commands.emplace_back(Command{"breathe", cmd_breathe, "smooth breathing effect with sine wave"});
  commands.emplace_back(Command{"duration", cmd_duration, "set duration in milliseconds for breathe pattern (100-60000)"});
  commands.emplace_back(Command{"spacing", cmd_spacing, "set spacing in pixels for solid pattern (1-100)"});
  commands.emplace_back(
      Command("color", cmd_color, "set the first color of the pattern / pallet"));
  commands.emplace_back(
      Command("add", cmd_add_color,
              "adds a color to current pallet {red} {blue} {green}"));
  commands.emplace_back(
      Command("stripes", cmd_stripes,
              "stripes based on the current colors"));
  commands.emplace_back(Command("ledcount", cmd_set_led_count,
                                "sets the total number of leds on the strand"));
  commands.emplace_back(Command{"next", cmd_next, "cycles to the next mode"});

  commands.emplace_back(Command{"max_current", cmd_set_max_current, "sets maximum current draw in mA"});
  commands.emplace_back(
      Command{"previous", cmd_previous, "cycles to the previous mode"});
  commands.emplace_back(
      Command{"off", cmd_off, "turn lights off, device is still running"});
  commands.emplace_back(
      Command{"on", cmd_on, "turn lights on"});

  commands.emplace_back(
    Command{"scan_networks", cmd_scan_networks, "returns json describing network status"});

  commands.emplace_back(
    Command{"get_program", cmd_get_program, "returns json for the current program"});

  commands.emplace_back(
    Command{"set_program", cmd_set_program, "sets the program from a single line json doc without whitespace"});
  commands.emplace_back(
    Command("do_firmware_upgrade", cmd_do_ota_upgrade, "update firmware from nerdlights.net over wifi (must be connected)"));
  commands.emplace_back(
    Command("restart", cmd_restart, "restart the device"));
  commands.emplace_back(
    Command("get_nearby_devices", cmd_get_nearby_devices, "use network discover to find nearby Nerd Lights"));
  commands.emplace_back(
    Command("do_spiffs_upgrade", cmd_do_spiffs_upgrade, "update file system from nerdlights.net over wifi (must be connected)"));

  // Pattern system commands
  commands.emplace_back(
    Command("pattern_list", cmd_pattern_list, "List available patterns"));
  commands.emplace_back(
    Command("pattern_info", cmd_pattern_info, "Show pattern information [pattern_name]"));
  commands.emplace_back(
    Command("pattern_discover", cmd_pattern_discover, "Output pattern definitions as JSON"));
  commands.emplace_back(
    Command("pattern_set", cmd_pattern_set, "Set active pattern <pattern_name>"));


#if not defined(use_fastled)
  digitalLeds_initDriver();
  for (int i = 0; i < STRANDCNT; i++) {
    gpioSetup(STRANDS[i].gpioNum, OUTPUT, LOW);
    strands[i] = &STRANDS[i];
  }
  digitalLeds_addStrands(strands, STRANDCNT);
#endif
  
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setFilter(ON_STA_FILTER);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index-ap.html").setFilter(ON_AP_FILTER);
  server.on("/vector-icon.svg",[](AsyncWebServerRequest*request) {
    request->send(SPIFFS, "/vector-icon.svg", "image/svg+xml");
  });
  

  server.on(
    "/command",
    HTTP_POST,
    // request
    [](AsyncWebServerRequest * request){
      Serial.println("got a request");
      Serial.println(request->contentLength());

      auto params=request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost() && p->name() == "body") {
          CmdParser parser;
          parser.setOptIgnoreQuote();
          String body = p->value();

          int32_t n_start = 0;
          bool done = false;
          bool ok = true;
          String output_string = "";
          while(!done) {
            int n_sep = body.indexOf("\n", n_start);
            String str_command;
            if(n_sep > 0) {
              str_command = body.substring(n_start,n_sep);
              n_start = n_sep+1;
            } else {
              str_command = body.substring(n_start);
              done=true;
            }
            Serial.print("command: ");
            Serial.println(str_command);
          

            parser.parseCmd((char *)str_command.c_str());
            auto command = get_command_by_name(parser.getCommand());
            if(command == nullptr) {
              ok = false;
              break;
                request->send(200,"text/plain","command failed");
            } else {
                StringStream output_stream(output_string);
                CommandEnvironment env(parser, output_stream, output_stream);
                command->execute(env);
                ok = env.ok;
                if(!ok) break;

            }
          }
          if(ok) {
            request->send(200,"text/plain", output_string);
          } else {
            request->send(200,"text/plain","command failed");
          }
        } else {
        request->send(400,"text/plain","no post body sent");
        }
      }
    }
  );

  mqtt.setBufferSize(3000);
  mqtt.setServer("nerdlights.net", 1883);
  sprintf(mqtt_client_id, "esp32-%" PRIx64, ESP.getEfuseMac());

  // Initialize pattern system (must be called after all other setup)
  init_pattern_system();

}  // setup

// returns part of area up tree [0:1] that needs to be lit to cover height  up tree h[0:1]
double percent_lights_for_percent_up_tree(double h) {
  return 2*(h-((h*h)/2));
}

double percent_up_tree_for_percent_lights(double l) {
  l=clamp(l,0.,1.);
  return 1 - sqrt(1-l);
}


void gradient(bool is_tree = true) {

  size_t n_colors =  current_colors.size();
  float stretch = n_colors < 2 ? 1.0 : (float)(n_colors-1) / (n_colors);
  double shift_left = -1.0 * speed * millis() / 1000. * led_count;
  RepeatingPattern p(led_count, n_colors, stretch, shift_left);
  for(int i = 0; i<led_count; ++i) {
    auto r = p.segment_percent(i);
    if(r.segment >= n_colors) {
      Serial.printf("Invalid segment r.segment: %d n_colors: %d\n", r.segment, n_colors);
    }
    Color color_a = current_colors[r.segment];
    Color color_b = current_colors[(r.segment+1)%n_colors];
    Color color = mix_colors(color_a, color_b, r.percent);
    leds[i] = color;
  }
}

// percent is visual and goes [0,1]
float gamma_percent(float percent, float gamma = 2.8) {
  return powf(percent, gamma);
}

void meteor(bool is_tree = true) {
  size_t n_colors =  current_colors.size();
  int divisions = n_colors;
  int division = 1;
  double division_start = 0;
  double percent = (double)division/divisions;
  double division_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(percent) : percent);

  //double lights_per_division = (double) led_count / (n_colors < 2 ? 1 : n_colors-1);
  for(int i = 0; i<led_count; ++i) {
    if(i>division_end) {
      ++division;
      division_start = division_end;
      percent = (double)division/divisions;
      division_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(percent) : percent);
    }
    Color color_a = current_colors[division-1];
    //Color color_b = Color(0,0,0);
    float percent = 1-(i-division_start)/(division_end-division_start);
    float part_a = gamma_percent(percent);
    Color color(color_a.r * part_a, color_a.g*part_a, color_a.b*part_a);
    leds[i]=color;
  }
}


void pattern1() {
  auto ms = clock_millis();
  // strip.clear();
  // step through the LEDS, and update the colors as needed
  for (int i = 0; i < led_count; i++) {
    Color  c = current_colors[0];
    // use ints to allow overflow, will clamp at end
    int r = c.r;
    int g = c.g;
    int b = c.b;
    if (current_colors.size() >= 2 && (ms / 1000) % led_count == i) {
      c = current_colors[1];
      r += c.r;
      g += c.g;
      b += c.b;
    }

    if (current_colors.size() >= 3 && (ms / 20) % led_count == i) {
      c = current_colors[2];
      r += c.r;
      g += c.g;
      b += c.b;
    }
    if (current_colors.size() >= 4 && (ms / 30) % led_count == i) {
      c = current_colors[3];
      r += c.r;
      g += c.g;
      b += c.b;
    }
    if (current_colors.size() >= 5 && (ms / 40) % led_count == i) {
      c = current_colors[4];
      r += c.r;
      g += c.g;
      b += c.b;
    }
    if (current_colors.size() >= 6 && (ms / 35) % led_count == led_count - i) {
      c = current_colors[5];
      r += c.r;
      g += c.g;
      b += c.b;
    }

    // clamp colors to 255 while maintaining hue
    int max_c = max(r,max(g,b));
    if(max_c > 255) {
      c.r = r * 255. / max_c;
      c.g = g * 255. / max_c;
      c.b = b * 255. / max_c;
    } else {
      c.r = r;
      c.g = g;
      c.b = b;
    }

    leds[i]=c;
  }
}


// inputs:
//    h 0-360 s: 0-1 v: 0-1
//
// altered form of https://gist.github.com/fairlight1337/4935ae72bcbcc1ba5c72
// and https://www.codespeedy.com/hsv-to-rgb-in-cpp/
Color HSVtoRGB(float h, float s, float v) {
  float r, g, b;
  float c = v * s; // chroma
  float h_prime = h/60.;
  float x = c * (1 - abs(fmodf(h_prime, 2) - 1));
  float m = v - c;
  
  switch(int(h_prime)) {
    case 0:
      r = c; g = x; b = 0;
      break;

    case 1:
      r = x; g = c; b = 0;
      break;

    case 2:
      r = 0; g = c; b = x;
      break;

    case 3:
      r = 0; g = x; b = c;
      break;

    case 4:
      r = x; g = 0; b = c;
      break;

    case 5:
      r = c; g = 0; b = x;
      break;

    default:
      r = 0; g = 0; b = 0;
    break;

  }


  r += m;
  g += m;
  b += m;
  return Color(r*255,g*255,b*255);
}

void rainbow() {
  // uses speed and cycles
  auto ms = clock_millis();
  double offset = -1.0 * speed * ms / 1000. * led_count;

  double ratio = cycles / led_count;
  for (int i = 0; i < led_count; ++i) {
    float hue = fabs(fmod((i + offset) * ratio * 360, 360.));  // 0-360
    auto color = HSVtoRGB(hue, saturation/255., brightness/255.);
    leds[i]=color;
  }
}

void rotate() {
  auto ms = clock_millis();
  double offset = -1.0 * speed * ms / 1000. * led_count;
  std::vector<uint32_t> strip_copy;
  strip_copy.resize(led_count);
  for( int i = 0; i < led_count; ++i) {
    strip_copy[i] = leds[i];
  }
  for( int i = 0; i < led_count; ++i) {

    float f_pos = fmod(fabs(i+offset), led_count);
    int i_temp = floor(f_pos);
    i_temp = clamp(i_temp, 0, led_count-1);
    int i_temp2 = i_temp + 1;
    if(i_temp2 >= led_count) i_temp2 = 0;
    float part_2 = f_pos - i_temp;
    if(i_temp < 0 || i_temp >= led_count) {
      Serial.printf("Error: offset: %f i_temp:%d i:%d led_count:%d f_pos:%f\n",offset,i_temp,i,led_count,f_pos);
    }
    if(i_temp2 < 0 || i_temp2 >= led_count) {
      Serial.printf("Error: offset: %f i_temp2:%d i:%d led_count:%d f_pos:%f\n",offset, i_temp,i,led_count,f_pos);
    }
    leds[i] =  mix_colors(strip_copy[i_temp], strip_copy[i_temp2], part_2);
  }
}

const Color black = {0,0,0};

void strobe() {
  // uses speed and cycles
  auto ms = clock_millis();
  
  bool multi = current_colors.size() >= 2;
  
  int n  = floor( (ms * (double)speed) / 1000.0);

  uint32_t color = Color(0,0,0);
  if(multi) {
    color = current_colors[n%current_colors.size()];
  } else {
    color = (n%2==0)?black:current_colors[0];
  }

   for (int i = 0; i < led_count; ++i) {
    leds[i]=color;
  }
}

Color color_at_brightness( Color color , uint8_t new_brightness) {
  Color c = color;
  uint32_t max_c = max<uint8_t>(max<uint8_t>(c.r,c.g),c.b);
  if(max_c > 0) {
    c.r = mul_div(c.r, new_brightness, max_c);
    c.g = mul_div(c.g, new_brightness, max_c);
    c.b = mul_div(c.b, new_brightness, max_c);
  }
  return c;
}

void twinkle() {
  uint32_t ms = clock_millis();
  static double next_ms = 0;

  if(ms - next_ms > 1000) {
    next_ms = ms;
  };

  struct blinking_led_t {
    uint16_t led_number;
    uint32_t done_ms;
    uint32_t twinkle_color;
  };

  // static std::deque<blinking_led_t> blinking_leds;
  static blinking_led_t arr[100];
  static nonstd::ring_span<blinking_led_t> blinking_leds( arr, arr + dim(arr), arr, 0);
  
  // about 10% of the lights are twinkling at any time
  float ratio_twinkling = .1;
  uint32_t average_twinkling_count = led_count * ratio_twinkling;
  uint32_t max_twinkling_count = dim(arr);//average_twinkling_count*2;

  // twinkling takes 1 second
  uint16_t twinkle_ms = 1000;
  float average_ms_per_new_twinkle = (float)twinkle_ms / average_twinkling_count;

  bool multi = current_colors.size() >= 2;
  uint32_t base_color = multi ? current_colors[0] : black;

  for(int i = 0; i < led_count; ++i) {
    leds[i] = base_color;
  }
  
  // remove done LEDS
  while(blinking_leds.size() > 0 && blinking_leds.front().done_ms <= ms) {
    blinking_leds.pop_front();
  }

  while(next_ms <= ms) {
    // add a twinkling led to list
    if(blinking_leds.size() < max_twinkling_count) {
      
      blinking_led_t led;
      led.done_ms = ms+twinkle_ms;
      led.led_number = rand() % led_count;
      led.twinkle_color = multi ? current_colors[rand()% (current_colors.size()-1)+1] : current_colors[0];
      bool already_blinking = false;
      for(auto blinking : blinking_leds) {
        if(blinking.led_number == led.led_number) {
          already_blinking = true;
        }
      }
      if(!already_blinking) {
        blinking_leds.push_back(led);
      }
    }

    // add random time to next_ms
    double add_ms = frand() * 2 * average_ms_per_new_twinkle;
    next_ms = next_ms + add_ms;
  }

  for(auto & led : blinking_leds) {
    // time from midpoint peak
    auto ms_peak = led.done_ms - twinkle_ms/2.;
    auto from_peak = abs(ms-ms_peak);
    auto level = 1. - from_peak / (twinkle_ms/2);
 

    uint32_t color = mix_colors(base_color, led.twinkle_color, level*level);
    
    leds[led.led_number]=color;
  }
}


// add two colors (adding light) clamping with same shade on overflow
Color add(Color c1, Color c2) {
  int r = c1.r+c2.r;
  int g = c1.g+c2.g;
  int b = c1.b+c2.b;

  // try to clamp while keeping color
  int v = max(r,max(g,b));
  if(v > 255) {
    r = r * 255 / v;
    g = g * 255 / v;
    b = b * 255 / v;
  }
  Color rv;
  rv.r = r;
  rv.g = g;
  rv.b = b;

  return rv;
}



void explosion() {
  uint32_t ms = clock_millis();
  static double next_ms = 0;

  if(ms - next_ms > 1000) {
    next_ms = ms;
  };

  struct explosion_t {
    int16_t center_led_number;
    int32_t start_ms;
    int32_t explosion_color;
    int8_t max_radius_in_leds;
  };
  const int32_t explosion_ms = 1000;

  // static std::deque<blinking_led_t> blinking_leds;
  static explosion_t arr[100];
  static nonstd::ring_span<explosion_t> explosions( arr, arr + dim(arr), arr, 0);
  
  // about 10% of the lights are twinkling at any time
  float ratio_exploding = .1;
  uint32_t average_explosion_count = led_count * ratio_exploding;
  uint32_t max_explosion_count = dim(arr);//average_twinkling_count*2;

  // twinkling takes 1 second
  float average_ms_per_new_explosion = (float)explosion_ms / average_explosion_count;

  bool multi = current_colors.size() >= 2;
  Color base_color = multi ? current_colors[0] : black;

  for(int i = 0; i < led_count; ++i) {
    leds[i]=base_color;
  }
  
  // remove done explosions
  while(explosions.size() > 0 && ms - explosions.front().start_ms > explosion_ms  ) {
    explosions.pop_front();
  }

  while(next_ms <= ms) {
    // add a explosion to list
    if(explosions.size() < max_explosion_count) {
      
      explosion_t explosion;
      explosion.center_led_number = rand() % led_count;
      explosion.start_ms = ms;
      explosion.explosion_color = current_colors[rand()%current_colors.size()];
      explosion.max_radius_in_leds = 5 + rand() % 10;
      explosions.push_back(explosion);
    }

    // add random time to next_ms
    double add_ms = frand() * 2 * average_ms_per_new_explosion;
    next_ms = next_ms + add_ms;
  }

  for(auto & explosion : explosions) {
    Color color = color_at_brightness(explosion.explosion_color, brightness);
    for (int x = - explosion.max_radius_in_leds; x <= explosion.max_radius_in_leds; ++x) {
      auto distance = abs(x);
      int i = explosion.center_led_number + x;
      uint32_t elapsed_ms = ms - explosion.start_ms;

      uint16_t radius_in_leds =
          (uint64_t)explosion.max_radius_in_leds * elapsed_ms / explosion_ms;

      float percent = (float)(explosion.max_radius_in_leds - radius_in_leds) *
                      (explosion.max_radius_in_leds - radius_in_leds) /
                      (explosion.max_radius_in_leds * explosion.max_radius_in_leds);

      if(i>0 && i<led_count) {
        if (abs(distance - radius_in_leds) < 2) {
          leds[i] =  mix_colors(leds[i], color, percent);
        }
      }
    }
  }
}

void confetti() {
  uint32_t ms = clock_millis();
  static double next_ms = 0;

  for(int i=0; i<led_count; ++i) {
    // fade the led
    leds[i].r *= 0.95;
    leds[i].g *= 0.95;
    leds[i].b *= 0.95;

    // randomly set a color
    if( rand() % 15 == 0 ) {
      Color c1 = leds[i];
      Color c2 = current_colors[rand()%current_colors.size()];
      Color c3;
      c3.r = qadd8(c1.r,c2.r);
      c3.g = qadd8(c1.g,c2.g);
      c3.b = qadd8(c1.b,c2.b);

      leds[i] = c3;
    }
  }
}

void breathe() {
  uint32_t ms = clock_millis();
  
  // Initialize cycle start on first call or mode change
  if (breathe_cycle_start_ms == 0) {
    breathe_cycle_start_ms = ms;
  }

  // note: if num_colors is zero, this forces them to be black
  Color blended = black;
  
  const int num_colors = current_colors.size();
  
  // Special case: single color - breathe between black and that color
  if (num_colors == 1) {
    uint32_t elapsed = ms - breathe_cycle_start_ms;
    // we use half the duration so color to black will
    // take the same time as color to color below
    auto d = breathe_duration_ms * 2;
    double cycle_position = (double)(elapsed % d) / d;
    
    // Full sine wave for smooth breathing (0.0 to 1.0 and back to 0.0)
    double amplitude = (sin(cycle_position * 2 * PI - PI/2) + 1.0) / 2.0;
    
    // Blend between black and the color
    blended = mix_colors(black, current_colors[0], amplitude);
  }
  else if (num_colors > 1) {
  
    // Calculate total elapsed time to determine which color pair we're breathing between
    uint32_t total_elapsed = ms - breathe_cycle_start_ms;
    uint32_t total_cycle_duration = breathe_duration_ms * num_colors;
    uint32_t position_in_all_cycles = total_elapsed % total_cycle_duration;
    
    // Which color transition are we in? (0 = color0->color1, 1 = color1->color2, etc.)
    int color_index = position_in_all_cycles / breathe_duration_ms;
    uint32_t elapsed_in_cycle = position_in_all_cycles % breathe_duration_ms;
    
    // Position within this specific color transition (0.0 to 1.0)
    double cycle_position = (double)elapsed_in_cycle / breathe_duration_ms;
    
    // Sine wave interpolation (0.0 to 1.0)
    double amplitude = (sin(cycle_position * PI - PI/2) + 1.0) / 2.0;
    double blend_factor = amplitude;
    
    // Get the two colors to blend between
    Color color1 = current_colors[color_index % num_colors];
    Color color2 = current_colors[(color_index + 1) % num_colors];
    
    // Interpolate between the two colors using mix_colors
    blended = mix_colors(color1, color2, blend_factor);
  }
  
  // Apply the same color to all LEDs
  for (int i = 0; i < led_count; ++i) {
    leds[i] = blended;
  }
}


void stripes(const std::vector<Color> & colors, bool is_tree = false) {
  auto ms = clock_millis();
  
  double start_color = -1.0 * speed * ms / 1000. * colors.size();
  for (int i = 0; i < led_count; ++i) {
    auto f = (double)i/led_count;
    auto p = is_tree?percent_up_tree_for_percent_lights((double)i/led_count) : f;
    auto n_color = int(start_color + p * colors.size() * cycles)%colors.size();
    leds[i]=colors[n_color];
  }

}

void normal(const std::vector<Color> & colors, uint16_t repeat_count = 1) {
  for (int i = 0; i < led_count; ++i) {
    auto color = colors[(i / repeat_count) % colors.size()];
    leds[i] = color;
  }
}

// Solid pattern - displays colors with configurable spacing
void solid(const std::vector<Color> & colors) {
  for (int i = 0; i < led_count; ++i) {
    auto color = colors[(i / pattern_spacing) % colors.size()];
    leds[i] = color;
  }
}

void flicker(const std::vector<Color> & colors) {
  for (int i = 0; i < led_count; ++i) {
    if(rand()%5==0) {

      // if( random(500) !=0)  continue;// individual lights to blink at different rates

      // add up contribution from each color
      int r = 0;
      int g = 0;
      int b = 0;
      for(Color c : colors) {
        auto level =random(50,100);
        // auto level = 100;
        r += c.r * level;
        g += c.g * level;
        b += c.b * level;
      }
      r /= 100*colors.size();
      g /= 100*colors.size();
      b /= 100*colors.size();

      leds[i]=Color(r,g,b);

    }
  }
}


#include "HTTPClient.h"



bool is_wifi_connected_to_internet() {
  return WiFi.isConnected();

}


void mqtt_callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.println(". Message: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
  }
  Serial.println();

  // NEW: Check for observable pattern setpoint topics first
  String topicStr = String(topic);
  String devicePrefix = "controllers/" + String(mqtt_client_id) + "/setpoints/";
  
  if (topicStr.startsWith(devicePrefix)) {
    String settingType = topicStr.substring(devicePrefix.length());
    
    if (settingType == "power") {
      handle_power_setpoint(message, length);
      return;
    } else if (settingType == "program") {
      handle_program_setpoint(message, length);
      return;
    }
  }
  
  // NEW: Check for command console topics
  String commandPrefix = "controllers/" + String(mqtt_client_id) + "/commands/";
  if (topicStr.startsWith(commandPrefix)) {
    String commandType = topicStr.substring(commandPrefix.length());
    
    if (commandType == "console") {
      handle_console_command(message, length);
      return;
    }
  }

  // EXISTING: Legacy MQTT message handling for backward compatibility
  // set json document to message
  auto & doc = shared_json_input_doc;
  doc.clear();
  DeserializationError error = deserializeJson(doc, message, length);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc["cmd"] == "on") {
    lights_on = true;
    // NOTE: Legacy command - power state now handled through observable pattern
  }

  if (doc["cmd"] == "off") {
    lights_on = false;
    // NOTE: Legacy command - power state now handled through observable pattern
  }

  if (doc["cmd"] == "set_program") {
    Serial.println("setting program");
    set_program(doc);
    publish_program();
  }

  // if(doc["cmd"]) {
  //   std::string command_string = doc["cmd"];

  //   CmdParser parser;
  //   parser.setOptIgnoreQuote();

  //   parser.parseCmd((char *)command_string.c_str());
  //   Command *command = get_command_by_name(parser.getCommand());
  //   if (command) {
  //     CommandEnvironment env(parser, Serial, Serial);
  //     command->execute(env);
  //   } else {
  //     Serial.print("ERROR: Command not found - ");
  //     Serial.println(parser.getCommand());
  //   }
  
  // }
}

bool mqtt_subscribed = false;

void loop() {
  // Check for pending restart
  if (restart_pending && millis() >= restart_scheduled_ms) {
    Serial.println("Executing scheduled restart");
    blacken_pixels();
    display.clear();
    display.drawString(0,0,"Restarting");
    display.display();
    delay(100);
    ESP.restart();
  }
  
  static unsigned long loop_count = 0;
  static unsigned long draw_count = 0;
  ++loop_count;
  static unsigned long last_loop_ms = 0;
  unsigned long loop_ms = clock_millis();


  esp32_common_loop();
  mqtt.loop();

  command_button.execute(loop_ms);
  if(command_button.is_click()) {
    Serial.println("command clicked");
    
    // Use the new toggle function from upstream
    toggle_on_off();
    
    // ADD: Publish physical button change to server using observable pattern
    if(mqtt.connected()) {
      String unique_id = "controller_" + String(mqtt_client_id) + "_" + String(millis());
      last_actual_power_id = unique_id;
      publish_setpoint_power(unique_id);  // Tell server about our change
      publish_actual_power(unique_id, "applied");  // Report that we applied it
    }
  }
  if(command_button.is_long_press()) {
    Serial.println("command long pressed");
  }

  if (every_n_ms(last_loop_ms, loop_ms, 5000)) {
    if(is_wifi_connected_to_internet() && !mqtt.connected()) {
      mqtt_subscribed = false;
      const char * username = "api_user";
      const char * password = "api_user";
      mqtt.connect(mqtt_client_id, username, password);
      Serial.printf("connecting to MQTT as %s\n", mqtt_client_id);

    }
  }

  if(mqtt.connected() && !mqtt_subscribed) {
    mqtt.setCallback(mqtt_callback);

    Serial.printf("MQTT Connected, listening to topic: %s\n", mqtt_client_id);
    mqtt.subscribe(mqtt_client_id); // Keep legacy subscription for backward compatibility
    
    // NEW: Subscribe to observable pattern setpoint topics
    char powerSetpointTopic[100];
    char programSetpointTopic[100];
    snprintf(powerSetpointTopic, 99, "controllers/%s/setpoints/power", mqtt_client_id);
    snprintf(programSetpointTopic, 99, "controllers/%s/setpoints/program", mqtt_client_id);
    
    mqtt.subscribe(powerSetpointTopic);
    mqtt.subscribe(programSetpointTopic);
    Serial.printf("Subscribed to observable pattern topics: %s, %s\n", 
                  powerSetpointTopic, programSetpointTopic);
    
    // NEW: Subscribe to command console topic
    char consoleCommandTopic[100];
    snprintf(consoleCommandTopic, 99, "controllers/%s/commands/console", mqtt_client_id);
    mqtt.subscribe(consoleCommandTopic);
    Serial.printf("Subscribed to console commands topic: %s\n", consoleCommandTopic);
    
    mqtt_subscribed = true;
    
    // Initialize sync state
    sync_start_time = millis();
    pending_power_sync = true;
    pending_program_sync = true;
    initial_sync_complete = false;
    
    publish_statistics();
    publish_settings();
    publish_pattern_definitions();  // Publish pattern definitions on connect
    // NOTE: lights_on status now handled through observable pattern (actuals topic)
  }

  // every 10 minutes, publish statistics
  if (every_n_ms(last_loop_ms, loop_ms, 10*60*1000)) {
    publish_statistics();
  }

  // NEW: Handle retained message timeout for observable pattern
  if (!initial_sync_complete && (pending_power_sync || pending_program_sync)) {
    if (millis() - sync_start_time > retained_message_timeout) {
      Serial.println("Retained message timeout - assuming no retained messages exist");
      pending_power_sync = false;
      pending_program_sync = false;
      check_initial_sync_complete();
    }
  }

  if(every_n_ms(last_loop_ms, loop_ms, 1000)) {
    // Serial.printf("draw_count: %lu\n", draw_count);
  }

  if (every_n_ms(last_loop_ms, loop_ms, 100)) {
    display.clear();
    
    char buff[80];

    if(is_wifi_connected_to_internet()) {
      // we are connected
      sprintf(buff, "Nerd Lights - %s", device_name.c_str());
      display.drawString(0, 0, buff);

      sprintf(buff,"Connected to %s", wifi_task.ssid.c_str());
      display.drawString(0, 10, buff);
      
      sprintf(buff,"to control your lights,");
      display.drawString(0, 20, buff);

      sprintf(buff,"browse to ");
      display.drawString(0, 30, buff);

      sprintf(buff, "http://%s", WiFi.localIP().toString().c_str());
      display.drawString(0, 40, buff);

/*
Below stuff would be good for a config page
      IPAddress ip_address = ;
      display.drawString(0, 20, ip_address.toString());

      display.drawString(0, 30, "v0.4");
        
      struct tm timeinfo;
      if(!getLocalTime(&timeinfo)){
        display.drawString(0,40,"wifi not connected");
      }

        
      sprintf(buff, "%d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      display.drawString(0, 40, buff);

      sprintf(buff, "free: %d",ESP.getFreeHeap());
      display.drawString(0,50,buff);
*/
    } else {

      // device isn't connected to internet, show instructions to connect
      sprintf(buff,"No Internet");
      display.drawString(0,0, buff);
      sprintf(buff,"Connect your WiFi to");
      display.drawString(0,10, buff);
      sprintf(buff,"%s",ap_ssid.c_str());
      display.drawString(0,20, buff);
      sprintf(buff,"then browse to");
      display.drawString(0,30, buff);
      sprintf(buff,"http://%s",WiFi.softAPIP().toString().c_str());
      display.drawString(0,40, buff);
    }

    display.display();
  }

  static LineReader serial_line_reader;

  // check for a new line in serial
  if (serial_line_reader.get_line(Serial)) {
    CmdParser parser;
    parser.setOptIgnoreQuote();

    parser.parseCmd((char *)serial_line_reader.line.c_str());
    Command *command = get_command_by_name(parser.getCommand());
    if (command) {
      CommandEnvironment env(parser, Serial, Serial);
      command->execute(env);
    } else {
      Serial.print("ERROR: Command not found - ");
      Serial.println(parser.getCommand());
    }
  }


  if(every_n_ms(last_loop_ms, loop_ms, 30)) {
    ++draw_count;

    if(lights_on) {
      switch (light_mode) {

        case mode_rainbow:
          rainbow();
          break;
        case mode_strobe:
          strobe();
          break;
        case mode_stripes:
          stripes(current_colors, is_tree);
          break;
        case mode_twinkle:
          twinkle();
          break;
        case mode_explosion:
          explosion();
          break;
        case mode_gradient:
          gradient(is_tree);
          break;
        case mode_meteor:
          meteor(is_tree);
          break;
        case mode_pattern1:
          pattern1();
          break;
        case mode_normal:
          normal(current_colors);
          break;
        case mode_flicker:
          flicker(current_colors);
          break;
        case mode_confetti:
          confetti();
          break;
        case mode_breathe:
          breathe();
          break;
        case mode_solid:
          solid(current_colors);
          break;
      }
    } else {
      off();
    }

    // rotate
    if(light_mode != mode_rainbow && light_mode != mode_stripes && light_mode != mode_gradient) {
      rotate();
    }

    // enforce current limit
    {
      const int base_current = 180; // mA with nothing turned on
      int max_sum_rgb = (max_current-base_current) * 50 / 3;
      int sum_rgb = 0;
      for (uint16_t i = 0; i < led_count; i++) {
        Color p = leds[i];
        sum_rgb += p.r + p.g + p.b;
      }
      if(sum_rgb > max_sum_rgb) {
        for (uint16_t i = 0; i < led_count; i++) {
          Color p = leds[i];
          p.r = mul_div(p.r, max_sum_rgb, sum_rgb);
          p.g = mul_div(p.g, max_sum_rgb, sum_rgb);
          p.b = mul_div(p.b, max_sum_rgb, sum_rgb);
          leds[i] = p;
        }

        // The white on-board LED is used to indicate overload
        // condition.
        // white=overloaded.
        digitalWrite(pin_white_led, HIGH);
      } else {
        digitalWrite(pin_white_led, LOW);
      }
    }

    update_pixels();
  }

  delay(10);

  last_loop_ms = loop_ms;
}

// ============================================================================
// Pattern System
// ============================================================================
// This section implements the new pattern class hierarchy for discoverable,
// configurable LED patterns. Eventually this will move to separate files.

// Pattern Base Class and Parameter Structures
enum class ParameterType {
    PERCENTAGE,
    NUMBER,
    COLORS,
    MOVEMENT,
    DURATION
};

// Global parameter names (predefined vocabulary)
// These are shared across all patterns that use them
const char* GLOBAL_PARAM_BRIGHTNESS = "brightness";
const char* GLOBAL_PARAM_COLORS = "colors";
const char* GLOBAL_PARAM_MOVEMENT = "movement";
const char* GLOBAL_PARAM_DURATION = "duration";
const char* GLOBAL_PARAM_FADE = "fade";
const char* GLOBAL_PARAM_REPEATS = "repeats";
const char* GLOBAL_PARAM_RATE = "rate";

struct ParameterDef {
    const char* name;
    ParameterType type;
    const char* description;
    int default_value;
    int min_value;
    int max_value;
    const char* unit;
    const char* scale;  // "linear", "logarithmic", "perceptual"
};

class PatternBase {
public:
    virtual ~PatternBase() = default;
    
    // Metadata
    virtual const char* get_name() const = 0;
    virtual const char* get_description() const = 0;
    virtual const char* get_help() const = 0;
    
    // Parameter definitions
    virtual std::vector<const char*> get_global_parameters_used() const = 0;
    virtual std::vector<ParameterDef> get_local_parameters() const = 0;
    
    // Rendering - patterns render to LED buffer
    virtual void render(Color* leds, int led_count, uint32_t time_ms) = 0;
    
    // Configuration - patterns receive typed values
    virtual void set_parameter_int(const char* name, int value) {}
    virtual void set_parameter_colors(const char* name, const Color* colors, int count) {}
    
    // State management
    virtual void reset() {}  // Reset pattern state when activated
};

// Solid Pattern Implementation
class SolidPattern : public PatternBase {
private:
    int brightness_;        // 0-100
    Color colors_[10];      // Up to 10 colors in palette
    int color_count_;       // Number of colors in palette
    int spacing_;           // 1-100 pixels per color

public:
    SolidPattern() 
        : brightness_(100)
        , color_count_(1)
        , spacing_(1)
    {
        // Default warm white
        colors_[0] = {0xFF, 0xA5, 0x00};
    }
    
    // Metadata
    const char* get_name() const override { return "Solid"; }
    const char* get_description() const override { 
        return "Display solid colors or repeating color patterns along the strand"; 
    }
    const char* get_help() const override {
        return "Set one color for the entire strand, or multiple colors that repeat "
               "along it. Use 'spacing' to control how many pixels show each color "
               "before moving to the next.";
    }
    
    // Parameter definitions
    std::vector<const char*> get_global_parameters_used() const override {
        return {"brightness", "colors"};
    }
    
    std::vector<ParameterDef> get_local_parameters() const override {
        ParameterDef spacing;
        spacing.name = "spacing";
        spacing.type = ParameterType::NUMBER;
        spacing.description = "How many pixels show each color before moving to the next";
        spacing.default_value = 1;
        spacing.min_value = 1;
        spacing.max_value = 100;
        spacing.unit = "px";
        spacing.scale = "linear";
        
        return {spacing};
    }
    
    // Rendering - uses existing solid() logic
    void render(Color* leds, int led_count, uint32_t time_ms) override {
        // Apply brightness using gamma correction
        auto apply_brightness = [this](const Color& c) -> Color {
            // Perceptual brightness scaling (gamma correction)
            float gamma_brightness = (brightness_ * brightness_) / 10000.0f;
            return {
                (uint8_t)(c.r * gamma_brightness),
                (uint8_t)(c.g * gamma_brightness),
                (uint8_t)(c.b * gamma_brightness)
            };
        };
        
        // Fill LEDs with repeating color pattern (from solid() function)
        for (int i = 0; i < led_count; i++) {
            int color_index = (i / spacing_) % color_count_;
            leds[i] = apply_brightness(colors_[color_index]);
        }
    }
    
    // Configuration
    void set_parameter_int(const char* name, int value) override {
        if (strcmp(name, "brightness") == 0) {
            brightness_ = clamp(value, 0, 100);
        } else if (strcmp(name, "spacing") == 0) {
            spacing_ = clamp(value, 1, 100);
        }
    }
    
    void set_parameter_colors(const char* name, const Color* colors, int count) override {
        if (strcmp(name, "colors") == 0 && count > 0) {
            color_count_ = min(count, 10);
            memcpy(colors_, colors, color_count_ * sizeof(Color));
        }
    }
    
    void reset() override {
        // No state to reset for solid pattern
    }
};

// Breathe Pattern Implementation
class BreathePattern : public PatternBase {
private:
    int brightness_;        // 0-100
    Color colors_[10];      // Colors to breathe between
    int color_count_;       // Number of colors
    int duration_;          // Milliseconds per breath cycle
    uint32_t cycle_start_ms_; // Track cycle start for pattern

public:
    BreathePattern()
        : brightness_(100)
        , color_count_(2)
        , duration_(6000)
        , cycle_start_ms_(0)
    {
        // Default black to blue
        colors_[0] = {0x00, 0x00, 0x00};
        colors_[1] = {0x00, 0x00, 0xFF};
    }
    
    // Metadata
    const char* get_name() const override { return "Breathe"; }
    const char* get_description() const override {
        return "Entire strand smoothly fades between colors like breathing";
    }
    const char* get_help() const override {
        return "Creates a calming, meditative effect as the whole strand gently "
               "transitions between colors. Use 'duration' for precise millisecond "
               "control of each breath cycle.";
    }
    
    // Parameter definitions
    std::vector<const char*> get_global_parameters_used() const override {
        return {"brightness", "colors", "duration"};
    }
    
    std::vector<ParameterDef> get_local_parameters() const override {
        // Breathe has no local parameters
        return {};
    }
    
    // Rendering - uses exact logic from breathe() function
    void render(Color* leds, int led_count, uint32_t time_ms) override {
        // Initialize cycle start on first call or mode change
        if (cycle_start_ms_ == 0) {
            cycle_start_ms_ = time_ms;
        }

        // note: if num_colors is zero, this forces them to be black
        Color blended = black;
        
        const int num_colors = color_count_;
        
        // Special case: single color - breathe between black and that color
        if (num_colors == 1) {
            uint32_t elapsed = time_ms - cycle_start_ms_;
            // we use half the duration so color to black will
            // take the same time as color to color below
            auto d = duration_ * 2;
            double cycle_position = (double)(elapsed % d) / d;
            
            // Full sine wave for smooth breathing (0.0 to 1.0 and back to 0.0)
            double amplitude = (sin(cycle_position * 2 * PI - PI/2) + 1.0) / 2.0;
            
            // Blend between black and the color
            blended = mix_colors(black, colors_[0], amplitude);
        }
        else if (num_colors > 1) {
            // Calculate total elapsed time to determine which color pair we're breathing between
            uint32_t total_elapsed = time_ms - cycle_start_ms_;
            uint32_t total_cycle_duration = duration_ * num_colors;
            uint32_t position_in_all_cycles = total_elapsed % total_cycle_duration;
            
            // Which color transition are we in? (0 = color0->color1, 1 = color1->color2, etc.)
            int color_index = position_in_all_cycles / duration_;
            uint32_t elapsed_in_cycle = position_in_all_cycles % duration_;
            
            // Position within this specific color transition (0.0 to 1.0)
            double cycle_position = (double)elapsed_in_cycle / duration_;
            
            // Sine wave interpolation (0.0 to 1.0)
            double amplitude = (sin(cycle_position * PI - PI/2) + 1.0) / 2.0;
            double blend_factor = amplitude;
            
            // Get the two colors to blend between
            Color color1 = colors_[color_index % num_colors];
            Color color2 = colors_[(color_index + 1) % num_colors];
            
            // Interpolate between the two colors using mix_colors
            blended = mix_colors(color1, color2, blend_factor);
        }
        
        // Apply the same color to all LEDs
        for (int i = 0; i < led_count; ++i) {
            leds[i] = blended;
        }
    }
    
    // Configuration
    void set_parameter_int(const char* name, int value) override {
        if (strcmp(name, "brightness") == 0) {
            brightness_ = clamp(value, 0, 100);
        } else if (strcmp(name, "duration") == 0) {
            duration_ = clamp(value, 1000, 10000);
        }
    }
    
    void set_parameter_colors(const char* name, const Color* colors, int count) override {
        if (strcmp(name, "colors") == 0 && count > 0) {
            color_count_ = min(count, 10);
            memcpy(colors_, colors, color_count_ * sizeof(Color));
        }
    }
    
    void reset() override {
        // Reset cycle start time when pattern is activated
        cycle_start_ms_ = 0;
    }
};

// Pattern Registry
class PatternRegistry {
private:
    std::vector<PatternBase*> patterns_;
    PatternBase* active_pattern_;
    
public:
    PatternRegistry() 
        : active_pattern_(nullptr) 
    {
    }
    
    ~PatternRegistry() {
        // Note: We don't delete patterns - they're owned by caller
    }
    
    // Registration
    void register_pattern(PatternBase* pattern) {
        patterns_.push_back(pattern);
        if (active_pattern_ == nullptr) {
            active_pattern_ = pattern;
        }
    }
    
    // Discovery
    int get_pattern_count() const { return patterns_.size(); }
    
    PatternBase* get_pattern(int index) const {
        if (index >= 0 && index < patterns_.size()) {
            return patterns_[index];
        }
        return nullptr;
    }
    
    PatternBase* get_pattern_by_name(const char* name) const {
        for (auto* pattern : patterns_) {
            if (strcmp(pattern->get_name(), name) == 0) {
                return pattern;
            }
        }
        return nullptr;
    }
    
    // Activation
    void set_active_pattern(const char* name) {
        PatternBase* pattern = get_pattern_by_name(name);
        if (pattern) {
            if (active_pattern_ != pattern) {
                active_pattern_ = pattern;
                active_pattern_->reset();
            }
        }
    }
    
    PatternBase* get_active_pattern() const { return active_pattern_; }
    
    // JSON serialization for MQTT
    const char* patterns_to_json() const {
        static char json_buffer[4096];
        char* p = json_buffer;
        
        p += sprintf(p, "{\"patterns\":[");
        
        for (int i = 0; i < patterns_.size(); i++) {
            if (i > 0) p += sprintf(p, ",");
            
            PatternBase* pattern = patterns_[i];
            p += sprintf(p, "{\"name\":\"%s\",", pattern->get_name());
            p += sprintf(p, "\"description\":\"%s\",", pattern->get_description());
            p += sprintf(p, "\"help\":\"%s\",", pattern->get_help());
            
            // Global parameters used
            p += sprintf(p, "\"global_parameters\":[");
            auto global_params = pattern->get_global_parameters_used();
            for (int j = 0; j < global_params.size(); j++) {
                if (j > 0) p += sprintf(p, ",");
                p += sprintf(p, "\"%s\"", global_params[j]);
            }
            p += sprintf(p, "],");
            
            // Local parameters
            p += sprintf(p, "\"local_parameters\":[");
            auto local_params = pattern->get_local_parameters();
            for (int j = 0; j < local_params.size(); j++) {
                if (j > 0) p += sprintf(p, ",");
                
                const auto& param = local_params[j];
                p += sprintf(p, "{");
                p += sprintf(p, "\"name\":\"%s\",", param.name);
                
                // Type mapping
                const char* type_str = "unknown";
                switch (param.type) {
                    case ParameterType::PERCENTAGE: type_str = "percentage"; break;
                    case ParameterType::NUMBER: type_str = "number"; break;
                    case ParameterType::COLORS: type_str = "colors"; break;
                    case ParameterType::MOVEMENT: type_str = "movement"; break;
                    case ParameterType::DURATION: type_str = "duration"; break;
                }
                p += sprintf(p, "\"type\":\"%s\",", type_str);
                p += sprintf(p, "\"description\":\"%s\"", param.description);
                
                // Optional fields
                if (param.type != ParameterType::COLORS) {
                    p += sprintf(p, ",\"default\":%d", param.default_value);
                    p += sprintf(p, ",\"min\":%d", param.min_value);
                    p += sprintf(p, ",\"max\":%d", param.max_value);
                }
                if (param.unit) {
                    p += sprintf(p, ",\"unit\":\"%s\"", param.unit);
                }
                if (param.scale) {
                    p += sprintf(p, ",\"scale\":\"%s\"", param.scale);
                }
                
                p += sprintf(p, "}");
            }
            
            p += sprintf(p, "]}");
        }
        
        p += sprintf(p, "]}");
        
        return json_buffer;
    }
};

// Global pattern instances (will be initialized in setup())
SolidPattern solid_pattern;
BreathePattern breathe_pattern;
PatternRegistry pattern_registry;

// Pattern system console commands
void cmd_pattern_list(CommandEnvironment &env) {
    env.cout.println("Available patterns:");
    for (int i = 0; i < pattern_registry.get_pattern_count(); i++) {
        PatternBase* p = pattern_registry.get_pattern(i);
        env.cout.printf("  %s - %s\n", p->get_name(), p->get_description());
    }
}

void cmd_pattern_info(CommandEnvironment &env) {
    const char* pattern_name = nullptr;
    if (env.args.getParamCount() >= 1) {
        pattern_name = env.args.getCmdParam(1);
    }
    
    PatternBase* pattern = nullptr;
    if (pattern_name) {
        pattern = pattern_registry.get_pattern_by_name(pattern_name);
    } else {
        pattern = pattern_registry.get_active_pattern();
    }
    
    if (pattern) {
        env.cout.printf("Pattern: %s\n", pattern->get_name());
        env.cout.printf("Description: %s\n", pattern->get_description());
        env.cout.printf("Help: %s\n", pattern->get_help());
        
        // Show global parameters used
        auto global_params = pattern->get_global_parameters_used();
        if (global_params.size() > 0) {
            env.cout.print("Global parameters: ");
            for (int i = 0; i < global_params.size(); i++) {
                if (i > 0) env.cout.print(", ");
                env.cout.print(global_params[i]);
            }
            env.cout.println();
        }
        
        // Show local parameters
        auto local_params = pattern->get_local_parameters();
        if (local_params.size() > 0) {
            env.cout.println("Local parameters:");
            for (const auto& param : local_params) {
                env.cout.printf("  %s (%s): %s\n", 
                    param.name, 
                    param.unit ? param.unit : "n/a",
                    param.description);
            }
        }
    } else {
        env.cerr.printf("Pattern not found: %s\n", pattern_name);
    }
}

void cmd_pattern_discover(CommandEnvironment &env) {
    // Output JSON definitions for testing
    env.cout.println(pattern_registry.patterns_to_json());
}

void cmd_pattern_set(CommandEnvironment &env) {
    if (env.args.getParamCount() < 1) {
        env.cerr.println("Usage: pattern_set <pattern_name>");
        return;
    }
    
    const char* pattern_name = env.args.getCmdParam(1);
    PatternBase* pattern = pattern_registry.get_pattern_by_name(pattern_name);
    
    if (pattern) {
        pattern_registry.set_active_pattern(pattern_name);
        env.cout.printf("Activated pattern: %s\n", pattern_name);
    } else {
        env.cerr.printf("Unknown pattern: %s\n", pattern_name);
    }
}

// Initialize the pattern system - called from setup()
void init_pattern_system() {
    pattern_registry.register_pattern(&solid_pattern);
    pattern_registry.register_pattern(&breathe_pattern);
    pattern_registry.set_active_pattern("Solid");
    Serial.println("Pattern registry initialized");
}

// Publish pattern definitions to MQTT - called from setup() after pattern init
void publish_pattern_definitions() {
    char topic[100];
    snprintf(topic, 99, "controllers/%s/patterns", mqtt_client_id);
    const char* json = pattern_registry.patterns_to_json();
    mqtt.publish(topic, json, true); // retained=true
    
    Serial.println("Published pattern definitions to MQTT:");
    Serial.print("  Topic: ");
    Serial.println(topic);
    Serial.print("  Payload length: ");
    Serial.println(strlen(json));
}

