#include <Adafruit_NeoPixel.h>
#include "math.h"
#ifdef __AVR__
  #include <avr/power.h>
#endif
#include <vector>

// Neopixel constants
#define PIN 15        // Arduino pin for the LED strand data line
#define NUMLEDS 50  // number of LEDs total


// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMLEDS, PIN, NEO_RGB + NEO_KHZ400);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

struct Colors {
    uint32_t black = strip.Color(0,0,0);
} PresetColors;


void setup() {
  // setup the LED strand
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
} // setup


void pattern1() {
   auto ms = millis();
  //strip.clear();
  // step through the LEDS, and update the colors as needed
  for (int i=0; i<NUMLEDS; i++) {
    int r=0,g=0,b=0;
    r+=8;g+=5;b+=2;
    if((ms/1000)%NUMLEDS==i) {
      r+=50;
      g+=50;
      b+=50;
    }

    
    if((ms/20)%NUMLEDS ==i) {
      r+=50;
      g+=50;
      b+=50;
    }
    if((ms/30)%NUMLEDS ==i) {
      r+=50;
    }
    if((ms/40)%NUMLEDS ==i) {
      g+=50;
    }
    if((ms/35)%NUMLEDS ==NUMLEDS-i) {
      r+=50;
      b+=50;
    }
    
    strip.setPixelColor(i,r,g,b);
  }
}


void rainbow(int pixels_per_second = 30) {

  auto ms = millis();

  uint16_t  offset = pixels_per_second * ms / 1000 * 0xffff / NUMLEDS;
//  auto rand_light = rand() % NUMLEDS;
  for(int i = 0; i < NUMLEDS; ++i) {
    uint16_t hue = 2*i*(0xffff/NUMLEDS)+offset; // 0-0xffff
    uint8_t saturation = 200;
    uint8_t value = 20;
    // if(i==rand_light) {
    //   value = 100;
    // }
    auto color = strip.ColorHSV(hue, saturation, value);
    strip.setPixelColor(i, color);
  }
  strip.show();
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
      center_led = rand() % NUMLEDS;
      start_millis = ms;
      hue = rand();
      max_radius_in_leds = 5 + rand()%10;
      explosion_ms = 500 + rand()%1000;
  } 
  uint16_t radius_in_leds = (uint64_t) max_radius_in_leds * elapsed_ms / explosion_ms;
  int intensity = (uint64_t) 100 * (max_radius_in_leds - radius_in_leds) * (max_radius_in_leds - radius_in_leds) /  (max_radius_in_leds*max_radius_in_leds);
  for(int i = 0; i < NUMLEDS; ++i) {

    auto distance = abs(i-center_led);
    uint32_t color = abs(distance - radius_in_leds)<2 ? strip.ColorHSV(hue, 250, intensity) : PresetColors.black;
    strip.setPixelColor(i, color);
  }
}



constexpr int16_t degrees_to_hex(int32_t degrees) {
  return (degrees % 360) * 0xffff/360;
}


void repeat(std::vector<uint32_t> colors) {
  for(int i = 0; i < NUMLEDS; ++i) {
    auto color = colors[i%colors.size()];
    strip.setPixelColor(i, color);
  }
}

void rgb() {
  repeat({strip.Color(20,0,0), strip.Color(0,20,0), strip.Color(0,0,20)});
}

void trans() {
  auto black = strip.Color(0,0,0);
  auto  light_blue = strip.ColorHSV(degrees_to_hex(197), 50 * 0xff/100, 20);
  auto  pink = strip.ColorHSV(degrees_to_hex(350), 30* 0xff/100, 20);
  auto  white = strip.Color(20, 23, 20);

  repeat({light_blue, light_blue, pink, pink, white, white, pink, pink});
}

void usa() {
  auto  white = strip.Color(20, 23, 20);
  auto red = strip.Color(20,0,0);
  auto blue = strip.Color(0,0,20);
  repeat({red, white, blue});
};


void loop() {
  //trans();
  //rainbow();
  //usa();
  //pattern1();
  //rgb();
  //repeat({strip.Color(18,0,22)});// purple 0.090A
  //repeat({strip.Color(20,10,0), strip.Color(25,16,0),strip.Color(14,5,2)});// orange/brown?
  //repeat({strip.Color(20,0,0), strip.Color(0,20,0)});// red/green 0.063A
  //repeat({strip.Color(255,255,255)}); // bright white, 2.123A / 50LEDS

  explosion();
  delay(1); // for some reason, this gets rid of the spurious pixels being displayed on the string
  portDISABLE_INTERRUPTS();
  //auto state = portENTER_CRITICAL_NESTED();
  strip.show();
  //portEXIT_CRITICAL_NESTED(state);
  portENABLE_INTERRUPTS();
  delay(29);
 }

 
