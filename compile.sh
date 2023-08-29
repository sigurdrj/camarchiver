g++ -std=c++2a -Wall -O2 -c main.cpp -o camarchiver.o && g++ -std=c++2a camarchiver.o -Wall -O2 -lpthread -lcurl -lcurlpp -o camarchiver -lsfml-system
