#include <Adafruit_NeoPixel.h>
#include "math.h"
#ifdef __AVR__
  #include <avr/power.h>
#endif

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
    strip.clear();
    // for(int i = 0; i < NUMLEDS; ++i) {
      // auto color = strip.ColorHSV(500, 200, 20);
      // strip.setPixelColor(i, color);
    // }
    // strip.show();
    // return;

  uint32_t ms = millis();
  static bool started = false;
  static int16_t center;
  static uint32_t start_millis = 0;
  static int16_t hue;
  const int16_t max_radius_in_leds = 10;
  const int16_t explosion_ms = 2000;


  
  if(!started) {
      started = true;
      center = rand() % NUMLEDS;
      start_millis = ms;
      hue = rand();
      return;
  } 
  else 
  {
    uint32_t elapsed_ms = ms-start_millis;
    uint16_t radius_in_leds = (uint64_t) max_radius_in_leds * elapsed_ms / explosion_ms;
    int intensity = (uint64_t) 100 * (max_radius_in_leds - radius_in_leds) * (max_radius_in_leds - radius_in_leds) /  (max_radius_in_leds*max_radius_in_leds);
    if(elapsed_ms < explosion_ms) {
      for(int i = 0; i < NUMLEDS; ++i) {

        auto distance = abs(i-center);
        if( abs(distance - radius_in_leds)<2) {
          strip.setPixelColor(i,strip.ColorHSV(hue, 250, intensity));
        } else {
          strip.setPixelColor(i,strip.ColorHSV(5000, 0, 0));
        }
      }
    } else {
      started = false;
    }
  }
}


void loop() {
  explosion();
  //rainbow();
  //pattern1();
  strip.show();
  delay(30);
 } // loop

 
