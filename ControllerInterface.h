#ifndef LSC_CONTROLLER_INTERFACE_H
#define LSC_CONTROLLER_INTERFACE_H

#include <string>
#include <vector>


namespace lsc
{
	class Controller
	{
	public:
		virtual void execute(
				const std::string& inst,
				const std::vector<std::string>& args)
			= 0;
		virtual std::string state() const = 0;
		/*Verify instruction will cause no error.
		 *Returns empty string on success, explaination on failure.
		 */
		virtual std::string verify(
				const std::string& inst,
				const std::vector<std::string>& args)
			= 0;
		virtual operator bool() const = 0;

		virtual ~Controller() { }
	};
}


#endif

