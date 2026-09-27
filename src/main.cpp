// standard libs
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

// include miniaudio.h
#include "../include/miniaudio.c"

// include cavacore.h
#include "../include/cavacore.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define NUMBER_OF_BARS 40
#define CHANNELS 2
#define SAMPLE_RATE 44100
#define AUTOSENS 1
#define NOISE_REDUCTION 0.77
#define LOW_CUT_OFF 50
#define HIGH_CUT_OFF 10000
#define WAVE_WIDTH 80
#define FALL_ROWS 16
#define ART_W 24
#define ART_H 10
#define PAL_N 5

namespace fs = std::filesystem;
using namespace ftxui;

unsigned char art_px[ART_H * 2][ART_W][3];
unsigned char art_pal[PAL_N][3];
int art_loaded = 0;
int art_pal_n = 0;

int load_cover_art(const char *dir) {
  static const char *names[] = {
      "cover.jpg", "cover.jpeg", "cover.png",  "folder.jpg",
      "folder.png", "AlbumArt.jpg", "front.png", NULL,
  };
  char full[1024];
  for (int ni = 0; names[ni] != NULL; ++ni) {
    snprintf(full, sizeof(full), "%s/%s", dir, names[ni]);
    int w = 0, h = 0, ch = 0;
    unsigned char *px = stbi_load(full, &w, &h, &ch, 3);
    if (px == NULL || w <= 0 || h <= 0) {
      if (px != NULL) {
        stbi_image_free(px);
      }
      continue;
    }
    for (int cy = 0; cy < ART_H * 2; ++cy) {
      int y0 = (cy * h) / (ART_H * 2);
      int y1 = ((cy + 1) * h) / (ART_H * 2);
      if (y1 <= y0) {
        y1 = y0 + 1;
      }
      for (int cx = 0; cx < ART_W; ++cx) {
        int x0 = (cx * w) / ART_W;
        int x1 = ((cx + 1) * w) / ART_W;
        if (x1 <= x0) {
          x1 = x0 + 1;
        }
        long sr = 0, sg = 0, sb = 0;
        long n = 0;
        for (int y = y0; y < y1 && y < h; ++y) {
          for (int x = x0; x < x1 && x < w; ++x) {
            unsigned char *p = px + (y * w + x) * 3;
            sr += p[0];
            sg += p[1];
            sb += p[2];
            n++;
          }
        }
        if (n == 0) {
          n = 1;
        }
        art_px[cy][cx][0] = (unsigned char)(sr / n);
        art_px[cy][cx][1] = (unsigned char)(sg / n);
        art_px[cy][cx][2] = (unsigned char)(sb / n);
      }
    }
    stbi_image_free(px);

    int hist[4096] = {0};
    int hr[4096] = {0};
    int hg[4096] = {0};
    int hb[4096] = {0};
    for (int cy = 0; cy < ART_H * 2; ++cy) {
      for (int cx = 0; cx < ART_W; ++cx) {
        int r = art_px[cy][cx][0] >> 4;
        int g = art_px[cy][cx][1] >> 4;
        int b = art_px[cy][cx][2] >> 4;
        int k = (r << 8) | (g << 4) | b;
        hist[k]++;
        hr[k] += art_px[cy][cx][0];
        hg[k] += art_px[cy][cx][1];
        hb[k] += art_px[cy][cx][2];
      }
    }
    int picked[PAL_N] = {-1, -1, -1, -1, -1};
    art_pal_n = 0;
    for (int t = 0; t < PAL_N; ++t) {
      int best = -1;
      for (int k = 0; k < 4096; ++k) {
        if (hist[k] == 0) {
          continue;
        }
        int skip = 0;
        for (int p = 0; p < t; ++p) {
          int dr = ((k >> 8) & 15) - ((picked[p] >> 8) & 15);
          int dg = ((k >> 4) & 15) - ((picked[p] >> 4) & 15);
          int db = (k & 15) - (picked[p] & 15);
          if (dr * dr + dg * dg + db * db < 18) {
            skip = 1;
            break;
          }
        }
        if (skip) {
          continue;
        }
        if (best < 0 || hist[k] > hist[best]) {
          best = k;
        }
      }
      if (best < 0) {
        break;
      }
      picked[t] = best;
      art_pal[t][0] = (unsigned char)(hr[best] / hist[best]);
      art_pal[t][1] = (unsigned char)(hg[best] / hist[best]);
      art_pal[t][2] = (unsigned char)(hb[best] / hist[best]);
      art_pal_n++;
    }
    for (int a = 0; a < art_pal_n; ++a) {
      for (int b = a + 1; b < art_pal_n; ++b) {
        int la = art_pal[a][0] + art_pal[a][1] + art_pal[a][2];
        int lb = art_pal[b][0] + art_pal[b][1] + art_pal[b][2];
        if (la > lb) {
          for (int c = 0; c < 3; ++c) {
            unsigned char tmp = art_pal[a][c];
            art_pal[a][c] = art_pal[b][c];
            art_pal[b][c] = tmp;
          }
        }
      }
    }
    art_loaded = 1;
    return 1;
  }
  return 0;
}

typedef struct {
  std::vector<double> bars_right;
  std::vector<double> bars_left;
  // double pcm_double[4096] ; //this variable holds the convert data from
  // pOutput to the format cava_execute can process
  ma_uint64 *framesRead;
  std::mutex mutex;
  double wave[WAVE_WIDTH];
  double fall[FALL_ROWS][NUMBER_OF_BARS];
  double energy;
} visualizationData;

// GLOBALS
//  cava_plan* plan = cava_init(NUMBER_OF_BARS, SAMPLE_RATE, CHANNELS, AUTOSENS,
//  NOISE_REDUCTION, LOW_CUT_OFF, HIGH_CUT_OFF);
cava_plan *plan = nullptr;
visualizationData viz_data;
ma_bool32 playing = MA_TRUE; // a global variable for checkin the playing state
std::string simple_test;
int bar_spacing = 1; // Configurable spacing between bars

//
ma_uint64 current_audio_position;
int audio_progress;
ma_uint64 audio_length;

// Add global screen pointer for posting updates
ScreenInteractive *global_screen = nullptr;

// Add timing control for refresh rate
auto last_update = std::chrono::steady_clock::now();
const auto refresh_interval = std::chrono::milliseconds(16); // ~60 FPS

std::vector<std::string> get_music_list(const std::string &directory) {
  std::vector<std::string> audio_files;
  // std::vector<std::string> supported_extension = {".mp3", ".wav", ".flac"};
  // //a future problem to worry about
  for (const auto &entry : fs::directory_iterator(directory)) {
    std::string file_extension = entry.path().extension();
    if (file_extension == ".mp3" || file_extension == ".wav" ||
        file_extension == ".flac") {
      audio_files.push_back(entry.path().filename().string());
    }

    // audio_files.push_back(entry.path().filename().string());
  }

  return audio_files;
}

// Function to get gradient color based on frequency and amplitude
Color get_gradient_color(double value, int bar_index, int total_bars,
                         int theme) {
  // Create frequency-based color mapping (low to high: green -> yellow -> red)
  float freq_ratio =
      static_cast<float>(bar_index) / static_cast<float>(total_bars - 1);

  // Amplitude-based intensity
  float intensity = static_cast<float>(value);

  int t = theme;
  if (t == 3 && art_pal_n == 0) {
    t = 0;
  }

  if (t == 3) {
    float f = freq_ratio * (art_pal_n - 1);
    int stop = (int)(f + 0.5f);
    if (stop < 0) {
      stop = 0;
    }
    if (stop > art_pal_n - 1) {
      stop = art_pal_n - 1;
    }
    float k = intensity;
    if (k < 0.0f) {
      k = 0.0f;
    }
    if (k > 1.0f) {
      k = 1.0f;
    }
    k = 0.08f + 0.92f * k;
    int r = (int)(art_pal[stop][0] * k);
    int g = (int)(art_pal[stop][1] * k);
    int b = (int)(art_pal[stop][2] * k);
    return Color::RGB(r, g, b);
  }

  if (theme == 1) {
    if (freq_ratio < 0.33f) {
      if (intensity > 0.7f)
        return Color::LightSkyBlue1;
      if (intensity > 0.4f)
        return Color::DeepSkyBlue1;
      if (intensity > 0.2f)
        return Color::DodgerBlue1;
      return Color::RGB(0, 32, 64);
    }
    if (freq_ratio < 0.66f) {
      if (intensity > 0.7f)
        return Color::CyanLight;
      if (intensity > 0.4f)
        return Color::Cyan;
      if (intensity > 0.2f)
        return Color::DarkCyan;
      return Color::RGB(0, 48, 48);
    }
    if (intensity > 0.7f)
      return Color::White;
    if (intensity > 0.4f)
      return Color::GrayLight;
    if (intensity > 0.2f)
      return Color::GrayDark;
    return Color::RGB(16, 16, 24);
  }

  if (theme == 2) {
    if (intensity > 0.7f)
      return Color::White;
    if (intensity > 0.4f)
      return Color::GrayLight;
    if (intensity > 0.2f)
      return Color::GrayDark;
    return Color::RGB(8, 8, 8);
  }

  if (freq_ratio < 0.33f) {
    // Low frequencies: Green to Yellow-Green
    if (intensity > 0.7f)
      return Color::GreenLight;
    else if (intensity > 0.4f)
      return Color::Green;
    else if (intensity > 0.2f)
      return Color::GreenLight;
    else
      return Color::RGB(0, 64, 0); // Dark green
  } else if (freq_ratio < 0.66f) {
    // Mid frequencies: Yellow to Orange
    if (intensity > 0.7f)
      return Color::Yellow;
    else if (intensity > 0.4f)
      return Color::RGB(255, 165, 0); // Orange
    else if (intensity > 0.2f)
      return Color::RGB(255, 140, 0); // Dark orange
    else
      return Color::RGB(128, 64, 0); // Dark yellow
  } else {
    // High frequencies: Orange to Red
    if (intensity > 0.7f)
      return Color::RedLight;
    else if (intensity > 0.4f)
      return Color::Red;
    else if (intensity > 0.2f)
      return Color::RGB(139, 0, 0); // Dark red
    else
      return Color::RGB(64, 0, 0); // Very dark red
  }
}

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                   ma_uint32 frameCount) {
  (void *)pInput; // we wouldn't be receiving any input

  ma_decoder *pDecoder = (ma_decoder *)pDevice->pUserData;
  // ma_uint64 current_position_in_pcm;

  if (pDecoder == NULL) {
    printf("Failed to initialize the Decoder\n");
    return;
  }

  if (!playing) {
    ma_silence_pcm_frames(pOutput, frameCount, pDevice->playback.format,
                          pDevice->playback.channels);
  } else {
    ma_decoder_read_pcm_frames(pDecoder, pOutput, frameCount, NULL);

    ma_decoder_get_cursor_in_pcm_frames(pDecoder, &current_audio_position);

    /*audio_progress = static_cast<int>(((current_audio_position / SAMPLE_RATE)
       / (audio_length / SAMPLE_RATE)) * 100.0);*/
    audio_progress =
        static_cast<int>((static_cast<double>(current_audio_position) /
                          static_cast<double>(audio_length)) *
                         100.0);

    const float *pcm_float32 = (const float *)pOutput;
    double pcm_double[4096];
    double cava_out[NUMBER_OF_BARS * CHANNELS] = {0};
    double wave_acc[WAVE_WIDTH] = {0};
    int wave_n[WAVE_WIDTH] = {0};
    double energy_sum = 0.0;

    for (int i = 0; i < frameCount * 2; ++i) {
      double s = (double)pcm_float32[i];
      if (!isfinite(s)) {
        s = 0.0;
      } else if (s > 1.0) {
        s = 1.0;
      } else if (s < -1.0) {
        s = -1.0;
      }
      pcm_double[i] = s;
    }

    int frames = (int)frameCount;
    for (int f = 0; f < frames; ++f) {
      double mono = (pcm_double[f * 2] + pcm_double[f * 2 + 1]) * 0.5;
      int w = (f * WAVE_WIDTH) / frames;
      if (w >= WAVE_WIDTH) {
        w = WAVE_WIDTH - 1;
      }
      wave_acc[w] += mono;
      wave_n[w]++;
      energy_sum += mono < 0 ? -mono : mono;
    }

    cava_execute(pcm_double, frameCount * CHANNELS, cava_out, plan);

    for (int i = 0; i < NUMBER_OF_BARS * CHANNELS; ++i) {
      if (!isfinite(cava_out[i])) {
        cava_out[i] = 0.0;
      }
    }

    std::lock_guard<std::mutex> lock(viz_data.mutex);
    viz_data.bars_left.assign(cava_out, cava_out + NUMBER_OF_BARS);
    viz_data.bars_right.assign(cava_out + NUMBER_OF_BARS,
                               cava_out + (NUMBER_OF_BARS * CHANNELS));
    for (int w = 0; w < WAVE_WIDTH; ++w) {
      viz_data.wave[w] = wave_n[w] > 0 ? wave_acc[w] / wave_n[w] : 0.0;
    }
    viz_data.energy = frames > 0 ? energy_sum / frames : 0.0;
    static int fall_tick = 0;
    if ((++fall_tick % 3) == 0) {
      for (int r = FALL_ROWS - 1; r > 0; --r) {
        memcpy(viz_data.fall[r], viz_data.fall[r - 1],
               sizeof(double) * NUMBER_OF_BARS);
      }
      for (int i = 0; i < NUMBER_OF_BARS; ++i) {
        viz_data.fall[0][i] = (cava_out[i] + cava_out[NUMBER_OF_BARS + i]) / 2;
      }
    }

    // ma_decoder_get_data_format(pDecoder,pDevice->playback.format,pDevice->playback.channels,
    // pDevice->playback.sampleRate , NULL , NULL);

    // ma_decoder_get_cursor_in_pcm_frames(pDecoder,
    // &current_audio_position_in_pcm);
    // current_audio_position = current_position_in_pcm / frameCount;
  }

  // CRITICAL: Post screen update at controlled rate
  auto now = std::chrono::steady_clock::now();

  if (global_screen && (now - last_update) >= refresh_interval) {
    global_screen->Post(Event::Custom);

    last_update = now;
  }
}

int main(int argc, char *argv[]) {

  if (argc != 2) {
    std::cout << "Usage: test-starter [path to music folder]";
    std::cout << "\n" << argc << "\n";
    exit(0);
  }

  int bar_spacing = 1; // Default spacing between bars
  // bool show_spacing_controls = false;
  ma_result results;
  ma_decoder decoder;
  ma_device device;
  ma_device_config deviceConfig;
  bool has_audio = false;

  //  cavacore
  plan = cava_init(NUMBER_OF_BARS, SAMPLE_RATE, CHANNELS, AUTOSENS,
                   NOISE_REDUCTION, LOW_CUT_OFF, HIGH_CUT_OFF);

  if (plan->status != 0) {
    std::cout << "failed to init cava plan" << std::endl;
    return -1;
  }

  // std::string path = "/home/lonely_shepard/Downloads/Music/";
  // std::string path = "/home/lonely_shepard/Downloads/";
  std::string path = std::string(argv[1]);

  std::vector<std::string> audio_files_list = get_music_list(path);
  int file_selected = 0;
  int vis_mode = 0;
  int color_theme = 0;
  if (load_cover_art(path.c_str())) {
    color_theme = 3;
  }

  auto theme_accent = [](int theme) -> Color {
    if (theme == 1) {
      return Color::CyanLight;
    }
    if (theme == 2) {
      return Color::White;
    }
    if (theme == 3 && art_pal_n > 0) {
      return Color::RGB(art_pal[art_pal_n - 1][0], art_pal[art_pal_n - 1][1],
                        art_pal[art_pal_n - 1][2]);
    }
    return Color::Yellow;
  };

  // FTXUI
  auto screen = ScreenInteractive::Fullscreen();
  global_screen = &screen; // Set global screen pointer
  screen.TrackMouse();

  // Create CAVA-style vertical bar visualizer
  auto create_cava_bar = [](double value, Color bar_color,
                            int max_height = 20) -> Element {
    int filled_height = static_cast<int>(value * max_height);

    std::vector<Element> bar_elements;

    // Create vertical bar from bottom to top
    for (int i = 0; i < max_height; i++) {
      if (i < (max_height - filled_height)) {
        // Empty space at top
        bar_elements.push_back(text(" "));
      } else {
        // Filled bar using block characters
        bar_elements.push_back(text("█") | color(bar_color));
      }
    }

    return vbox(bar_elements);
  };

  auto visualizer = Renderer([&] {
    double left[NUMBER_OF_BARS] = {0};
    double right[NUMBER_OF_BARS] = {0};
    double wave[WAVE_WIDTH] = {0};
    double fall[FALL_ROWS][NUMBER_OF_BARS] = {0};
    {
      std::lock_guard<std::mutex> lock(viz_data.mutex);
      for (int i = 0; i < NUMBER_OF_BARS; ++i) {
        if (i < (int)viz_data.bars_left.size()) {
          left[i] = viz_data.bars_left[i];
        }
        if (i < (int)viz_data.bars_right.size()) {
          right[i] = viz_data.bars_right[i];
        }
        for (int r = 0; r < FALL_ROWS; ++r) {
          fall[r][i] = viz_data.fall[r][i];
        }
      }
      for (int w = 0; w < WAVE_WIDTH; ++w) {
        wave[w] = viz_data.wave[w];
      }
    }

    std::string mode_name =
        vis_mode == 1 ? "waveform" : vis_mode == 2 ? "waterfall" : "spectrum";
    std::string theme_name = color_theme == 1   ? "ocean"
                               : color_theme == 2 ? "mono"
                               : color_theme == 3 ? "cover"
                                                  : "ember";
    Element title = text("♪ " + mode_name + " · " + theme_name + " ♪") | bold |
                    center | color(Color::Cyan);
    Element legend =
        hbox({text("Low Freq") | color(Color::Green),
              text(" ← ") | color(Color::White),
              text("Mid Freq") | color(Color::Yellow),
              text(" → ") | color(Color::White),
              text("High Freq") | color(Color::Red1)}) |
        center;

    if (vis_mode == 1) {
      const int wave_h = 12;
      const int mid = wave_h / 2;
      Elements cols;
      cols.reserve(WAVE_WIDTH);
      for (int w = 0; w < WAVE_WIDTH; ++w) {
        double v = wave[w];
        if (v > 1.0) {
          v = 1.0;
        }
        if (v < -1.0) {
          v = -1.0;
        }
        int amp = (int)((v < 0 ? -v : v) * mid);
        if (amp <= 0) {
          cols.push_back(filler());
          continue;
        }
        if (amp > mid) {
          amp = mid;
        }
        int start = v >= 0 ? mid - amp : mid;
        Elements col;
        if (start > 0) {
          col.push_back(filler() | size(HEIGHT, EQUAL, start));
        }
        for (int k = 0; k < amp; ++k) {
          col.push_back(text("█") | color(theme_accent(color_theme)));
        }
        int rest = wave_h - start - amp;
        if (rest > 0) {
          col.push_back(filler() | size(HEIGHT, EQUAL, rest));
        }
        cols.push_back(vbox(col));
      }
      return vbox({title, separator(),
                   hbox(cols) | center | size(HEIGHT, EQUAL, wave_h),
                   separator(),
                   text("amplitude · time →") | dim | center});
    }

    if (vis_mode == 2) {
      Elements rows;
      rows.reserve(FALL_ROWS);
      for (int r = FALL_ROWS - 1; r >= 0; --r) {
        Elements cells;
        cells.reserve(NUMBER_OF_BARS);
        for (int i = 0; i < NUMBER_OF_BARS; ++i) {
          cells.push_back(text("█") | color(get_gradient_color(
                                                fall[r][i], i, NUMBER_OF_BARS,
                                                color_theme)));
        }
        rows.push_back(hbox(cells) | center);
      }
      return vbox({title, separator(), vbox(rows) | center, separator(),
                   legend});
    }

    const int max_height = 20; // Height of visualization

    Elements combined_bars;

    // Function to add spacing
    auto add_spacing = [&]() {
      for (int s = 0; s < bar_spacing; ++s) {
        combined_bars.push_back(text(" "));
      }
    };

    // Combine left and right channels for a fuller spectrum
    // Mirror left channel (reverse order) + right channel
    for (int i = NUMBER_OF_BARS - 1; i >= 0; i--) {
      double value = left[i];
      Color bar_color = get_gradient_color(value, NUMBER_OF_BARS - 1 - i,
                                           NUMBER_OF_BARS * 2, color_theme);
      combined_bars.push_back(create_cava_bar(value, bar_color, max_height));

      // Add spacing after each bar (except the last one)
      if (i > 0 && bar_spacing > 0) {
        add_spacing();
      }
    }

    // Add center spacing between left and right channels
    if (bar_spacing > 0) {
      add_spacing();
    }

    // Add right channel bars
    for (int i = 0; i < NUMBER_OF_BARS; i++) {
      double value = right[i];
      Color bar_color = get_gradient_color(value, i + NUMBER_OF_BARS,
                                           NUMBER_OF_BARS * 2, color_theme);
      combined_bars.push_back(create_cava_bar(value, bar_color, max_height));

      // Add spacing after each bar (except the last one)
      if (i < NUMBER_OF_BARS - 1 && bar_spacing > 0) {
        add_spacing();
      }
    }

    return vbox({title, separator(), hbox(combined_bars) | center, separator(),
                 legend});
  });

  // int current_audio_position = ma_decoder_get_cursor_in_pcm_frames(&decoder,
  // &current_audio_position);

  auto play_index = [&](int idx) -> bool {
    int count = (int)audio_files_list.size();
    if (count == 0 || idx < 0 || idx >= count) {
      return false;
    }

    if (has_audio) {
      ma_device_stop(&device);
      ma_device_uninit(&device);
      ma_decoder_uninit(&decoder);
      has_audio = false;
      if (!playing) {
        playing = !playing;
      }
    }

    std::string full_music_path =
        (fs::path(path) / audio_files_list[idx]).string();
    results = ma_decoder_init_file(full_music_path.c_str(), NULL, &decoder);
    if (results != MA_SUCCESS) {
      return false;
    }

    ma_decoder_get_length_in_pcm_frames(&decoder, &audio_length);

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;

    deviceConfig.playback.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;

    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &decoder;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
      ma_decoder_uninit(&decoder);
      return false;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
      ma_device_uninit(&device);
      ma_decoder_uninit(&decoder);
      return false;
    }

    has_audio = true;
    file_selected = idx;
    return true;
  };

  auto menu_music_list =
      Menu(&audio_files_list, &file_selected, MenuOption::VerticalAnimated());
  menu_music_list = CatchEvent(menu_music_list, [&](Event event) {
    if (event == Event::Return) {
      play_index(file_selected);
      return true;
    }
    if (event.is_mouse() && event.mouse().motion == Mouse::Released &&
        (event.mouse().button == Mouse::Left ||
         event.mouse().button == Mouse::None)) {
      int longest = 0;
      for (auto &f : audio_files_list) {
        if ((int)f.size() > longest) {
          longest = (int)f.size();
        }
      }
      int menu_w = std::max(25, longest);
      int count = (int)audio_files_list.size();
      int ix = event.mouse().x;
      int iy = event.mouse().y;
      if (ix >= 1 && ix <= menu_w && iy >= 1 && iy < 1 + count) {
        play_index(iy - 1);
        return true;
      }
      return false;
    }
    return false;
  });

  auto display_music_state = Renderer([&] {
    static double glow = 0.0;
    static double avg = 0.0;
    static auto last_beat = std::chrono::steady_clock::now() -
                            std::chrono::seconds(10);
    double e = 0.0;
    if (playing) {
      std::lock_guard<std::mutex> lock(viz_data.mutex);
      e = viz_data.energy;
    }
    avg += (e - avg) * 0.08;
    glow += (e - glow) * (e > glow ? 0.5 : 0.08);
    auto now = std::chrono::steady_clock::now();
    if (e > avg * 1.35 + 0.015) {
      last_beat = now;
    }
    bool beat = (now - last_beat) < std::chrono::milliseconds(300);
    Color pulse = theme_accent(color_theme);
    Element status = text(playing ? " ♪ PLAYING ♪ " : "⏸ PAUSED ⏸");
    if (beat) {
      status = status | color(pulse) | bold | inverted;
    } else if (glow > 0.2) {
      status = status | color(pulse);
    } else {
      status = status | color(playing ? Color::Green : Color::Red);
    }
    return hbox({
               text("Playback: ") | bold,
               status | bold,
           }) |
           size(HEIGHT, EQUAL, 1);
  });

  auto slider_player = Slider(" Progress: ", &audio_progress, 0, 100, 1);

  // auto slider_player = Renderer([&] {
  //   int results = std::div((audio_length / decoder.outputSampleRate),
  //   60).quot; return hbox({text(std::to_string(audio_progress) +
  //                     "% : " + std::to_string(results))});
  // });
  auto right_container = Renderer([&] {
    return vbox({
               display_music_state->Render() | bold,
               separator(),
               visualizer->Render(),
               separator(),
               filler(),
               slider_player->Render(),
               text("Enter/click play · space pause · v visual · c colors · "
                    "q quit") |
                   dim,
           }) |
           flex;
  });

  auto art_panel = Renderer([&] {
    if (!art_loaded) {
      return text("♪ no cover ♪") | dim | center;
    }
    Elements rows;
    rows.reserve(ART_H);
    for (int y = 0; y < ART_H; ++y) {
      Elements cells;
      cells.reserve(ART_W);
      for (int x = 0; x < ART_W; ++x) {
        unsigned char *top = art_px[y * 2][x];
        unsigned char *bot = art_px[y * 2 + 1][x];
        cells.push_back(text("▀") | color(Color::RGB(top[0], top[1], top[2])) |
                        bgcolor(Color::RGB(bot[0], bot[1], bot[2])));
      }
      rows.push_back(hbox(cells) | center);
    }
    return vbox(rows);
  });

  auto left_panel = Renderer(menu_music_list, [&] {
    return vbox({
        menu_music_list->Render(),
        separator(),
        art_panel->Render() | size(HEIGHT, EQUAL, ART_H),
    });
  });

  auto container = Container::Horizontal({
      left_panel,
      right_container,
  });

  // auto right_container=Container::Vertical({
  //
  // 	//visualizer here,
  // 	//separator
  // 	//control
  // 	//slider for music
  //
  // });

  auto renderer = Renderer(container, [&] {
    return window(text("TUI_MUSIC_PLAYER") | bold,
                  hbox({
                      left_panel->Render() |
                          size(WIDTH, GREATER_THAN, 25) | color(Color::Red),
                      separatorStyled(DASHED),
                      right_container->Render(),

                  }));
  });

  renderer = CatchEvent(renderer, [&](Event event) {
    // set q to quit, space_bar to toggle to between pause and play
    if (event == Event::Character('q')) {
      if (has_audio) {
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        has_audio = false;
      }

      screen.ExitLoopClosure()();

      return true;
    } else if (event == Event::Character(' ')) {
      playing = !playing;

      return true;
    } else if (event == Event::Character('v')) {
      vis_mode = (vis_mode + 1) % 3;

      return true;
    } else if (event == Event::Character('c')) {
      color_theme = (color_theme + 1) % 4;

      return true;
    } else if (event == Event::Custom) {
      // This will trigger a screen refresh

      return true;
    }

    return false;
  });

  screen.Loop(renderer);

  // Clean up global pointer
  global_screen = nullptr;

  return EXIT_SUCCESS;
}
