// std
#include <bit>
#include <cstdint>
// icu
#include <unicode/uchar.h>
// swg
#include "itemizer.hpp"

namespace swg {

namespace {

size_t u8char_len(unsigned char leading) {
  int ones = std::countl_one(leading);
  // 0 leading ones => ASCII (1 byte). 2..4 leading ones => multi-byte length.
  // 1 leading one is a continuation byte and >4 is invalid as a leading byte.
  if (ones == 0) {
    return 1;
  }
  return (ones >= 2 && ones <= 4) ? static_cast<size_t>(ones) : 0;
}

}  // namespace

struct itemizer::impl {
  static void consume(itemizer* self, UChar32 uch, size_t bytes) {
    size_t byte_offset = self->byte_offset_;
    size_t pos_offset = self->pos_offset_;
    self->byte_offset_ += bytes;
    self->pos_offset_ += 1;
    UErrorCode ec = U_ZERO_ERROR;
    UScriptCode sc = uscript_getScript(uch, &ec);
    if (U_FAILURE(ec)) {
      sc = USCRIPT_UNKNOWN;
    }
    if (u_hasBinaryProperty(uch, UCHAR_EMOJI_PRESENTATION)) {
      sc = USCRIPT_SYMBOLS_EMOJI;
    }
    if (!self->run_.has_value()) {
      if ((sc == USCRIPT_COMMON || sc == USCRIPT_INHERITED) && self->carry_.has_value()) {
        sc = *self->carry_;
      }
      self->carry_.reset();
      self->run_.emplace(scriptrun{byte_offset, bytes, pos_offset, 1, sc});
      return;
    }
    if (sc == USCRIPT_COMMON || sc == USCRIPT_INHERITED) {
      self->run_->byte_length += bytes;
      self->run_->pos_length += 1;
      return;
    }
    if (self->run_->script_code == USCRIPT_COMMON || self->run_->script_code == USCRIPT_INHERITED) {
      self->run_->script_code = sc;
      self->run_->byte_length += bytes;
      self->run_->pos_length += 1;
      return;
    }
    if (self->run_->script_code == sc) {
      self->run_->byte_length += bytes;
      self->run_->pos_length += 1;
      return;
    }
    flush(self);
    self->run_.emplace(scriptrun{byte_offset, bytes, pos_offset, 1, sc});
  }
  static void flush(itemizer* self) {
    if (self->run_.has_value()) {
      self->sink_(*self->run_);
      self->run_.reset();
    }
  }
};

void itemizer::feed(std::string_view u8str) {
  pending_.append(u8str);
  size_t i = 0;
  while (i < pending_.length()) {
    size_t chlen = u8char_len(static_cast<unsigned char>(pending_[i]));
    if (chlen == 0) {
      // invalid utf-8 character, fallback to 1 byte placeholder character.
      impl::consume(this, 0xfffd, 1);
      ++i;
      continue;
    }
    if (i + chlen > pending_.length()) {
      break;
    }
    UChar32 uch;
    int32_t j = static_cast<int32_t>(i);
    U8_NEXT(pending_.data(), j, pending_.length(), uch);
    if (uch < 0) {
      uch = 0xfffd;  // fallback to 1 byte placeholder character.
    }
    impl::consume(this, uch, j - i);
    i = j;
  }
  pending_.erase(0, i);
}

void itemizer::flush() {
  // split a long run for early rendering while preserving its script as context for the next run
  if (run_.has_value() && run_->script_code != USCRIPT_COMMON &&
      run_->script_code != USCRIPT_INHERITED) {
    carry_ = run_->script_code;
    impl::flush(this);
  }
}

void itemizer::finish() {
  if (run_.has_value() && run_->script_code != USCRIPT_COMMON &&
      run_->script_code != USCRIPT_INHERITED) {
    carry_ = run_->script_code;
  }
  impl::flush(this);
  pending_.clear();
  run_.reset();
}

}  // namespace swg
