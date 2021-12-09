#include <iostream>
#include <cctype>
#include <yaml-cpp/yaml.h>
//#include <mcl/ttymanip.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>

#include "dmxctl/interface.h"
//#include "sfx-ctl.h"

#include <chrono>
using Clock = std::chrono::steady_clock;
using namespace std::literals::chrono_literals;
#include <thread>
#include <poll.h>
#include <signal.h>


class NullMod : public lsc::Controller
{
public:
	NullMod(const std::vector<std::string>&) { }
	void execute(const std::string& inst, const std::vector<std::string>& args) { }
	std::string state() const { return "good"; }
	std::string verify(const std::string& inst, const std::vector<std::string>& args) { return ""; }
	operator bool() const { return 1; }
};

struct Instruction
{
	enum Timing {
		enter, simul, after
	} timing;

	std::string handler;
	std::string command;

	std::vector<std::string> args;

	Instruction(Timing t, std::string h, std::string c, std::vector<std::string> a)
		: timing{t}, handler{h}, command{c}, args{a} { } //Supposed to be redundant
};


std::vector<std::vector<std::string>> TokenizeScript(const std::string& filename);
std::vector<Instruction> secondProcessing(const std::vector<std::vector<std::string>>& lines);

std::map<std::string, lsc::Controller*> cons;

termios oldSetting;

/*
void onInterrupt(int sig)
{
	for (auto& con : cons)
		delete con.second;
	tcsetattr(0, TCSANOW, &oldSetting);
	exit(0);
}
*/

void deleteCons()
{
	for (auto& con : cons)
		delete con.second;
}
void resettty()
{
	tcsetattr(0, TCSANOW, &oldSetting);
}


int main(int argc, char** argv)
{
	std::atexit(deleteCons);

	if (argc != 2)
	{
		std::cerr << "bad args\n";
		return 1;
	}

	auto lines = TokenizeScript(argv[1]);
	auto instructions = secondProcessing(lines);
	if (instructions.size() == 0)
	{
		std::cerr << "Give actual instructions.\n";
		return 1;
	}

	/*
	struct sigaction sa;
	sa.sa_handler = onInterrupt;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	*/



	//mcl::tty::init(0);


	/*
	pollfd* stdinPoll = new pollfd{0, POLLIN, 0};
	std::stringstream msgbuf{""};
	*/

	auto tty = new termios;
	tcgetattr(0, &oldSetting);
	std::atexit(resettty);

	tcgetattr(0, tty);
	tty->c_lflag &= ~(ECHO | ICANON);
	tty->c_cc[VMIN]  = 0;
	tty->c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, tty);

	auto isp = instructions.begin();

	const auto tick = 50ms;
	auto next = Clock::now();
	for (bool running = 1; running; )
	{
		if (isp == instructions.end())
			isp = instructions.begin();
		//Make sure loop starts steadily, no matter how long each particular
		//goround takes.
		std::this_thread::sleep_until(next += tick);
		std::cout << "\r\x1B[K" << isp->handler << "." << isp->command;
		std::cout.flush();

		int lines = 0;
		//msgbuf << AEC_ERASE_SE;
		running = isp != instructions.end();
		/*
		for (auto& p : pl.getPlaying())
		{
			if (p.second > 0)
			{
				msgbuf << AEC_GREEN_FG << p.first << AEC_RESET "\n";
				++lines;
				running = 1;
			}
			else if (p.second > -30)
			{
				msgbuf << AEC_RED_FG << p.first << AEC_RESET "\n";
				--p.second;
				++lines;
				running = 1;
			}
		}
		*/


		/*
		msgbuf << mcl::aec::up(lines);
		lines = 0;
		*/

		/*
		for (auto i = pl.getInstructions().begin();
			i != pl.getInstructions().end(); i++)
		{
			msgbuf << mcl::aec::forward(columnWidth);
			if (i == pl.getISP()+pl.getInstructions().begin())
			{
				msgbuf << "> "
					<< std::string("end ", 0, 4 * (i->m_e == playlist::inst::end))
					<< AEC_YELLOW_FG;
				for (auto& a : i->args)
					msgbuf << a << ' ';
				msgbuf << AEC_RESET "<\n";
			}
			else
			{
				msgbuf <<
					(i->m_e == playlist::inst::start ? AEC_BLUE_FG :
					 i->m_e == playlist::inst::play  ? AEC_CYAN_FG :
					                                   AEC_BRIGHT_RED_FG);
				for (auto& a : i->args)
					msgbuf << a << ' ';
				msgbuf << AEC_RESET "\n";
			}
			++lines;
		}
		std::cout << msgbuf.str() << mcl::aec::up(lines) << std::flush;
		msgbuf.str("");
		*/

		/**
		 * Task list:
		 * - Poll && Recieve user input
		 * x Check status of running sounds (handled by playlist)
		 */


		char c;
		switch (read(0, &c, 1))
		{
		case 0:
			break;
		case -1:
			std::cerr << "\nError in reading.\n";
			return 2;
		case 1:
			switch (c)
			{
			case '\n':
			case ' ':
				if (isp != instructions.end())
				{
					cons[isp->handler]->execute(isp->command, isp->args);
					++isp;
				}
				while (isp != instructions.end() && isp->timing != Instruction::enter)
				{
					switch (isp->timing)
					{
					case Instruction::after:
						//TODO: make not rarted
						std::this_thread::sleep_for(1s);
					case Instruction::simul:
						cons[isp->handler]->execute(isp->command, isp->args);
						++isp;
						break;
					}
				}

				break;
			case '\b':
			case 'b':
			case 'k':
				if (isp != instructions.begin())
					--isp;
				break;
			case 's':
			case 'j':
				if (isp != instructions.end())
					++isp;
				break;
			case 'r':
				//TODO: reee
				isp = instructions.begin();
				break;
			case 'q':
				running = 0;
				break;
			default:
				break;
			}
		}

	}

	std::cout << '\n';

	return 0;
}



std::vector<std::vector<std::string>> TokenizeScript(const std::string& filename)
{
	std::vector<std::vector<std::string>> out{{""}};

	std::ifstream f{filename};
	if (!f.is_open())
		throw std::runtime_error(
				"[TokenizeScript] Failed to open file `" + filename + "`"
			);

	for (char c; f.get(c); )
	{
		switch (c)
		{
		case '#':
			while (f.get(c) && c != '\n');
		case ';':
		case '\n':
			if (out.back().size() > 1)
			{
				if (out.back().back().size() == 0)
					out.back().pop_back();
				out.push_back({""});
			}
			else if (out.back().front().size())
				out.push_back({""});
			break;
		case ' ':
		case '\t':
			if (out.back().back().size())
				out.back().push_back("");
			break;
		case '"':
			for (bool esc = 0; f.get(c); )
			{
				if (c ==  '"' && !esc)
					break;
				if (c == '\\' && !esc)
				{
					esc = 1;
					continue;
				}

				if (esc && c != '"' && c != '\\')
					out.back().back() += '\\';
				out.back().back() += c;
			}
			break;
		case '\'':
			while (f.get(c) && c != '\'')
				out.back().back() += c;
			break;
		default:
			out.back().back() += c;
		}

		if (!f)
			throw std::runtime_error(
					"[TokenizeScript] Unexpected EOF in script."
				);
	}

	if (out.back().size() == 1 && out.back().front().size() == 0)
		out.pop_back();
	return out;
}

std::vector<Instruction> secondProcessing(const std::vector<std::vector<std::string>>& lines)
{
	std::vector<Instruction> insts;
	std::map<std::string, std::vector<std::string>> aliases;
	for (auto line : lines)
	{
		if (aliases.count(line[0]))
		{
			auto b = aliases[line[0]].begin(), e = aliases[line[0]].end();
			line.erase(line.begin());
			line.insert(line.begin(), b, e);
		}

		if (line[0] == "alias")
			aliases[line[1]] = std::vector<std::string>{line.begin()+2, line.end()};
		else if (line[0] == "load")
		{
			if (line[1] == "DmxCtl")
				cons["dmx"] = new lsc::DmxCtl(std::vector<std::string>{ line.begin()+2, line.end() });
			else if (line[1] == "SfxCtl")
				cons["sfx"] = new NullMod(std::vector<std::string>{ line.begin()+2, line.end() });
		}
		else
		{
			if (line[0] == "&")
			{
				size_t p = line[1].find('.');
				if (p == std::string::npos)
					insts.emplace_back(
							Instruction::simul,
							std::string{"sys"},
							line[1],
							std::vector<std::string>{ line.begin()+2, line.end() }
						);
				else
					insts.emplace_back(
							Instruction::simul,
							std::string{ line[1].begin(), line[1].begin()+p },
							std::string{ line[1].begin()+p+1, line[1].end() },
							std::vector<std::string>{ line.begin()+2, line.end() }
						);
			}
			else if (line[0] == "->")
			{
				size_t p = line[1].find('.');
				if (p == std::string::npos)
					insts.emplace_back(
							Instruction::after,
							std::string{"sys"},
							line[1],
							std::vector<std::string>{ line.begin()+2, line.end() }
						);
				else
					insts.emplace_back(
							Instruction::after,
							std::string{ line[1].begin(), line[1].begin()+p },
							std::string{ line[1].begin()+p+1, line[1].end() },
							std::vector<std::string>{ line.begin()+2, line.end() }
						);
			}
			else
			{
				size_t p = line[0].find('.');
				if (p == std::string::npos)
					insts.emplace_back(
							Instruction::enter,
							std::string{"sys"},
							line[0],
							std::vector<std::string>{ line.begin()+1, line.end() }
						);
				else
					insts.emplace_back(
							Instruction::enter,
							std::string{ line[0].begin(), line[0].begin()+p },
							std::string{ line[0].begin()+p+1, line[0].end() },
							std::vector<std::string>{ line.begin()+1, line.end() }
						);
			}
		}
	}
	return insts;
}
