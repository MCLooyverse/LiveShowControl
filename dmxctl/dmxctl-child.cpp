#include <unistd.h>
#include <fcntl.h>
#include <chrono>
using namespace std::literals::chrono_literals;
using Clock = std::chrono::steady_clock;
#include <string>
#include <vector>
#include <map>
#include <termios.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
namespace fs = std::filesystem;
#include <signal.h>



using byte = unsigned char;

constexpr auto kMinRefr = 20ms;

struct State
{
	struct Fader
	{
		std::chrono::time_point<Clock> upd;
		float vel; //DMX points per ms
		byte  tgt;
	};

	byte slots[513];
	std::map<size_t, Fader> faders;
	bool updated;
	bool supressBadAddr;

	State() : slots{0}, faders{}, updated{1}, supressBadAddr{1} { }
};

bool continueLine(int, std::string&, std::chrono::microseconds maxt = 5ms);
std::string doCommand(const std::string&, State&);

template <typename T> inline constexpr
int sgn(T x)
{ return (T{0} < x) - (x < T{0}); }
template <typename DT, typename DF>
inline constexpr typename DT::rep numberOf(DF d)
{ return std::chrono::duration_cast<DT>(d).count(); }


void onParentDeath(int sig)
{
	exit(0);
}



int main(int argc, char** argv)
{
	struct sigaction sa;
	sa.sa_handler = onParentDeath;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);

	if (argc != 2 || !fs::is_character_file(fs::path(argv[1])))
	{
		const char cerrstr[] = "needs one device argument\n";
		write(2, cerrstr, sizeof(cerrstr));
	}


	auto tty = new termios;
	tcgetattr(0, tty);
	tty->c_lflag &= ~ICANON;
	tty->c_cc[VMIN]  = 0; //Can read 0 bytes and return
	tty->c_cc[VTIME] = 0; //Return immediately, data or no.
	tcsetattr(0, TCSANOW, tty);
	delete tty;

	int dev = open(argv[1], O_WRONLY);
	if (dev == -1)
	{
		const char cerrstr[] = "failed to open device: ";
		write(2, cerrstr, sizeof(cerrstr)-1);
		std::string dynerrstr = strerror(errno);
		dynerrstr += '\n';
		write(2, dynerrstr.c_str(), dynerrstr.size());
		return 1;
	}

	std::string comm;
	std::string errstr;
	State state;
	auto lastWrite = Clock::now();
	for(;;)
	{
		if (continueLine(0, comm) && comm.size())
		{
			errstr = doCommand(comm, state);
			if (errstr.size())
			{
				errstr += '\n';
				write(2, errstr.c_str(), errstr.size());
			}
			comm.clear();
		}

		std::erase_if(
			state.faders,
			[&state](const auto& p) -> bool
			{ return p.second.tgt == state.slots[p.first+1]; }
		);

		for (auto& [idx, fader] : state.faders)
		{
			int delta = numberOf<std::chrono::milliseconds>(
					(Clock::now() - fader.upd) * fader.vel);
			if (delta)
			{
				if (sgn(delta) !=
						sgn((int)fader.tgt - ((int)state.slots[idx+1] + delta)))
					state.slots[idx+1] = fader.tgt;
				else
					state.slots[idx+1] += delta;
				fader.upd = Clock::now();
				state.updated = 1;
			}
		}


		if (state.updated || lastWrite + kMinRefr <= Clock::now())
		{
			state.updated = 0;
			int ok = write(dev, state.slots, 513);
			if (ok == -1 && (errno != EFAULT || !state.supressBadAddr))
			{
				std::string errstr = std::to_string(errno);
				errstr += ": ";
				errstr += strerror(errno);
				errstr += "\n";
				write(2, errstr.c_str(), errstr.size());
			}
			lastWrite = Clock::now();
		}
	}
}



bool continueLine(int fd, std::string& line, std::chrono::microseconds maxt)
{
	auto end = Clock::now() + maxt;
	char c;
	while (Clock::now() < end)
	{
		switch (read(fd, &c, 1))
		{
		case -1:
			switch (errno)
			{
			case EAGAIN:
#if EWOULDBLOCK != EAGAIN
			case EWOULDBLOCK:
#endif
				return 0;
			case EBADF:
				throw std::domain_error(
						std::string{"bad file descriptor: "} + strerror(EBADF)
					);
			case EFAULT:
				throw std::runtime_error(
						std::string{"impossible error: "} + strerror(EFAULT)
					);
			case EINTR:
				break;
			case EINVAL:
				throw std::domain_error(
						std::string{"unsuitable file descriptor: "} + strerror(EINVAL)
					);
			case EISDIR:
				throw std::domain_error(
						std::string{"file descriptor is a directory: "} + strerror(EISDIR)
					);
			default:
				throw std::runtime_error(
						strerror(errno)
					);
			}
			break;
		case 0:
			return 0;
		case 1:
			if (c == '\n')
				return 1;
			if ((c == ' ' || c == '\t') && line.back() != ' ')
				line += ' ';
			else
				line += c;
			break;
		}
	}
	return 0;
}


int hexToNybble(char c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	if ('A' <= c && c <= 'F')
		return c - 'A' + 10;
	if ('a' <= c && c <= 'f')
		return c - 'a' + 10;
	return -1;
}
signed char& hexMkNybble(signed char& c)
{
	if ('0' <= c && c <= '9')
		return c -= '0';
	if ('A' <= c && c <= 'F')
		return c -= 'A' - 10;
	if ('a' <= c && c <= 'f')
		return c -= 'a' - 10;
	return c = -1;
}


std::string doCommand(const std::string& line, State& state)
{
	std::istringstream ss{line};
	switch ((char)ss.get())
	{
	case '#':
		state.updated = 1;
		state.slots[0] = 0;
		for (size_t i = 2; i < 1026; i++)
		{
			signed char c;
			ss >> c;
			if (!ss || c == '\0')
			{
				i = (i >> 1) + (i & 1); //4 -> 2; 3 -> 2
				while (i < 513)
					state.slots[i++] = 0;
				break;
			}


			if (hexMkNybble(c) == -1)
				return "invalid character in frame command";
			if (i & 1)
				state.slots[i >> 1] |= (byte)c << 4;
			else
				state.slots[i >> 1]  = c;
		}
		break;
	case '@': {
		state.updated = 1;
		size_t i;
		ss >> i;
		++i <<= 1;
		for (signed char c; ss >> c, ss; i++)
		{
			if (1026 <= i)
				return "index too high";

			if (hexMkNybble(c) == -1)
				return "invalid character in index command";
			if (i & 1)
				state.slots[i >> 1] |= (byte)c << 4;
			else
				state.slots[i >> 1]  = c;
		}
	} break;
	case '>': {
		size_t i, d;
		ss >> i >> d;

		if (i >= 512)
			return "invalid index";

		auto& newFader = state.faders[i]
			= State::Fader{ Clock::now(), 0, 0 };

		signed char c;
		ss >> c;
		if (hexMkNybble(c) == -1)
			return "invalid character in fade command";
		newFader.tgt  = c;
		ss >> c;
		if (hexMkNybble(c) == -1)
			return "invalid character in fade command";
		newFader.tgt |= (byte)c << 4;

		newFader.vel = newFader.tgt - state.slots[i+1];
		if (newFader.vel == 0)
			return ""; //TODO: error?

		newFader.vel /= d;
	} break;
	case 'e': { //Echo
		auto tty = new termios;
		tcgetattr(0, tty);
		tty->c_lflag ^= ECHO;
		tcsetattr(0, TCSANOW, tty);
	} break;
	case 's': { //Supress dumb error
		state.supressBadAddr = !state.supressBadAddr;
	} break;
	default:
		return "unknown command";
	}
	return "";
}
