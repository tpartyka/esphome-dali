#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome::dali_api_client {

static const char *const TAG = "dali_api_client";

class DaliApiClient : public Component {
 public:
  void set_http_request(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void set_base_url(const std::string &base_url) { this->base_url_ = base_url; }

  bool send_lamp(uint8_t address, bool group, bool on, uint8_t brightness_pct, uint16_t color_temp_mireds = 0) {
    if (this->http_ == nullptr) {
      ESP_LOGE(TAG, "No http_request component configured");
      return false;
    }
    const char *target_type = group ? "group" : "lamp";
    std::string url = this->base_url_ + "/api/lamp?target=" + target_type + ":" + std::to_string(address) +
                      "&on=" + (on ? "1" : "0") + "&brightness_pct=" + std::to_string(brightness_pct);
    if (color_temp_mireds != 0) {
      url += "&color_temp_mireds=" + std::to_string(color_temp_mireds);
    }
    const std::vector<http_request::Header> headers{{"Content-Length", "0"}};
    auto response = this->http_->post(url, "", headers);
    if (response == nullptr) {
      ESP_LOGW(TAG, "Failed to queue %s %u: no HTTP response", target_type, address);
      return false;
    }
    const bool accepted = http_request::is_success(response->status_code);
    if (!accepted) {
      ESP_LOGW(TAG, "DALI controller rejected %s %u: HTTP %d", target_type, address, response->status_code);
    }
    response->end();
    return accepted;
  }

  bool recall_scene(uint8_t scene, bool all, bool group, uint8_t address) {
    if (this->http_ == nullptr) {
      ESP_LOGE(TAG, "No http_request component configured");
      return false;
    }
    std::string target = "all";
    if (!all) {
      target = std::string(group ? "group:" : "lamp:") + std::to_string(address);
    }
    const std::string url = this->base_url_ + "/api/scene?scene=" + std::to_string(scene) + "&target=" + target +
                            "&action=recall";
    const std::vector<http_request::Header> headers{{"Content-Length", "0"}};
    auto response = this->http_->post(url, "", headers);
    if (response == nullptr) {
      ESP_LOGW(TAG, "Failed to recall scene %u: no HTTP response", scene);
      return false;
    }
    const bool accepted = http_request::is_success(response->status_code);
    if (!accepted) {
      ESP_LOGW(TAG, "DALI controller rejected scene %u: HTTP %d", scene, response->status_code);
    }
    response->end();
    return accepted;
  }

 protected:
  http_request::HttpRequestComponent *http_{nullptr};
  std::string base_url_;
};

template<typename... Ts> class DaliApiRecallSceneAction : public Action<Ts...> {
 public:
  explicit DaliApiRecallSceneAction(DaliApiClient *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint8_t, scene)
  TEMPLATABLE_VALUE(uint8_t, address)
  void set_target_all(bool value) { this->target_all_ = value; }
  void set_target_group(bool value) { this->target_group_ = value; }
  void play(const Ts &...x) override {
    this->parent_->recall_scene(this->scene_.value(x...), this->target_all_, this->target_group_, this->address_.value(x...));
  }

 protected:
  DaliApiClient *parent_;
  bool target_all_{true};
  bool target_group_{false};
};

class DaliApiLight : public light::LightOutput {
 public:
  explicit DaliApiLight(DaliApiClient *parent) : parent_(parent) {}

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->supports_color_temperature_) {
      traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
      traits.set_min_mireds(this->cold_white_temperature_);
      traits.set_max_mireds(this->warm_white_temperature_);
    } else {
      traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    }
    return traits;
  }

  void write_state(light::LightState *state) override {
    const auto &values = state->current_values;
    const auto brightness_pct = static_cast<uint8_t>(std::lround(std::clamp(values.get_brightness(), 0.0f, 1.0f) * 100.0f));
    uint16_t color_temp_mireds = 0;
    if (this->supports_color_temperature_) {
      const auto color_temp = values.get_color_temperature();
      if (color_temp > 0.0f) {
        color_temp_mireds = static_cast<uint16_t>(std::lround(color_temp));
      }
    }
    this->parent_->send_lamp(this->address_, this->group_, values.is_on(), brightness_pct, color_temp_mireds);
  }

  void set_address(uint8_t address) { this->address_ = address; }
  void set_group(bool group) { this->group_ = group; }
  void set_supports_color_temperature(bool value) { this->supports_color_temperature_ = value; }
  void set_cold_white_temperature(float value) { this->cold_white_temperature_ = value; }
  void set_warm_white_temperature(float value) { this->warm_white_temperature_ = value; }

 protected:
  DaliApiClient *parent_;
  uint8_t address_{0};
  bool group_{false};
  bool supports_color_temperature_{false};
  float cold_white_temperature_{153.0f};
  float warm_white_temperature_{370.0f};
};

}  // namespace esphome::dali_api_client
