main: main.cpp dmxctl/interface.o
	g++ main.cpp dmxctl/interface.o -o main -lyaml-cpp -std=c++2a

dmxctl/interface.o: dmxctl/interface.cpp dmxctl/interface.h
	make -C dmxctl

debug: main.cpp dmxctl/interface.cpp dmxctl/interface.h
	make -C dmxctl debug
	g++ main.cpp dmxctl/interface.o -o main -lyaml-cpp -std=c++2a -g

clean:
	rm -f *.o */*.o main
