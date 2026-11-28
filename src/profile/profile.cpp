#include <profile/profile.hpp>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace snes2dhd::profile {
namespace {

constexpr std::size_t MaxJsonDepth = 64;
constexpr std::uint32_t MaxWramAddr = 0x20000;

auto is_hex_digit(char c) -> bool {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

auto hex_value(char c) -> std::uint32_t {
  if(c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
  if(c >= 'a' && c <= 'f') return static_cast<std::uint32_t>(c - 'a' + 10);
  return static_cast<std::uint32_t>(c - 'A' + 10);
}

auto ascii_lower(char c) -> char {
  if(c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

auto to_lower_ascii(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for(char c : s) out.push_back(ascii_lower(c));
  return out;
}

auto is_valid_sha256_hex(std::string_view s) -> bool {
  if(s.size() != 64) return false;
  for(char c : s) {
    if(!is_hex_digit(c)) return false;
  }
  return true;
}

auto is_json_ws(char c) -> bool {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

auto has_non_whitespace(std::string_view s) -> bool {
  for(char c : s) {
    if(!is_json_ws(c)) return true;
  }
  return false;
}

auto append_utf8(std::string& out, std::uint32_t cp) -> void {
  if(cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if(cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if(cp <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

struct JsonNumber {
  double value = 0.0;
  std::int64_t int_value = 0;
  bool is_integer = false;
};

struct JsonValue {
  std::variant<std::nullptr_t, bool, JsonNumber, std::string, JsonArray, JsonObject> data;
};

class JsonParser {
public:
  explicit JsonParser(std::string_view src) : src_(src) {}

  auto parse(JsonValue& root, std::string& error) -> bool {
    if(!parse_value(root, 0)) {
      error = std::move(error_);
      return false;
    }
    skip_ws();
    if(pos_ != src_.size()) {
      error = "unexpected trailing data after JSON root";
      return false;
    }
    return true;
  }

private:
  std::string_view src_;
  std::size_t pos_ = 0;
  std::string error_;

  auto fail(std::string msg) -> bool {
    if(error_.empty()) error_ = std::move(msg);
    return false;
  }

  auto skip_ws() -> void {
    while(pos_ < src_.size() && is_json_ws(src_[pos_])) pos_++;
  }

  auto parse_value(JsonValue& out, std::size_t depth) -> bool {
    // Cap nesting depth so adversarial inputs cannot exhaust the call stack.
    if(depth > MaxJsonDepth) return fail("JSON nesting depth exceeds limit");
    skip_ws();
    if(pos_ >= src_.size()) return fail("unexpected end of JSON input");

    const char c = src_[pos_];
    if(c == '{') return parse_object(out, depth + 1);
    if(c == '[') return parse_array(out, depth + 1);
    if(c == '"') {
      std::string s;
      if(!parse_string(s)) return false;
      out.data = std::move(s);
      return true;
    }
    if(c == 't') return parse_literal("true", JsonValue{true}, out);
    if(c == 'f') return parse_literal("false", JsonValue{false}, out);
    if(c == 'n') return parse_literal("null", JsonValue{nullptr}, out);
    if(c == '-' || (c >= '0' && c <= '9')) {
      JsonNumber num;
      if(!parse_number(num)) return false;
      out.data = num;
      return true;
    }
    return fail("unexpected token in JSON");
  }

  auto parse_literal(std::string_view lit, JsonValue val, JsonValue& out) -> bool {
    if(src_.substr(pos_, lit.size()) != lit) return fail("invalid JSON literal");
    pos_ += lit.size();
    out = std::move(val);
    return true;
  }

  auto parse_hex4(std::uint32_t& out) -> bool {
    if(pos_ + 4 > src_.size()) return fail("truncated unicode escape");
    std::uint32_t v = 0;
    for(int i = 0; i < 4; i++) {
      const char c = src_[pos_++];
      if(!is_hex_digit(c)) return fail("invalid hex digit in unicode escape");
      v = (v << 4) | hex_value(c);
    }
    out = v;
    return true;
  }

  auto parse_utf8_multibyte(unsigned char lead, std::string& out) -> bool {
    auto cont = [&](std::size_t idx, std::uint8_t min_b, std::uint8_t max_b) -> bool {
      if(idx >= src_.size()) return false;
      const auto b = static_cast<unsigned char>(src_[idx]);
      return b >= min_b && b <= max_b;
    };

    std::size_t len = 0;
    if(lead >= 0xc2 && lead <= 0xdf) {
      if(!cont(pos_, 0x80, 0xbf)) return fail("invalid UTF-8 sequence");
      len = 1;
    } else if(lead >= 0xe0 && lead <= 0xef) {
      const std::uint8_t lo = (lead == 0xe0) ? 0xa0 : 0x80;
      const std::uint8_t hi = (lead == 0xed) ? 0x9f : 0xbf;
      if(!cont(pos_, lo, hi) || !cont(pos_ + 1, 0x80, 0xbf)) return fail("invalid UTF-8 sequence");
      len = 2;
    } else if(lead >= 0xf0 && lead <= 0xf4) {
      const std::uint8_t lo = (lead == 0xf0) ? 0x90 : 0x80;
      const std::uint8_t hi = (lead == 0xf4) ? 0x8f : 0xbf;
      if(!cont(pos_, lo, hi) || !cont(pos_ + 1, 0x80, 0xbf) || !cont(pos_ + 2, 0x80, 0xbf)) {
        return fail("invalid UTF-8 sequence");
      }
      len = 3;
    } else {
      return fail("invalid UTF-8 lead byte");
    }

    out.push_back(static_cast<char>(lead));
    for(std::size_t i = 0; i < len; i++) out.push_back(src_[pos_++]);
    return true;
  }

  auto parse_string(std::string& out) -> bool {
    if(pos_ >= src_.size() || src_[pos_] != '"') return fail("expected '\"'");
    pos_++;
    while(pos_ < src_.size()) {
      const auto c = static_cast<unsigned char>(src_[pos_++]);
      if(c == '"') return true;
      if(c < 0x20) return fail("unescaped control character in JSON string");
      if(c == '\\') {
        if(pos_ >= src_.size()) return fail("unterminated escape sequence");
        const char esc = src_[pos_++];
        switch(esc) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            std::uint32_t cp = 0;
            if(!parse_hex4(cp)) return false;
            if(cp >= 0xd800 && cp <= 0xdbff) {
              if(pos_ + 2 > src_.size() || src_[pos_] != '\\' || src_[pos_ + 1] != 'u') {
                return fail("unpaired UTF-16 high surrogate");
              }
              pos_ += 2;
              std::uint32_t lo = 0;
              if(!parse_hex4(lo)) return false;
              if(lo < 0xdc00 || lo > 0xdfff) return fail("invalid UTF-16 low surrogate");
              cp = 0x10000u + (((cp - 0xd800u) << 10) | (lo - 0xdc00u));
            } else if(cp >= 0xdc00 && cp <= 0xdfff) {
              return fail("unpaired UTF-16 low surrogate");
            }
            append_utf8(out, cp);
            break;
          }
          default: return fail("invalid escape sequence in JSON string");
        }
      } else if(c < 0x80) {
        out.push_back(static_cast<char>(c));
      } else {
        if(!parse_utf8_multibyte(c, out)) return false;
      }
    }
    return fail("unterminated JSON string");
  }

  auto parse_number(JsonNumber& out) -> bool {
    const std::size_t start = pos_;
    if(src_[pos_] == '-') pos_++;
    if(pos_ >= src_.size()) return fail("invalid JSON number");

    if(src_[pos_] == '0') {
      pos_++;
      if(pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
        return fail("leading zero in JSON number");
      }
    } else if(src_[pos_] >= '1' && src_[pos_] <= '9') {
      while(pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') pos_++;
    } else {
      return fail("invalid JSON number");
    }

    bool has_frac_or_exp = false;
    if(pos_ < src_.size() && src_[pos_] == '.') {
      has_frac_or_exp = true;
      pos_++;
      if(pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
        return fail("invalid fraction in JSON number");
      }
      while(pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') pos_++;
    }
    if(pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
      has_frac_or_exp = true;
      pos_++;
      if(pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) pos_++;
      if(pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
        return fail("invalid exponent in JSON number");
      }
      while(pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') pos_++;
    }

    const char* begin = src_.data() + start;
    const char* end = src_.data() + pos_;
    double dval = 0.0;
    const auto [dptr, dec] = std::from_chars(begin, end, dval);
    if(dec != std::errc{} || dptr != end || !std::isfinite(dval)) {
      return fail("JSON number out of finite double range");
    }

    out.value = dval;
    out.is_integer = false;
    out.int_value = 0;
    if(!has_frac_or_exp) {
      std::int64_t ival = 0;
      const auto [iptr, iec] = std::from_chars(begin, end, ival, 10);
      if(iec == std::errc{} && iptr == end) {
        out.is_integer = true;
        out.int_value = ival;
      }
    }
    return true;
  }

  auto parse_array(JsonValue& out, std::size_t depth) -> bool {
    pos_++;  // '['
    skip_ws();
    JsonArray arr;
    if(pos_ < src_.size() && src_[pos_] == ']') {
      pos_++;
      out.data = std::move(arr);
      return true;
    }
    while(true) {
      JsonValue elem;
      if(!parse_value(elem, depth)) return false;
      arr.push_back(std::move(elem));
      skip_ws();
      if(pos_ >= src_.size()) return fail("unterminated JSON array");
      if(src_[pos_] == ']') {
        pos_++;
        out.data = std::move(arr);
        return true;
      }
      if(src_[pos_] != ',') return fail("expected ',' or ']' in JSON array");
      pos_++;
    }
  }

  auto parse_object(JsonValue& out, std::size_t depth) -> bool {
    pos_++;  // '{'
    skip_ws();
    JsonObject obj;
    if(pos_ < src_.size() && src_[pos_] == '}') {
      pos_++;
      out.data = std::move(obj);
      return true;
    }
    while(true) {
      skip_ws();
      if(pos_ >= src_.size() || src_[pos_] != '"') return fail("expected object key string");
      std::string key;
      if(!parse_string(key)) return false;
      for(const auto& [existing, _] : obj) {
        if(existing == key) return fail("duplicate JSON key '" + key + "'");
      }
      skip_ws();
      if(pos_ >= src_.size() || src_[pos_] != ':') return fail("expected ':' after object key");
      pos_++;
      JsonValue val;
      if(!parse_value(val, depth)) return false;
      obj.emplace_back(std::move(key), std::move(val));
      skip_ws();
      if(pos_ >= src_.size()) return fail("unterminated JSON object");
      if(src_[pos_] == '}') {
        pos_++;
        out.data = std::move(obj);
        return true;
      }
      if(src_[pos_] != ',') return fail("expected ',' or '}' in JSON object");
      pos_++;
    }
  }
};

auto read_int_range(const JsonValue& v, const std::string& ctx, std::int64_t min_val,
                    std::int64_t max_val, std::int64_t& out, std::string& error) -> bool {
  const auto* num = std::get_if<JsonNumber>(&v.data);
  if(!num || !num->is_integer) {
    error = ctx + " must be an integer";
    return false;
  }
  if(num->int_value < min_val || num->int_value > max_val) {
    error = ctx + " out of allowed integer range";
    return false;
  }
  out = num->int_value;
  return true;
}

auto read_float(const JsonValue& v, const std::string& ctx, float min_val, float max_val,
                bool min_inclusive, float& out, std::string& error) -> bool {
  const auto* num = std::get_if<JsonNumber>(&v.data);
  if(!num) {
    error = ctx + " must be a number";
    return false;
  }
  const float f = static_cast<float>(num->value);
  if(!std::isfinite(f)) {
    error = ctx + " must be a finite float";
    return false;
  }
  const bool min_ok = min_inclusive ? (f >= min_val) : (f > min_val);
  if(!min_ok || f > max_val) {
    error = ctx + " out of allowed range";
    return false;
  }
  out = f;
  return true;
}

auto parse_render_mode(const JsonValue& v, const std::string& ctx,
                       std::vector<std::string>& warnings) -> RenderMode {
  const auto* s = std::get_if<std::string>(&v.data);
  if(!s) {
    warnings.push_back(ctx + ": invalid mode type; falling back to stock2d");
    return RenderMode::Stock2D;
  }
  const std::string lower = to_lower_ascii(*s);
  if(lower == "stock2d") return RenderMode::Stock2D;
  if(lower == "flat") return RenderMode::Flat;
  if(lower == "voxel") return RenderMode::Voxel;
  warnings.push_back(ctx + ": unknown render mode '" + *s + "'; falling back to stock2d");
  return RenderMode::Stock2D;
}

auto parse_camera(const JsonValue& v, const std::string& ctx, CameraPreset& out,
                  std::vector<std::string>& warnings, std::string& error) -> bool {
  const auto* obj = std::get_if<JsonObject>(&v.data);
  if(!obj) {
    error = ctx + " must be an object";
    return false;
  }
  constexpr float kMaxFloat = std::numeric_limits<float>::max();
  for(const auto& [key, val] : *obj) {
    if(key == "fov_deg") {
      if(!read_float(val, ctx + ".fov_deg", 5.0f, 120.0f, true, out.fov_deg, error)) return false;
    } else if(key == "pitch_deg") {
      if(!read_float(val, ctx + ".pitch_deg", -90.0f, 90.0f, true, out.pitch_deg, error)) return false;
    } else if(key == "distance") {
      if(!read_float(val, ctx + ".distance", 0.0f, kMaxFloat, false, out.distance, error)) return false;
    } else {
      warnings.push_back(ctx + ": unknown key '" + key + "' ignored");
    }
  }
  return true;
}

auto parse_light(const JsonValue& v, const std::string& ctx, LightPreset& out,
                 std::vector<std::string>& warnings, std::string& error) -> bool {
  const auto* obj = std::get_if<JsonObject>(&v.data);
  if(!obj) {
    error = ctx + " must be an object";
    return false;
  }
  constexpr float kMaxFloat = std::numeric_limits<float>::max();
  constexpr float kLowestFloat = std::numeric_limits<float>::lowest();
  for(const auto& [key, val] : *obj) {
    if(key == "ambient") {
      if(!read_float(val, ctx + ".ambient", 0.0f, 1.0f, true, out.ambient, error)) return false;
    } else if(key == "dir_x") {
      if(!read_float(val, ctx + ".dir_x", kLowestFloat, kMaxFloat, true, out.dir_x, error)) return false;
    } else if(key == "dir_y") {
      if(!read_float(val, ctx + ".dir_y", kLowestFloat, kMaxFloat, true, out.dir_y, error)) return false;
    } else if(key == "dir_z") {
      if(!read_float(val, ctx + ".dir_z", kLowestFloat, kMaxFloat, true, out.dir_z, error)) return false;
    } else if(key == "bloom") {
      if(!read_float(val, ctx + ".bloom", 0.0f, kMaxFloat, true, out.bloom, error)) return false;
    } else if(key == "dof") {
      if(!read_float(val, ctx + ".dof", 0.0f, kMaxFloat, true, out.dof, error)) return false;
    } else {
      warnings.push_back(ctx + ": unknown key '" + key + "' ignored");
    }
  }
  return true;
}

auto parse_layer(const JsonValue& v, const std::string& ctx, LayerRule& out,
                 std::vector<std::string>& warnings, std::string& error) -> bool {
  const auto* obj = std::get_if<JsonObject>(&v.data);
  if(!obj) {
    error = ctx + " must be an object";
    return false;
  }
  constexpr float kMaxFloat = std::numeric_limits<float>::max();
  constexpr float kLowestFloat = std::numeric_limits<float>::lowest();
  bool has_layer = false;
  for(const auto& [key, val] : *obj) {
    if(key == "layer") {
      std::int64_t idx = 0;
      if(!read_int_range(val, ctx + ".layer", 0, 3, idx, error)) return false;
      out.layer = static_cast<int>(idx);
      has_layer = true;
    } else if(key == "mode") {
      out.mode = parse_render_mode(val, ctx + ".mode", warnings);
    } else if(key == "depth") {
      if(!read_float(val, ctx + ".depth", kLowestFloat, kMaxFloat, true, out.depth, error)) return false;
    } else if(key == "tilt_deg") {
      if(!read_float(val, ctx + ".tilt_deg", -90.0f, 90.0f, true, out.tilt_deg, error)) return false;
    } else {
      warnings.push_back(ctx + ": unknown key '" + key + "' ignored");
    }
  }
  if(!has_layer) {
    error = ctx + " missing required 'layer'";
    return false;
  }
  return true;
}

auto parse_sprite(const JsonValue& v, const std::string& ctx, SpriteRule& out,
                  std::vector<std::string>& warnings, std::string& error) -> bool {
  const auto* obj = std::get_if<JsonObject>(&v.data);
  if(!obj) {
    error = ctx + " must be an object";
    return false;
  }
  constexpr float kMaxFloat = std::numeric_limits<float>::max();
  constexpr float kLowestFloat = std::numeric_limits<float>::lowest();
  bool has_tile_hash = false;
  for(const auto& [key, val] : *obj) {
    if(key == "tile_hash") {
      const auto* s = std::get_if<std::string>(&val.data);
      if(!s || !has_non_whitespace(*s)) {
        error = ctx + ".tile_hash must be a non-empty string";
        return false;
      }
      out.tile_hash = *s;
      has_tile_hash = true;
    } else if(key == "mode") {
      out.mode = parse_render_mode(val, ctx + ".mode", warnings);
    } else if(key == "depth_bias") {
      if(!read_float(val, ctx + ".depth_bias", kLowestFloat, kMaxFloat, true, out.depth_bias, error)) return false;
    } else if(key == "height_scale") {
      if(!read_float(val, ctx + ".height_scale", 0.0f, kMaxFloat, false, out.height_scale, error)) return false;
    } else {
      warnings.push_back(ctx + ": unknown key '" + key + "' ignored");
    }
  }
  if(!has_tile_hash) {
    error = ctx + " missing required 'tile_hash'";
    return false;
  }
  return true;
}

auto parse_scene(const JsonValue& v, std::size_t index, SceneRule& out,
                 std::vector<std::string>& warnings, std::string& error) -> bool {
  const std::string ctx = "scenes[" + std::to_string(index) + "]";
  const auto* obj = std::get_if<JsonObject>(&v.data);
  if(!obj) {
    error = ctx + " must be an object";
    return false;
  }

  bool seen_id = false;
  bool seen_wram_addr = false;
  bool seen_wram_value = false;

  for(const auto& [key, val] : *obj) {
    if(key == "id") {
      const auto* s = std::get_if<std::string>(&val.data);
      if(!s) {
        error = ctx + ".id must be a string";
        return false;
      }
      out.id = *s;
      seen_id = true;
    } else if(key == "wram_addr") {
      std::int64_t addr = 0;
      if(!read_int_range(val, ctx + ".wram_addr", 0, MaxWramAddr - 1, addr, error)) return false;
      out.wram_addr = static_cast<std::uint32_t>(addr);
      seen_wram_addr = true;
    } else if(key == "wram_value") {
      std::int64_t byte_val = 0;
      if(!read_int_range(val, ctx + ".wram_value", 0, 255, byte_val, error)) return false;
      out.wram_value = static_cast<std::uint8_t>(byte_val);
      seen_wram_value = true;
    } else if(key == "camera") {
      if(!parse_camera(val, ctx + ".camera", out.camera, warnings, error)) return false;
    } else if(key == "light") {
      if(!parse_light(val, ctx + ".light", out.light, warnings, error)) return false;
    } else if(key == "layers") {
      const auto* arr = std::get_if<JsonArray>(&val.data);
      if(!arr) {
        error = ctx + ".layers must be an array";
        return false;
      }
      out.layers.reserve(arr->size());
      for(std::size_t j = 0; j < arr->size(); j++) {
        LayerRule rule;
        if(!parse_layer((*arr)[j], ctx + ".layers[" + std::to_string(j) + "]", rule, warnings, error)) {
          return false;
        }
        out.layers.push_back(std::move(rule));
      }
    } else if(key == "sprites") {
      const auto* arr = std::get_if<JsonArray>(&val.data);
      if(!arr) {
        error = ctx + ".sprites must be an array";
        return false;
      }
      out.sprites.reserve(arr->size());
      for(std::size_t j = 0; j < arr->size(); j++) {
        SpriteRule rule;
        if(!parse_sprite((*arr)[j], ctx + ".sprites[" + std::to_string(j) + "]", rule, warnings, error)) {
          return false;
        }
        out.sprites.push_back(std::move(rule));
      }
    } else {
      warnings.push_back(ctx + ": unknown key '" + key + "' ignored");
    }
  }

  if(seen_wram_addr != seen_wram_value) {
    error = ctx + ": selector requires both 'wram_addr' and 'wram_value'";
    return false;
  }
  out.has_selector = seen_wram_addr && seen_wram_value;
  if(!seen_id || !has_non_whitespace(out.id)) {
    warnings.push_back(ctx + ": missing or empty 'id'");
  }
  return true;
}

}  // namespace

bool Profile::matches_rom(std::string_view sha256) const {
  if(!is_valid_sha256_hex(sha256)) return false;
  for(const auto& candidate : rom_sha256) {
    if(!is_valid_sha256_hex(candidate)) continue;
    bool eq = true;
    for(std::size_t i = 0; i < 64; i++) {
      if(ascii_lower(candidate[i]) != ascii_lower(sha256[i])) {
        eq = false;
        break;
      }
    }
    if(eq) return true;
  }
  return false;
}

LoadResult parse_profile_json(std::string_view json_text) {
  LoadResult result;
  if(json_text.empty()) {
    result.error = "profile JSON is empty";
    return result;
  }
  if(json_text.size() > MaxProfileBytes) {
    result.error = "profile JSON exceeds 1 MiB limit";
    return result;
  }

  JsonValue root;
  JsonParser parser(json_text);
  if(!parser.parse(root, result.error)) return result;

  const auto* obj = std::get_if<JsonObject>(&root.data);
  if(!obj) {
    result.error = "profile JSON root must be an object";
    return result;
  }

  Profile parsed;
  bool seen_schema = false;
  bool seen_game_id = false;
  bool seen_hashes = false;

  for(const auto& [key, val] : *obj) {
    if(key == "schema_version") {
      std::int64_t ver = 0;
      if(!read_int_range(val, "schema_version", 1, std::numeric_limits<std::uint32_t>::max(), ver, result.error)) {
        return result;
      }
      if(ver != ProfileSchemaVersion) {
        result.error = "unsupported profile schema_version";
        return result;
      }
      parsed.schema_version = static_cast<std::uint32_t>(ver);
      seen_schema = true;
    } else if(key == "game_id") {
      const auto* s = std::get_if<std::string>(&val.data);
      if(!s || !has_non_whitespace(*s)) {
        result.error = "game_id must be a non-empty string";
        return result;
      }
      parsed.game_id = *s;
      seen_game_id = true;
    } else if(key == "rom_sha256") {
      const auto* arr = std::get_if<JsonArray>(&val.data);
      if(!arr || arr->empty()) {
        result.error = "rom_sha256 must be a non-empty array";
        return result;
      }
      parsed.rom_sha256.reserve(arr->size());
      for(std::size_t i = 0; i < arr->size(); i++) {
        const auto* h = std::get_if<std::string>(&(*arr)[i].data);
        if(!h || !is_valid_sha256_hex(*h)) {
          result.error = "rom_sha256[" + std::to_string(i) + "] must be a 64-char hex string";
          return result;
        }
        parsed.rom_sha256.push_back(to_lower_ascii(*h));
      }
      seen_hashes = true;
    } else if(key == "scenes") {
      const auto* arr = std::get_if<JsonArray>(&val.data);
      if(!arr) {
        result.error = "scenes must be an array";
        return result;
      }
      parsed.scenes.reserve(arr->size());
      for(std::size_t i = 0; i < arr->size(); i++) {
        SceneRule scene;
        if(!parse_scene((*arr)[i], i, scene, result.warnings, result.error)) return result;
        parsed.scenes.push_back(std::move(scene));
      }
    } else {
      result.warnings.push_back("root: unknown key '" + key + "' ignored");
    }
  }

  if(!seen_schema) {
    result.error = "missing required 'schema_version'";
    return result;
  }
  if(!seen_game_id) {
    result.error = "missing required 'game_id'";
    return result;
  }
  if(!seen_hashes) {
    result.error = "missing required 'rom_sha256'";
    return result;
  }

  result.profile = std::move(parsed);
  return result;
}

LoadResult load_profile_file(const std::string& path) {
  LoadResult result;
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  if(ec || !std::filesystem::exists(status)) {
    result.error = "cannot open profile file: " + path;
    return result;
  }
  if(!std::filesystem::is_regular_file(status)) {
    result.error = "profile path is not a regular file: " + path;
    return result;
  }

  // Reject empty or oversized files before reading the contents into memory.
  const std::uintmax_t file_bytes = std::filesystem::file_size(path, ec);
  if(ec) {
    result.error = "cannot stat profile file: " + path;
    return result;
  }
  if(file_bytes == 0) {
    result.error = "profile file is empty: " + path;
    return result;
  }
  if(file_bytes > MaxProfileBytes) {
    result.error = "profile file exceeds 1 MiB limit: " + path;
    return result;
  }

  std::ifstream in(path, std::ios::binary);
  if(!in) {
    result.error = "cannot open profile file: " + path;
    return result;
  }

  std::string content;
  content.resize(static_cast<std::size_t>(file_bytes) + 1);
  in.read(content.data(), static_cast<std::streamsize>(content.size()));
  const auto bytes_read = static_cast<std::size_t>(in.gcount());
  if(in.bad()) {
    result.error = "failed reading profile file: " + path;
    return result;
  }
  if(bytes_read == 0) {
    result.error = "profile file is empty: " + path;
    return result;
  }
  if(bytes_read > MaxProfileBytes) {
    result.error = "profile file exceeds 1 MiB limit: " + path;
    return result;
  }
  content.resize(bytes_read);

  return parse_profile_json(content);
}

}  // namespace snes2dhd::profile
