#include "esp32-common.h"

#include <Adafruit_NeoPixel.h>
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
template< typename T, size_t N >
inline size_t dim( T (&arr)[N] ) { return N; }


const int pin_strand_1 = 2;
int led_count = 500;


// globals
String device_name="nerdlights";
bool lights_on = true; // true if lights are currently turned on
bool use_ap_mode = false; // todo: change this depending on whether we can connect



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

strand_t *strands[8];


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



////////////////////////////////
// define commands
////////////////////////////////

void cmd_status(CommandEnvironment & env) {
  env.cout.print("Device name: ");
  env.cout.println(device_name);
  env.cout.print("The lights are: ");
  env.cout.println(lights_on ? "ON" : "OFF");
  env.cout.println("SSID: "+ wifi_task.ssid);

  //env.cout.println("IP Address: " + WiFi.localIP().toString());
  env.cout.println("IP Address: " + WiFi.softAPIP().toString());
  env.cout.print("ledcount: ");
  env.cout.println(led_count);
  env.cout.print("free bytes: ");
  env.cout.println(ESP.getFreeHeap());
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

std::vector<uint32_t> unscaled_colors = {strip.Color(15, 15, 15)};
std::vector<uint32_t> current_colors = {strip.Color(15, 15, 15)};

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

inline uint8_t mul_div(uint8_t number, uint8_t numerator, uint8_t denominator) {
    int32_t ret = number;
    ret *= numerator;
    ret /= denominator;
    return (uint8_t) ret;
}

void calculate_scaled_colors() {
  uint8_t max_c = 0;
  // calculate max of all components
  pixelColor_t p;
  for(auto & c : unscaled_colors){
    p.num = c;

    max_c = max(max_c, p.r);
    max_c = max(max_c, p.g);
    max_c = max(max_c, p.b);
  }

  current_colors.clear();
  for(auto & c : unscaled_colors){
    p.num = c;
    if(max_c > 0) {
      p.r = mul_div(p.r, brightness, max_c);
      p.g = mul_div(p.g, brightness, max_c);
      p.b = mul_div(p.b, brightness, max_c);
    }
    current_colors.push_back(p.num);
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


void cmd_explosion(CommandEnvironment &env) { set_light_mode(mode_explosion); }
void cmd_pattern1(CommandEnvironment &env) { set_light_mode(mode_pattern1); }
void cmd_gradient(CommandEnvironment &env) { set_light_mode(mode_gradient); }
void cmd_rainbow(CommandEnvironment &env) { set_light_mode(mode_rainbow); }
void cmd_strobe(CommandEnvironment &env) { set_light_mode(mode_strobe); }
void cmd_twinkle(CommandEnvironment &env) { set_light_mode(mode_twinkle); }
void cmd_normal(CommandEnvironment &env) { set_light_mode(mode_color); }
void cmd_off(CommandEnvironment &env) { lights_on = false; }
void cmd_on(CommandEnvironment &env) { lights_on = true; }



void add_color(uint32_t color) {
  unscaled_colors.push_back(color);
  uint16_t color_index = unscaled_colors.size()-1; 
  String key = String("color")+color_index;

  preferences.begin("main");
  preferences.putUInt("color_count", unscaled_colors.size());
  preferences.putUInt(key.c_str(), unscaled_colors[color_index]);
  preferences.end();
  calculate_scaled_colors();
}

void cmd_color(CommandEnvironment &env) {
  if (env.args.getParamCount() != 3) {
    env.cerr.printf("failed - requires three parameters");
    return;
  }

  uint32_t color =
      strip.Color(atoi(env.args.getCmdParam(1)), atoi(env.args.getCmdParam(2)),
                  atoi(env.args.getCmdParam(3)));
  unscaled_colors.clear();
  add_color(color);
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
    if (!(fabs(new_speed) <= 10)) {
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

  uint32_t color =
      strip.Color(atoi(env.args.getCmdParam(1)), atoi(env.args.getCmdParam(2)),
                  atoi(env.args.getCmdParam(3)));
  add_color(color);
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

using namespace Colors;

void setup() {
  esp32_common_setup();
  // read preferences
  preferences.begin("main");

  led_count = preferences.getInt("led_count", 50);
  light_mode = (LightMode)preferences.getInt("light_mode", mode_rainbow);
  saturation = preferences.getInt("saturation", 200);
  brightness = preferences.getInt("brightness", 30);
  speed = preferences.getFloat("speed", 1.0);
  cycles = preferences.getFloat("cycles", 1.0);
  device_name = preferences.getString("bt_name", "ledlights");

  current_colors.clear();
  auto color_count = preferences.getUInt("color_count", 1);
  Serial.println((String)"color_count:" + color_count);
  for(int i=0;i<color_count; ++i) {
    String key = (String)"color"+i;
    uint32_t color = preferences.getUInt(key.c_str(), 0x300000);
    current_colors.push_back(color);
    Serial.print(key+": "+color);
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
  commands.emplace_back(Command{"normal", cmd_normal, "colors are repeated through the strand"});
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
  commands.emplace_back(
      Command{"previous", cmd_previous, "cycles to the previous mode"});
  commands.emplace_back(
      Command{"off", cmd_off, "turn lights off, device is still running"});
  commands.emplace_back(
      Command{"on", cmd_on, "turn lights on"});

  digitalLeds_initDriver();
  for (int i = 0; i < STRANDCNT; i++) {
    gpioSetup(STRANDS[i].gpioNum, OUTPUT, LOW);
    strands[i] = &STRANDS[i];
  }
  digitalLeds_addStrands(strands, STRANDCNT);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
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

void gradient(bool is_tree = true) {
  size_t n_colors =  current_colors.size();
  int divisions = n_colors < 2 ? 1: n_colors -1;
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
      division_end = led_count * is_tree ? percent_lights_for_percent_up_tree(percent) : percent;
    }
    uint32_t color_a = current_colors[(division-1)%n_colors]; // modulus might be unnecessary, but prevents overflow
    uint32_t color_b = current_colors[(division)%n_colors];
    double part_b = (i-division_start)/(division_end-division_start);
    uint32_t color = mix_colors(color_a, color_b, part_b);
    strip.setPixelColor(i, color);
  }
}

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
  
  bool multi = current_colors.size() >= 2;
  
  int n  = floor( (ms * (double)speed) / 1000.0);

  uint32_t color = black;
  if(multi) {
    color = current_colors[n%current_colors.size()];
  } else {
    color = (n%2==0)?black:current_colors[0];
  }

   for (int i = 0; i < led_count; ++i) {
    strip.setPixelColor(i, color);
  }
}

uint32_t color_at_brightness( uint32_t color , uint8_t new_brightness) {
  pixelColor_t c;
  c.num = color;
  uint32_t max_c = max<uint8_t>(max<uint8_t>(c.r,c.g),c.b);
  if(max_c > 0) {
    c.r = mul_div(c.r, new_brightness, max_c);
    c.g = mul_div(c.g, new_brightness, max_c);
    c.b = mul_div(c.b, new_brightness, max_c);
  }
  return c.num;
}

void twinkle() {
  bool trace = false;
  if(trace) Serial.println("start twinkle");
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

  if(trace) Serial.println("a");

  bool multi = current_colors.size() >= 2;
  uint32_t base_color = multi ? current_colors[0] : black;

  for(int i = 0; i < led_count; ++i) {
    strip.setPixelColor(i, base_color);
  }
  
  // remove done LEDS
  while(blinking_leds.size() > 0 && blinking_leds.front().done_ms <= ms) {
    strip.setPixelColor( blinking_leds.front().led_number, base_color );
    blinking_leds.pop_front();
  }

  if(trace) Serial.println("b");

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

  if(trace) Serial.println("c");

  for(auto & led : blinking_leds) {
    // time from midpoint peak
    auto ms_peak = led.done_ms - twinkle_ms/2.;
    auto from_peak = abs(ms-ms_peak);
    auto level = 1. - from_peak / (twinkle_ms/2);
 

    uint32_t color = mix_colors(base_color, led.twinkle_color, level*level);
    
    strip.setPixelColor( led.led_number, color);
  }
  if(trace) Serial.println("done twinkle");
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
  int intensity = (uint64_t)brightness * (max_radius_in_leds - radius_in_leds) *
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

void stripes(std::vector<uint32_t> colors, bool is_tree = true) {
  if(led_count==0) {
    return;
  }
  size_t n_color = 0;
  double ratio_end = (n_color+1.0) / colors.size();
  double led_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(ratio_end) : ratio_end);
  for (int i = 0; i < led_count; ++i) {
    if(i > led_end) {
      ++n_color;
      ratio_end = (n_color+1.0) / colors.size();
      led_end = led_count * (is_tree ? percent_lights_for_percent_up_tree(ratio_end) : ratio_end);
    }
    auto color = colors[clamp<size_t>(n_color,0,colors.size()-1)];
    strip.setPixelColor(i, color);
  }
}

void repeat(std::vector<uint32_t> colors, uint16_t repeat_count = 1) {
  for (int i = 0; i < led_count; ++i) {
    auto color = colors[(i / repeat_count) % colors.size()];
    strip.setPixelColor(i, color);
  }
}

void loop() {
  static unsigned long loop_count = 0;
  ++loop_count;
  static unsigned long last_loop_ms = 0;
  unsigned long loop_ms = clock_millis();


  // if (every_n_ms(last_loop_ms, loop_ms, 1000)) {
  //   Serial.print("bytes free: ");
  //   Serial.println(ESP.getFreeHeap());
  // }

  if (every_n_ms(loop_ms, last_loop_ms, 1)) {
    wifi_task.execute();
  }
  

  if (every_n_ms(last_loop_ms, loop_ms, 100)) {
    display.clear();
    display.drawString(0, 0, "name: " + device_name);
    display.drawString(0, 10, (String) "SSID: " + wifi_task.ssid);

    IPAddress ip_address = use_ap_mode ? WiFi.softAPIP() : WiFi.localIP();
    display.drawString(0, 20, ip_address.toString());
      
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      display.drawString(0,40,"Failed to obtain time");
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
          stripes(current_colors);
          break;
        case mode_twinkle:
          twinkle();
          break;
        case mode_explosion:
          explosion();
          break;
        case mode_gradient:
          gradient();
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

  delay(10);

  last_loop_ms = loop_ms;
}

