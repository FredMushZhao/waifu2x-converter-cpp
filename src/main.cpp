/*
* main.cpp
*   (ここにファイルの簡易説明を記入)
*
*  Created on: 2015/05/24
*	  Author: wlamigo
*
*   (ここにファイルの説明を記入)
*/

#include <cassert>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <cmath>
#include <deque>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <experimental/filesystem>
#include <algorithm>

#include "tclap/CmdLine.h"
#include "sec.hpp"

#if defined(WIN32) && defined(UNICODE)
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#pragma comment ( linker, "/entry:\"wmainCRTStartup\"" )
#endif


#include "w2xconv.h"

#ifndef DEFAULT_MODELS_DIRECTORY
#define DEFAULT_MODELS_DIRECTORY "models_rgb"
#endif

#ifdef HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace fs = std::experimental::filesystem;



class CustomFailOutput : public TCLAP::StdOutput {
public:
	virtual void failure( TCLAP::CmdLineInterface& _cmd, TCLAP::ArgException& e ) override {
		std::string progName = _cmd.getProgramName();

		std::cerr << "PARSE ERROR: " << e.argId() << std::endl
				  << "			 " << e.error() << std::endl << std::endl;

		if ( _cmd.hasHelpAndVersion() )
			{
				std::cerr << "Brief USAGE: " << std::endl;

				_shortUsage( _cmd, std::cerr );	

				std::cerr << std::endl << "For complete USAGE and HELP type: " 
						  << std::endl << "   " << progName << " --help" 
						  << std::endl << std::endl;
				std::cerr << "Waifu2x OpenCV - Version " << GIT_TAG << " (" << GIT_COMMIT_HASH << ") - https://github.com/DeadSix27/waifu2x-converter-cpp" << std::endl << std::endl;
				std::cerr << "If you find issues or need help, visit: https://github.com/DeadSix27/waifu2x-converter-cpp/issues" << std::endl << std::endl;
			}
		else
			usage(_cmd);

		throw TCLAP::ExitException(1);
	}
};

static void
dump_procs()
{
	const W2XConvProcessor *procs;
	size_t num_proc;
	procs = w2xconv_get_processor_list(&num_proc);

	for (int i = 0; i < num_proc; i++) {
		const W2XConvProcessor *p = &procs[i];
		const char *type;
		switch (p->type) {
		case W2XCONV_PROC_HOST:
			switch (p->sub_type) {
			case W2XCONV_PROC_HOST_AVX:
				type = "AVX";
				break;
			case W2XCONV_PROC_HOST_FMA:
				type = "FMA";
				break;
			case W2XCONV_PROC_HOST_SSE3:
				type = "SSE3";
				break;
			case W2XCONV_PROC_HOST_NEON:
				type = "NEON";
				break;
			default:
				type = "OpenCV";
				break;
			}
			break;

		case W2XCONV_PROC_CUDA:
			type = "CUDA";
			break;

		case W2XCONV_PROC_OPENCL:
			type = "OpenCL";
			break;

		default:
			type = "??";
			break;
		}

		printf("%4d: %-45s(%-10s): num_core=%d\n",
			i,
			p->dev_name,
			type,
			p->num_core);
	}
}

void check_for_errors(W2XConv* converter, int error) {
	if (error) {
		char *err = w2xconv_strerror(&converter->last_error);
		std::string errorMessage(err);
		w2xconv_free(err);
		throw std::runtime_error(errorMessage);
	}
}



std::map<std::string,bool> opencv_formats = {
	// Windows Bitmaps
	{"BMP",  true},
	{"DIB",  true},
	
	// JPEG Files
	{"JPEG", true},
	{"JPG",  true},
	{"JPE",  true},
	
	// JPEG 2000 Files
	{"JP2",  false},
	
	// Portable Network Graphics
	{"PNG",  true},
	
	// WebP
	{"WEBP", true},
	
	// Portable Image Format
	{"PBM",  true},
	{"PGM",  true},
	{"PPM",  true},
	{"PXM",  true},
	{"PNM",  true},
	
	// Sun Rasters
	{"SR",   true},
	{"RAS",  true},
	
	// TIFF Files
	{"TIF",  true},
	{"TIFF", true},
	
	// OpenEXR Image Files
	{"EXR",  true},
	
	// Radiance HDR
	{"HDR",  true},
	{"PIC",  true}
};

bool validate_format_extension(std::string ext) {
	if(ext.length() == 0)
		return false;
	
	if(ext.at(0) == '.')
		ext=ext.substr(1);
	
	std::transform(ext.begin(), ext.end(), ext.begin(), toupper);
	
	auto index = opencv_formats.find(ext);
	if (index != opencv_formats.end()) {
		return index->second;
	}
	return false;
}

#if defined(WIN32) && defined(UNICODE)
bool validate_format_extension(std::wstring ext_w) {
	std::string ext;
	
	if(ext_w.at(0) == L'.')
		ext_w=ext_w.substr(1);
	
	ext.assign(ext_w.begin(), ext_w.end());
	return validate_format_extension(ext);
}
#endif


#ifdef HAVE_OPENCV_3_X
	#define w2xHaveImageWriter(x) cvHaveImageWriter(x)
#else
	#define w2xHaveImageWriter(x) cv::haveImageWriter(x)
#endif
	
//check for opencv formats
void check_opencv_formats()
{
	// Portable Network Graphics
	if (!w2xHaveImageWriter(".png"))
	{
		opencv_formats["PNG"] = false;
	}
	
	// JPEG Files
	if (!w2xHaveImageWriter(".jpg"))
	{
		opencv_formats["JPEG"] = false;
		opencv_formats["JPG"] = false;
		opencv_formats["JPE"] = false;
	}
	
	/* 
	Disabled due to vulnerabilities in Jasper codec, see: https://github.com/opencv/opencv/issues/14058
	// JPEG 2000 Files
	if (!w2xHaveImageWriter(".jp2"))
	{
		opencv_formats["JP2"] = false;
	}
	*/
	
	// WebP Files
	if (!w2xHaveImageWriter(".webp"))
	{
		opencv_formats["WEBP"] = false;
	}
	
	// TIFF Files
	if (!w2xHaveImageWriter(".tif"))
	{
		opencv_formats["TIF"] = false;
		opencv_formats["TIFF"] = false;
	}
	
	// OpenEXR Image Files
	if (!w2xHaveImageWriter(".exr"))
	{
		opencv_formats["EXR"] = false;
	}

	/* These formats are always available.
	// Windows Bitmaps (Always Supported)
	opencv_formats["BMP"] = true;
	opencv_formats["DIB"] = true;
	
	// Portable Image Format (Always Supported)
	opencv_formats["PBM"] = true;
	opencv_formats["PGM"] = true;
	opencv_formats["PPM"] = true;
	opencv_formats["PXM"] = true;
	opencv_formats["PNM"] = true;
	
	// Sun Rasters (Always Supported)
	opencv_formats["SR"] = true;
	opencv_formats["RAS"] = true;
	
	// Radiance HDR (Always Supported)
	opencv_formats["HDR"] = true;
	opencv_formats["PIC"] = true;
	*/
}

void debug_show_opencv_formats()
{
	std::cout << "This is a list of supported formats, it depends on which formats opencv has been built with." << std::endl ;
	for (auto const& x : opencv_formats)
	{
		std::cout << "\t" << std::setw(4) << x.first << " -> " << (x.second ? "Yes" : "No") << std::endl ;
	}
}



struct ConvInfo {
	std::string mode;
	int NRLevel;
	double scaleRatio;
	int blockSize;
	W2XConv* converter;
	int* imwrite_params;
	std::string outputFormat;
	ConvInfo(
		std::string mode,
		int NRLevel,
		double scaleRatio,
		int blockSize,
		W2XConv* converter,
		int* imwrite_params,
		std::string outputFormat
	) :	mode(mode),
		NRLevel(NRLevel),
		scaleRatio(scaleRatio),
		blockSize(blockSize),
		converter(converter),
		imwrite_params(imwrite_params),
		outputFormat(outputFormat) {};
};


std::string generate_output_location(std::string inputFileName, std::string outputFileName, std::string mode, int NRLevel, double scaleRatio, std::string outputFormat) {

	size_t lastSlashPos = outputFileName.find_last_of("/\\");
	size_t lastDotPos = outputFileName.find_last_of('.');

	if (strcmp(outputFileName.c_str(), "auto")==0) {
		outputFileName = inputFileName;
		size_t tailDot = outputFileName.find_last_of('.');
		if (tailDot != std::string::npos)
			outputFileName.erase(tailDot, outputFileName.length());
		outputFileName = outputFileName + "_[";
		
		if(mode.find("noise-scale") != mode.npos)
			outputFileName = outputFileName + "NS";
		
		if (mode.find("noise") != mode.npos) {
			outputFileName = outputFileName + "-L" + std::to_string(NRLevel) + "]";
		}
		else
			outputFileName = outputFileName + "]";
		
		if (mode.find("scale") != mode.npos) {
			outputFileName = outputFileName + "[x" + std::to_string(scaleRatio) + "]";
		}
		outputFileName += "." + outputFormat;
	}
	else if (outputFileName.back() == '/' || outputFileName.back() == '\\') {
		//outputFileName = output folder or "auto/"
		if ((!fs::is_directory(outputFileName))) {
			fs::create_directories(outputFileName);
		}
		//We pass tmp into generate_output_location because we will use the default way of naming processed files.
		//We will remove everything, in the tmp string, prior to the last slash to get the filename.
		//This removes all contextual information about where a file originated from if "recursive_directory" was enabled.
		std::string tmp = generate_output_location(inputFileName, "auto", mode, NRLevel, scaleRatio, outputFormat);
		//tmp = full formatted output file path
		size_t lastSlash = tmp.find_last_of("/\\");
		if (lastSlash != std::string::npos){
			tmp.erase(0, lastSlash+1);
		}

		outputFileName += tmp;
	}
	else if (lastDotPos == std::string::npos || (lastSlashPos != std::string::npos && lastDotPos < lastSlashPos)) {
		//e.g. ./test.d/out needs to be changed to ./test.d/out.png
		outputFileName += "." + outputFormat;
	}
	else if (lastSlashPos == std::string::npos || lastDotPos > lastSlashPos) {
		//We may have a regular output file here or something went wrong.
		//outputFileName is already what it should be thus nothing needs to be done.
		#ifdef HAVE_OPENCV
		if(validate_format_extension(outputFileName.substr(lastDotPos))==false){
			throw std::runtime_error("Unsupported output extension. outputFileName:" + outputFileName + " extension:" +outputFileName.substr(lastDotPos));
		}
		#endif
	}
	else {
		throw std::runtime_error("An unknown 'outputFileName' has been inserted into generate_output_location. outputFileName: " + outputFileName);
	}
	return outputFileName;
}

#if defined(WIN32) && defined(UNICODE)
std::wstring generate_output_location(std::wstring inputFileName, std::wstring outputFileName, std::string mode, int NRLevel, double scaleRatio, std::string outputFormat) {

	size_t lastSlashPos = outputFileName.find_last_of(L"/\\");
	size_t lastDotPos = outputFileName.find_last_of(L'.');

	if (wcscmp(outputFileName.c_str(), L"auto")==0) {
		outputFileName = inputFileName;
		size_t tailDot = outputFileName.find_last_of(L'.');
		if (tailDot != std::wstring::npos)
			outputFileName.erase(tailDot, outputFileName.length());
		outputFileName = outputFileName + L"_[";
		
		if(mode.find("noise-scale") != mode.npos)
			outputFileName = outputFileName + L"NS";
		
		if (mode.find("noise") != mode.npos) {
			outputFileName = outputFileName + L"-L" + std::to_wstring(NRLevel) + L"]";
		}
		else
			outputFileName = outputFileName + L"]";
		
		if (mode.find("scale") != mode.npos) {
			outputFileName = outputFileName + L"[x" + std::to_wstring(scaleRatio) + L"]";
		}
		outputFileName += L".";
		std::wstring of;
		of.assign(outputFormat.begin(), outputFormat.end());
		outputFileName += of;
	}	
	else if (outputFileName.back() == L'/' || outputFileName.back() == L'\\') {
		//outputFileName = output folder or "auto/"
		if ((!fs::is_directory(outputFileName))) {
			fs::create_directories(outputFileName);
		}
		//We pass tmp into generate_output_location because we will use the default way of naming processed files.
		//We will remove everything, in the tmp string, prior to the last slash to get the filename.
		//This removes all contextual information about where a file originated from if "recursive_directory" was enabled.
		std::wstring tmp = generate_output_location(inputFileName, L"auto", mode, NRLevel, scaleRatio, outputFormat);
		//tmp = full formatted output file path
		size_t lastSlash = tmp.find_last_of(L"/\\");
		if (lastSlash != std::wstring::npos){
			tmp.erase(0, lastSlash+1);
		}

		outputFileName += tmp;
	}
	else if (lastDotPos == std::wstring::npos || lastSlashPos != std::wstring::npos && lastDotPos < lastSlashPos) {
		//e.g. ./test.d/out needs to be changed to ./test.d/out.png
		outputFileName += L".";
		std::wstring of;
		of.assign(outputFormat.begin(), outputFormat.end());
		outputFileName += of;
	}
	else if (lastSlashPos == std::wstring::npos || lastDotPos > lastSlashPos) {
		//We may have a regular output file here or something went wrong.
		//outputFileName is already what it should be thus nothing needs to be done.
		#ifdef HAVE_OPENCV
		if(validate_format_extension(outputFileName.substr(lastDotPos))==false){
			throw std::runtime_error("Unsupported output extension.");
		}
		#endif
	}
	else {
		throw std::runtime_error("An unknown 'outputFileName' has been inserted into generate_output_location.");
	}
	return outputFileName;
}
#endif


void convert_file(ConvInfo info, fs::path inputName, fs::path output) {
	//std::cout << "Operating on: " << fs::absolute(inputName).string() << std::endl;
	std::string outputName = generate_output_location(fs::absolute(inputName).string(), output.string(), info.mode, info.NRLevel, info.scaleRatio, info.outputFormat);

	int _nrLevel = -1;

	if (strcmp(info.mode.c_str(), "noise")==0 || strcmp(info.mode.c_str(), "noise-scale")==0) {
		_nrLevel = info.NRLevel;
	}

	double _scaleRatio = 1;
	if (strcmp(info.mode.c_str(), "scale")==0 || strcmp(info.mode.c_str(), "noise-scale")==0) {
		_scaleRatio = info.scaleRatio;
	}

	int error = w2xconv_convert_file(info.converter,
			outputName.c_str(),
			fs::absolute(inputName).string().c_str(),
			_nrLevel,
			_scaleRatio,
			info.blockSize,
			info.imwrite_params
		);

	check_for_errors(info.converter, error);
}

#if defined(WIN32) && defined(UNICODE)
void convert_fileW(ConvInfo info, fs::path inputName, fs::path output) {
	//std::cout << "Operating on: " << fs::absolute(inputName).string() << std::endl;
	std::wstring outputName = generate_output_location(fs::absolute(inputName).wstring(), output.wstring(), info.mode, info.NRLevel, info.scaleRatio, info.outputFormat);

	int _nrLevel = -1;

	if (strcmp(info.mode.c_str(), "noise")==0 || strcmp(info.mode.c_str(), "noise-scale")==0) {
		_nrLevel = info.NRLevel;
	}

	double _scaleRatio = 1;
	if (strcmp(info.mode.c_str(), "scale")==0 || strcmp(info.mode.c_str(), "noise-scale")==0) {
		_scaleRatio = info.scaleRatio;
	}

	int error = w2xconv_convert_fileW(info.converter,
			outputName.c_str(),
			fs::absolute(inputName).wstring().c_str(),
			_nrLevel,
			_scaleRatio,
			info.blockSize,
			info.imwrite_params
		);

	check_for_errors(info.converter, error);
}
#endif



#if defined(WIN32) && defined(UNICODE)
//CommandLineToArgvA source from: http://alter.org.ua/en/docs/win/args/
char** CommandLineToArgvA( char* CmdLine, int* _argc ) {
	char** argv;
	char*  _argv;
	size_t   len;
	int   argc;
	char   a;
	size_t   i, j;
	bool  in_QM;
	bool  in_TEXT;
	bool  in_SPACE;
	len = strlen(CmdLine);
	i = ((len+2)/2)*sizeof(void*) + sizeof(void*);
	argv = (char**)malloc(i + (len+2)*sizeof(char));
	_argv = (char*)(((unsigned char*)argv)+i);
	argc = 0;
	argv[argc] = _argv;
	in_QM = false;
	in_TEXT = false;
	in_SPACE = true;
	i = 0;
	j = 0;
	while( a = CmdLine[i] ) {
		if(in_QM) {
			if(a == '\"') {
				in_QM = false;
			} else {
				_argv[j] = a;
				j++;
			}
		} else {
			switch(a) {
			case '\"':
				in_QM = true;
				in_TEXT = true;
				if(in_SPACE) {
					argv[argc] = _argv+j;
					argc++;
				}
				in_SPACE = false;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				if(in_TEXT) {
					_argv[j] = '\0';
					j++;
				}
				in_TEXT = false;
				in_SPACE = true;
				break;
			default:
				in_TEXT = true;
				if(in_SPACE) {
					argv[argc] = _argv+j;
					argc++;
				}
				_argv[j] = a;
				j++;
				in_SPACE = false;
				break;
			}
		}
		i++;
	}
	_argv[j] = '\0';
	argv[argc] = nullptr;
	
	(*_argc) = argc;
	return argv;
}

#endif



#if defined(WIN32) && defined(UNICODE)
int wmain(void)
#else
int main(int argc, char** argv)
#endif
{
	int ret = 1;
#if defined(WIN32) && defined(UNICODE)
	int argc = 0;
	char **argv = CommandLineToArgvA(GetCommandLineA(), &argc);
	int argc_w = 0;
	std::wstring inputFileName, outputFileName=L"auto";
	LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
	
	for (int ai = 1; ai < argc_w; ai++) {
		if ((wcscmp(argv_w[ai], L"-i") == 0) || (wcscmp(argv_w[ai], L"--input") == 0)) {
			if( ai+1 < argc_w )
				inputFileName = std::wstring(argv_w[ai+1]);
			continue;
		}
		else if ((wcscmp(argv_w[ai], L"-o") == 0) || (wcscmp(argv_w[ai], L"--output") == 0)) {
			if( ai+1 < argc_w )
				outputFileName = std::wstring(argv_w[ai+1]);
			continue;
		}
		else if ((wcscmp(argv_w[ai], L"-l") == 0) || (wcscmp(argv_w[ai], L"--list-processor") == 0)) {
			dump_procs();
			return 0;
		}
		#ifdef HAVE_OPENCV
		else if ((wcscmp(argv_w[ai], L"--list-opencv-formats") == 0)) {
			check_opencv_formats();
			debug_show_opencv_formats();
			return 0;
		}
		#endif
	}
#else
	for (int ai = 1; ai < argc; ai++) {
		if ((strcmp(argv[ai], "--list-processor") == 0) || (strcmp(argv[ai], "-l") == 0)) {
			dump_procs();
			return 0;
		}
		#ifdef HAVE_OPENCV
		if (strcmp(argv[ai], "--list-opencv-formats") == 0) {
			check_opencv_formats();
			debug_show_opencv_formats();
			return 0;
		}
		#endif
	}
#endif	
	
#ifdef HAVE_OPENCV
	check_opencv_formats();
#endif
	
	// definition of command line arguments
	TCLAP::CmdLine cmd("waifu2x OpenCV Fork - https://github.com/DeadSix27/waifu2x-converter-cpp", ' ', std::string(GIT_TAG) + " (" + GIT_BRANCH + "-" + GIT_COMMIT_HASH + ")", true);
	cmd.setOutput(new CustomFailOutput());

	TCLAP::ValueArg<std::string> cmdInput("i", "input",
		"path to input image file or directory (you should use the full path)", true, "",
		"string", cmd);

	TCLAP::ValueArg<std::string> cmdOutput("o", "output",
		"path to output image file or directory  (you should use the full path)", false,
		"auto", "string", cmd);

	TCLAP::ValueArg<bool> cmdRecursiveDirectoryIterator("r", "recursive-directory",
		"Search recursively through directories to find more images to process.\nIf this is set to 0 it will only check in the directory specified if the input is a directory instead of an image.\nYou mustn't supply this argument with something other than 0 or 1.", false,
		0, "bool", cmd);


	TCLAP::SwitchArg cmdQuiet("s", "silent", "Enable silent mode.", cmd, false);

	std::vector<std::string> cmdModeConstraintV;
	cmdModeConstraintV.push_back("noise");
	cmdModeConstraintV.push_back("scale");
	cmdModeConstraintV.push_back("noise-scale");
	TCLAP::ValuesConstraint<std::string> cmdModeConstraint(cmdModeConstraintV);
	TCLAP::ValueArg<std::string> cmdMode("m", "mode", "image processing mode",
		false, "noise-scale", &cmdModeConstraint, cmd);

	std::vector<int> cmdNRLConstraintV;
	cmdNRLConstraintV.push_back(0);
	cmdNRLConstraintV.push_back(1);
	cmdNRLConstraintV.push_back(2);
	cmdNRLConstraintV.push_back(3);
	TCLAP::ValuesConstraint<int> cmdNRLConstraint(cmdNRLConstraintV);
	TCLAP::ValueArg<int> cmdNRLevel("", "noise-level", "noise reduction level",
		false, 1, &cmdNRLConstraint, cmd);

	TCLAP::ValueArg<double> cmdScaleRatio("", "scale-ratio",
		"custom scale ratio", false, 2.0, "double", cmd);

	TCLAP::ValueArg<std::string> cmdModelPath("", "model-dir",
		"path to custom model directory (don't append last / )", false,
		DEFAULT_MODELS_DIRECTORY, "string", cmd);

	TCLAP::ValueArg<int> cmdNumberOfJobs("j", "jobs",
		"number of threads launching at the same time", false, 0, "integer",
		cmd);

	TCLAP::ValueArg<int> cmdTargetProcessor("p", "processor",
		"set target processor", false, -1, "integer",
		cmd);

	TCLAP::SwitchArg cmdForceOpenCL("", "force-OpenCL",
		"force to use OpenCL on Intel Platform",
		cmd, false);

	TCLAP::SwitchArg cmdDisableGPU("", "disable-gpu", "disable GPU", cmd, false);

	TCLAP::ValueArg<int> cmdBlockSize("", "block-size", "block size",
		false, 0, "integer", cmd);
		
	TCLAP::ValueArg<int> cmdImgQuality("q", "image-quality", "JPEG & WebP Compression quality (0-101, 0 being smallest size and lowest quality), use 101 for lossless WebP",
		false, 90, "0-101", cmd);
		
	TCLAP::ValueArg<int> cmdPngCompression("c", "png-compression", "Set PNG compression level (0-9), 9 = Max compression (slowest & smallest)",
		false, 5, "0-9", cmd);
		
	TCLAP::ValueArg<std::string> cmdOutputFormat("f", "output-format", "The format used when running in recursive/folder mode\nSee --list-opencv-formats for a list of supported formats/extensions.",
		false, "png", "png,jpg,webp,...", cmd);
		
	TCLAP::SwitchArg cmdListProcessor("l", "list-processor", "dump processor list", cmd, false);
	
	#ifdef HAVE_OPENCV
	TCLAP::SwitchArg showOpenCVFormats("", "list-opencv-formats", "dump opencv supported format list", cmd, false);
	#endif

	// definition of command line argument : end

	// parse command line arguments
	try {
		cmd.parse(argc, argv);
	}
	catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
		std::cerr << "Error : cmd.parse() threw exception" << std::endl;
		std::exit(-1);
	}
	
	//Check Quality/Compression option ranges.
	if (cmdPngCompression.getValue() < 0 || cmdPngCompression.getValue() > 9)
	{
		std::cout << "Error: PNG Compression level range is 0-9, 9 being the slowest and resulting in the smallest file size." << std::endl;
		std::exit(-1);
	}
	if (cmdImgQuality.getValue() < 0 || cmdImgQuality.getValue() > 101)
	{
		std::cout << "Error: JPEG & WebP Compression quality range is 0-101! (0 being smallest size and lowest quality), use 101 for lossless WebP" << std::endl;
		std::exit(-1);
	}
	#ifdef HAVE_OPENCV
	if(validate_format_extension(cmdOutputFormat.getValue())==false){
		printf("Unsupported output extension: %s\nUse option --list-opencv-formats to see a list of supported formats", cmdOutputFormat.getValue().c_str());
		std::exit(-1);
	}
	#endif
	
	//We need to do this conversion because using a TCLAP::ValueArg<fs::path> can not handle spaces.
#if defined(WIN32) && defined(UNICODE)
	fs::path input = inputFileName;
	std::wstring tmpOutput = outputFileName;
	if (fs::is_directory(input) && (tmpOutput.back() != L'/') && wcscmp(tmpOutput.c_str(), L"auto")!=0) {
		tmpOutput += L"/";
	}
#else
	fs::path input = cmdInput.getValue();
	std::string tmpOutput = cmdOutput.getValue();
	if (fs::is_directory(input) && (tmpOutput.back() != '/') && strcmp(tmpOutput.c_str(), "auto")!=0) {
		tmpOutput += "/";
	}
#endif
	fs::path output = tmpOutput;
	
	enum W2XConvGPUMode gpu = W2XCONV_GPU_AUTO;

	if (cmdDisableGPU.getValue()) {
		gpu = W2XCONV_GPU_DISABLE;
	}
	else if (cmdForceOpenCL.getValue()) {
		gpu = W2XCONV_GPU_FORCE_OPENCL;
	}

	W2XConv *converter;
	size_t num_proc;
	w2xconv_get_processor_list(&num_proc);
	int proc = cmdTargetProcessor.getValue();
	bool verbose = !cmdQuiet.getValue();

	if (proc != -1 && proc < num_proc) {
		converter = w2xconv_init_with_processor(proc, cmdNumberOfJobs.getValue(), verbose);
	}
	else {
		converter = w2xconv_init(gpu, cmdNumberOfJobs.getValue(), verbose);
	}
	
	int imwrite_params[6];
	imwrite_params[0] = cv::IMWRITE_WEBP_QUALITY;
	imwrite_params[1] = cmdImgQuality.getValue();
	imwrite_params[2] = cv::IMWRITE_JPEG_QUALITY;
	imwrite_params[3] = cmdImgQuality.getValue();
	imwrite_params[4] = cv::IMWRITE_PNG_COMPRESSION;
	imwrite_params[5] = cmdPngCompression.getValue();

	ConvInfo convInfo(cmdMode.getValue(), cmdNRLevel.getValue(), cmdScaleRatio.getValue(), cmdBlockSize.getValue(), converter, imwrite_params, cmdOutputFormat.getValue());
	
	double time_start = getsec();

	switch (converter->target_processor->type) {
	case W2XCONV_PROC_HOST:
		printf("CPU: %s\n",
			converter->target_processor->dev_name);
		break;

	case W2XCONV_PROC_CUDA:
		printf("CUDA: %s\n",
			converter->target_processor->dev_name);
		break;

	case W2XCONV_PROC_OPENCL:
		printf("OpenCL: %s\n",
			converter->target_processor->dev_name);
		break;
	}

	bool recursive_directory_iterator = cmdRecursiveDirectoryIterator.getValue();
	int error = w2xconv_load_models(converter, cmdModelPath.getValue().c_str());
	check_for_errors(converter, error);

	//This includes errored files.
	int numFilesProcessed = 0;
	int numErrors = 0;
	
	if (fs::is_directory(input) == true) {
		
		//Build files list
		std::deque<fs::path> files_list;
		std::cout << "We're going to be operating in a directory. dir:" << fs::absolute(input) << std::endl;
		
		if (recursive_directory_iterator) {
			for (auto & inputFile : fs::recursive_directory_iterator(input)) {
				if (!fs::is_directory(inputFile)) {
					std::string ext = inputFile.path().extension().string();
					if (validate_format_extension(ext)) {
						files_list.push_back(inputFile);
					}
					else {
						std::cout << "Skipping file '" << inputFile.path().filename().string() <<
								"' for having an unsupported file extension (" << ext << ")" << std::endl;
						continue;
					}
				}
			}
		}
		else {
			for (auto & inputFile : fs::directory_iterator(input)) {
				if (!fs::is_directory(inputFile)) {
					std::string ext = inputFile.path().extension().string();
					if (validate_format_extension(ext)) {
						files_list.push_back(inputFile);
					}
					else {
						std::cout << "Skipping file '" << inputFile.path().filename().string() <<
								"' for having an unsupported file extension (" << ext << ")" << std::endl;
						continue;
					}
				}
			}
		}

		//Proceed by list
		double timeAvg = 0.0;
		int files_count = static_cast<int>(files_list.size());
		for (auto &fn : files_list) {
			++numFilesProcessed;
			double time_file_start = getsec();

			std::cout << "[" << numFilesProcessed << "/" << files_count << "] " << fn.filename() << (verbose ? "\n" : " Ok. ");

			try {
#if defined(WIN32) && defined(UNICODE)
				convert_fileW(convInfo, fn, output);
#else
				convert_file(convInfo, fn, output);
#endif
			}
			catch (const std::exception& e) {
				numErrors++;
				std::cout << e.what() << std::endl;
			}

			//Calculate and out elapsed time
			double time_end = getsec();
			double time_file = time_end - time_file_start;
			double time_all = time_end - time_start;
			if (timeAvg > 0.0)
				timeAvg = time_all / numFilesProcessed;
			else
				timeAvg = time_all;
			double elapsed = files_count * timeAvg - time_all;
			int el_h = (int) elapsed / (60 * 60);
			int el_m = (int) (elapsed - el_h * 60 * 60) / 60;
			int el_s = (int) (elapsed - el_h * 60 * 60 - el_m * 60);
			std::cout << "Elapsed: ";
			if (el_h)
				std::cout << el_h << "h";
			if (el_m)
				std::cout << el_m << "m";
			std::cout << el_s << "s file: " << time_file << "s avg: " << timeAvg << "s" << std::endl;
		}


	}
	else {
		numFilesProcessed++;
		try {
#if defined(WIN32) && defined(UNICODE)
			convert_fileW(convInfo, input, output);
#else
			convert_file(convInfo, input, output);
#endif
		}
		catch (const std::exception& e) {
			numErrors++;
			std::cout << e.what() << std::endl;
		}
	}
	


	{
		double time_end = getsec();

		double gflops_proc = (converter->flops.flop / (1000.0*1000.0*1000.0)) / converter->flops.filter_sec;
		double gflops_all = (converter->flops.flop / (1000.0*1000.0*1000.0)) / (time_end - time_start);

		std::cout << "process successfully done! (all:"
			<< (time_end - time_start) << "[sec], "
			<< numFilesProcessed << " [files processed], "
			<< numErrors << " [files errored], "
			<< gflops_all << "[GFLOPS], filter:"
			<< converter->flops.filter_sec
			<< "[sec], " << gflops_proc << "[GFLOPS])" << std::endl;
	}

	w2xconv_fini(converter);

#if defined(WIN32) && defined(UNICODE)
	free(argv);
	LocalFree(argv_w);
#endif

	return 0;
}
