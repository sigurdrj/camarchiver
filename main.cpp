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

#include <iostream>
#include <string>

#include "camarchiver.hpp"
#include "flagparser/flagparser.hpp"

using std::string;

void usage(){
	std::cout << "Usage: camarchiver [options] cameralisting\n";
	std::cout << "\toptions:\n";
	std::cout << "\t\t--silent\n";
	std::cout << "\t\t--pipe\n";
	std::cout << "\t\t--loglevel=off normal full\n";
}

int main(int argc, char *argv[]){
	if (argc<2){
		usage();
		return 1;
	}

	string logLevel = "normal";

	flagparser::FlagList flags = flagparser::get_flags(argc, argv);
	bool silent = flags.get_flag_position("silent", flags.wordFlags) != flags.wordFlags.end();
	bool pipe = flags.get_flag_position("pipe", flags.wordFlags) != flags.wordFlags.end();
	if (flags.get_value_flag_position("loglevel") != flags.valueFlags.end())
		logLevel = flags.valueFlags[flags.get_value_flag_position("loglevel") - flags.valueFlags.begin()].value;

	const string cameralistingFilename = flags.plainFlags[0];

	camarchiver::CamArchiver archiver;
	if (!archiver.load_cameralisting(cameralistingFilename))
		return 2;

	archiver.silent=silent;
	archiver.pipe=pipe;
	archiver.logLevel=logLevel;

	if (!archiver.start_archiving()){
		if (!silent)
			std::cout << "No cameras to archive, empty cameralisting?\n";
		return 3;
	}

	if (!silent)
		std::cout << "Type q to quit\n";

	for (;;){
		char input;
		std::cin.get(input);
		if (input == 'q')
			break;
	}

	archiver.stop_archiving();
}
