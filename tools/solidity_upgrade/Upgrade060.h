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
#include "UpgradeSuite.h"

#include <libsolidity/ast/ASTVisitor.h>

namespace dev
{
namespace solidity
{

/**
 * Module that performs analysis on the AST. Finds abstract contracts that are
 * not marked as such and adds the `abstract` keyword.
 */
class AbstractContract: public AnalysisUpgrade {
public:
	using AnalysisUpgrade::AnalysisUpgrade;

	void analyze(SourceUnit const& _sourceUnit) { _sourceUnit.accept(*this); }
private:
	void endVisit(ContractDefinition const& _contract);
};

/**
 * Module that performs analysis on the AST. Finds functions that need to be
 * marked `override` and adds the keyword to the function header.
 */
class OverridingFunction: public AnalysisUpgrade {
public:
	using AnalysisUpgrade::AnalysisUpgrade;

	void analyze(SourceUnit const& _sourceUnit) { _sourceUnit.accept(*this); }
private:
	void endVisit(ContractDefinition const& _contract);
};

/**
 * Module that performs analysis on the AST. Finds functions that need to be
 * marked `virtual` and adds the keyword to the function header.
 */
class VirtualFunction: public AnalysisUpgrade {
public:
	using AnalysisUpgrade::AnalysisUpgrade;

	void analyze(SourceUnit const& _sourceUnit) { _sourceUnit.accept(*this); }
private:
	void endVisit(ContractDefinition const& _function);
};

/**
 * Solidity 0.6.0 specific upgrade suite that hosts all available
 * upgrade modules.
 */
class Upgrade060: public UpgradeSuite
{
public:
	void analyze(
		SourceUnit const& _sourceUnit,
		std::string const& _source,
		std::vector<UpgradeChange>& _changes
	)
	{
		AbstractContract{_source, _changes}.analyze(_sourceUnit);
		OverridingFunction{_source, _changes}.analyze(_sourceUnit);
		VirtualFunction{_source, _changes}.analyze(_sourceUnit);
	}
};

}
}
