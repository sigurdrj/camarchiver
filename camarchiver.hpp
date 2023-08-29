/*
 * MIT License
 * 
 * Copyright (c) 2020 Sigurd Rønningen Jenssen
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

/* camarchiver.hpp
 * Sigurd Rønningen Jenssen 2020-2023
 * camarchiver - Program which archives web camera images from multiple cameras at a time
*/

#ifndef CAMARCHIVER_HPP
#define CAMARCHIVER_HPP

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <SFML/System.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <filesystem>
#include <mutex>
#include <condition_variable>

using std::string;
using std::vector;

namespace camarchiver{
	std::mutex cameraWaitMutex;
	std::condition_variable waitInterrupt;

	template <class T>
	string to_str(const T val){
		std::ostringstream convert;
		convert << val;
		return convert.str();
	}

	bool stream_is_empty(std::stringstream &stream){
		stream.seekg(0, std::ios::beg);
		return stream.eof();
	}

	// TODO Make this compare the data in chunks (add an unsigned bufferSize argument)
	bool stream_is_identical_to_file(std::stringstream &stream, const string filename){
		std::ifstream file(filename, std::ios::binary);

		if (!file)
			return false;

		char buf1,buf2;
		while (stream.get(buf1) && file.get(buf2)){
			if (buf1 != buf2){
				stream.seekg(0, std::ios::beg);
				return false;
			}
		}
		stream.seekg(0, std::ios::beg);
		return true;
	}

	class Camera{
		string get_date(){
			const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			const string timeStr = std::ctime(&time);
			return timeStr.substr(0, timeStr.size()-1); // Remove trailing newline
		}

		public:

		bool exitOnNextArchive = false;

		string url;
		string dirPath;
		string subDir;
		string fileExtension;
		double delay=0; // Seconds

		bool silent=false;
		bool pipe=false;
		string logLevel = "medium";

		long downloadTimeout; // Seconds
		string useragent;

		unsigned long long tooLongDownloadCount=0;

		// TODO add cameralisting config line to only get current index on first archive
		unsigned long long get_current_index(){
			const std::filesystem::path dirToIterate = std::filesystem::path(dirPath) / subDir;

			std::filesystem::directory_iterator iterator(dirToIterate);

			unsigned long long out=0;
			for (auto &p : iterator){
				if (!std::filesystem::is_regular_file(p))
					continue; // Only look at files

				unsigned long long pathIndex;
				try{
					pathIndex = std::stoi(p.path().stem());
				} catch (std::invalid_argument &){
					continue; // Ignore files which aren't indexed images
				}

				out = pathIndex > out ? pathIndex : out;
			}

			return out;
		}

		bool archive_single_image(){
			const std::filesystem::path fullDirPath = std::filesystem::path(dirPath) / subDir;

			try{
				std::filesystem::create_directories(fullDirPath);
			} catch(std::filesystem::filesystem_error &){
				if (!silent)
					std::cout << "Insufficient permission to create directories " << fullDirPath << '\n';
				return false;
			}

			std::stringstream imageStream;

			curlpp::Easy client;
			client.setOpt(new curlpp::options::Url(url));
			client.setOpt(new curlpp::options::WriteStream(&imageStream));

			client.setOpt(new curlpp::options::Timeout(downloadTimeout));
			client.setOpt(new curlpp::options::UserAgent(useragent));
			client.setOpt(new curlpp::options::SslVerifyHost(false));

			bool curlppError = false;
			try{
				client.perform();
			} catch(curlpp::LibcurlRuntimeError &e){
				curlppError = true;
				std::cout << e.what() << '\n';
			}

			const unsigned long long newIndex = get_current_index() + 1;
			const std::filesystem::path imageFilePath = fullDirPath / (to_str(newIndex) + '.' + fileExtension);
			const std::filesystem::path previousImageFilePath = fullDirPath / (to_str(newIndex-1) + '.' + fileExtension);

			bool doWriteFile=false;

			if (logLevel != "off"){
				std::ofstream logFile(fullDirPath / "log", std::ofstream::app);
				if (!logFile){
					if (!silent)
						std::cout << "Could not write log file " << std::filesystem::path(fullDirPath/"log") << '\n';
					return false;
				}

				string logLine;

				string fullLogSection="";
				if (logLevel == "full")
					fullLogSection = "{delay:" + to_str(delay) + ",url:\"" + url + "\",useragent:\"" + useragent + "\",toolongdownloadcount:" + to_str(tooLongDownloadCount) + "} ";

				if (curlppError){
					logLine = "Timed out " + fullLogSection + "(path " + string(imageFilePath) + ')';
				} else if (stream_is_empty(imageStream)){
					logLine = "Empty     " + fullLogSection + "(path " + string(imageFilePath) + ')';
				} else if (stream_is_identical_to_file(imageStream, previousImageFilePath)){
					logLine = "Duplicate " + fullLogSection + "(path " + string(imageFilePath) + ')';
				} else{
					logLine = "Success   " + fullLogSection + "(path " + string(imageFilePath) + ')';
					doWriteFile = true;
				}

				logFile << get_date() << ' ' << logLine << '\n';
			} else{
				doWriteFile = (!stream_is_empty(imageStream)) && (!stream_is_identical_to_file(imageStream, previousImageFilePath));
			}

			// Write from memory to disk
			if (doWriteFile){
				std::fstream imageFile(imageFilePath, std::ofstream::out);
				if (!imageFile)
					return false;
				imageFile << imageStream.rdbuf();
			}

			return true;
		}

		void run(){
			while (!exitOnNextArchive){
				sf::Clock timer;

				if (!archive_single_image())
					return;

				const unsigned long long timeSpentDownloading = timer.getElapsedTime().asMicroseconds();
				const unsigned long long delayInMicroSeconds = delay*1000000;
				if (timeSpentDownloading > delayInMicroSeconds){
					++tooLongDownloadCount;
					continue;
				}
				const unsigned long long timeToSleep = delayInMicroSeconds - timeSpentDownloading;

				std::unique_lock <std::mutex> locker(cameraWaitMutex);
				waitInterrupt.wait_for(locker, std::chrono::microseconds(timeToSleep), [this](){return exitOnNextArchive;});
			}
		}
	};

	enum cameralistingParserState{
		stateDirPath,
		stateURL,
		stateFileExtension,
		stateSubDir,
		stateDelay
	};

	struct CameralistingParserFail{
		string errorStr; // "FileNotOpen", "InvalidCameralisting"
		unsigned long long lineNumber;

		CameralistingParserFail(const string newErrorStr, const unsigned long long newLineNumber=0){
			errorStr=newErrorStr;
			lineNumber=newLineNumber;
		}
	};

	vector <Camera> get_parsed_cameralisting(const string cameralistingFilename){
		vector <Camera> out;

		std::ifstream cameralistingFile(cameralistingFilename);

		if (!cameralistingFile){
			CameralistingParserFail f("FileNotOpen");
			throw f;
		}

		Camera tmp;

		string dirPath;

		// TODO Make the useragent a config thing
		string globalUseragent = "Camarchiver";
		string localUseragent;
		long globalTimeout = 5;
		long localTimeout;

		bool hasLocalUseragent = false;
		bool hasLocalTimeout = false;

		cameralistingParserState state = stateDirPath;
		string line;
		for (unsigned long long lineNumber=0; std::getline(cameralistingFile, line); ++lineNumber){
			if ((line[0] == '#') || (line.empty()))
				continue;

			// Settings (for HTTP client)
			if (line[0] == '^'){
				line.erase(0,1); // Remove prefix

				if (line.starts_with("httpclient.useragent ")){
					const string valueStr = line.substr(20);

					if (state == stateDirPath){
						globalUseragent = valueStr;
					} else if (state == stateURL){
						localUseragent = valueStr;
						hasLocalUseragent = true;
					}
				} else if (line.starts_with("httpclient.timeout ")){
					const string valueStr = line.substr(18);
					long valueLong;
				
					try{
						valueLong = std::stoi(valueStr);
					} catch (std::invalid_argument &){
						CameralistingParserFail f("InvalidCameralisting", lineNumber);
						throw f;
					}

					if (state == stateDirPath){
						globalTimeout = valueLong;
					} else if (state == stateURL){
						localTimeout = valueLong;
						hasLocalTimeout = true;
					}
				}

				continue;
			}

			switch (state){
				case stateURL:
					tmp.url = line;
					break;

				case stateFileExtension:
					tmp.fileExtension = line;
					break;

				case stateSubDir:
					tmp.subDir = line;
					break;

				case stateDelay:
					try{
						tmp.delay = std::stod(line);
					} catch (std::invalid_argument &){
						CameralistingParserFail f("InvalidCameralisting", lineNumber);
						throw f;
					}
					break;
				default:
					dirPath = line;
					break;
			}

			if (state == stateDelay){ // End of camera in cameralisting
				tmp.dirPath = dirPath;
				if (hasLocalUseragent)
					tmp.useragent = localUseragent;
				else
					tmp.useragent = globalUseragent;

				if (hasLocalTimeout)
					tmp.downloadTimeout = localTimeout;
				else
					tmp.downloadTimeout = globalTimeout;

				out.push_back(tmp);
				hasLocalUseragent = false;
			}

			state = cameralistingParserState((state+1)%5);
			if (state == stateDirPath)
				state = stateURL;
		}

		return out;
	}

	class CamArchiver{
		vector <Camera> cameras;
		vector <std::thread> threads;
		public:

		bool silent=false;
		bool pipe=false;
		string logLevel = "medium";

		bool load_cameralisting(const string cameralistingFilename){
			try{
				cameras = get_parsed_cameralisting(cameralistingFilename);
			} catch(CameralistingParserFail f){
				if (silent)
					return false;

				std::cout << "Invalid cameralisting | ";

				if (f.errorStr == "InvalidCameralisting")
					std::cout << "Found error on line " << f.lineNumber;
				else if (f.errorStr == "FileNotOpen")
					std::cout << "Could not open file " << cameralistingFilename;
				std::cout << '\n';

				return false;
			}

			return true;
		}

		bool start_archiving(){
			if (cameras.empty())
				return false;

			for (Camera &camera : cameras){
				camera.silent = silent;
				camera.pipe = pipe;
				camera.logLevel = logLevel;
			}

			for (Camera &camera : cameras)
				threads.push_back(std::thread(&Camera::run, &camera));

			return true;
		}

		void stop_archiving(){
			{
				std::lock_guard<std::mutex> locker(cameraWaitMutex);
				for (Camera &camera : cameras)
					camera.exitOnNextArchive = true;
				waitInterrupt.notify_all();
			}

			for (std::thread &thread : threads)
				thread.join();
			threads.clear();
		}
	};
}

#endif // CAMARCHIVER_HPP
