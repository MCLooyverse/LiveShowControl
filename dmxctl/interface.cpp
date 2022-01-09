#include "interface.h"
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cmath>
#include <signal.h>

// #include <iostream>


namespace lsc
{
	DmxCtl::DmxCtl(std::vector<std::string> args)
	{
		if (args.size() != 2)
			throw std::domain_error(
					"[DmxCtl::DmxCtl] Expects a device path and an instrument file path."
				);

		auto instrFile = yaml::LoadFile(args[1]);
		if (!instrFile.IsSequence())
			throw std::domain_error(
					"[DmxCtl::DmxCtl] Instrument YAML file must be a sequence."
				);

		for (auto i = instrFile.begin(); i != instrFile.end(); ++i)
		{
			if (!i->IsMap())
				throw std::domain_error(
						"[DmxCtl::DmxCtl] All instruments in instrument file must be objects."
					);
#define NEXIST_THROW(n, x)                                                      \
			if (!(n)[#x])                                                             \
				throw std::domain_error(                                                \
						"[DmxCtl::DmxCtl] Missing `" #x "` from instrument."                \
					);
#define WRONGTYPE_THROW(n, x, t)                                                \
			if (!(n)[#x].Is ## t())                                                   \
				throw std::domain_error(                                                \
						"[DmxCtl::DmxCtl] Instrument's " #x " must be a " #t                \
					);
#define BADKEY_THROW(n, x, t) NEXIST_THROW(n, x) WRONGTYPE_THROW(n, x, t)


			BADKEY_THROW(*i, name, Scalar);
			BADKEY_THROW(*i, addr, Scalar);
			BADKEY_THROW(*i, channels, Sequence);

			instruments.emplace_back(
					(*i)["name"].as<std::string>(),
					(*i)["addr"].as<size_t>(),
					std::vector<Channel>{}
				);

			auto instChannels = (*i)["channels"];
			for (auto j = instChannels.begin(); j != instChannels.end(); ++j)
			{
				BADKEY_THROW(*j, name, Scalar);
				std::string name = (*j)["name"].as<std::string>();
				size_t idx = std::string::npos;
				if (name.back() == ']' &&
						(idx = name.rfind('[')) != std::string::npos)
				{
					size_t val = std::stoul(name.substr(idx+1, name.size() - idx - 2));
					name.erase(idx);
					idx = val;
				}

				auto occurances = instruments.back()[name];
				if (idx == std::string::npos)
				{
					if (occurances.size())
						throw std::domain_error(
								"[DmxCtl::DmxCtl] More than one occurance of channel \""
								+ name + "\" in instrument \"" + instruments.back().name
								+ "\"."
							);

					instruments.back().channels.emplace_back(
							instruments.back().channels.size(), name, 0, Channel::generic, Unit{}, 0
						);
				}
				else
				{
					if (occurances.size())
					{
						for (auto chan : occurances)
							if (chan->valindex == idx)
								throw std::domain_error(
										"[DmxCtl::DmxCtl] More than one occurance of "
										+ name + " [" + std::to_string(idx) + "] in instrument "
										+ instruments.back().name
									);

						/*
						instruments.back().channels.push_back(
								*(occurances.front())
							);
						*/
						instruments.back().channels.emplace_back(
								instruments.back().channels.size(), name, idx,
								occurances.front()->target, occurances.front()->values,
								0
							);
						//instruments.back().channels.back().chanid = instruments.back().channels.size()-1;
						//instruments.back().channels.back().valindex = idx;
					}
					else
						instruments.back().channels.emplace_back(
								instruments.back().channels.size(), name, idx, Channel::generic, Unit{}, 0
							);
				}

				if ((*j)["target"])
				{
					std::string targetStr = (*j)["target"].as<std::string>();
					if (Channel::targetMapping.count(targetStr))
						instruments.back().channels.back().target =
							Channel::targetMapping.at(targetStr);
					else
						throw std::domain_error(
								"[DmxCtl::DmxCtl] Unrecognized channel target \"" +
								targetStr + "\"."
							);
				}

				if ((*j)["values"])
				{
					if (!(*j)["values"]["type"]
							|| (*j)["values"]["type"].as<std::string>() == "range")
					{
						if ((*j)["values"]["min"] &&
								(*j)["values"]["max"] &&
								(*j)["values"]["min"].IsScalar() &&
								(*j)["values"]["max"].IsScalar())
								instruments.back().channels.back().values =
									Channel::Range{
										(*j)["values"]["min"].as<int>(),
										(*j)["values"]["max"].as<int>()
									};
						else
							throw std::domain_error(
									"[DmxCtl::DmxCtl] Channel " +
									instruments.back().channels.back().name + " of instrument " +
									instruments.back().name +
									" requires min and max scalars for its value range."
								);
					}
					else if ((*j)["values"]["type"].as<std::string>() == "discrete")
					{
						instruments.back().channels.back().values =
							std::vector<Channel::DiscreteValue>{};
						for (auto k = (*j)["values"].begin(); k != (*j)["values"].end(); ++k)
						{
							if (k->first.as<std::string>() != "type")
							{
								if (!k->second.IsSequence()   ||
										 k->second.size() != 2    ||
										 !k->second[0].IsScalar() ||
										 !k->second[1].IsScalar()   )
									throw std::domain_error(
											"[DmxCtl::DmxCtl] " +
											instruments.back().name +
											" > " +
											instruments.back().channels.back().name +
											" > values > " +
											k->first.as<std::string>() +
											" must be a 2-list of scalars."
										);

								std::get<1>(instruments.back().channels.back().values)
									.emplace_back(
											k->second[0].as<size_t>(),
											k->second[1].as<size_t>(),
											k->first.as<std::string>()
										);
							}
						}
					}
					else
						throw std::domain_error(
								"[DmxCtl::DmxCtl] " +
								instruments.back().name +
								" > " +
								instruments.back().channels.back().name +
								" > values > type Unknown type."
							);
				}
			}
		}





#undef BADKEY_THROW
#undef WRONGTYPE_THROW
#undef NEXIST_THROW

		int ok = pipe2(tochildpipe, O_NONBLOCK);  //Should be redundant
		if (ok == -1)
			throw std::runtime_error(
					"[DmxCtl::DmxCtl] Failed to get pipe."
				);
		ok = pipe2(toparentpipe, O_NONBLOCK);  //Should be redundant
		if (ok == -1)
			throw std::runtime_error(
					"[DmxCtl::DmxCtl] Failed to get pipe."
				);

		chid = fork();
		switch (chid)
		{
		case -1:
			throw std::runtime_error(
					"[DmxCtl::DmxCtl] Failed to fork."
				);
		case 0: //Child
			close(0); dup(childin);
			close(1); dup(toparent);
			//close(2); dup(toparent); //For now, let errors pass through
		#ifdef COMPILE_TIME_PWD
			ok = execl(COMPILE_TIME_PWD "/dmxctl-child", "dmxctl-child", args[0].c_str(), 0);
			if (ok == -1)
			{
				const char msg[] = "bruh moment\n";
				write(1, msg, sizeof(msg)-1);
				throw std::runtime_error(
						"[DmxCtl::DmxCtl] Failed to execute child."
					);
			}
		#else
			#error "Macro `COMPILE_TIME_PWD` is not defined, but is required to be the absolute path to the directory of the file.  Recommend use of Makefile with requirement satisfied."
		#endif
		}
	}



	std::vector<Channel*> Instrument::operator[](const std::string& name)
	{
		std::vector<Channel*> out{};
		for (auto& chan : channels)
			if (chan.name == name)
				out.push_back(&chan);
		return out;
	}
	std::vector<const Channel*> Instrument::operator[](const std::string& name) const
	{
		std::vector<const Channel*> out{};
		for (auto& chan : channels)
			if (chan.name == name)
				out.push_back(&chan);
		return out;
	}
	std::vector<Channel*> Instrument::operator[](
			const Channel::TargetType& tt)
	{
		std::vector<Channel*> out{};
		for (auto& chan : channels)
			if (chan.target == tt)
				out.push_back(&chan);
		return out;
	}
	std::vector<const Channel*> Instrument::operator[](
			const Channel::TargetType& tt) const
	{
		std::vector<const Channel*> out{};
		for (auto& chan : channels)
			if (chan.target == tt)
				out.push_back(&chan);
		return out;
	}
	Channel& Instrument::operator[](size_t idx)
	{ return channels[idx]; }
	const Channel& Instrument::operator[](size_t idx) const
	{ return channels[idx]; }


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

	int hexToByte(const char* p)
	{
		int a = hexToNybble(*p), b = hexToNybble(p[1]);
		if (a != -1 && b != -1)
			return a | (b << 4);
		return -1;
	}

	int Channel::rangeIndex(const std::string& str) const
	{
		if (values.index() == 1)
		{
			auto& dvs = std::get<1>(values);
			for (int i = 0; i < dvs.size(); i++)
				if (dvs[i].name == str) return i;
		}
		return -1;
	}
	size_t Channel::rangeMinimum(const std::string& str) const
	{
		if (values.index() == 1)
		{
			auto& dvs = std::get<1>(values);
			for (int i = 0; i < dvs.size(); i++)
				if (dvs[i].name == str) return dvs[i].min;
		}
		return 0;
	}

	void setChannelValues(const std::vector<Channel*>& chans, const std::string& val)
	{
		using severalBytes = unsigned long long int;

		assert(chans.size() <= sizeof(severalBytes)); //TODO: something less dumb

		severalBytes valbytes = 0;
		auto i = val.begin();
		if (val.starts_with("#"))
			i += 1;
		else if (val.starts_with("0x"))
			i += 2;

		if (i != val.begin())
			for (size_t nybble = 0, i = 1; i < val.size() && nybble < chans.size()*2; i++)
			{
				auto nv = hexToNybble(val[i]);
				if (nv == -1)
					continue;
				valbytes |= (severalBytes)nv << 4*nybble++;
			}
		else if (chans.front()->rangeIndex(val) != -1)
		{
			valbytes = chans.front()->rangeMinimum(val);
		}
		else
		{
			//*
			valbytes = (1 << 8*chans.size()) - 1;
			valbytes *= std::stod(val);
			//*/
			//valbytes = std::ldexp(std::stold(val), 8*chans.size()) - 1;
		}

		for (auto chan : chans)
			chan->value = valbytes >> 8*chan->valindex;
	}

	bool Instrument::setValue(Channel::TargetType targ, const std::string& val)
	{
		auto selChans = (*this)[targ];
		if (!selChans.size())
			return 0;

		setChannelValues(selChans, val);
		return 1;
	}
	bool Instrument::setValue(const std::string& chname, const std::string& val)
	{
		auto selChans = (*this)[chname];
		if (!selChans.size())
			return 0;

		setChannelValues(selChans, val);
		return 1;
	}





	void DmxCtl::execute(
			const std::string& inst,
			const std::vector<std::string>& args
			)
	{


		std::string errorString = verify(inst, args);
		if (errorString.size())
			throw std::domain_error(
					"[DmxCtl::execute (" + inst + ")] " + errorString
				);

		// All error-checking has been exported.


		if (inst == "loadAndFade")
		{
			std::istringstream strm{args[1]};
			float num;
			std::string unit;
			strm >> num >> unit;
			std::chrono::milliseconds dur;
			if (unit == "s")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::seconds{(std::chrono::seconds::rep)num}
					);
			else if (unit == "ms")
				dur = std::chrono::milliseconds{(std::chrono::milliseconds::rep)num};
			else if (unit == "m")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::minutes{(std::chrono::minutes::rep)num}
					);

			loadScene(args[0]);
			setValues(Channel::master, 0.0f);
			writeOut();
			for (auto& inst : instruments)
			for (auto& chan : inst[Channel::master])
				fadeChannel(inst.addr + chan->chanid, dur.count(), 0xFF);

			return;
		} //loadAndFade
		else if (inst == "fadeTo")
		{
			float fade;
			float num;
			std::string unit;
			std::istringstream strm{args[0]};
			strm >> fade;

			strm.clear();
			strm.str(args[1]);
			strm >> num >> unit;

			std::chrono::milliseconds dur;
			if (unit == "s")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::seconds{(std::chrono::seconds::rep)num}
					);
			else if (unit == "ms")
				dur = std::chrono::milliseconds{(std::chrono::milliseconds::rep)num};
			else if (unit == "m")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::minutes{(std::chrono::minutes::rep)num}
					);

			for (auto& inst : instruments)
			for (auto& chan : inst[Channel::master])
				fadeChannel(inst.addr + chan->chanid, dur.count(), fade * 0xFF);
			/* ^^^ not great ^^^ */
		} //fadeTo
		else if (inst == "dark")
		{
			setValues(Channel::master, 0.0f);
			writeOut();
		}
		else if (inst == "load")
		{
			loadScene(args[0]);
			writeOut();
		}
		else if (inst == "loadBright")
		{
			loadScene(args[0]);
			setValues(Channel::master, 1.0f);
			writeOut();
		}
		else if (inst == "loadDark")
		{
			loadScene(args[0]);
			setValues(Channel::master, 0.0f);
			writeOut();
		}
		else if (inst == "fadeInstTo")
		{
			float fade;
			float num;
			std::string unit;
			std::istringstream strm{args[1]};
			strm >> fade;

			strm.clear();
			strm.str(args[2]);
			strm >> num >> unit;

			std::chrono::milliseconds dur;
			if (unit == "s")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::seconds{(std::chrono::seconds::rep)num}
					);
			else if (unit == "ms")
				dur = std::chrono::milliseconds{(std::chrono::milliseconds::rep)num};
			else if (unit == "m")
				dur = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::minutes{(std::chrono::minutes::rep)num}
					);

			//std::clog << "Fading instrument " << args[0] << " to " <<
			//	fade << " over " << dur.count() << "ms\n";
			for (auto lp : (*this)[args[0]])
			for (auto chan : (*lp)[Channel::master])
				fadeChannel(lp->addr + chan->chanid, dur.count(), fade * 0xFF);
			/* ^^^ Not great ^^^ */
		} //fadeInstTo
	} //execute


	std::string DmxCtl::verify(
			const std::string& inst,
			const std::vector<std::string>& args
			)
	{
		if (inst == "loadAndFade")
		{
			/*
			if (args.size() != 2 ||
					!fs::is_regular_file(fs::path(args[0]))
					)
				return "Expects a file and a duration.";
			*/
			if (args.size() != 2)
				return "Expects a file and a duration.";
			if (!fs::is_regular_file(fs::path(args[0])))
				return "Expected `" + args[0] + "` to be a filepath.";

			std::string errorString = checkScene(args[0]);
			if (errorString.size())
				return "Invalid scene file: " + errorString;
			std::istringstream strm{args[1]};
			float num;
			std::string unit;
			strm >> num >> unit;
			if (num < 0)
				return "Negative duration not supported.";
			if (unit != "s" &&
					unit != "ms" &&
					unit != "m")
				return "Unrecognized unit \"" + unit + "\".";
		} //loadAndFade
		else if (inst == "fadeTo")
		{
			if (args.size() != 2)
				return "Expects a fade value and a duration.";
			float fade;
			float dur;
			std::string unit;
			std::istringstream strm{args[0]};
			strm >> fade;
			if (fade < 0 || fade > 1)
				return "Fade value must be in [0, 1].";
			strm.clear();
			strm.str(args[1]);
			strm >> dur >> unit;
			if (dur < 0)
				return "Negative duration not supported.";
			if (unit != "s" &&
					unit != "ms" &&
					unit != "m")
				return "Unrecognized unit \"" + unit + "\".";
		} //fadeTo
		else if (inst == "dark")
		{
			if (args.size() != 0)
				return "Takes no arguments.";
		}
		else if (inst == "load")
		{
			if (args.size() != 1 ||
					!fs::is_regular_file(fs::path(args[0]))
					)
				return "Expects a scene file.";

			std::string errorString = checkScene(args[0]);
			if (errorString.size())
				return "Invalid scene file: " + errorString;
		}
		else if (inst == "loadBright")
		{
			if (args.size() != 1 ||
					!fs::is_regular_file(fs::path(args[0]))
					)
				return "Expects a scene file.";

			std::string errorString = checkScene(args[0]);
			if (errorString.size())
				return "Invalid scene file: " + errorString;
		}
		else if (inst == "loadDark")
		{
			if (args.size() != 1 ||
					!fs::is_regular_file(fs::path(args[0]))
					)
				return "Expects a scene file.";

			std::string errorString = checkScene(args[0]);
			if (errorString.size())
				return "Invalid scene file: " + errorString;
		}
		else if (inst == "fadeInstTo")
		{
			if (args.size() != 3)
				return "Expects an instrument, fade value, and duration.";
			float fade;
			float dur;
			std::string unit;
			std::istringstream strm{args[1]};
			strm >> fade;
			if (fade < 0 || fade > 1)
				return "Fade value must be in [0, 1].";
			strm.clear();
			strm.str(args[2]);
			strm >> dur >> unit;
			if (dur < 0)
				return "Negative duration not supported.";
			if (unit != "s" &&
					unit != "ms" &&
					unit != "m")
				return "Unrecognized unit \"" + unit + "\".";
		} //fadeTo
		else
			return "Unknown command.";

		return "";
	}
	std::string DmxCtl::state() const
	{
		return "good"; //TODO: bruh
	}

	DmxCtl::operator bool() const
	{
		return 1;
	}

	DmxCtl::~DmxCtl()
	{
		kill(chid, SIGTERM);
	}




	void DmxCtl::getSlots(byte (&slots)[512]) const
	{
		auto ini = instruments.begin();
		for (size_t chi = 0; chi < 512; chi++)
		{
			if (ini != instruments.end() && ini->addr <= chi)
			{
				if (chi < ini->addr + ini->channels.size())
					slots[chi] = ini->channels[chi - ini->addr].value;
				else
					++ini;
			}
			else
				slots[chi] = 0;
		}
	}
	bool DmxCtl::setValues(Channel::TargetType targ, float f)
	{
		using severalBytes = unsigned long long int;
		bool changed = 0;
		for (auto& inst : instruments)
		{
			auto chans = inst[targ];
			assert(chans.size() < sizeof(severalBytes));
			severalBytes bytes = 1 << 8*chans.size();
			--bytes *= f;

			for (auto pchan : chans)
			{
				byte old = pchan->value;
				pchan->value = bytes >> 8*pchan->valindex;
				if (old != pchan->value)
					changed = 1;
			}
		}
		return changed;
	}
	bool DmxCtl::minValues(Channel::TargetType targ, float f)
	{
		using severalBytes = unsigned long long int;
		bool changed = 0;
		for (auto& inst : instruments)
		{
			auto chans = inst[targ];
			assert(chans.size() <= sizeof(severalBytes));
			severalBytes bytes = std::ldexp(f, 8*chans.size()) - 1;
			severalBytes oldBytes = 0;

			for (auto pchan : chans)
				oldBytes |= (severalBytes)pchan->value << 8*pchan->valindex;

			if (bytes < oldBytes)
			{
				changed = 1;
				for (auto pchan : chans)
					pchan->value = bytes >> 8*pchan->valindex;
			}
		}
		return changed;
	}
	bool DmxCtl::maxValues(Channel::TargetType targ, float f)
	{
		using severalBytes = unsigned long long int;
		bool changed = 0;
		for (auto& inst : instruments)
		{
			auto chans = inst[targ];
			assert(chans.size() <= sizeof(severalBytes));
			severalBytes bytes = std::ldexp(f, 8*chans.size()) - 1;
			severalBytes oldBytes = 0;

			for (auto pchan : chans)
				oldBytes |= (severalBytes)pchan->value << 8*pchan->valindex;

			if (bytes > oldBytes)
			{
				changed = 1;
				for (auto pchan : chans)
					pchan->value = bytes >> 8*pchan->valindex;
			}
		}
		return changed;
	}
	float DmxCtl::maxValue(Channel::TargetType targ)
	{
		float maxVal = 0;
		for (auto& inst : instruments)
		{
			auto chans = inst[targ];
			if (!chans.size())
				continue;
			float newVal = 0;
			for (auto chan : chans)
				newVal += std::ldexp((float)chan->value, 8*chan->valindex);
			newVal = std::ldexp(newVal, -8*(chans.size() + 1));
			if (newVal > maxVal)
				maxVal = newVal;
		}
		return maxVal;
	}
	float DmxCtl::minValue(Channel::TargetType targ)
	{
		float minVal = 0;
		for (auto& inst : instruments)
		{
			auto chans = inst[targ];
			if (!chans.size())
				continue;
			float newVal = 0;
			for (auto chan : chans)
				newVal += std::ldexp((float)chan->value, 8*chan->valindex);
			newVal = std::ldexp(newVal, -8*(chans.size() + 1));
			if (newVal < minVal)
				minVal = newVal;
		}
		return minVal;
	}


	void DmxCtl::setChannel(size_t idx, byte b)
	{
		constexpr char hexits[] = "0123456789ABCDEF";

		(*this)[idx].value = b;
		std::string msg = "@";
		msg += std::to_string(idx);
		msg += ' ';
		msg += hexits[b & 15];
		msg += hexits[b >> 4];
		msg += '\n';
		write(tochild, msg.c_str(), msg.size());
	}
	void DmxCtl::fadeChannel(size_t idx, size_t mills, byte b)
	{
		constexpr char hexits[] = "0123456789ABCDEF";

		(*this)[idx].value = b;
		std::string msg = ">";
		msg += std::to_string(idx);
		msg += ' ';
		msg += std::to_string(mills);
		msg += ' ';
		msg += hexits[b & 15];
		msg += hexits[b >> 4];
		msg += '\n';
		//std::clog << '\n' << msg;
		write(tochild, msg.c_str(), msg.size());
	}

	void DmxCtl::writeOut()
	{
		byte slots[512]{0};
		getSlots(slots);
		constexpr char hexits[] = "0123456789ABCDEF";
		char hexpair[2];
		write(tochild, "#", 1);
		for (auto sl : slots)
		{
			hexpair[0] = hexits[sl & 15];
			hexpair[1] = hexits[sl >> 4];
			write(tochild, hexpair, 2);
		}
		write(tochild, "\n", 1);
	}




	std::vector<Instrument*> DmxCtl::operator[](const std::string& prefix)
	{
		std::vector<Instrument*> out;
		for (auto& inst : instruments)
			if (inst.name.starts_with(prefix))
				out.push_back(&inst);
		return out;
	}
	std::vector<const Instrument*> DmxCtl::operator[](const std::string& prefix) const
	{
		std::vector<const Instrument*> out;
		for (auto& inst : instruments)
			if (inst.name.starts_with(prefix))
				out.push_back(&inst);
		return out;
	}
	Channel& DmxCtl::operator[](size_t idx)
	{
		for (auto& inst : instruments)
			if (inst.addr <= idx && idx < inst.addr + inst.channels.size())
				return inst[idx - inst.addr];
		throw std::domain_error("undefined channel"); //TODO: errmsg
	}
	const Channel& DmxCtl::operator[](size_t idx) const
	{
		for (auto& inst : instruments)
			if (inst.addr <= idx && idx < inst.addr + inst.channels.size())
				return inst[idx - inst.addr];
		throw std::domain_error("undefined channel"); //TODO: errmsg
	}


	struct RGBColor {
		byte red, green, blue;
	};
	RGBColor getColor(const std::string& cs)
	{
		if (cs[0] == '#')
		{
			int rred, rgreen, rblue;
			if (cs.size() == 7)
			{
				if ((rred   = hexToByte(cs.data()+1)) != -1 &&
						(rgreen = hexToByte(cs.data()+3)) != -1 &&
						(rblue  = hexToByte(cs.data()+5)) != -1)
					return RGBColor{rred, rgreen, rblue};
				else
					throw std::domain_error("Bad color"); //TODO: poor error
			}
			else if (cs.size() == 4)
			{
				if ((rred   = hexToNybble(cs[1])) != -1 &&
						(rgreen = hexToNybble(cs[2])) != -1 &&
						(rblue  = hexToNybble(cs[3])) != -1)
					return RGBColor{rred, rgreen, rblue};
				else
					throw std::domain_error("Bad color"); //TODO: poor error
			}
		}
		else
			throw std::domain_error("Unsupported color specification");

		return RGBColor{0,0,0}; //Make the compiler shut up
	}

	std::string DmxCtl::checkScene(const std::string& file) const
	{
		auto yamlModel = yaml::LoadFile(file);
		if (!yamlModel.IsMap())
			return "File must be an object.";

		for (auto i = yamlModel.begin(); i != yamlModel.end(); i++)
		{
			auto namedInsts = (*this)[i->first.as<std::string>()];
			if (!namedInsts.size())
				return "`" + i->first.as<std::string>() + "` does not name a known instrument.";

			if (!i->second.IsMap())
				return "`" + i->first.as<std::string>() + "` must be an object.";

			for (auto j = i->second.begin(); j != i->second.end(); ++j)
			{
				std::string chname = j->first.as<std::string>();
				if (Channel::targetMapping.count(chname))
				{
					for (auto inst : namedInsts)
						if (!(*inst)[Channel::targetMapping.at(chname)].size())
							return "`" + inst->name + "` does not have a channel `" + chname + "`";
				}
				else
				{
					for (auto inst : namedInsts)
						if (!(*inst)[chname].size())
							return "`" + inst->name + "` does not have a channel `" + chname + "`";
				}
			}
		}

		return "";
	}

	void DmxCtl::loadScene(const std::string& file)
	{
		std::string stringError = checkScene(file);
		if (stringError.size())
			throw std::domain_error(
					"[DmxCtl::loadScene] " + stringError
				);

		auto yamlModel = yaml::LoadFile(file);

		for (auto i = yamlModel.begin(); i != yamlModel.end(); i++)
		{
			auto namedInsts = (*this)[i->first.as<std::string>()];

			for (auto j = i->second.begin(); j != i->second.end(); ++j)
			{
				std::string name = j->first.as<std::string>();
				if (Channel::targetMapping.count(name))
				{
					auto targ = Channel::targetMapping.at(name);
					for (auto& inst : namedInsts)
						inst->setValue(targ, j->second.as<std::string>());
				}
				else
				{
					for (auto& inst : namedInsts)
						inst->setValue(name, j->second.as<std::string>());
				}
			}
		}
	}
}
