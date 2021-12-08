#include <unistd.h>
#include <fcntl.h>
#include <chrono>
using namespace std::literals::chrono_literals;
using Clock = std::chrono::steady_clock;
#include <string>
#include <termios.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
namespace fs = std::filesystem;


using byte = unsigned char;

constexpr auto kMinRefr = 20ms;

bool continueLine(int, std::string&, std::chrono::microseconds maxt = 5ms);
std::string doCommand(const std::string&, byte (&slots)[513]);



int main(int argc, char** argv)
{
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
	}

	bool updated = 1;
	std::string comm;
	std::string errstr;
	byte slots[513]{0};
	auto lastWrite = Clock::now();
	for(int linec = 0;;)
	{
		if (continueLine(0, comm) && comm.size())
		{
			errstr = doCommand(comm, slots);
			if (errstr.size())
			{
				errstr += '\n';
				write(2, errstr.c_str(), errstr.size());
				/*errstr = "in line " + std::to_string(linec) + " of length "
					+ std::to_string(comm.size()) + "\n";
				write(2, errstr.c_str(), errstr.size());*/
			}
			else
			{
				/*
				errstr = "line " + std::to_string(linec) + " of length "
					+ std::to_string(comm.size()) + " successful\n";
					*/
				write(2, errstr.c_str(), errstr.size());
				updated = 1;
			}
			comm.clear();
			++linec;
		}

		if (updated || lastWrite + kMinRefr <= Clock::now())
		{
			updated = 0;
			write(dev, slots, 513);
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



std::string doCommand(const std::string& line, byte (&slots)[513])
{
	std::istringstream ss{line};
	switch ((char)ss.get())
	{
	case '#':
		slots[0] = 0;
		for (size_t i = 2; i < 1026; i++)
		{
			signed char c;
			ss >> c;
			if (!ss || c == '\0')
			{
				i = (i >> 1) + (i & 1); //4 -> 2; 3 -> 2
				while (i < 513)
					slots[i++] = 0;
				break;
			}


			if ('0' <= c && c <= '9')
				c -= '0';
			else if ('a' <= c && c <= 'f')
				c -= 'a' - 10;
			else if ('A' <= c && c <= 'F')
				c -= 'A' - 10;
			else
				c = -1;

			if (c == -1)
				return "invalid character in frame command";
			if (i & 1)
				slots[i >> 1] |= (byte)c << 4;
			else
				slots[i >> 1]  = c;
		}
		break;
	case '@': {
		size_t i;
		ss >> i;
		++i <<= 1;
		for (signed char c; ss >> c, ss; i++)
		{
			if (1026 <= i)
				return "index too high";

			if ('0' <= c && c <= '9')
				c -= '0';
			else if ('a' <= c && c <= 'f')
				c -= 'a' - 10;
			else if ('A' <= c && c <= 'F')
				c -= 'A' - 10;
			else
				c = -1;

			if (c == -1)
				return "invalid character in index command";
			if (i & 1)
				slots[i >> 1] |= (byte)c << 4;
			else
				slots[i >> 1]  = c;
		}
	} break;
	default:
		return "unknown command";
	}
	return "";
}
