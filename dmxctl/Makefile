std := c++2a
pwd != pwd

all: dmxctl-child interface.o

interface.o: interface.cpp interface.h
	g++ -DCOMPILE_TIME_PWD='"$(pwd)"' -c interface.cpp -lyaml-cpp -std=$(std) -Wno-narrowing

dmxctl-child: dmxctl-child.cpp
	g++ dmxctl-child.cpp -o dmxctl-child -std=$(std)

debug: interface.cpp interface.h
	g++ -DCOMPILE_TIME_PWD='"$(pwd)"' -c interface.cpp -lyaml-cpp -std=$(std) -Wno-narrowing -g
