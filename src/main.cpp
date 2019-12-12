#include <Adafruit_NeoPixel.h>
#include "math.h"
#ifdef __AVR__
#include <avr/power.h>
#endif
#include <vector>
#include "StringStream.h"

#include "Preferences.h"  // save preferences to non-volatile memory

#include <BluetoothSerial.h>
#include <CmdParser.hpp>

#include "OLEDDisplay.h"
#include "SSD1306.h"

#include "SPIFFS.h"
#include "WiFi.h"
#include "esp_wifi.h"
#include "time.h"
// https://github.com/me-no-dev/ESPAsyncWebServer
#include "ESPAsyncWebServer.h"

#include <chrono>

// board at https://www.amazon.com/gp/product/B07DKD79Y9
// oled
const int oled_address = 0x3c;
const int pin_oled_sda = 4;
const int pin_oled_sdl = 15;
const int pin_oled_rst = 16;

const int pin_strand_1 = 2;
int led_count = 500;

String bluetooth_device_name = "bke";

SSD1306 display(oled_address, pin_oled_sda, pin_oled_sdl);

// globals
const uint32_t bluetooth_buffer_reserve = 500;
BluetoothSerial bluetooth;
Preferences preferences;
bool lights_on = true; // true if lights are currently turned on


float speed = 1.0;
float cycles = 1.0;

// *********************************
// ESP32 Digital LED Driver
//
#include "esp32_digital_led_lib.h"
//#include "esp32_digital_led_funcs.h"
strand_t STRANDS[] = {{.rmtChannel = 2,
                       .gpioNum = pin_strand_1,
                       .ledType = LED_WS2812B_V3,
                       .brightLimit = 24,
                       .numPixels = led_count}};
const int STRANDCNT = sizeof(STRANDS) / sizeof(STRANDS[0]);
;
strand_t *strands[8];

// **********************************

//////////////////////////////////////////////////////
// Command Interface

class LineReader {
 public:
  const uint32_t buffer_size = 500;
  String buffer;
  String line;
  // BluetoothSerial bluetoothx
  bool line_available = true;

  LineReader() {
    buffer.reserve(buffer_size);
    line.reserve(buffer_size);
  }

  bool get_line(Stream &stream) {
    while (stream.available()) {
      char c = (char)stream.read();
      if (c == '\n') continue;
      if (c == '\r') {
        if (true) {  // buffer.length() > 0) {
          line = buffer;
          buffer = "";
          return true;
        }
      } else {
        buffer.concat(c);
      }
    }
    return false;
  }
};

class CommandEnvironment {
 public:
  CommandEnvironment(CmdParser &args, Stream &cout, Stream &cerr)
      : args(args), cout(cout), cerr(cerr) {}
  CmdParser &args;
  Stream &cout;
  Stream &cerr;
  bool ok = true;
};

typedef void (*CommandCallback)(CommandEnvironment &env);

class Command {
 public:
  Command() : execute(nullptr) {}
  Command(const char *name, CommandCallback callback,
          const char *helpstring = nullptr) {
    this->name = name;
    this->execute = callback;
    this->helpstring = helpstring;
  }
  String name;
  CommandCallback execute;
  String helpstring;
};

////////////////////////////////////////////////////
// WIFI
////////////////////////////////////////////////////

const int wifi_port = 80;
// const int wifi_max_clients = 1;
AsyncWebServer server(wifi_port);

struct HttpRoute {
  String method;
  String path;
  std::function<void(WiFiClient &client)> execute;
};

class WifiTask {
 public:
  LineReader line_reader;
  WiFiClient client;
  unsigned long connect_start_ms = 0;
  unsigned long last_execute_ms = 0;
  unsigned long last_client_activity_ms = 0;
  String ssid;
  String password;

  String method = "";   // GET, PUT, ETC.
  String path = "";     // URI
  String version = "";  // HTTP Version

  bool enabled = false;
  bool trace = false;
  bool log_serial = true;

  enum {
    status_disabled,
    status_not_connected,
    status_connecting,
    status_awaiting_client,
    status_awaiting_command,
    status_awaiting_header
  } current_state = status_disabled;

  void set_enable(bool enable_wifi) {
    if (enable_wifi == this->enabled) return;
    enabled = enable_wifi;
    if (enabled) {
      current_state = status_not_connected;
    } else {
      WiFi.disconnect(true, true);
      current_state = status_disabled;
    }
  }

  void set_connection_info(String ssid, String password) {
    WiFi.disconnect();
    this->ssid = ssid;
    this->password = password;
    if (current_state != status_disabled) {
      current_state = status_not_connected;
    }

    Serial.print("connection info set to ssid: ");
    Serial.print(ssid);
    Serial.print(" password: ");
    Serial.print(password);
    Serial.println();
  }

  void execute() {
    auto ms = millis();
    auto wifi_status = WiFi.status();

    switch (current_state) {
      case status_disabled:
        break;

      case status_not_connected:
        connect_start_ms = ms;
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(bluetooth_device_name.c_str());
        esp_wifi_set_ps(WIFI_PS_NONE);
        WiFi.begin(ssid.c_str(), password.c_str());
        current_state = status_connecting;
        break;

      case status_connecting:
        if (wifi_status == WL_CONNECT_FAILED) {
          current_state = status_not_connected;
          if (trace) Serial.println("connection failed");
          current_state = status_not_connected;
          break;
        }
        if (wifi_status == WL_CONNECTED) {
          configTime(-8*60*60, -7*60*60, "pool.ntp.org");
          server.begin();
          current_state = status_awaiting_client;
          if (trace) Serial.print("wifi connected, web server started");
        } else {
          // if(every_n_ms(last_execute_ms, ms, 1000)) {
          //   Serial.print(wifi_status);
          // }
          if (ms - connect_start_ms > 5000) {
            if (trace) Serial.print("couldn't connect, trying again");
            WiFi.disconnect();
            current_state = status_not_connected;
            break;
          }
        }
        break;

      case status_awaiting_client:
        if (wifi_status != WL_CONNECTED) {
          if (trace) Serial.print("wifi connected, web server stopped");
          current_state = status_not_connected;
          server.end();
          break;
        }
        break;

      default:
        Serial.println("invalid sate in WifiTask");
    }
    last_execute_ms = ms;
  }
};

WifiTask wifi_task;


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


////////////////////////////////
// define commands
////////////////////////////////

std::vector<Command> commands;

void cmd_help(CommandEnvironment &env) {
  for (auto command : commands) {
    env.cout.print(command.name);
    env.cout.print(": ");
    env.cout.print(command.helpstring);
    env.cout.println();
  }
}

void cmd_status(CommandEnvironment & env) {
  env.cout.print("Device name: ");
  env.cout.println(bluetooth_device_name);
  env.cout.print("The lights are: ");
  env.cout.println(lights_on ? "ON" : "OFF");
  env.cout.println("SSID: "+ wifi_task.ssid);
  env.cout.println("IP Address: " + WiFi.localIP().toString());
  env.cout.print("Bluetooth status: ");
  env.cout.println(bluetooth.hasClient() ? "connected" : "disconnected");

}


// current lighting pattern selected for display
enum LightMode {
  mode_rgb,
  mode_usa,
  mode_explosion,
  mode_pattern1,
  mode_rainbow,
  mode_green,
  mode_strobe,
  mode_color,
  mode_last = mode_color
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
Adafruit_NeoPixel strip =
    Adafruit_NeoPixel(led_count, pin_strand_1, NEO_RGB + NEO_KHZ400);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

constexpr int16_t degrees_to_hex(int32_t degrees) {
  return (degrees % 360) * 0xffff / 360;
}

namespace Colors {
uint32_t black = strip.Color(0, 0, 0);
uint32_t warm_white = strip.Color(8, 5, 2);
uint32_t light_blue = strip.ColorHSV(degrees_to_hex(197), 50 * 0xff / 100, 20);
uint32_t pink = strip.ColorHSV(degrees_to_hex(350), 30 * 0xff / 100, 20);
uint32_t white = strip.Color(20, 23, 20);
uint32_t red = strip.Color(20, 0, 0);
uint32_t blue = strip.Color(0, 0, 20);
};  // namespace Colors

void cmd_name(CommandEnvironment &env) {
  if (env.args.getParamCount() != 1) {
    env.cout.printf("Failed - requires a single parameter for name");
    return;
  }
  String name = env.args.getCmdParam(1);
  /*
  for(int i = 0; i < name.length; ++i) {
    if(name[i] < 'a' || name[i] < 'z') {
      env.cout.printf("Failed - bluetooth device name must be all lowercase
  letters"); return;
    }
  }
  */
  if (name.length() > 32) {
    env.cout.printf(
        "Failed - bluetooth device name must be 32 or fewer characters");
    return;
  }
  preferences.begin("main");
  preferences.putString("bt_name", name);
  preferences.end();
  esp_restart();
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
}

void set_saturation(uint8_t new_saturation) {
  saturation = new_saturation;
  preferences.begin("main");
  preferences.putInt("saturation", saturation);
  preferences.end();
}

void cmd_rgb(CommandEnvironment &env) { set_light_mode(mode_rgb); }

void cmd_usa(CommandEnvironment &env) { set_light_mode(mode_usa); }

void cmd_explosion(CommandEnvironment &env) { set_light_mode(mode_explosion); }

void cmd_pattern1(CommandEnvironment &env) { set_light_mode(mode_pattern1); }

void cmd_rainbow(CommandEnvironment &env) { set_light_mode(mode_rainbow); }
void cmd_strobe(CommandEnvironment &env) { set_light_mode(mode_strobe); }

void cmd_off(CommandEnvironment &env) { lights_on = false; }
void cmd_on(CommandEnvironment &env) { lights_on = true; }

void cmd_green(CommandEnvironment &env) { set_light_mode(mode_green); }

std::vector<uint32_t> current_colors = {strip.Color(15, 15, 15)};

void cmd_color(CommandEnvironment &env) {
  if (env.args.getParamCount() != 3) {
    env.cerr.printf("failed - requires three parameters");
    return;
  }

  uint32_t color =
      strip.Color(atoi(env.args.getCmdParam(1)), atoi(env.args.getCmdParam(2)),
                  atoi(env.args.getCmdParam(3)));
  current_colors = {color};
  set_light_mode(mode_color);
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
    if (!(new_cycles > 0.01 && new_cycles < 1000)) {
      env.cerr.printf("failed - cycles should be between 0.01 and 1000");
      return;
    }
    cycles = new_cycles;
    preferences.begin("main");
    preferences.putFloat("cycles", cycles);
    preferences.end();
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
    if (!(fabs(new_speed) < 10)) {
      env.cerr.printf("failed - cycles speed should be less than 10");
      return;
    }
    speed = new_speed;
    preferences.begin("main");
    preferences.putFloat("speed", speed);
    preferences.end();
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

  light_mode = mode_color;
  uint32_t color =
      strip.Color(atoi(env.args.getCmdParam(1)), atoi(env.args.getCmdParam(2)),
                  atoi(env.args.getCmdParam(3)));
  current_colors.push_back(color);
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
    led_count = v;
    preferences.begin("main");
    preferences.putInt("led_count", led_count);
    preferences.end();
    STRANDS[0].numPixels = led_count;
    strip.updateLength(led_count);
  }
  env.cout.print("ledcount = ");
  env.cout.println(led_count);
}

void cmd_set_wifi_config(CommandEnvironment &env) {
  String ssid = env.args.getCmdParam(1);
  String password = env.args.getCmdParam(2);
  preferences.begin("main", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  preferences.begin("main", true);
  wifi_task.set_connection_info(ssid, password);
  env.cout.print("ssid set to \"");
  env.cout.print(preferences.getString("ssid"));
  env.cout.print("\", password set to \"");
  env.cout.print(preferences.getString("password"));
  env.cout.print("\"");
  env.cout.println();
  preferences.end();
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

Command *get_command_by_name(const char *command_name) {
  for (auto &command : commands) {
    if (command.name == command_name) {
      return &command;
    }
  }
  return nullptr;
}

using namespace Colors;

// feeds variables to html
String get_variable_value(const String& var) {
  if(var == "device_name"){
    return bluetooth_device_name;
  }

  return String();
}

void setup() {
  // enable file system
  SPIFFS.begin();

  // read preferences
  preferences.begin("main");
  wifi_task.set_connection_info(preferences.getString("ssid"), preferences.getString("password"));
  wifi_task.set_enable(true);//preferences.getBool("enable_wifi"));

  led_count = preferences.getInt("led_count", 50);
  light_mode = (LightMode)preferences.getInt("light_mode", mode_rainbow);
  saturation = preferences.getInt("saturation", 200);
  brightness = preferences.getInt("brightness", 30);
  speed = preferences.getFloat("speed", 1.0);
  cycles = preferences.getFloat("cycles", 1.0);
  bluetooth_device_name = preferences.getString("bt_name", "ledlights");

  preferences.end();

  pinMode(pin_oled_rst, OUTPUT);
  delay(100);

  // set up oled
  digitalWrite(pin_oled_rst, LOW);
  delay(10);
  digitalWrite(pin_oled_rst, HIGH);
  delay(100);

  // init display before mpu since it initializes shared i2c
  display.init();
  Serial.begin(921600);
  bluetooth.begin(bluetooth_device_name);
  //
  // set up command interfaces
  commands.reserve(50);
  commands.emplace_back(
      Command{"help", cmd_help, "displays list of available commands"});
  commands.emplace_back(
      Command{"status", cmd_status, "displays current device status information"});
  commands.emplace_back(
      Command{"wifi", cmd_set_wifi_config, "set wifi, {ssid} {password}"});
  commands.emplace_back(Command{"enablewifi", cmd_set_enable_wifi, "on/off"});
  commands.emplace_back(Command{"usa", cmd_usa, "red white and blue"});
  commands.emplace_back(Command{"rgb", cmd_rgb, "red green and blue lights"});
  commands.emplace_back(
      Command{"explosion", cmd_explosion, "colored explosions"});
  commands.emplace_back(
      Command{"pattern1", cmd_pattern1, "lights chase at different rates"});
  commands.emplace_back(Command{"rainbow", cmd_rainbow, "rainbow road"});
  commands.emplace_back(Command{"strobe", cmd_strobe, "flashes whole strand at once"});
  commands.emplace_back(Command{"saturation", cmd_saturation,
                                "set saturation 0-255 for rainbow effect"});
  commands.emplace_back(Command{"brightness", cmd_brightness,
                                "set brightness 1-255 for rainbow effect"});
  commands.emplace_back(Command{"name", cmd_name, "set bluetooth device name"});
  commands.emplace_back(Command{
      "cycles", cmd_cycles,
      "the number of times the current pattern will fit on the light strand, "
      "if less than one, it will only fit part of the pattern"});
  commands.emplace_back(
      Command{"speed", cmd_speed,
              "speed of effect,  1.0 would mean moving entire strip once per "
              "second, 2.0 would do it twice per second"});
  commands.emplace_back(Command{"green", cmd_green, "solid green color"});
  commands.emplace_back(
      Command("color", cmd_color, "solid {red} {blue} {green}"));
  commands.emplace_back(
      Command("add", cmd_add_color,
              "adds a color to current pallet {red} {blue} {green}"));
  commands.emplace_back(Command("ledcount", cmd_set_led_count,
                                "sets the total number of leds on the strand"));
  commands.emplace_back(Command{"next", cmd_next, "cycles to the next mode"});
  commands.emplace_back(
      Command{"previous", cmd_previous, "cycles to the previous mode"});
  commands.emplace_back(
      Command{"off", cmd_off, "turn lights off, device is still running"});
  commands.emplace_back(
      Command{"on", cmd_on, "turn lights on"});

  // setup the LED strand
  // strip.begin();
  // strip.show(); // Initialize all pixels to 'off'

  digitalLeds_initDriver();
  for (int i = 0; i < STRANDCNT; i++) {
    gpioSetup(STRANDS[i].gpioNum, OUTPUT, LOW);
    strands[i] = &STRANDS[i];
  }
  digitalLeds_addStrands(strands, STRANDCNT);

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
          String body = p->value();

          parser.parseCmd((char *)body.c_str());
          auto command = get_command_by_name(parser.getCommand());
          if(command == nullptr) {
              request->send(200,"text/plain","command failed");
          } else {
              String output_string;
              StringStream output_stream(output_string);
              CommandEnvironment env(parser, output_stream, output_stream);
              command->execute(env);
              if(env.ok) {
                request->send(200,"text/plain", output_string);
              } else {
                request->send(200,"text/plain","command failed");
              }
          }
        }
        request->send(400,"text/plain","no post body sent");
      }
    }
    );
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html", false, get_variable_value);
  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html", false, get_variable_value);
  });
  server.on("/command_line.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/command_line.html", "text/html", false, get_variable_value);
  });

}  // setup

void pattern1() {
  auto ms = clock_millis();
  // strip.clear();
  // step through the LEDS, and update the colors as needed
  for (int i = 0; i < led_count; i++) {
    int r = 0, g = 0, b = 0;
    r += 8;
    g += 5;
    b += 2;
    if ((ms / 1000) % led_count == i) {
      r += 50;
      g += 50;
      b += 50;
    }

    if ((ms / 20) % led_count == i) {
      r += 50;
      g += 50;
      b += 50;
    }
    if ((ms / 30) % led_count == i) {
      r += 50;
    }
    if ((ms / 40) % led_count == i) {
      g += 50;
    }
    if ((ms / 35) % led_count == led_count - i) {
      r += 50;
      b += 50;
    }

    strip.setPixelColor(i, r, g, b);
  }
}

void rainbow() {
  // uses speed and cycles
  auto ms = clock_millis();

  double offset = -1.0 * speed * ms / 1000. * led_count;

  // uint16_t  offset = pixels_per_second * ms / 1000 * 0xffff / led_count;
  //  auto rand_light = rand() % led_count;
  double ratio = cycles / led_count * 0xffff;
  for (int i = 0; i < led_count; ++i) {
    uint16_t hue = fmod((i + offset) * ratio, 0xffff);  // 0-0xffff
    auto color = strip.ColorHSV(hue, saturation, brightness);
    strip.setPixelColor(i, color);
  }
}

void strobe() {
  // uses speed and cycles
  auto ms = clock_millis();
  bool on = fmod(ms * (double)speed, 1000) > 500;
  auto color = on ? strip.ColorHSV(0,0,brightness): strip.Color(0,0,0);

   for (int i = 0; i < led_count; ++i) {
    strip.setPixelColor(i, color);
  }
}

void off() { strip.clear(); }

void explosion() {
  uint32_t ms = clock_millis();
  static int16_t center_led = 0;
  static uint32_t start_millis = 0;
  static int16_t hue = 0;
  int16_t max_radius_in_leds = 10;
  int16_t explosion_ms = 1000;

  uint32_t elapsed_ms = ms - start_millis;
  if (elapsed_ms > explosion_ms) {
    center_led = rand() % led_count;
    start_millis = ms;
    hue = rand();
    max_radius_in_leds = 5 + rand() % 10;
    explosion_ms = 1000 + rand() % 1000;
  }
  uint16_t radius_in_leds =
      (uint64_t)max_radius_in_leds * elapsed_ms / explosion_ms;
  int intensity = (uint64_t)100 * (max_radius_in_leds - radius_in_leds) *
                  (max_radius_in_leds - radius_in_leds) /
                  (max_radius_in_leds * max_radius_in_leds);
  for (int i = 0; i < led_count; ++i) {
    auto distance = abs(i - center_led);
    uint32_t color = abs(distance - radius_in_leds) < 2
                         ? strip.ColorHSV(hue, 250, intensity)
                         : black;
    strip.setPixelColor(i, color);
  }
}

void repeat(std::vector<uint32_t> colors, uint16_t repeat_count = 1) {
  for (int i = 0; i < led_count; ++i) {
    auto color = colors[(i / repeat_count) % colors.size()];
    strip.setPixelColor(i, color);
  }
}

void rgb() {
  repeat({strip.Color(20, 0, 0), strip.Color(0, 20, 0), strip.Color(0, 0, 20)});
}

void trans() {
  repeat({light_blue, light_blue, pink, pink, white, white, pink, pink});
}

void usa() { repeat({red, white, blue}, 5); };

bool every_n_ms(unsigned long last_loop_ms, unsigned long loop_ms,
                unsigned long ms) {
  return (last_loop_ms % ms) + (loop_ms - last_loop_ms) >= ms;
}

void loop() {
  static unsigned long last_loop_ms = 0;
  unsigned long loop_ms = clock_millis();

  if (every_n_ms(last_loop_ms, loop_ms, 1000)) {
    Serial.print("loop ");
    Serial.println(loop_ms);
  }

  if (every_n_ms(loop_ms, last_loop_ms, 1)) {
    wifi_task.execute();
  }

  if (every_n_ms(last_loop_ms, loop_ms, 100)) {
    display.clear();
    display.drawString(0, 0, "bluetooth: " + bluetooth_device_name);
    display.drawString(0, 10,
                       bluetooth.hasClient() ? "connected" : "disconnected");
      display.drawString(0, 20, (String) "SSID: " + wifi_task.ssid);
      display.drawString(0, 30, WiFi.localIP().toString());
      
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      display.drawString(0,40,"Failed to obtain time");
    } else {
      
      char buff[80];
      sprintf(buff, "%d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      display.drawString(0, 40, buff);
    }

    display.drawString(0,50,String(clock_millis()));

    display.display();
  }

  static LineReader line_reader;
  static String last_bluetooth_line;

  // check for a new line in bluetooth
  if (line_reader.get_line(bluetooth)) {
    CmdParser parser;
    parser.parseCmd((char *)line_reader.line.c_str());
    Command *command = get_command_by_name(parser.getCommand());
    if (command) {
      CommandEnvironment env(parser, bluetooth, bluetooth);
      command->execute(env);
    } else {
      bluetooth.print("ERROR: Command not found - ");
      bluetooth.println(parser.getCommand());
    }
    last_bluetooth_line = line_reader.line;
  }

  if(every_n_ms(last_loop_ms, loop_ms, 30)) {

    if(lights_on) {
      switch (light_mode) {
        case mode_usa:
          usa();
          break;
        case mode_rainbow:
          rainbow();
          break;
        case mode_strobe:
          strobe();
          break;
        case mode_rgb:
          rgb();
          break;
        case mode_green:
          repeat({strip.Color(0, 50, 0)});
          break;
        case mode_explosion:
          explosion();
          break;
        case mode_pattern1:
          pattern1();
          break;
        case mode_color:
          repeat(current_colors);
          break;
      }
    } else {
      off();
    }
      
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
      auto &p = strands[0]->pixels[i];
      p.num = strip.getPixelColor(i);
      std::swap(p.g, p.b);
      std::swap(p.r, p.b);
    }

    digitalLeds_drawPixels(strands, STRANDCNT);
  }


  // esp_sleep_enable_timer_wakeup(30000);
  // esp_light_sleep_start();
  last_loop_ms = loop_ms;
}
