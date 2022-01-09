std := c++20
pwd != pwd

all: dmxctl-child interface.o

interface.o: interface.cpp interface.h
	g++ -DCOMPILE_TIME_PWD='"$(pwd)"' -c interface.cpp -lyaml-cpp -std=c++20 -Wno-narrowing

dmxctl-child: dmxctl-child.cpp
	g++ dmxctl-child.cpp -o dmxctl-child -std=c++20

debug: interface.cpp interface.h
	g++ -DCOMPILE_TIME_PWD='"$(pwd)"' -c interface.cpp -lyaml-cpp -std=c++20 -Wno-narrowing -g
