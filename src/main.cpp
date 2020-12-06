//#define CONFIG_ASYNC_TCP_RUNNING_CORE -1
// #define CONFIG_ASYNC_TCP_USE_WDT 0
#include "esp32-common.h"

#include "math.h"
#ifdef __AVR__
#include <avr/power.h>
#endif

#include <vector>
#include <queue>

//#include "OLEDDisplay.h"
#include "SSD1306Wire.h"

#include "time.h"

#include <chrono>

// circular buffer, from https://github.com/martinmoene/ring-span-lite
#include "ring_span.hpp"

#define ARDIUNOJSON_TAB "  "
#include <ArduinoJson.h>

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

Color leds[1000];




// globals
const int pin_strand_1 = 5 ;
const int max_led_count = 1000;
int led_count = 10;
int max_current = 500;
String device_name="nerdlights";
bool lights_on = true; // true if lights are currently turned on
bool is_tree = false;

const int pin_command_button = 0; // built in command button
Pushbutton command_button(pin_command_button, 0);



float speed = 1.0;
float cycles = 1.0;

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

uint32_t mix_colors(uint32_t c1, uint32_t c2, float part2) {
  part2 = clamp<float>(part2, 0., 1.);
  float part1 = 1-part2;
  auto r1 = (c1 & 0xff0000)>>16;
  auto g1 = (c1 & 0xff00)>>8;
  auto b1 = c1 & 0xff;
  auto r2 = (c2 & 0xff0000)>>16;
  auto g2 = (c2 & 0xff00)>>8;
  auto b2 = c2 & 0xff;
  uint32_t color = (uint32_t(r1*part1+r2*part2+0.5)<<16) + (uint32_t(g1*part1+g2*part2+0.5)<<8) +  (uint32_t(b1*part1+b2*part2+0.5));

  return color;
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
  mode_last = mode_flicker
};

LightMode light_mode = mode_rainbow;
uint8_t saturation = 200;
uint8_t brightness = 50;

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

constexpr int16_t degrees_to_hex(int32_t degrees) {
  return (degrees % 360) * 0xffff / 360;
}


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


void cmd_do_ota_upgrade(CommandEnvironment & env) {

  preferences.begin("main");
  preferences.putBool(pref_do_ota_on_boot,true);
  preferences.end();

  blacken_pixels();

  display.clear();
  display.drawString(0,0,"Restarting to");
  display.drawString(0,10,"firmware upgrade");
  display.display();

  Serial.println("Restarting to perform firmware upgrade");
  delay(500);
  ESP.restart();
}

void cmd_do_spiffs_upgrade(CommandEnvironment & env) {
  preferences.begin("main");
  preferences.putBool(pref_do_ota_spiffs_on_boot,true);
  preferences.end();

  blacken_pixels();


  Serial.println("Restarting to perform SPIFFS upgrade");
  delay(500);
  ESP.restart();
}



void cmd_get_program(CommandEnvironment & env) {
  Stream & o = env.cout;
  const int doc_capacity = 2000;
  StaticJsonDocument<doc_capacity> doc;
  doc["light_mode"]=light_mode;
  doc["brightness"]=brightness;
  doc["saturation"]=saturation;
  doc["cycles"]=cycles;
  doc["speed"]=speed;

  JsonArray colors_array = doc.createNestedArray("colors");
  for (int i = 0; i < unscaled_colors.size();++i) {
    Color c = unscaled_colors[i];
    auto color_json = colors_array.createNestedObject();
    color_json["r"]=c.r;
    color_json["g"]=c.g;
    color_json["b"]=c.b;
  }
  //serializeJsonPretty(doc, o);
  serializeJson(doc, o);
  // print memory usage
  env.cerr.println();
  env.cerr.print("used bytes:");
  env.cerr.print(doc.memoryUsage());
  env.cerr.print("/");
  env.cerr.print(doc_capacity);
  env.cerr.println();
}

void cmd_status(CommandEnvironment & env) {
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
  esp_restart();
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


void set_light_mode(LightMode mode) {
  lights_on = true;
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
    auto old_lights_on = lights_on;
    set_light_mode((LightMode)new_light_mode.as<int>());
    lights_on = old_lights_on;
  }

  auto new_brightness = doc["brightness"];
  if(new_brightness.is<float>()) {
    set_brightness(new_brightness.as<float>());
  }

  auto new_saturation = doc["saturation"];
  if(new_brightness.is<int>()) {
    set_saturation(new_saturation.as<int>());
  }

  auto new_cycles = doc["cycles"];
  if(new_cycles.is<float>()) {
    set_cycles(new_cycles.as<float>());
  }

  auto new_speed = doc["speed"];
  if(new_speed.is<float>()) {
    set_speed(new_speed.as<float>());
  }

  auto new_colors = doc["colors"].as<JsonArray>();
  if(!new_colors.isNull() && new_colors.size() > 0) {
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

  const int doc_capacity = 2000;
  StaticJsonDocument<doc_capacity> doc;
  String doc_string = env.args.getCmdParam(1);
  auto error = deserializeJson(doc, doc_string);

  if(error) {
    env.cerr.println("could not parse json");
    env.cerr.println(doc_string);
    env.cerr.println(error.c_str());
  } else {
    set_program(doc);

    serializeJsonPretty(doc, env.cout);
  }
}

void cmd_explosion(CommandEnvironment &env) { set_light_mode(mode_explosion); }
void cmd_pattern1(CommandEnvironment &env) { set_light_mode(mode_pattern1); }
void cmd_gradient(CommandEnvironment &env) { set_light_mode(mode_gradient); }
void cmd_rainbow(CommandEnvironment &env) { set_light_mode(mode_rainbow); }
void cmd_strobe(CommandEnvironment &env) { set_light_mode(mode_strobe); }
void cmd_twinkle(CommandEnvironment &env) { set_light_mode(mode_twinkle); }
void cmd_normal(CommandEnvironment &env) { set_light_mode(mode_normal); }
void cmd_flicker(CommandEnvironment &env) { set_light_mode(mode_flicker); }
void cmd_off(CommandEnvironment &env) { lights_on = false; }
void cmd_on(CommandEnvironment &env) { lights_on = true; }





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
    // clear existing leds
    for(int i=0;i<sizeof(leds)/sizeof(leds[0]);++i) {
      leds[i]=Color(0,0,0);
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
      Command{"pattern1", cmd_pattern1, "lights chase at different rates"});
  commands.emplace_back(
      Command{"gradient", cmd_gradient, "gradual blending of chosen colors"});
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
  commands.emplace_back(Command{"flicker", cmd_flicker, "flicker like a candle"});
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
    Command("do_spiffs_upgrade", cmd_do_spiffs_upgrade, "update file system from nerdlights.net over wifi (must be connected)"));


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
  int divisions = (n_colors < 2) ? 1: n_colors -1;
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
    uint32_t color_a = current_colors[(division-1)%n_colors]; // modulus might be unnecessary, but prevents overflow
    uint32_t color_b = current_colors[(division)%n_colors];
    double part_b = (i-division_start)/(division_end-division_start);
    uint32_t color = mix_colors(color_a, color_b, part_b);
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
    int i_temp2 = i_temp + 1;
    if(i_temp2 >= led_count) i_temp2 = 0;
    float part_2 = f_pos - i_temp;
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




void stripes2(std::vector<Color> colors, bool is_tree = false) {
  auto ms = clock_millis();
  
  double start_color = -1.0 * speed * ms / 1000. * colors.size();
  for (int i = 0; i < led_count; ++i) {
    auto f = (double)i/led_count;
    auto p = is_tree?percent_up_tree_for_percent_lights((double)i/led_count) : f;
    auto n_color = int(start_color + p * colors.size() * cycles)%colors.size();
    leds[i]=colors[n_color];
  }

}

void stripes(std::vector<Color> colors, bool is_tree = false) {
  if(led_count==0) {
    return;
  }
  size_t n_color = 0;
  double ratio_end = (n_color+1.0) / colors.size() / cycles;
  double led_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(ratio_end) : ratio_end);
  for (int i = 0; i < led_count; ++i) {
    if(i > led_end) {
      ++n_color;
      ratio_end = (n_color+1.0) / colors.size() / cycles;
      led_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(ratio_end) : ratio_end);
    }
    auto color = colors[clamp<size_t>(n_color%colors.size(),0,colors.size()-1)];
    leds[i]=color;
  }
}

void normal(std::vector<Color> colors, uint16_t repeat_count = 1) {
  for (int i = 0; i < led_count; ++i) {
    auto color = colors[(i / repeat_count) % colors.size()];
    leds[i]=color;
  }
}

void flicker(std::vector<Color> colors) {
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

void download_program() {
  HTTPClient client;

  // Make a HTTP request:
  //client.begin("http://www.arduino.cc/asciilogo.txt");
  client.begin("http://nerdlights.net/programs/program1.json");
  auto http_code = client.GET();
  if(http_code>0) {
    const int doc_capacity = 2000;
    StaticJsonDocument<doc_capacity> doc;
    String doc_string = client.getString();
    auto error = deserializeJson(doc, doc_string);


    if(error) {
      Serial.println("could not parse json");
      Serial.println(doc_string);
      Serial.println(error.c_str());
    } else {
      set_program(doc);
      Serial.println("program set from nerdlights.net");
    }
  }
}

void loop() {
  static unsigned long loop_count = 0;
  ++loop_count;
  static unsigned long last_loop_ms = 0;
  unsigned long loop_ms = clock_millis();


  esp32_common_loop();

  command_button.execute(loop_ms);
  if(command_button.is_click()) {
    Serial.println("command clicked");
  }
  if(command_button.is_long_press()) {
    Serial.println("command long pressed");
  }

  if(false && every_n_ms(last_loop_ms, loop_ms, 10000)) {
    download_program();
  }

/*
  if(every_n_ms(last_loop_ms, loop_ms, 1000)) {
    for(int i=0;i<32;++i) {
    Serial.printf("D%d: %d ",i, digitalRead(i));
    }
    Serial.println();
  }
*/  
  

  if (every_n_ms(last_loop_ms, loop_ms, 100)) {
    display.clear();
    display.drawString(0, 0, "name: " + device_name);
    
    String ssid = WiFi.isConnected() ? wifi_task.ssid : "nerdlights";
    display.drawString(0, 10, (String) "SSID: " + ssid);
    

    IPAddress ip_address = WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
    display.drawString(0, 20, ip_address.toString());

    display.drawString(0, 30, "v0.4");
      
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      display.drawString(0,40,"wifi not connected");
    } else {
      
      char buff[80];
      sprintf(buff, "%d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      display.drawString(0, 40, buff);
    }

    char buff[80];
    sprintf(buff, "free: %d",ESP.getFreeHeap());
    display.drawString(0,50,buff);

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

    if(lights_on) {
      switch (light_mode) {

        case mode_rainbow:
          rainbow();
          break;
        case mode_strobe:
          strobe();
          break;
        case mode_stripes:
          stripes2(current_colors, is_tree);
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
        case mode_pattern1:
          pattern1();
          break;
        case mode_normal:
          normal(current_colors);
          break;
        case mode_flicker:
          flicker(current_colors);
          break;
      }
    } else {
      off();
    }

    // rotate
    if(light_mode != mode_rainbow && light_mode != mode_stripes ) {
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
        leds[0]=Color(255,0,0);
        
      }
    }

    update_pixels();
  }

  delay(10);

  last_loop_ms = loop_ms;
}

