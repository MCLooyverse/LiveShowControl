#include <iostream>
#include <vector>
#include <string>
using namespace std::literals::string_literals;
#include <cstdio>
#include <chrono>
using namespace std::literals::chrono_literals;
using Clock = std::chrono::steady_clock;
#include <sstream>

#include <mcl/ttymanip.h>
#include <mcl/miscutils.h>
#include "dmxctl/interface.h"



enum class Escd {
	Up, Down, Right, Left,
	None
};

std::string getEscd();


std::string nameVal(const lsc::Channel&);


int main(int argc, char** argv)
{
	std::vector<std::string> args;
	for (int i = 1; i < argc; i++)
		args.emplace_back(argv[i]);
	lsc::DmxCtl con{ args };


	//std::vector<int> as{0, 0};
	
	auto spots = con["Spot"s];
	int i = 0;
	auto pan = [&](){
		return (*spots[i])[lsc::Channel::TargetType::pan];
	};
	auto tilt = [&](){
		return (*spots[i])[lsc::Channel::TargetType::tilt];
	};


	mcl::tty::init();
	for (;;)
	{
		std::cout 
			<< spots[i]->name << ":\n"
			<< "\t" << nameVal(*pan()[0])
			<< "\t" << nameVal(*pan()[1]) << "\n"
			<< "\t" << nameVal(*tilt()[0])
			<< "\t" << nameVal(*tilt()[1]) << "\n";
		std::cout.flush();



		// Fetch input
		// - Switch light
		// - Move light

		switch (getchar())
		{
		case 'n':
			//++i %= as.size();
			if (++i == spots.size())
				i = 0;

			break;
		case 'p':
			if (i-- == 0)
				i = spots.size() - 1;
			break;

		case 't':
			(*spots[i])[lsc::Channel::master].front()->value ^= 0xFF;
			break;

		case '\x1B': {
			auto s = getEscd();
			if (s == "[A")
			{
				if (tilt()[0]->value < 255)
					tilt()[0]->value += 1;
			}
			else if (s == "[1;2A") //Shift+UP
			{
				if (tilt()[1]->value < 0xF0)
					tilt()[1]->value += 16;
				else
					tilt()[1]->value = 255;
			}
			else if (s == "[1;5A") //Ctrl+UP
			{
				if (tilt()[1]->value < 255)
					tilt()[1]->value += 1;
				else if (tilt()[0]->value < 255)
				{
					tilt()[0]->value += 1;
					tilt()[1]->value += 1;
				}
			}
			else if (s == "[B")
			{
				if (tilt()[0]->value > 0)
					tilt()[0]->value -= 1;
			}
			else if (s == "[1;2B")
			{
				if (tilt()[0]->value > 15)
					tilt()[0]->value -= 16;
				else
					tilt()[0]->value = 0;
			}
			else if (s == "[1;5B")
			{
				if (tilt()[1]->value > 0)
					tilt()[1]->value -= 1;
				else if (tilt()[0]->value > 0)
				{
					tilt()[0]->value -= 1;
					tilt()[1]->value -= 1;
				}
			}
			else if (s == "[C")
			{
				if (pan()[0]->value < 255)
					pan()[0]->value += 1;
			}
			else if (s == "[1;2C")
			{
				if (pan()[0]->value < 0xF0)
					pan()[0]->value += 16;
				else
					pan()[0]->value = 255;
			}
			else if (s == "[1;5C") //Ctrl+UP
			{
				if (pan()[1]->value < 255)
					pan()[1]->value += 1;
				else if (pan()[0]->value < 255)
				{
					pan()[0]->value += 1;
					pan()[1]->value += 1;
				}
			}
			else if (s == "[D")
			{
				if (pan()[0]->value > 0)
					pan()[0]->value -= 1;
			}
			else if (s == "[1;2C")
			{
				if (pan()[0]->value > 15)
					pan()[0]->value -= 16;
				else
					pan()[0]->value = 0;
			}
			else if (s == "[1;5D")
			{
				if (pan()[1]->value > 0)
					pan()[1]->value -= 1;
				else if (pan()[0]->value > 0)
				{
					pan()[0]->value -= 1;
					pan()[1]->value -= 1;
				}
			}

		} break;
		}


		// Dispatch

		con.writeOut();

		std::cout << AEC_CUU(3) AEC_ERASE_SE;

		//std::cout << "\r" << i << ": " << as[i] << AEC_ERASE_LE;
	}
}

std::string getEscd()
{
	std::string retr;
	do retr += getchar();
	while (!std::isalpha(retr.back()));

	return retr;
}

std::string nameVal(const lsc::Channel& c)
{
	char hexits[] = "0123456789ABCDEF";
	std::ostringstream s;
	s << c.name << '[' << c.valindex << "]: "
		<< std::string{{hexits[c.value & 0xF], hexits[c.value >> 4]}};
	return s.str();
}
