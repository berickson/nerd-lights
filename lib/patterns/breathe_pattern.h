#ifndef BREATHE_PATTERN_H
#define BREATHE_PATTERN_H

#include "pattern_base.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Simple constrain function if not available
template<typename T>
inline T constrain(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(value, max_val));
}

class BreathePattern : public PatternBase {
private:
    int brightness_;        // 0-100
    Color colors_[10];      // Colors to breathe between
    int color_count_;       // Number of colors
    int duration_;          // Milliseconds per breath cycle

public:
    BreathePattern()
        : brightness_(100)
        , color_count_(2)
        , duration_(6000)
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
    
    // Rendering
    void render(Color* leds, int led_count, uint32_t time_ms) override {
        if (color_count_ < 1) return;
        
        // Calculate position in breath cycle (0.0 to 1.0)
        float cycle_pos = (float)(time_ms % duration_) / duration_;
        
        // Smooth easing using sine wave
        float ease = (sin(cycle_pos * 2.0 * PI - PI/2) + 1.0) / 2.0;
        
        // Determine which two colors to blend
        float color_float_index = ease * (color_count_ - 1);
        int color_index_1 = (int)color_float_index;
        int color_index_2 = (color_index_1 + 1) % color_count_;
        float blend = color_float_index - color_index_1;
        
        // Blend colors
        Color blended = {
            (uint8_t)(colors_[color_index_1].r * (1 - blend) + colors_[color_index_2].r * blend),
            (uint8_t)(colors_[color_index_1].g * (1 - blend) + colors_[color_index_2].g * blend),
            (uint8_t)(colors_[color_index_1].b * (1 - blend) + colors_[color_index_2].b * blend)
        };
        
        // Apply brightness with gamma correction
        float gamma_brightness = (brightness_ * brightness_) / 10000.0f;
        Color final_color = {
            (uint8_t)(blended.r * gamma_brightness),
            (uint8_t)(blended.g * gamma_brightness),
            (uint8_t)(blended.b * gamma_brightness)
        };
        
        // Fill all LEDs with same color
        for (int i = 0; i < led_count; i++) {
            leds[i] = final_color;
        }
    }
    
    // Configuration
    void set_parameter_int(const char* name, int value) override {
        if (strcmp(name, "brightness") == 0) {
            brightness_ = constrain(value, 0, 100);
        } else if (strcmp(name, "duration") == 0) {
            duration_ = constrain(value, 1000, 10000);
        }
    }
    
    void set_parameter_colors(const char* name, const Color* colors, int count) override {
        if (strcmp(name, "colors") == 0 && count > 0) {
            color_count_ = std::min(count, 10);
            memcpy(colors_, colors, color_count_ * sizeof(Color));
        }
    }
    
    void reset() override {
        // No state to reset - breathe is purely time-based
    }
};

#endif // BREATHE_PATTERN_H
