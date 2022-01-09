main: main.cpp dmxctl/interface.o
	g++ main.cpp dmxctl/interface.o -o main -lyaml-cpp -std=c++20

dmxctl/interface.o: dmxctl/interface.cpp dmxctl/interface.h
	make -C dmxctl

debug: main.cpp dmxctl/interface.cpp dmxctl/interface.h
	make -C dmxctl debug
	g++ main.cpp dmxctl/interface.o -o main -lyaml-cpp -std=c++20 -g

clean:
	rm -f *.o */*.o main
