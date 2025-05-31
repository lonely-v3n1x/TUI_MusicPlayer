//standard libs
#include<iostream>
#include<cstdlib>
#include<filesystem>
#include<string>
#include<vector>


#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"

//include miniaudio.h
#include "../include/miniaudio.h"


namespace fs =  std::filesystem;
using namespace ftxui;

std::vector<std::string> get_music_list(const std::string& directory){
	std::vector<std::string> audio_files;
	// std::vector<std::string> supported_extension = {".mp3", ".wav", ".flac"}; //a future problem to worry about
	for(const auto& entry : fs::directory_iterator(directory)){
		std::string file_extension = entry.path().extension();
		if(file_extension == ".mp3" || file_extension == ".wav" || file_extension == ".flac"){
			audio_files.push_back(entry.path().filename().string());
		}
		
			// audio_files.push_back(entry.path().filename().string());
	}

	return audio_files;
}


ma_bool32 playing=MA_TRUE;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount){

	(void*) pInput; // we wouldn't be receiving any input
	
	ma_decoder* pDecoder = (ma_decoder*) pDevice->pUserData;

	if(pDecoder == NULL){
		printf("Failed to initialize the Decoder\n");
		return;
		
	}

	if(!playing){
		ma_silence_pcm_frames(pOutput, frameCount, pDevice->playback.format, pDevice->playback.channels);
	}
	else{
		ma_decoder_read_pcm_frames(pDecoder, pOutput,frameCount, NULL);
	}
	
}


int main(){

	ma_result results;
	ma_decoder decoder;
	ma_device device;
	ma_device_config deviceConfig;

	//device configuration
	// deviceConfig = ma_device_config_init(ma_device_type_playback);
	// deviceconfig.playback.format = decoder.outputformat;
	// deviceconfig.playback.channels = decoder.outputchannels;
	// deviceconfig.samplerate = decoder.outputsamplerate;
	// deviceconfig.datacallback = data_callback; 
	// deviceconfig.puserdata = &decoder;
	
	
	std::string path="/home/lonely_shepard/Downloads/Music";
	
	std::vector<std::string> audio_files_list = get_music_list(path);
	int file_selected=0;

	//tesing the get_music_list fuction
	// for(int i,j=1; i < audio_files_list.size(); ++i,++j){
	// 	std::cout<<"["<<j <<"] "<< audio_files_list[i] <<std::endl;
	// }
	

	
	//FTXUI
	auto screen = ScreenInteractive::Fullscreen();

	auto menu_music_list = Menu(&audio_files_list,&file_selected, MenuOption::VerticalAnimated());
	menu_music_list= CatchEvent([&](Event event){
		if(event == Event::Return){
			//load the file, check playing state and play the song
			std::string full_music_path = path+audio_files_list[file_selected];
			results = ma_decoder_init_file(full_music_path.c_str(),NULL, &decoder);
			
			if(results != MA_SUCCESS){
				// printf("", path+);
				
				screen.ExitLoopClosure()();
				std::cout<<"Failed to load music file"<<path+audio_files_list[file_selected]<<std::endl;
				
				return -2;
				
			}

			deviceConfig = ma_device_config_init(ma_device_type_playback);
			deviceConfig.playback.format = decoder.outputFormat;
			deviceConfig.playback.channels = decoder.outputChannels;
			deviceConfig.sampleRate = decoder.outputSampleRate;
			deviceConfig.dataCallback = data_callback; 
			deviceConfig.puserdata = &decoder;

			if(ma_device_init(NULL, &deviceConfig, &device)!= MA_SUCCESS){
				screen.ExitLoopClosure()();
				std::cout<<"Failed ot open playback device"<<std::endl;
				ma_decoder_uninit(&decoder);
				return -3;
			}

			if(ma_device_start(&device)!=MA_SUCCES){
				ma_device_uninit(&device);
				ma_decoder_uninit(&decoder);
				screen.ExitLoopClosure()();
				
				std::cout<<"Failed to start playback device"<<std::endl;
				return -4;
				
			}

			
			return true;
		}
		return false;
	});

	auto container = Container::Horizontal({
		menu_music_list,
	});


	auto renderer = Renderer(container, [&]{
		return window(text("TUIMUSICPLAYER"), hbox({
			menu_music_list->Render() | size(WIDTH, GREATER_THAN, 20)| color(Color::Red),
			seperatedStyle(DASHED),
			vbox({
				
			}),
		}));
	});

	// renderer = CatchEvent(renderer, [&](Event event){
	//
	// 	//set q to quit, space_bar to pause 
	// 	if(){
	//
	// 		return true;
	// 	}
	// 	else{
	// 		return true;
	// 	}
	//
	// 	return false;
	// });


	screen.Loop(renderer);

	
	return EXIT_SUCCESS;
}
