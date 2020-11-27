#pragma once

#include "Arduino.h"

class Pushbutton {
    // debouncing:
    //   when stable, state considered changed on leading edge, but cannot change again
    //   until debounce time.  This allows a transition to register as fast as possible, 
    //   but eliminates chatter.
    private:
        int pin_;
        bool down_state_; // configure whether true or false represents the button being pressed
        bool last_reading_ = false;
        bool debounced_reading_ = false;
        bool changed_ = false;
        bool is_long_press_ = false;

        uint32_t last_transition_ms_ = 0;
        uint32_t hold_ms_ = 0;

        int long_press_ms_ = 500;
        int debounce_ms_ = 10;

        
    public:
        Pushbutton(int pin, bool down_state) {
            pin_ = pin;
            down_state_ = down_state;
            debounced_reading_ = !down_state_;
        }

        void execute(uint32_t ms) {
            bool new_reading = digitalRead(pin_);

            auto new_hold_ms = ms - last_transition_ms_;
            if(debounced_reading_ == down_state_ && new_hold_ms >= long_press_ms_ && hold_ms_ < long_press_ms_) {
                is_long_press_ = true; 
            } else {
                is_long_press_ = false;
            } 
            

            hold_ms_ = ms - last_transition_ms_;

            // find a leading edge
            if(new_reading != debounced_reading_  && (hold_ms_ > debounce_ms_)) {
                debounced_reading_ = new_reading;
                last_transition_ms_ = ms;
                changed_ = true;
            } else {
                changed_ = false;
            }

        }

        // returns true if click detected on most recent execute
        // 
        // click is when it is true for <long_press_ms_ and then goes low
        bool is_click() {
            bool rv = changed_ && debounced_reading_ != down_state_ && hold_ms_ < long_press_ms_;
            return rv;
        }

        // returns true if long click detected on most recent execute
        bool is_long_press() {
            return is_long_press_;
        }
};