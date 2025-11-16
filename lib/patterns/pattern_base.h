#ifndef PATTERN_BASE_H
#define PATTERN_BASE_H

#include <vector>
#include <cstdint>

// Forward declaration
struct Color {
    uint8_t r, g, b;
};

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

#endif // PATTERN_BASE_H
