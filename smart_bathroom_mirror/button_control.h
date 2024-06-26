#include "esphome.h"

const uint16_t I2C_ADDRESS = 0x78;
const uint16_t POLLING_PERIOD = 50; //milliseconds
const uint16_t NUM_READ_BYTES = 0x2;

const char HEATER_BUTTON_MASK  = 0x1;
const char LIGHT_BUTTON_MASK   = 0x2;

enum button_states {
  BOTH_BUTTONS_OFF = 0x55,
  HEATER_BUTTON_ON = 0x45,
  LIGHT_BUTTON_ON  = 0x15,
  BOTH_BUTTONS_ON  = 0x05
};

enum button_states button_led_state     = BOTH_BUTTONS_OFF;
enum button_states button_led_state_old = BOTH_BUTTONS_OFF;

char button_action   = 0x00;
char button_checksum = 0xff;

uint16_t heater_press_count = 0;
uint16_t light_press_count  = 0;

bool light_state      = false;
bool heater_state     = false;

class ButtonControl : public PollingComponent {
  public:
    ButtonControl() : PollingComponent(POLLING_PERIOD) {}
    float get_setup_priority() const override { return esphome::setup_priority::BUS; } //Access I2C bus

    void setup() override {
      // Initialize the indication led of the push buttons to off
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(BOTH_BUTTONS_OFF);
      Wire.write(~BOTH_BUTTONS_OFF); // Checksum
      Wire.endTransmission();
    }

    void update() override {
      bool light_state  = id(mirror_light).current_values.is_on();
      bool heater_state = id(mirror_heater).state;

      if (light_state && heater_state) {
        button_led_state = BOTH_BUTTONS_ON;
        
      } else if (light_state) {
        button_led_state = LIGHT_BUTTON_ON;

      } else if (heater_state) {
        button_led_state = HEATER_BUTTON_ON;

      } else {
        button_led_state = BOTH_BUTTONS_OFF;
      }

      //Did the user change the input?
      if (button_led_state != button_led_state_old) {
        ESP_LOGD("MIRROR_BUTTON", "A button was pressed. Updating light indicator with value: 0x%x", button_led_state);
        Wire.beginTransmission(I2C_ADDRESS);
        Wire.write(button_led_state);
        Wire.write(~button_led_state); // Checksum
        Wire.endTransmission();
        button_led_state_old = button_led_state; //Swap in the new value
      }

      Wire.requestFrom(I2C_ADDRESS, NUM_READ_BYTES);

      if (Wire.available() != NUM_READ_BYTES) {
        ESP_LOGW("I2C Read", "Incorrect number of bytes read. Expected: %d, received: %d", NUM_READ_BYTES, Wire.available());
        // Slave send more or less bytes than expected. Clear the buffer
        while (Wire.available()) {
          Wire.read();
        }
        return;
      }

      button_action   = Wire.read();
      button_checksum = Wire.read();

      // Validate the checksum
      if (button_action != (~button_checksum & 0xff)) {
        ESP_LOGW("I2C Read", "Incorrect checksum. Received data: 0x%x, received checksum: 0x%x, inverted checksum: 0x%x", button_action, button_checksum, ~button_checksum);
        return;
      }

      if (button_action != 0) {
        ESP_LOGD("I2C Read", "Button pressed. Button value is: 0x%x", button_action);
      }

      if (button_action & HEATER_BUTTON_MASK) {
        heater_press_count += 1;
      } else {
        if (heater_press_count >= 2) {
          id(mirror_heater).toggle();
        }
        heater_press_count = 0;
      }

      if (button_action & LIGHT_BUTTON_MASK) {
        light_press_count += 1;
      } else {
        if (light_press_count >= 2 && light_press_count < 20) {
          auto call_light = id(mirror_light).toggle();
          call_light.perform();
        }
        light_press_count = 0;
      }
    }
};