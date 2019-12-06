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
#include "SolidityUpgrade.h"

#include "Upgrade060.h"

#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatterHuman.h>

#include <libsolidity/ast/AST.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

using namespace std;

namespace dev
{
namespace solidity
{

static string const g_argHelp = "help";
static string const g_argVersion = "version";
static string const g_argInputFile = "input-file";
static string const g_argDryRun = "dry-run";
static string const g_argUnsafe = "unsafe";
static string const g_argVerbose = "verbose";
static string const g_argIgnoreMissingFiles = "ignore-missing";
static string const g_argAllowPaths = "allow-paths";

namespace
{
	ostream& out()
	{
		return cout;
	}

	AnsiColorized log()
	{
		return AnsiColorized(cout, true, {});
	}

	AnsiColorized success()
	{
		return AnsiColorized(cout, true, {formatting::CYAN});
	}

	AnsiColorized warning()
	{
		return AnsiColorized(cout, true, {formatting::YELLOW});
	}

	AnsiColorized error()
	{
		return AnsiColorized(cout, true, {formatting::MAGENTA});
	}

	void logVersion()
	{
		/// TODO Replace by variable that can be set during build.
		out() << "0.1.0" << endl;
	}

	void logProgress()
	{
		out() << ".";
		out().flush();
	}
}

bool SolidityUpgrade::parseArguments(int _argc, char** _argv)
{
	po::options_description desc(R"(solidity-upgrade, the Solidity upgrade assistant.

The solidity-upgrade tool can help upgrade smart contracts to breaking language features.

It does not support all breaking changes for each version,
but will hopefully assist upgrading your contracts to the desired Solidity version.

List of supported breaking changes:

0.5.0
	none

0.6.0
	- abstract contracts (safe)
	- override / virtual (safe)


solidity-upgrade is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY. Please be careful when running upgrades on
your contracts.

Usage: solidity-upgrade [options] contract.sol

Allowed options)",
		po::options_description::m_default_line_length,
		po::options_description::m_default_line_length - 23
	);
	desc.add_options()
		(g_argHelp.c_str(), "Show help message and exit.")
		(g_argVersion.c_str(), "Show version and exit.")
		(g_argDryRun.c_str(), "Do not accept *safe* changes and don't write to input file.")
		(g_argUnsafe.c_str(), "Accept *unsafe* changes.")
		(g_argVerbose.c_str(), "Prints errors and changes. Shortens output of upgrade patches.")

		(g_argIgnoreMissingFiles.c_str(), "Ignores missing files.")
		(
			g_argAllowPaths.c_str(),
			po::value<string>()->value_name("path(s)"),
			"Allow a given path for imports. A list of paths can be supplied by separating them with a comma."
		);

	po::options_description allOptions = desc;
	allOptions.add_options()("input-file", po::value<vector<string>>(), "input file");

	po::positional_options_description filesPositions;
	filesPositions.add("input-file", -1);

	// parse the compiler arguments
	try
	{
		po::command_line_parser cmdLineParser(_argc, _argv);
		cmdLineParser.style(po::command_line_style::default_style & (~po::command_line_style::allow_guessing));
		cmdLineParser.options(allOptions).positional(filesPositions);
		po::store(cmdLineParser.run(), m_args);
	}
	catch (po::error const& _exception)
	{
		error() << _exception.what() << endl;
		return false;
	}

	if (m_args.count(g_argHelp) || (isatty(fileno(stdin)) && _argc == 1))
	{
		out() << endl;
		log() << desc;
		return false;
	}

	if (m_args.count(g_argVersion))
	{
		logVersion();
		return false;
	}

	/// TODO Share with solc commandline interface.
	if (m_args.count(g_argAllowPaths))
	{
		vector<string> paths;
		for (string const& path: boost::split(paths, m_args[g_argAllowPaths].as<string>(), boost::is_any_of(",")))
		{
			auto filesystem_path = boost::filesystem::path(path);
			/// If the given path had a trailing slash, the Boost filesystem
			/// path will have it's last component set to '.'. This breaks
			/// path comparison in later parts of the code, so we need to strip
			/// it.
			if (filesystem_path.filename() == ".")
				filesystem_path.remove_filename();
			m_allowedDirectories.push_back(filesystem_path);
		}
	}

	return true;
}

void SolidityUpgrade::printPrologue()
{
	out() << endl;
	out() << endl;

	log() <<
		"solidity-upgrade does not support all breaking changes for each version." <<
		endl <<
		"Please run `solidity-upgrade --help` and get a list of implemented upgrades." <<
		endl <<
		endl;

	log() <<
		"Running analysis (and upgrade) on given source files..." <<
		endl;
}

bool SolidityUpgrade::processInput()
{
	vector<UpgradeChange> changes;

	/// TODO Share with solc commandline interface.
	ReadCallback::Callback fileReader = [this](string const&, string const& _path)
	{
		try
		{
			auto path = boost::filesystem::path(_path);
			auto canonicalPath = boost::filesystem::weakly_canonical(path);
			bool isAllowed = false;
			for (auto const& allowedDir: m_allowedDirectories)
			{
				// If dir is a prefix of boostPath, we are fine.
				if (
					std::distance(allowedDir.begin(), allowedDir.end()) <= std::distance(canonicalPath.begin(), canonicalPath.end()) &&
					std::equal(allowedDir.begin(), allowedDir.end(), canonicalPath.begin())
				)
				{
					isAllowed = true;
					break;
				}
			}
			if (!isAllowed)
				return ReadCallback::Result{false, "File outside of allowed directories."};

			if (!boost::filesystem::exists(canonicalPath))
				return ReadCallback::Result{false, "File not found."};

			if (!boost::filesystem::is_regular_file(canonicalPath))
				return ReadCallback::Result{false, "Not a valid file."};

			auto contents = dev::readFileAsString(canonicalPath.string());
			m_sourceCodes[path.generic_string()] = contents;
			return ReadCallback::Result{true, contents};
		}
		catch (Exception const& _exception)
		{
			return ReadCallback::Result{false, "Exception in read callback: " + boost::diagnostic_information(_exception)};
		}
		catch (...)
		{
			return ReadCallback::Result{false, "Unknown exception in read callback."};
		}
	};

	if (!readInputFiles())
		return false;

	resetCompiler(fileReader);
	tryCompile();

	runUpgrade(changes);

	out() << endl;
	out() << endl;
	error() << "Found " << m_compiler->errors().size() << " errors." << endl;
	success() << "Found " << changes.size() << " upgrades." << endl;

	return true;
}

void SolidityUpgrade::runUpgrade(vector<UpgradeChange>& _changes)
{
	bool recompile = true;

	while (recompile && !m_compiler->errors().empty())
	{
		for (auto& sourceCode: m_sourceCodes)
		{
			recompile = analyzeAndUpgrade(sourceCode, _changes);
			if (recompile)
				break;
		}

		if (recompile)
		{
			_changes.clear();
			resetCompiler();
			tryCompile();
		}
	}
}

bool SolidityUpgrade::analyzeAndUpgrade(
	pair<string, string> const& _sourceCode,
	vector<UpgradeChange>& _changes
)
{
	bool applyUnsafe = m_args.count(g_argUnsafe);
	bool verbose = m_args.count(g_argVerbose);

	if (verbose)
		log() << "Analyzing and upgrading " << _sourceCode.first << "..." << endl;

	if (m_compiler->state() >= CompilerStack::State::AnalysisPerformed)
		Upgrade060{}.analyze(
			m_compiler->ast(_sourceCode.first),
			_sourceCode.second,
			_changes
		);

	if (!_changes.empty())
	{
		auto& change = _changes.front();

		if (verbose)
			change.log(true);

		if (change.level() == UpgradeChange::Level::Safe)
		{
			applyChange(_sourceCode, change);
			return true;
		}
		else if (change.level() == UpgradeChange::Level::Unsafe)
		{
			if (applyUnsafe)
			{
				applyChange(_sourceCode, change);
				return true;
			}
		}
	}

	return false;
}

void SolidityUpgrade::applyChange(
	pair<string, string> const& _sourceCode,
	UpgradeChange& _change
)
{
	bool dryRun = m_args.count(g_argDryRun);

	_change.apply();
	m_sourceCodes[_sourceCode.first] = _change.source();

	if (!dryRun)
		writeInputFile(_sourceCode.first, _change.source());
}

void SolidityUpgrade::resetCompiler()
{
	m_compiler->reset();
	m_compiler->setSources(m_sourceCodes);
	m_compiler->setParserErrorRecovery(true);
}

void SolidityUpgrade::resetCompiler(ReadCallback::Callback const& _callback)
{
	m_compiler.reset(new CompilerStack(_callback));
	m_compiler->setSources(m_sourceCodes);
	m_compiler->setParserErrorRecovery(true);
}

void SolidityUpgrade::tryCompile() const
{
	bool verbose = m_args.count(g_argVerbose);

	if (verbose)
		log() << "Running compilation phases..." << endl << endl;
	else
		logProgress();

	try
	{
		if (m_compiler->parse())
		{
			if (m_compiler->analyze())
				m_compiler->compile();
			else
				if (verbose)
				{
					error() <<
						"Compilation errors that solidity-upgrade may resolve occurred." <<
						endl <<
						endl;

					printErrors();
				}
		}
		else
			if (verbose)
			{
				error() <<
					"Compilation errors that solidity-upgrade cannot resolve occurred." <<
					endl <<
					endl;

				printErrors();
			}
	}
	catch (Exception const& _exception)
	{
		error() << "Exception during compilation: " << boost::diagnostic_information(_exception) << endl;
	}
	catch (std::exception const& _e)
	{
		error() << (_e.what() ? ": " + string(_e.what()) : ".") << endl;
	}
	catch (...)
	{
		error() << "Unknown exception during compilation." << endl;
	}
}

void SolidityUpgrade::printErrors() const
{
	auto formatter = make_unique<langutil::SourceReferenceFormatterHuman>(cout, true);

	for (auto const& error: m_compiler->errors())
		if (error->type() != langutil::Error::Type::Warning)
			formatter->printErrorInformation(*error);
}

bool SolidityUpgrade::readInputFiles()
{
	bool ignoreMissing = m_args.count(g_argIgnoreMissingFiles);

	if (m_args.count(g_argInputFile))
		for (string path: m_args[g_argInputFile].as<vector<string>>())
		{
			auto infile = boost::filesystem::path(path);
			if (!boost::filesystem::exists(infile))
			{
				if (!ignoreMissing)
				{
					error() << infile << " is not found." << endl;
					return false;
				}
				else
					error() << infile << " is not found. Skipping." << endl;

				continue;
			}

			if (!boost::filesystem::is_regular_file(infile))
			{
				if (!ignoreMissing)
				{
					error() << infile << " is not a valid file." << endl;
					return false;
				}
				else
					error() << infile << " is not a valid file. Skipping." << endl;

				continue;
			}

			m_sourceCodes[infile.generic_string()] = dev::readFileAsString(infile.string());
			path = boost::filesystem::canonical(infile).string();
		}

	if (m_sourceCodes.size() == 0)
	{
		warning() << "No input files given. If you wish to use the standard input please specify \"-\" explicitly." << endl;
		return false;
	}

	return true;
}

bool SolidityUpgrade::writeInputFile(string const& _path, string const& _source)
{
	bool verbose = m_args.count(g_argVerbose);

	if (verbose)
	{
		out() << endl;
		log() << "Writing to input file " << _path << "..." << endl;
	}

	ofstream file(_path, ios::trunc);
	file << _source;

	return true;
}

}
}
