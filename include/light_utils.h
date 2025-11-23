#include "math.h"
#include "stdint.h"

struct SegmentPercent {
    int segment = 0;
    float percent = 0.0;
};

class RepeatingPattern {
private:
public:
    
    void init(int led_count, int segment_count, float stretch, float shift_left) {
        led_count_ = led_count;
        segment_count_ = segment_count;
        stretch_ = stretch;
        shift_left_ = shift_left;
    }

    RepeatingPattern(int led_count, int segment_count, float stretch, float shift_left) {
        init(led_count, segment_count, stretch, shift_left);
    }

    double shift_left_ = 0.0;
    int segment_count_ = 0;
    int led_count_=0;
    double stretch_ = 1.0;

    uint32_t ms_;

    SegmentPercent segment_percent(int led) {
        SegmentPercent x;
        //std::cout<< "led: " << led << "segment_count_: " << segment_count_ << "led_count_: " << led_count_ << std::endl;
        
        // Normalize shift to prevent precision loss with large time values
        // Work in segment units, not pixel units, to maintain color alignment
        double shift_in_segments = (shift_left_ * segment_count_) / led_count_;
        double normalized_shift_segments = fmod(shift_in_segments, segment_count_);
        if (normalized_shift_segments < 0) normalized_shift_segments += segment_count_;
        double normalized_shift = (normalized_shift_segments * led_count_) / segment_count_;
        
        double p = (led_count_ > 1)?((stretch_*led + normalized_shift)*segment_count_)/(led_count_) : 0;

        double fp = floor(p);
        x.segment = int(fp) % segment_count_;
        if(x.segment < 0) {
            x.segment += segment_count_;
        }
        x.percent = p - fp;

        return x;
    }
};
