#include "math.h"

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

    float shift_left_ = 0.0;
    int segment_count_ = 0;
    int led_count_=0;
    float stretch_ = 1.0;

    uint32_t ms_;

    SegmentPercent segment_percent(int led) {
        SegmentPercent x;
        //std::cout<< "led: " << led << "segment_count_: " << segment_count_ << "led_count_: " << led_count_ << std::endl;
        float p = (led_count_ > 1)?((stretch_*led+shift_left_)*segment_count_)/(led_count_) : 0;


        int fp = floor(p);
        x.segment = fp % segment_count_;
        x.percent = p - fp;

        return x;
    }
};
