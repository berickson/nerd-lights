#ifndef SOLID_PATTERN_H
#define SOLID_PATTERN_H

#include "pattern_base.h"
#include <cstring>
#include <algorithm>

// Simple constrain function if not available
template<typename T>
inline T constrain(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(value, max_val));
}

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
    
    // Rendering
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
        
        // Fill LEDs with repeating color pattern
        for (int i = 0; i < led_count; i++) {
            int color_index = (i / spacing_) % color_count_;
            leds[i] = apply_brightness(colors_[color_index]);
        }
    }
    
    // Configuration
    void set_parameter_int(const char* name, int value) override {
        if (strcmp(name, "brightness") == 0) {
            brightness_ = constrain(value, 0, 100);
        } else if (strcmp(name, "spacing") == 0) {
            spacing_ = constrain(value, 1, 100);
        }
    }
    
    void set_parameter_colors(const char* name, const Color* colors, int count) override {
        if (strcmp(name, "colors") == 0 && count > 0) {
            color_count_ = std::min(count, 10);
            memcpy(colors_, colors, color_count_ * sizeof(Color));
        }
    }
    
    void reset() override {
        // No state to reset for solid pattern
    }
};

#endif // SOLID_PATTERN_H
