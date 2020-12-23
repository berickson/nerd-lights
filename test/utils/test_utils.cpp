#include "unity.h"
#include <iostream>
#include <light_utils.h>



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