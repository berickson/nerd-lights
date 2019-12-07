#include <Adafruit_NeoPixel.h>
#include "math.h"
#ifdef __AVR__
  #include <avr/power.h>
#endif
#include <vector>

#include "Preferences.h" // save preferences to non-volatile memory


#include <BluetoothSerial.h>
#include <CmdParser.hpp>

#include "SSD1306.h"
#include "OLEDDisplay.h"


// board at https://www.amazon.com/gp/product/B07DKD79Y9
// oled
const int oled_address=0x3c;
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

float speed = 1.0;
float cycles = 1.0;


// *********************************
// ESP32 Digital LED Driver
//
#include "esp32_digital_led_lib.h"
//#include "esp32_digital_led_funcs.h"
strand_t STRANDS[] = {
  {.rmtChannel = 2, .gpioNum = pin_strand_1, .ledType = LED_WS2812B_V3, .brightLimit = 24, .numPixels =  led_count}
};
const int STRANDCNT = sizeof(STRANDS)/sizeof(STRANDS[0]);;
strand_t * strands[8];

// **********************************





//////////////////////////////////////////////////////
// Command Interface

class LineReader {
  public:
  const uint32_t buffer_size = 500;
  String buffer;
  String line;
  //BluetoothSerial bluetoothx
  bool line_available = true;

  LineReader() {
    buffer.reserve(buffer_size);
    line.reserve(buffer_size);
  }

  bool get_line(Stream & stream) {
    while(stream.available()) {
      char c = (char)stream.read();
      if(c=='\n') continue;
      if(c== '\r') {
        if(true){ //buffer.length() > 0) {
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
  CommandEnvironment(CmdParser & args, Stream & cout, Stream & cerr) 
  : args(args),cout(cout),cerr(cerr)
  {
  }
  CmdParser & args;
  Stream & cout;
  Stream & cerr;
  bool ok = true;
};

typedef void (*CommandCallback)(CommandEnvironment &env);


class Command {
public:
  Command() 
    : execute(nullptr)
  {}
  Command(const char * name, CommandCallback callback, const char * helpstring = nullptr) {
    this->name = name;
    this->execute = callback;
    this->helpstring = helpstring;
  }
  String name;
  CommandCallback execute;
  String helpstring;
};

std::vector<Command> commands;

void cmd_help(CommandEnvironment & env) {
  for(auto command : commands) {
    env.cout.print(command.name);
    env.cout.print(": ");
    env.cout.print(command.helpstring);
    env.cout.println();
  }
}

enum LightMode {
  mode_rgb,
  mode_usa,
  mode_explosion,
  mode_pattern1,
  mode_rainbow,
  mode_green,
  mode_color,
  mode_off // mode off must always be last
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
Adafruit_NeoPixel strip = Adafruit_NeoPixel(led_count, pin_strand_1, NEO_RGB + NEO_KHZ400);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.


constexpr int16_t degrees_to_hex(int32_t degrees) {
  return (degrees % 360) * 0xffff/360;
}

namespace Colors {
    uint32_t black = strip.Color(0,0,0);
    uint32_t warm_white = strip.Color(8,5,2);
    uint32_t light_blue = strip.ColorHSV(degrees_to_hex(197), 50 * 0xff/100, 20);
    uint32_t pink = strip.ColorHSV(degrees_to_hex(350), 30* 0xff/100, 20);
    uint32_t white = strip.Color(20, 23, 20);
    uint32_t red = strip.Color(20,0,0);
    uint32_t blue = strip.Color(0,0,20);
};

void cmd_name(CommandEnvironment & env) {
  if(env.args.getParamCount() != 1) {
    env.cout.printf("Failed - requires a single parameter for name");
    return;
  }
  String name = env.args.getCmdParam(1);
  /*
  for(int i = 0; i < name.length; ++i) {
    if(name[i] < 'a' || name[i] < 'z') {
      env.cout.printf("Failed - bluetooth device name must be all lowercase letters");
      return;
    }
  }
  */
  if(name.length() > 32) {
    env.cout.printf("Failed - bluetooth device name must be 32 or fewer characters");
    return;
  }
  preferences.begin("main");
  preferences.putString("bt_name", name);
  preferences.end();
  esp_restart();
}

void set_light_mode(LightMode mode) {
  light_mode = mode;
  preferences.begin("main");
  preferences.putInt("light_mode", (int) light_mode);
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

void cmd_rgb(CommandEnvironment & env) {
  set_light_mode(mode_rgb);
}

void cmd_usa(CommandEnvironment & env) {
  set_light_mode(mode_usa);
}

void cmd_explosion(CommandEnvironment & env) {
  set_light_mode(mode_explosion);
}

void cmd_pattern1(CommandEnvironment & env) {
  set_light_mode(mode_pattern1);
}

void cmd_rainbow(CommandEnvironment & env) {
  set_light_mode(mode_rainbow);
}

void cmd_off(CommandEnvironment & env) {
  set_light_mode(mode_off);
}

void cmd_green(CommandEnvironment & env) {
  set_light_mode(mode_green);
}


std::vector<uint32_t> current_colors = {strip.Color(15,15,15)};

void cmd_color(CommandEnvironment & env) {
  if(env.args.getParamCount() != 3) {
    env.cerr.printf("failed - requires three parameters");
    return;
  }

  
  uint32_t color = strip.Color(atoi(env.args.getCmdParam(1)),atoi(env.args.getCmdParam(2)),atoi(env.args.getCmdParam(3)));
  current_colors = {color};
  set_light_mode(mode_color);
}

void cmd_brightness(CommandEnvironment & env) {
  if(env.args.getParamCount() != 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  int new_brightess = atoi(env.args.getCmdParam(1));
  if(new_brightess < 1 || new_brightess > 255) {
    env.cerr.printf("failed - brightness must be between 1 and 255");
    return;
  }
  set_brightness(new_brightess);
}

void cmd_cycles(CommandEnvironment & env) {
  if(env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if(env.args.getParamCount() == 1) {
    auto new_cycles = atof(env.args.getCmdParam(1));
    if(! (new_cycles > 0.01 && new_cycles < 1000) ) {
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

void cmd_speed(CommandEnvironment & env) {
  if(env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if(env.args.getParamCount() == 1) {
    auto new_speed = atof(env.args.getCmdParam(1));
    if(! (fabs(new_speed) < 10) ) {
      env.cerr.printf("failed - cycles should be less than 10");
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

void cmd_saturation(CommandEnvironment & env) {
  if(env.args.getParamCount() != 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  int new_saturation = atoi(env.args.getCmdParam(1));
  if(new_saturation < 0 || new_saturation > 255) {
    env.cerr.printf("failed - saturation must be between 0 and 255");
    return;
  }
  set_saturation(new_saturation);
}



void cmd_add_color(CommandEnvironment & env) {
  if(env.args.getParamCount() != 3) {
    env.cerr.printf("failed - requires three parameters");
    return;
  }

  light_mode = mode_color;
  uint32_t color = strip.Color(atoi(env.args.getCmdParam(1)),atoi(env.args.getCmdParam(2)),atoi(env.args.getCmdParam(3)));
  current_colors.push_back(color);
}

void cmd_set_led_count(CommandEnvironment & env) {
  if(env.args.getParamCount() > 1) {
    env.cerr.printf("failed - requires one parameter");
    return;
  }
  if(env.args.getParamCount() == 1) {
    auto v = atoi(env.args.getCmdParam(1));
    if(v < 1) {
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



void cmd_next(CommandEnvironment & env) {
  LightMode mode = (LightMode) (light_mode + 1);
  if(mode > mode_off) {
    mode = (LightMode) 0;
  }
  set_light_mode(mode);
}

void cmd_previous(CommandEnvironment & env) {
  LightMode mode = (light_mode==0) ? light_mode = mode_off : (LightMode)(light_mode - 1);
  set_light_mode(mode);
}



Command * get_command_by_name(const char * command_name) {
  for(auto & command : commands) {
    if(command.name == command_name) {
      return &command;
    }
  }
  return nullptr;
}



using namespace Colors;

void setup() {
  // read preferences
  preferences.begin("main");
  led_count = preferences.getInt("led_count", 50);
  light_mode = (LightMode) preferences.getInt("light_mode", mode_rainbow);
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
  commands.emplace_back(Command{"help", cmd_help, "displays list of available commands"});
  commands.emplace_back(Command{"usa", cmd_usa, "red white and blue"});
  commands.emplace_back(Command{"rgb", cmd_rgb, "red green and blue lights"});
  commands.emplace_back(Command{"explosion", cmd_explosion, "colored explosions"});
  commands.emplace_back(Command{"pattern1", cmd_pattern1, "lights chase at different rates"});
  commands.emplace_back(Command{"rainbow", cmd_rainbow, "rainbow road"});
  commands.emplace_back(Command{"saturation", cmd_saturation, "set saturation 0-255 for rainbow effect"});
  commands.emplace_back(Command{"brightness", cmd_brightness, "set brightness 1-255 for rainbow effect"});
  commands.emplace_back(Command{"name", cmd_name, "set bluetooth device name"});
  commands.emplace_back(Command{"cycles", cmd_cycles, "the number of times the current pattern will fit on the light strand, if less than one, it will only fit part of the pattern"});
  commands.emplace_back(Command{"speed", cmd_speed, "speed of effect,  1.0 would mean moving entire strip once per second, 2.0 would do it twice per second"});
 


  commands.emplace_back(Command{"green", cmd_green, "solid green color"});
  commands.emplace_back(Command("color", cmd_color, "solid {red} {blue} {green}"));
  commands.emplace_back(Command("add", cmd_add_color, "adds a color to current pallet {red} {blue} {green}"));

  commands.emplace_back(Command("ledcount", cmd_set_led_count, "sets the total number of leds on the strand"));

  
  commands.emplace_back(Command{"next", cmd_next, "cycles to the next mode"});
  commands.emplace_back(Command{"previous", cmd_previous, "cycles to the previous mode"});

  commands.emplace_back(Command{"off", cmd_off, "lights turn off, device is still running"});


  // setup the LED strand
  //strip.begin();
  //strip.show(); // Initialize all pixels to 'off'
  
  digitalLeds_initDriver();
  for (int i = 0; i < STRANDCNT; i++) {
    gpioSetup(STRANDS[i].gpioNum, OUTPUT, LOW);
    strands[i] = &STRANDS[i];
  }
  digitalLeds_addStrands(strands, STRANDCNT);

} // setup




void pattern1() {
   auto ms = millis();
  //strip.clear();
  // step through the LEDS, and update the colors as needed
  for (int i=0; i<led_count; i++) {
    int r=0,g=0,b=0;
    r+=8;g+=5;b+=2;
    if((ms/1000)%led_count==i) {
      r+=50;
      g+=50;
      b+=50;
    }

    
    if((ms/20)%led_count ==i) {
      r+=50;
      g+=50;
      b+=50;
    }
    if((ms/30)%led_count ==i) {
      r+=50;
    }
    if((ms/40)%led_count ==i) {
      g+=50;
    }
    if((ms/35)%led_count ==led_count-i) {
      r+=50;
      b+=50;
    }
    
    strip.setPixelColor(i,r,g,b);
  }
}


void rainbow() {

  // uses speed and cycles
  auto ms = millis();

  float offset = -1.0 * speed * ms/ 1000. * led_count;

  // uint16_t  offset = pixels_per_second * ms / 1000 * 0xffff / led_count;
//  auto rand_light = rand() % led_count;
  float ratio = cycles / led_count * 0xffff;
  for(int i = 0; i < led_count; ++i) {
    uint16_t hue = (i + offset) * ratio; // 0-0xffff
    auto color = strip.ColorHSV(hue, saturation, brightness);
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void off() {
  strip.clear();
}

void explosion() {
  uint32_t ms = millis();
  static int16_t center_led=0;
  static uint32_t start_millis = 0;
  static int16_t hue=0;
  int16_t max_radius_in_leds = 10;
  int16_t explosion_ms = 1000;
  
  uint32_t elapsed_ms = ms-start_millis;
  if(elapsed_ms > explosion_ms) {
      center_led = rand() % led_count;
      start_millis = ms;
      hue = rand();
      max_radius_in_leds = 5 + rand()%10;
      explosion_ms = 1000 + rand()%1000;
  } 
  uint16_t radius_in_leds = (uint64_t) max_radius_in_leds * elapsed_ms / explosion_ms;
  int intensity = (uint64_t) 100 * (max_radius_in_leds - radius_in_leds) * (max_radius_in_leds - radius_in_leds) /  (max_radius_in_leds*max_radius_in_leds);
  for(int i = 0; i < led_count; ++i) {

    auto distance = abs(i-center_led);
    uint32_t color = abs(distance - radius_in_leds)<2 ? strip.ColorHSV(hue, 250, intensity) : black;
    strip.setPixelColor(i, color);
  }
}

void repeat(std::vector<uint32_t> colors, uint16_t repeat_count = 1) {
  for(int i = 0; i < led_count; ++i) {
    auto color = colors[(i/repeat_count)%colors.size()];
    strip.setPixelColor(i, color);
  }
}

void rgb() {
  repeat({strip.Color(20,0,0), strip.Color(0,20,0), strip.Color(0,0,20)});
}

void trans() {
  repeat({light_blue, light_blue, pink, pink, white, white, pink, pink});
}

void usa() {
  repeat({red, white, blue},5);
};


bool every_n_ms(unsigned long last_loop_ms, unsigned long loop_ms, unsigned long ms) {
  return (last_loop_ms % ms) + (loop_ms - last_loop_ms) >= ms;
}

void loop() {
  static unsigned long last_loop_ms = 0;
  unsigned long loop_ms = millis();

  if(every_n_ms(last_loop_ms, loop_ms, 1000)) {
    Serial.print("loop ");
    Serial.println(loop_ms);
  }

  if(every_n_ms(last_loop_ms, loop_ms, 100)) {
    display.clear();
    display.drawString(0, 0, "bluetooth: "  + bluetooth_device_name);
    display.drawString(0,10,bluetooth.hasClient() ? "connected" : "disconnected");

    //display.drawString(0, 10, "line2!");
    display.display();
  }
  
  static LineReader line_reader;
  static String last_bluetooth_line;

  // check for a new line in bluetooth
  if(line_reader.get_line(bluetooth)) {
    CmdParser parser;
    parser.parseCmd((char *)line_reader.line.c_str());
    Command * command = get_command_by_name(parser.getCommand());
    if(command) {
      CommandEnvironment env(parser, bluetooth, bluetooth);
      command->execute(env);
    } else {
      bluetooth.print("ERROR: Command not found - ");
      bluetooth.println(parser.getCommand());
    }
    last_bluetooth_line = line_reader.line;
  }

  switch(light_mode) {
    case mode_usa:
      usa();
      break;
    case mode_rainbow:
      rainbow();
      break;
    case mode_rgb:
      rgb();
      break;
    case mode_green:
      repeat({strip.Color(0,50,0)});
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

    case mode_off:
      off();
      break;
  }
  
  //trans();
  //rainbow();
  //usa();
  //pattern1();
  //rgb();
  //repeat({strip.Color(0,0,50)});
  //repeat({strip.Color(18,0,22)});// purple 0.090A
  //repeat({strip.Color(20,10,0), strip.Color(25,16,0),strip.Color(14,5,2)});// orange/brown?
  //repeat({strip.Color(20,0,0), strip.Color(0,20,0)});// red/green 0.063A
  //repeat({strip.Color(255,255,255)}); // bright white, 2.123A / 50LEDS
  // explosion();

  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    auto & p = strands[0]->pixels[i];
    p.num = strip.getPixelColor(i);
    std::swap(p.g,p.b);
    std::swap(p.r,p.b);
    
  }

  digitalLeds_drawPixels(strands, STRANDCNT);
  
  delay(light_mode == mode_off ? 2000 : 20);
  //esp_sleep_enable_timer_wakeup(30000);
  //esp_light_sleep_start();
  last_loop_ms = loop_ms;
 }

 
