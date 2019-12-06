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

#include <liblangutil/ErrorReporter.h>

#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/analysis/OverrideChecker.h>

#include <regex>

namespace dev
{
namespace solidity
{

/**
 * The base upgrade module that can be inherited from. Doing so
 * creates a basic upgrade module that facilitates access to
 * source code and change reporting.
 */
class Upgrade
{
public:
	Upgrade(
		std::string const& _source,
		std::vector<UpgradeChange>& _changes
	):
		m_source(_source),
		m_changes(_changes)
	{}

protected:
	/// The source code this upgrade operates on.
	std::string const& m_source;
	/// A reference to a global, runtime-specific set of changes.
	/// It is passed to all upgrade modules and meant to collect
	/// all reported changes.
	std::vector<UpgradeChange>& m_changes;
};

/**
 * A specific upgrade module meant to be run after the analysis phase
 * of the compiler.
 */
class AnalysisUpgrade: public Upgrade, public ASTConstVisitor
{
public:
	AnalysisUpgrade(
		std::string const& _source,
		std::vector<UpgradeChange>& _changes
	):
		Upgrade(_source, _changes),
		m_errorReporter(m_errors),
		m_overrideChecker(m_errorReporter)
	{}
	/// Interface function for all upgrade modules that are meant
	/// be run after the analysis phase of the compiler.
	void analyze(SourceUnit const&) {}
protected:
	langutil::ErrorList m_errors;
	langutil::ErrorReporter m_errorReporter;
	OverrideChecker m_overrideChecker;
};

/**
 * The generic upgrade suite. Should be inherited from for each set of
 * desired upgrade modules.
 */
class UpgradeSuite
{
public:
	/// The base interface function that needs to be implemented for each suite.
	/// It should create suite-specific upgrade modules and trigger their analysis.
	void analyze(
		SourceUnit const& _sourceUnit,
		std::string const& _source,
		std::vector<UpgradeChange>& _changes
	);
};
}
}
