// standard libs
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

#define NUMBER_OF_BARS 40
#define CHANNELS 2
#define SAMPLE_RATE 44100
#define AUTOSENS 1
#define NOISE_REDUCTION 0.77
#define LOW_CUT_OFF 50
#define HIGH_CUT_OFF 10000

namespace fs = std::filesystem;
using namespace ftxui;

typedef struct {
  std::vector<double> bars_right;
  std::vector<double> bars_left;
  // double pcm_double[4096] ; //this variable holds the convert data from
  // pOutput to the format cava_execute can process
  ma_uint64 *framesRead;
  std::mutex mutex;
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
Color get_gradient_color(double value, int bar_index, int total_bars) {
  // Create frequency-based color mapping (low to high: green -> yellow -> red)
  float freq_ratio =
      static_cast<float>(bar_index) / static_cast<float>(total_bars - 1);

  // Amplitude-based intensity
  float intensity = static_cast<float>(value);

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

    for (int i = 0; i < frameCount * 2; ++i) {
      pcm_double[i] = (double)pcm_float32[i];
    }

    cava_execute(pcm_double, frameCount * CHANNELS, cava_out, plan);

    std::lock_guard<std::mutex> lock(viz_data.mutex);
    viz_data.bars_left.assign(cava_out, cava_out + NUMBER_OF_BARS);
    viz_data.bars_right.assign(cava_out + NUMBER_OF_BARS,
                               cava_out + (NUMBER_OF_BARS * CHANNELS));

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

  // FTXUI
  auto screen = ScreenInteractive::Fullscreen();
  global_screen = &screen; // Set global screen pointer

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
    std::lock_guard<std::mutex> lock(viz_data.mutex);
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
      double value =
          (viz_data.bars_left.size() > i) ? viz_data.bars_left[i] : 0.0;
      Color bar_color =
          get_gradient_color(value, NUMBER_OF_BARS - 1 - i, NUMBER_OF_BARS * 2);
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
      double value =
          (viz_data.bars_right.size() > i) ? viz_data.bars_right[i] : 0.0;
      Color bar_color =
          get_gradient_color(value, i + NUMBER_OF_BARS, NUMBER_OF_BARS * 2);
      combined_bars.push_back(create_cava_bar(value, bar_color, max_height));

      // Add spacing after each bar (except the last one)
      if (i < NUMBER_OF_BARS - 1 && bar_spacing > 0) {
        add_spacing();
      }
    }

    return vbox({text("♪ littleFlower🌸 Music Player`♪") | bold | center |
                     color(Color::Cyan),
                 separator(), hbox(combined_bars) | center, separator(),
                 hbox({text("Low Freq") | color(Color::Green),
                       text(" ← ") | color(Color::White),
                       text("Mid Freq") | color(Color::Yellow),
                       text(" → ") | color(Color::White),
                       text("High Freq") | color(Color::Red1)}) |
                     center});
  });

  // int current_audio_position = ma_decoder_get_cursor_in_pcm_frames(&decoder,
  // &current_audio_position);

  auto menu_music_list =
      Menu(&audio_files_list, &file_selected, MenuOption::VerticalAnimated());
  menu_music_list = CatchEvent(menu_music_list, [&](Event event) {
    if (event == Event::Return) {
      // check if playing is false, change to true

      if (ma_device_is_started(&device)) {
        ma_device_stop(&device);
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        if (!playing) {
          playing = !playing;
        }
      }

      // load the file, check playing state and play the song
      std::string full_music_path = path + audio_files_list[file_selected];
      results = ma_decoder_init_file(full_music_path.c_str(), NULL, &decoder);

      ma_decoder_get_length_in_pcm_frames(&decoder, &audio_length);

      if (results != MA_SUCCESS) {
        // printf("", path+);

        screen.ExitLoopClosure()();
        std::cout << "Failed to load music file"
                  << path + audio_files_list[file_selected] << std::endl;

        // return -2;
      }

      // device_started=MA_TRUE;

      deviceConfig = ma_device_config_init(ma_device_type_playback);
      deviceConfig.playback.format = ma_format_f32;
      // deviceConfig.playback.channels = decoder.outputChannels;
      // deviceConfig.sampleRate = decoder.outputSampleRate;

      deviceConfig.playback.channels = CHANNELS;
      deviceConfig.sampleRate = SAMPLE_RATE;

      deviceConfig.dataCallback = data_callback;
      deviceConfig.pUserData = &decoder;
      if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        screen.ExitLoopClosure()();
        std::cout << "Failed ot open playback device" << std::endl;
        ma_decoder_uninit(&decoder);
        // return -3;
      }

      if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        screen.ExitLoopClosure()();

        std::cout << "Failed to start playback device" << std::endl;
        // return -4;
      }

      return true;
    }
    return false;
  });

  auto display_music_state = Renderer([&] {
    return hbox({
               text("Playback: ") | bold,
               text(playing ? " ♪ PLAYING ♪ " : "⏸ PAUSED ⏸") |
                   color(playing ? Color::Green : Color::Red) | bold,
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
           }) |
           flex;
  });

  auto container = Container::Horizontal({
      menu_music_list,
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
                      menu_music_list->Render() |
                          size(WIDTH, GREATER_THAN, 25) | color(Color::Red),
                      separatorStyled(DASHED),
                      right_container->Render(),

                  }));
  });

  renderer = CatchEvent(renderer, [&](Event event) {
    // set q to quit, space_bar to toggle to between pause and play
    if (event == Event::Character('q')) {
      if (ma_device_is_started(&device)) {
        // ma_device_stop(&device);
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
      }

      screen.ExitLoopClosure()();

      return true;
    } else if (event == Event::Character(' ')) {
      playing = !playing;

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
