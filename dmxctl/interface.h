#ifndef LSC_DMX_CONTROL_H
#define LSC_DMX_CONTROL_H

#include "../ControllerInterface.h"
#include <filesystem>
#include <vector>
#include <map>
#include <variant>
#include <string>
#include <sstream>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
#include <chrono>


namespace lsc
{
	namespace fs = std::filesystem;
	namespace yaml = YAML;
	using byte = unsigned char;
	using Clock = std::chrono::steady_clock;

	struct Unit{};

	struct Channel {
		enum TargetType {
			master,
			color, // red, green, blue,
			pan, tilt,
			generic
		};
		static const inline std::map<std::string, Channel::TargetType>
		targetMapping{
			{ "master",  Channel::master  },
			{ "color",   Channel::color   },
			//{ "red",     Channel::red     }, //replaced by color[0]
			//{ "green",   Channel::green   }, //replaced by color[1]
			//{ "blue",    Channel::blue    }, //replaced by color[2]
			{ "pan",     Channel::pan     },
			{ "tilt",    Channel::tilt    },
			{ "generic", Channel::generic },
			{ "dimmer",  Channel::generic }
		};
		struct DiscreteValue {
			size_t min, max;
			std::string name;

			DiscreteValue(size_t m, size_t M, std::string n)
				: min{m}, max{M}, name{n} { }
		};
		struct Range {
			int min, max;

			Range(int m, int M) : min{m}, max{M} { }
		};



		size_t chanid; /* Just this object's index in the vector.  Useful when
											extracting references to these. */

		std::string name;
		size_t valindex; /* For example, if something is a 2-byte value, it
												must be split across 2 channels -- Thing[0] and
												Thing[1], that is, `"Thing", 0` and `"Thing", 1`
												*/
		TargetType target;
		std::variant<
			Unit,
			std::vector<DiscreteValue>,
			Range
		> values;

		byte value;

		Channel(size_t c, std::string n, size_t i, TargetType t, Unit u, byte v)
			: chanid{c}, name{n}, valindex{i}, target{t}, values{u}, value{v} { }
		Channel(size_t c, std::string n, size_t i, TargetType t, std::vector<DiscreteValue> u, byte v)
			: chanid{c}, name{n}, valindex{i}, target{t}, values{u}, value{v} { }
		Channel(size_t c, std::string n, size_t i, TargetType t, Range u, byte v)
			: chanid{c}, name{n}, valindex{i}, target{t}, values{u}, value{v} { }
		Channel(size_t c, std::string n, size_t i, TargetType t, std::variant<Unit, std::vector<DiscreteValue>, Range> u, byte v)
			: chanid{c}, name{n}, valindex{i}, target{t}, values{u}, value{v} { }

		int rangeIndex(const std::string&) const; //-1 on failure
		size_t rangeMinimum(const std::string&) const;
	};

	struct Instrument
	{
		std::string name;
		size_t addr;
		std::vector<Channel> channels;

		Instrument(std::string n, size_t a, std::vector<Channel> c)
			: name{n}, addr{a}, channels{c} { }

		std::vector<Channel*> operator[](const std::string&);
		std::vector<const Channel*> operator[](const std::string&) const;
		std::vector<Channel*> operator[](const Channel::TargetType&);
		std::vector<const Channel*> operator[](const Channel::TargetType&) const;
		Channel& operator[](size_t);
		const Channel& operator[](size_t) const;

		bool setValue(Channel::TargetType, const std::string&);
		bool setValue(const std::string&, const std::string&);
	};

	class DmxCtl : public Controller
	{
#ifdef LSC_DEBUGGING_FUNCTION
		friend LSC_DEBUGGING_FUNCTION;
#endif
		std::vector<Instrument> instruments;

		//Old:
		/* This layout should not be modified -- this is an int[2] with named
		   elements */
		//int tochild, childin;
		// Same here
		//int toparent, parentin;

		int tochildpipe[2];
		int &tochild = tochildpipe[1], &childin = tochildpipe[0];
		//  ^ this is still stupid     ^
		int toparentpipe[2];
		int &toparent = toparentpipe[1], &parentin = toparentpipe[0];

		int chid;


		//Newly public
	public:
		std::vector<Instrument*> operator[](const std::string& prefix);
		std::vector<const Instrument*> operator[](const std::string& prefix) const;
		Channel& operator[](size_t);
		const Channel& operator[](size_t) const;

		std::string checkScene(const std::string&) const;
		void loadScene(const std::string&);

		void getSlots(byte (&slots)[512]) const;
		// Returns whether changes were made
		bool setValues(Channel::TargetType, float);
		// Sets each value to {min, max} of itself and float param
		bool minValues(Channel::TargetType, float);
		bool maxValues(Channel::TargetType, float);

		float maxValue(Channel::TargetType);
		float minValue(Channel::TargetType);

		void setChannel(size_t, byte);
		void fadeChannel(size_t, size_t mills, byte);
	//public:
		DmxCtl(const DmxCtl&) = delete;
		DmxCtl(std::vector<std::string>);

		//Takes device and instrument file
		void execute(
				const std::string& inst,
				const std::vector<std::string>& args
			) override;
		std::string verify(
				const std::string& inst,
				const std::vector<std::string>& args
			) override;
		std::string state() const override;

		operator bool() const override;

		~DmxCtl() override;



		// Previously private.  Now for manual control.
		void writeOut();
	};

}


#endif

