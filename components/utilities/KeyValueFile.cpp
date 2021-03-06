#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "File.h"
#include "KeyValueFile.h"
#include "String.h"
#include "StringView.h"
#include "../debug/Debug.h"

const char KeyValueFile::COMMENT = '#';
const char KeyValueFile::PAIR_SEPARATOR = '=';
const char KeyValueFile::SECTION_FRONT = '[';
const char KeyValueFile::SECTION_BACK = ']';

bool KeyValueFile::init(const char *filename)
{
	const std::string text = File::readAllText(filename);
	std::istringstream iss(text);

	// Check each line for a valid section or key-value pair. Start the line numbers at 1
	// since most users aren't programmers.
	std::string line;
	SectionMap *activeSectionMap = nullptr;
	for (int lineNumber = 1; std::getline(iss, line); lineNumber++)
	{
		// Get a filtered version of the current line so it can be parsed. If the filtered
		// string is empty, then skip to the next line.
		const std::string_view filteredLine = [&line]()
		{
			std::string_view str = line;

			// Skip empty strings.
			if ((str.size() == 0) || ((str.size() == 1) && (str.front() == '\r')))
			{
				return std::string_view();
			}

			// Remove carriage return at the end (if any).
			if (str.back() == '\r')
			{
				str.remove_suffix(1);
			}

			// Extract left-most comment (if any).
			const size_t commentIndex = str.find(KeyValueFile::COMMENT);
			if (commentIndex == 0)
			{
				// Comment covers the entire line.
				return std::string_view();
			}
			else if (commentIndex != std::string_view::npos)
			{
				// Comment is somewhere in the line, so only work with the left substring.
				str = str.substr(0, commentIndex);
			}

			// Trim leading and trailing whitespace (i.e., in case the key has whitespace
			// before it, or a comment had whitespace before it).
			return StringView::trimFront(StringView::trimBack(str));
		}();

		if (filteredLine.empty())
		{
			// It became an empty string after whitespace was removed.
			continue;
		}
		else if (filteredLine.size() < 3)
		{
			// Not long enough to be a section or key-value pair.
			DebugLogError("Syntax error \"" + std::string(filteredLine) + "\" (line " +
				std::to_string(lineNumber) + ") in " + filename + ".");
			return false;
		}

		// See if it's a section line or key-value pair line.
		const size_t sectionFrontIndex = filteredLine.find(KeyValueFile::SECTION_FRONT);
		if (sectionFrontIndex != std::string_view::npos)
		{
			// Section line. There must be a closing character with enough space between it
			// and the front character for at least one section character.
			const size_t sectionBackIndex = filteredLine.find(
				KeyValueFile::SECTION_BACK, sectionFrontIndex);

			if ((sectionBackIndex != std::string_view::npos) &&
				(sectionBackIndex > (sectionFrontIndex + 1)))
			{
				// Get the string that's between the section characters and trim any
				// leading or trailing whitespace.
				std::string_view sectionName = filteredLine.substr(
					sectionFrontIndex + 1, sectionBackIndex - sectionFrontIndex - 1);
				sectionName = StringView::trimFront(StringView::trimBack(sectionName));

				std::string sectionNameStr(sectionName);
				auto sectionIter = this->sectionMaps.find(sectionNameStr);

				// If the section is new, add it to the section maps.
				if (sectionIter == this->sectionMaps.end())
				{
					sectionIter = this->sectionMaps.insert(std::make_pair(
						std::move(sectionNameStr), SectionMap())).first;
					activeSectionMap = &sectionIter->second;
				}
				else
				{
					DebugLogError("Section \"" + sectionNameStr + "\" (line " +
						std::to_string(lineNumber) + ") already defined in " + filename + ".");
					return false;
				}
			}
			else
			{
				DebugLogError("Invalid section \"" + std::string(filteredLine) + "\" (line " +
					std::to_string(lineNumber) + ") in " + filename + ".");
				return false;
			}
		}
		else if (filteredLine.find(KeyValueFile::PAIR_SEPARATOR) != std::string::npos)
		{
			// Key-value pair line. There must be two tokens: key and value.
			std::array<std::string_view, 2> tokens;
			if (!StringView::splitExpected(filteredLine, KeyValueFile::PAIR_SEPARATOR, tokens))
			{
				DebugLogError("Invalid pair \"" + std::string(filteredLine) + "\" (line " +
					std::to_string(lineNumber) + ") in " + filename + ".");
				return false;
			}

			// Trim trailing whitespace from the key and leading whitespace from the value.
			const std::string_view &key = StringView::trimBack(tokens[0]);
			const std::string_view &value = StringView::trimFront(tokens[1]);

			if (key.size() == 0)
			{
				DebugLogError("Empty key in \"" + std::string(filteredLine) + "\" (line " +
					std::to_string(lineNumber) + ") in " + filename + ".");
				return false;
			}

			// Add the key-value pair to the active section map.
			if (activeSectionMap != nullptr)
			{
				activeSectionMap->insert(std::make_pair(std::string(key), std::string(value)));
			}
			else
			{
				// If no active section map, print a warning and ignore the current pair.
				// All key-value pairs must be in a section.
				DebugLogWarning("Ignoring \"" + std::string(filteredLine) + "\" (line " +
					std::to_string(lineNumber) + "), no active section in " + filename);
			}
		}
		else
		{
			// Filtered line is not a section or key-value pair.
			DebugLogError("Invalid line \"" + line + "\" (line " +
				std::to_string(lineNumber) + ") in " + filename + ".");
			return false;
		}
	}

	return true;
}

bool KeyValueFile::tryGetValue(const std::string &section, const std::string &key,
	std::string_view &value) const
{
	// See if the section exists.
	const auto sectionIter = this->sectionMaps.find(section);
	if (sectionIter == this->sectionMaps.end())
	{
		return false;
	}
	else
	{
		// See if the key exists in the section.
		const SectionMap &sectionMap = sectionIter->second;
		const auto keyIter = sectionMap.find(key);
		if (keyIter == sectionMap.end())
		{
			return false;
		}
		else
		{
			value = keyIter->second;
			return true;
		}
	}
}

bool KeyValueFile::tryGetBoolean(const std::string &section, const std::string &key, bool &value) const
{
	std::string_view str;
	if (!this->tryGetValue(section, key, str))
	{
		return false;
	}
	else
	{
		// Convert to lowercase for easier comparison.
		const std::string lowerStr = String::toLowercase(std::string(str));

		if (lowerStr == "true")
		{
			value = true;
			return true;
		}
		else if (lowerStr == "false")
		{
			value = false;
			return true;
		}
		else
		{
			return false;
		}
	}
}

bool KeyValueFile::tryGetInteger(const std::string &section, const std::string &key, int &value) const
{
	std::string_view str;
	if (!this->tryGetValue(section, key, str))
	{
		return false;
	}
	else
	{
		try
		{
			size_t index = 0;
			value = std::stoi(std::string(str), &index);
			return index == str.size();
		}
		catch (std::exception)
		{
			return false;
		}
	}
}

bool KeyValueFile::tryGetDouble(const std::string &section, const std::string &key, double &value) const
{
	std::string_view str;
	if (!this->tryGetValue(section, key, str))
	{
		return false;
	}
	else
	{
		try
		{
			size_t index = 0;
			value = std::stod(std::string(str), &index);
			return index == str.size();
		}
		catch (std::exception)
		{
			return false;
		}
	}
}

bool KeyValueFile::tryGetString(const std::string &section, const std::string &key,
	std::string_view &value) const
{
	return this->tryGetValue(section, key, value);
}

const std::unordered_map<std::string, KeyValueFile::SectionMap> &KeyValueFile::getAll() const
{
	return this->sectionMaps;
}
