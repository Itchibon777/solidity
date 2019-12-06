/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "UpgradeChange.h"

#include <libsolidity/interface/CompilerStack.h>
#include <libsolidity/interface/DebugSettings.h>
#include <libyul/AssemblyStack.h>
#include <liblangutil/EVMVersion.h>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>

#include <memory>

namespace dev
{
namespace solidity
{

class SolidityUpgrade {
public:
	/// Parse command line arguments and return false in case of a failure.
	bool parseArguments(int _argc, char** _argv);
	/// Prints additional information on the upgrade tool.
	void printPrologue();
	/// Parse / compile files and runs upgrade analysis on them.
	bool processInput();

private:
	/// Runs upgrade analysis on source and applies upgrades changes to it. Returns `true` if
	/// there're still changes that can be applied, `false` otherwise.
	bool analyzeAndUpgrade(
		std::pair<std::string, std::string> const& _sourceCode,
		std::vector<UpgradeChange>& _changes
	);
	/// Analyses and upgrades the sources given. The upgrade happens in a loop, applying one change
	/// at a time, which is run until no applicable changes are found any more.
	/// Only one change is done at a time and all sources are being compiled again after each change.
	void runUpgrade(std::vector<UpgradeChange>& _changes);
	/// Applies the change given to its source code. If no `--dry-run` was passed via the commandline,
	/// the upgraded source code is written back to its file.
	void applyChange(
		std::pair<std::string, std::string> const& _sourceCode,
		UpgradeChange& _change
	);

	/// Resets the compiler stack and configures sources to compile. Also enables error recovery.
	void resetCompiler();
	/// Resets the compiler stack and configures sources to compile. Also enables error recovery.
	/// Passes read callback to the compiler stack.
	void resetCompiler(ReadCallback::Callback const& _callback);
	/// Parses the current sources and runs analyses as well as compilation on them if parsing was
	/// successful.
	void tryCompile() const;

	/// Prints all errors (excluding warnings) the compiler currently reported.
	void printErrors() const;

	/// Reads all input files given and stores sources in the internal data structure. Reports errors
	/// if files cannot be found.
	bool readInputFiles();
	/// Writes source to file given.
	bool writeInputFile(std::string const& _path, std::string const& _source);

	/// Compiler arguments variable map
	boost::program_options::variables_map m_args;
	/// Map of input files to source code strings
	std::map<std::string, std::string> m_sourceCodes;
	/// Solidity compiler stack
	std::unique_ptr<dev::solidity::CompilerStack> m_compiler;
	/// List of allowed directories to read files from
	std::vector<boost::filesystem::path> m_allowedDirectories;
};

}
}
