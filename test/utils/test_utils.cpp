#include "unity.h"
#include <iostream>

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


void test_repeating_pattern() {
    TEST_MESSAGE("testing_repeating_pattern");

    for (float shift_left : {0}) {
        for (float stretch : {1.0}) {
            for(auto segment_count : {10}) {
                for(auto led_count : {100}) {
                    RepeatingPattern r(led_count, segment_count, stretch, shift_left);
                    std::cout 
                      << "leds: " << led_count 
                      << " segments: " << segment_count 
                      << " stretch: " << stretch 
                      << " shift_left: " << shift_left 
                      << std::endl;
                
                    for(int i=0; i<led_count; ++i) {
                        auto x = r.segment_percent(i);
                        std::cout << "i: " << i << " segment: " << x.segment << " percent:" << x.percent << std::endl;
                        TEST_ASSERT_TRUE(x.segment>=0);
                        TEST_ASSERT_TRUE(x.segment<segment_count);
                        TEST_ASSERT_TRUE(x.percent>=0.0);
                        TEST_ASSERT_TRUE(x.percent<=1.0);
                    }
                }
            }
        }
    }
}


int main() {
    UNITY_BEGIN();
    RUN_TEST(test_repeating_pattern);
    UNITY_END();
    return 0;
}