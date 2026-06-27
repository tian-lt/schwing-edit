#pragma once

// std
#include <functional>
#include <optional>
#include <string>
#include <string_view>
// icu
#include <unicode/uscript.h>

namespace swg {

struct scriptrun {
  size_t byte_offset = 0;
  size_t byte_length = 0;
  size_t pos_offset = 0;
  size_t pos_length = 0;
  UScriptCode script_code = USCRIPT_INVALID_CODE;
};

class itemizer {
  struct impl;

 public:
  using sink_type = std::move_only_function<void(scriptrun)>;

  explicit itemizer(sink_type sink) : sink_(std::move(sink)) {}
  void feed(std::string_view u8str);
  void flush();
  void finish();

 private:
  sink_type sink_;
  std::string pending_;
  std::optional<scriptrun> run_;
  std::optional<UScriptCode> carry_;
  size_t byte_offset_ = 0;
  size_t pos_offset_ = 0;
};

}  // namespace swg
