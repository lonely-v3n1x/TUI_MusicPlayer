//standard libs
#include<iostream>
#include<cstdlib>
#include<filesystem>
#include<string>
#include<vector>
#include<mutex>
#include<fstream>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"

//include miniaudio.h
#include "../include/miniaudio.c"

//include cavacore.h
#include "../include/cavacore.h"

#define NUMBER_OF_BARS 50  // Increased for more detailed visualization
#define CHANNELS       2
#define SAMPLE_RATE    44100
#define AUTOSENS       1
#define NOISE_REDUCTION 0.77
#define LOW_CUT_OFF     50
#define HIGH_CUT_OFF    10000

namespace fs =  std::filesystem;
using namespace ftxui;

typedef struct{
	std::vector<double> bars_right;
	std::vector<double> bars_left;
	ma_uint64* framesRead;
	std::mutex mutex;
}visualizationData;

cava_plan* plan = nullptr;
visualizationData viz_data;
ma_bool32 playing=MA_TRUE;
std::string simple_test;

ScreenInteractive* global_screen = nullptr;

auto last_update = std::chrono::steady_clock::now();
const auto refresh_interval = std::chrono::milliseconds(16); // ~60 FPS

std::vector<std::string> get_music_list(const std::string& directory){
	std::vector<std::string> audio_files;
	for(const auto& entry : fs::directory_iterator(directory)){
		std::string file_extension = entry.path().extension();
		if(file_extension == ".mp3" || file_extension == ".wav" || file_extension == ".flac"){
			audio_files.push_back(entry.path().filename().string());
		}
	}
	return audio_files;
}

// Function to get gradient color based on frequency and amplitude
Color get_gradient_color(double value, int bar_index, int total_bars) {
    // Create frequency-based color mapping (low to high: green -> yellow -> red)
    float freq_ratio = static_cast<float>(bar_index) / static_cast<float>(total_bars - 1);
    
    // Amplitude-based intensity
    float intensity = static_cast<float>(value);
    
    if (freq_ratio < 0.33f) {
        // Low frequencies: Green to Yellow-Green
        if (intensity > 0.7f) return Color::GreenLight;
        else if (intensity > 0.4f) return Color::Green;
        else if (intensity > 0.2f) return Color::GreenLight;
        else return Color::RGB(0, 64, 0); // Dark green
    }
    else if (freq_ratio < 0.66f) {
        // Mid frequencies: Yellow to Orange
        if (intensity > 0.7f) return Color::Yellow;
        else if (intensity > 0.4f) return Color::RGB(255, 165, 0); // Orange
        else if (intensity > 0.2f) return Color::RGB(255, 140, 0); // Dark orange
        else return Color::RGB(128, 64, 0); // Dark yellow
    }
    else {
        // High frequencies: Orange to Red
        if (intensity > 0.7f) return Color::RedLight;
        else if (intensity > 0.4f) return Color::Red;
        else if (intensity > 0.2f) return Color::RGB(139, 0, 0); // Dark red
        else return Color::RGB(64, 0, 0); // Very dark red
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount){
	(void*) pInput;
	
	ma_decoder* pDecoder = (ma_decoder*) pDevice->pUserData;

	if(pDecoder == NULL){
		printf("Failed to initialize the Decoder\n");
		return;
	}

	if(!playing){
		ma_silence_pcm_frames(pOutput, frameCount, pDevice->playback.format, pDevice->playback.channels);
	}
	else{
		ma_decoder_read_pcm_frames(pDecoder, pOutput,frameCount,NULL);
		
		const float* pcm_float32 = (const float*)pOutput;
		double pcm_double[4096];
		double cava_out[NUMBER_OF_BARS * CHANNELS] = {0};

		for(int i =0; i < frameCount*2; ++i){
			pcm_double[i]= (double) pcm_float32[i];
		}

		cava_execute(pcm_double, frameCount*CHANNELS, cava_out ,plan);

		std::lock_guard<std::mutex> lock(viz_data.mutex);
		viz_data.bars_left.assign(cava_out, cava_out+NUMBER_OF_BARS);
		viz_data.bars_right.assign(cava_out+NUMBER_OF_BARS, cava_out+(NUMBER_OF_BARS * CHANNELS));
	}
	
	auto now = std::chrono::steady_clock::now();
	if (global_screen && (now - last_update) >= refresh_interval) {
		global_screen->Post(Event::Custom);
		last_update = now;
	}
}

int main(){
	//cavacore
	plan = cava_init(NUMBER_OF_BARS, SAMPLE_RATE, CHANNELS, AUTOSENS, NOISE_REDUCTION, LOW_CUT_OFF, HIGH_CUT_OFF);
	
	if(plan->status != 0){
		std::cout<<"failed to init cava plan"<<std::endl;
		return -1;
	}

	ma_result results;
	ma_decoder decoder;
	ma_device device;
	ma_device_config deviceConfig;
	
	std::string path="/home/lonely_shepard/Downloads/Music/";
	std::vector<std::string> audio_files_list = get_music_list(path);
	int file_selected=0;

	//FTXUI
	auto screen = ScreenInteractive::Fullscreen();
	global_screen = &screen;
	
	// Create CAVA-style vertical bar visualizer
	auto create_cava_bar = [](double value, Color bar_color, int max_height = 20) -> Element {
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
	    
	    // Combine left and right channels for a fuller spectrum
	    // Mirror left channel (reverse order) + right channel
	    for (int i = NUMBER_OF_BARS - 1; i >= 0; i--) {
	        double value = (viz_data.bars_left.size() > i) ? viz_data.bars_left[i] : 0.0;
	        Color bar_color = get_gradient_color(value, NUMBER_OF_BARS - 1 - i, NUMBER_OF_BARS * 2);
	        combined_bars.push_back(create_cava_bar(value, bar_color, max_height));
	    }
	    
	    // Add right channel bars
	    for (int i = 0; i < NUMBER_OF_BARS; i++) {
	        double value = (viz_data.bars_right.size() > i) ? viz_data.bars_right[i] : 0.0;
	        Color bar_color = get_gradient_color(value, i + NUMBER_OF_BARS, NUMBER_OF_BARS * 2);
	        combined_bars.push_back(create_cava_bar(value, bar_color, max_height));
	    }

	    return vbox({
	        text("♪ AUDIO SPECTRUM VISUALIZER ♪") | bold | center | color(Color::Cyan),
	        separator(),
	        hbox(combined_bars) | center,
	        separator(),
	        hbox({
	            text("Low Freq") | color(Color::Green),
	            text(" ← ") | color(Color::White),
	            text("Mid Freq") | color(Color::Yellow), 
	            text(" → ") | color(Color::White),
	            text("High Freq") | color(Color::Red)
	        }) | center
	    });
	});

	auto menu_music_list = Menu(&audio_files_list,&file_selected, MenuOption::VerticalAnimated());
	menu_music_list= CatchEvent(menu_music_list, [&](Event event){
		if(event == Event::Return){
			if(ma_device_is_started(&device) ){
				ma_device_stop(&device);
				ma_device_uninit(&device);
				ma_decoder_uninit(&decoder);
				if(!playing){ playing = !playing;}
			}
			
			std::string full_music_path = path+audio_files_list[file_selected];
			results = ma_decoder_init_file(full_music_path.c_str(),NULL, &decoder);
			
			if(results != MA_SUCCESS){
				screen.ExitLoopClosure()();
				std::cout<<"Failed to load music file"<<path+audio_files_list[file_selected]<<std::endl;
			}

			deviceConfig = ma_device_config_init(ma_device_type_playback);
			deviceConfig.playback.format = ma_format_f32;
			deviceConfig.playback.channels = decoder.outputChannels;
			deviceConfig.sampleRate = decoder.outputSampleRate;
			deviceConfig.dataCallback = data_callback; 
			deviceConfig.pUserData = &decoder;
			
			if(ma_device_init(NULL, &deviceConfig, &device)!= MA_SUCCESS){
				screen.ExitLoopClosure()();
				std::cout<<"Failed to open playback device"<<std::endl;
				ma_decoder_uninit(&decoder);
			}

			if(ma_device_start(&device)!=MA_SUCCESS){
				ma_device_uninit(&device);
				ma_decoder_uninit(&decoder);
				screen.ExitLoopClosure()();
				std::cout<<"Failed to start playback device"<<std::endl;
			}
			
			return true;
		}
		return false;
	});

	auto current_music_state = []{ 
		if(playing) return "♪ PLAYING ♪"; 
		else return "⏸ PAUSED ⏸";
	};
	
	auto display_music_state = Renderer([&]{
	    return hbox({
	        text("STATUS: "),
	        text(current_music_state()) | bold | color(playing ? Color::Green : Color::Yellow),
	    }) | center;
	});

	auto right_container = Renderer([&] {
		return vbox({
			display_music_state->Render(),
			separator(),
			visualizer->Render(),
		}) | flex;
	}); 

	auto container = Container::Horizontal({
		menu_music_list,
		right_container,
	});

	auto renderer = Renderer(container, [&]{
		return window(text("♫ TUI MUSIC PLAYER WITH CAVA-STYLE VISUALIZER ♫") | bold | center, 
		    hbox({
			    menu_music_list->Render() | size(WIDTH, GREATER_THAN, 25) | color(Color::Cyan) | border,
			    separatorStyled(DASHED),
			    right_container->Render() | flex,
		    })
		);
	});

	renderer = CatchEvent(renderer, [&](Event event){
		if(event == Event::Character('q')){
			if(ma_device_is_started(&device)){
				ma_device_uninit(&device);
				ma_decoder_uninit(&decoder);
			}
			screen.ExitLoopClosure()();
			return true;
		}
		else if(event == Event::Character(' ')){
			playing=!playing;
			return true;
		}
		else if(event == Event::Custom){
			return true;
		}
		return false;
	});

	screen.Loop(renderer);
	global_screen = nullptr;
	
	return EXIT_SUCCESS;
}
