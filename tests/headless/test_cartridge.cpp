// Cartridge mapping (LoROM/HiROM/ExHiROM), region (NTSC/PAL), SMC copier
// header stripping, and per-frame audio capture regression using synthetic ROMs.

#include <core/core.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

int failures = 0;

auto fail(const std::string& message) -> void {
  std::fprintf(stderr, "cartridge: %s\n", message.c_str());
  failures++;
}

auto write_le16(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint16_t value) -> void {
  rom[offset] = (std::uint8_t)(value & 0xff);
  rom[offset + 1] = (std::uint8_t)((value >> 8) & 0xff);
}

auto finalize_header(std::vector<std::uint8_t>& rom, std::size_t header, const char* title,
                     std::uint8_t mode, std::uint8_t rom_size_code, std::uint8_t region_code) -> void {
  for(std::size_t i = 0; i < 21; i++) {
    rom[header + i] = (std::uint8_t)(title[i] ? title[i] : ' ');
  }
  rom[header + 0x15] = mode;
  rom[header + 0x16] = 0x00;  // ROM only
  rom[header + 0x17] = rom_size_code;
  rom[header + 0x18] = 0x00;  // no RAM
  rom[header + 0x19] = region_code;
  rom[header + 0x1a] = 0x00;
  rom[header + 0x1b] = 0x00;
  for(std::size_t vec = 0x20; vec < 0x40; vec += 2) {
    write_le16(rom, header + vec, 0x8000);
  }

  write_le16(rom, header + 0x1c, 0xffff);
  write_le16(rom, header + 0x1e, 0x0000);
  std::uint32_t sum = 0;
  for(std::uint8_t byte : rom) sum += byte;
  const std::uint16_t checksum = (std::uint16_t)(sum & 0xffff);
  write_le16(rom, header + 0x1c, (std::uint16_t)(checksum ^ 0xffff));
  write_le16(rom, header + 0x1e, checksum);
}

// 65816 program: set CGRAM0 to a constant 15-bit BGR555 colour, turn screen on, loop.
auto make_constant_colour_code(std::uint16_t bgr555) -> std::vector<std::uint8_t> {
  return {
    0x78, 0x18, 0xfb,                                // SEI; CLC; XCE
    0xa9, 0x80, 0x8d, 0x00, 0x21,                    // LDA #$80; STA $2100 (force blank)
    0x9c, 0x21, 0x21,                                // STZ $2121 (CGRAM index 0)
    0xa9, (std::uint8_t)(bgr555 & 0xff), 0x8d, 0x22, 0x21,
    0xa9, (std::uint8_t)((bgr555 >> 8) & 0xff), 0x8d, 0x22, 0x21,
    0xa9, 0x0f, 0x8d, 0x00, 0x21,                    // LDA #$0F; STA $2100 (screen on)
    0x80, 0xfe,                                      // BRA $FE
  };
}

// 65816 program: read 16-bit BGR555 colour from $C0:0000-$C0:0001 and show it on CGRAM0.
auto make_read_c0_colour_code() -> std::vector<std::uint8_t> {
  return {
    0x78, 0x18, 0xfb,              // SEI; CLC; XCE
    0xa9, 0x80, 0x8d, 0x00, 0x21,  // LDA #$80; STA $2100 (force blank)
    0x9c, 0x21, 0x21,              // STZ $2121 (CGRAM index 0)
    0xaf, 0x00, 0x00, 0xc0,        // LDA $C00000
    0x8d, 0x22, 0x21,              // STA $2122
    0xaf, 0x01, 0x00, 0xc0,        // LDA $C00001
    0x8d, 0x22, 0x21,              // STA $2122
    0xa9, 0x0f, 0x8d, 0x00, 0x21,  // LDA #$0F; STA $2100 (screen on)
    0x80, 0xfe,                    // BRA $FE
  };
}

auto make_lorom(std::uint16_t bgr555, std::uint8_t region_code, bool with_header,
                bool with_copier_header) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> rom(0x8000, 0xff);
  const auto code = make_constant_colour_code(bgr555);
  for(std::size_t i = 0; i < code.size(); i++) rom[i] = code[i];

  if(with_header) {
    finalize_header(rom, 0x7fc0, "SYNTH LOROM TEST     ", 0x20, 0x05, region_code);
  } else {
    // Minimal headerless ROM: zero out header area except reset vectors so score stays below threshold.
    for(std::size_t i = 0x7fc0; i < 0x7fe0; i++) rom[i] = 0x00;
    for(std::size_t vec = 0x7fe0; vec < 0x8000; vec += 2) write_le16(rom, vec, 0x8000);
  }

  if(!with_copier_header) return rom;
  std::vector<std::uint8_t> smc(512, 0x00);
  smc[0] = 0x04;
  smc.insert(smc.end(), rom.begin(), rom.end());
  return smc;
}

// Synthetic HiROM layout (128 KiB, Chrono Trigger US / Terranigma PAL style Fast HiROM):
// - $00:8000 (file 0x08000) jumps to $C1:0000 (file 0x10000)
// - $C1:0000 reads the colour from $C0:0000 (file 0x00000)
auto make_hirom(std::uint8_t region_code, std::uint16_t bgr555) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> rom(0x20000, 0xff);
  write_le16(rom, 0x00000, bgr555);

  // At $00:8000 (file 0x08000): JML $C10000
  rom[0x08000] = 0x5c;
  rom[0x08001] = 0x00;
  rom[0x08002] = 0x00;
  rom[0x08003] = 0xc1;

  const auto code = make_read_c0_colour_code();
  for(std::size_t i = 0; i < code.size(); i++) rom[0x10000 + i] = code[i];

  const char* title = region_code == 0x01 ? "SYNTH HIROM NTSC     " : "SYNTH HIROM PAL      ";
  finalize_header(rom, 0x0ffc0, title, 0x31, 0x07, region_code);
  return rom;
}

// Synthetic ExHiROM layout (4 MiB + 64 KiB):
// - $00:8000 (file 0x408000) reads colour from $C0:0000 (file 0x000000)
auto make_exhirom(std::uint16_t bgr555) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> rom(0x410000, 0xff);
  write_le16(rom, 0x000000, bgr555);

  const auto code = make_read_c0_colour_code();
  for(std::size_t i = 0; i < code.size(); i++) rom[0x408000 + i] = code[i];

  finalize_header(rom, 0x40ffc0, "SYNTH EXHIROM TEST   ", 0x35, 0x0d, 0x01);
  return rom;
}

struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const char* name) {
    path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

auto write_rom(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) -> std::string {
  std::ofstream out(path, std::ios::binary);
  out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
  return path.string();
}

struct RunResult {
  snes2dhd::Region region = snes2dhd::Region::NTSC;
  snes2dhd::Mapping mapping = snes2dhd::Mapping::LoROM;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint32_t> pixels;
  std::vector<std::int16_t> audio;
};

auto check_channel(const char* label, const RunResult& frame, char channel) -> void {
  if(frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
    fail(std::string(label) + ": empty frame");
    return;
  }
  const std::uint32_t pixel = frame.pixels[(frame.height / 2) * frame.width + (frame.width / 2)];
  const std::uint8_t r = (pixel >> 16) & 0xff;
  const std::uint8_t g = (pixel >> 8) & 0xff;
  const std::uint8_t b = pixel & 0xff;
  bool ok = false;
  if(channel == 'R') ok = r > 0xc0 && g == 0 && b == 0;
  if(channel == 'G') ok = g > 0xc0 && r == 0 && b == 0;
  if(channel == 'B') ok = b > 0xc0 && r == 0 && g == 0;
  if(!ok) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: unexpected pixel %08x for channel %c", label, pixel, channel);
    fail(buf);
  }
}

auto run_rom(const char* label, const std::string& path, int frames) -> RunResult {
  RunResult result;
  if(auto error = snes2dhd::load(path); !error.empty()) {
    fail(std::string(label) + ": load failed: " + error);
    return result;
  }
  result.region = snes2dhd::region();
  result.mapping = snes2dhd::mapping();
  if(!snes2dhd::audio_samples().empty()) {
    fail(std::string(label) + ": audio_samples must be empty right after load()");
  }

  const snes2dhd::Frame* frame = nullptr;
  for(int i = 0; i < frames; i++) {
    frame = &snes2dhd::run_frame();
    const auto audio = snes2dhd::audio_samples();
    if(audio.empty() || (audio.size() % 2) != 0) {
      fail(std::string(label) + ": audio_samples missing or not stereo aligned");
    }
    // Frame 0 runs from power-on vcounter=0 to vcounter=240; subsequent frames span a full field.
    if(i > 0) {
      if(result.region == snes2dhd::Region::NTSC && (audio.size() < 1450 || audio.size() > 1750)) {
        fail(std::string(label) + ": NTSC audio sample count out of expected 48kHz range: " +
             std::to_string(audio.size()));
      }
      if(result.region == snes2dhd::Region::PAL && (audio.size() < 1750 || audio.size() > 2150)) {
        fail(std::string(label) + ": PAL audio sample count out of expected 48kHz range: " +
             std::to_string(audio.size()));
      }
    }
  }

  if(frame) {
    result.width = frame->width;
    result.height = frame->height;
    result.pixels = frame->pixels;
  }
  const auto last_audio = snes2dhd::audio_samples();
  result.audio.assign(last_audio.begin(), last_audio.end());

  snes2dhd::unload();
  if(!snes2dhd::audio_samples().empty()) {
    fail(std::string(label) + ": audio_samples not cleared on unload()");
  }
  if(snes2dhd::region() != snes2dhd::Region::NTSC || snes2dhd::mapping() != snes2dhd::Mapping::LoROM) {
    fail(std::string(label) + ": region/mapping did not reset to defaults on unload()");
  }
  return result;
}

}  // namespace

auto main() -> int {
  if(snes2dhd::region() != snes2dhd::Region::NTSC || snes2dhd::mapping() != snes2dhd::Mapping::LoROM) {
    fail("initial unloaded region/mapping defaults unexpected");
  }
  if(!snes2dhd::audio_samples().empty()) fail("initial audio_samples not empty");

  TempDir tmp("snes2dhd-test-cartridge");

  // 1. LoROM NTSC + 512-byte SMC copier header equivalence.
  const auto lorom_path = write_rom(tmp.path / "lorom.sfc", make_lorom(0x001f, 0x01, true, false));
  const auto smc_path = write_rom(tmp.path / "lorom.smc", make_lorom(0x001f, 0x01, true, true));
  const auto lorom = run_rom("LoROM NTSC", lorom_path, 4);
  const auto smc = run_rom("LoROM SMC", smc_path, 4);
  if(lorom.region != snes2dhd::Region::NTSC || lorom.mapping != snes2dhd::Mapping::LoROM) {
    fail("LoROM NTSC detected wrong region or mapping");
  }
  if(smc.region != snes2dhd::Region::NTSC || smc.mapping != snes2dhd::Mapping::LoROM) {
    fail("LoROM SMC detected wrong region or mapping");
  }
  if(lorom.pixels != smc.pixels || lorom.audio != smc.audio) {
    fail("512-byte SMC copier header changed emulation output");
  }
  check_channel("LoROM NTSC", lorom, 'R');

  // 2. Minimal headerless ROM defaults cleanly to LoROM NTSC.
  const auto headerless_path = write_rom(tmp.path / "headerless.sfc", make_lorom(0x03e0, 0x00, false, false));
  const auto headerless = run_rom("Headerless", headerless_path, 4);
  if(headerless.region != snes2dhd::Region::NTSC || headerless.mapping != snes2dhd::Mapping::LoROM) {
    fail("headerless ROM did not default to LoROM NTSC");
  }
  check_channel("Headerless", headerless, 'G');

  // 3. Synthetic HiROM NTSC (Chrono Trigger US layout).
  const auto hirom_ntsc_path = write_rom(tmp.path / "hirom-ntsc.sfc", make_hirom(0x01, 0x03e0));
  const auto hirom_ntsc = run_rom("HiROM NTSC", hirom_ntsc_path, 4);
  const auto hirom_ntsc_repeat = run_rom("HiROM NTSC repeat", hirom_ntsc_path, 4);
  if(hirom_ntsc.region != snes2dhd::Region::NTSC || hirom_ntsc.mapping != snes2dhd::Mapping::HiROM) {
    fail("HiROM NTSC detected wrong region or mapping");
  }
  if(hirom_ntsc.pixels != hirom_ntsc_repeat.pixels || hirom_ntsc.audio != hirom_ntsc_repeat.audio) {
    fail("HiROM NTSC was not deterministic across reloads");
  }
  check_channel("HiROM NTSC", hirom_ntsc, 'G');

  // 4. Synthetic HiROM PAL (Terranigma PAL/EU layout).
  const auto hirom_pal_path = write_rom(tmp.path / "hirom-pal.sfc", make_hirom(0x02, 0x7c00));
  const auto hirom_pal = run_rom("HiROM PAL", hirom_pal_path, 4);
  if(hirom_pal.region != snes2dhd::Region::PAL || hirom_pal.mapping != snes2dhd::Mapping::HiROM) {
    fail("HiROM PAL detected wrong region or mapping");
  }
  check_channel("HiROM PAL", hirom_pal, 'B');
  if(hirom_pal.audio.size() <= hirom_ntsc.audio.size()) {
    fail("PAL frame did not emit more 48kHz audio samples than NTSC frame");
  }

  // 5. Additional PAL country code (0x11 Australia) + ExHiROM NTSC.
  const auto hirom_au_path = write_rom(tmp.path / "hirom-au.sfc", make_hirom(0x11, 0x7c00));
  const auto hirom_au = run_rom("HiROM AU PAL", hirom_au_path, 2);
  if(hirom_au.region != snes2dhd::Region::PAL || hirom_au.mapping != snes2dhd::Mapping::HiROM) {
    fail("region 0x11 did not detect HiROM PAL");
  }

  const auto exhirom_path = write_rom(tmp.path / "exhirom.sfc", make_exhirom(0x001f));
  const auto exhirom = run_rom("ExHiROM NTSC", exhirom_path, 4);
  if(exhirom.region != snes2dhd::Region::NTSC || exhirom.mapping != snes2dhd::Mapping::ExHiROM) {
    fail("ExHiROM NTSC detected wrong region or mapping");
  }
  check_channel("ExHiROM NTSC", exhirom, 'R');

  if(failures) {
    std::fprintf(stderr, "cartridge: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("cartridge: LoROM/HiROM/ExHiROM NTSC/PAL and audio capture ok\n");
  return 0;
}
