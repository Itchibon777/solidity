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
#include "Upgrade060.h"

#include <libsolidity/analysis/OverrideChecker.h>

#include <libyul/AsmData.h>

#include <regex>

using namespace std;
using namespace langutil;

namespace dev
{
namespace solidity
{

using ContractSet = set<ContractDefinition const*, OverrideChecker::LessFunction>;

namespace
{
	inline bool isMultilineFunction(string const& _functionSource, string const& _keyword)
	{
		regex multiline{"(\\b" + _keyword + "\\b\\n|\\r|\\r\\n)"};
		return regex_search(_functionSource, multiline);
	}

	inline string placeAfterFunctionHeaderKeyword(
		langutil::SourceLocation const& _location,
		string const& _headerKeyword,
		string const& _keyword
	)
	{
		string codeBefore = _location.source->source().substr(
			_location.start,
			_location.end - _location.start
		);

		bool isMultiline = isMultilineFunction(codeBefore, _headerKeyword);
		string toAppend = isMultiline ? ("\n        " + _keyword) : (" " + _keyword);
		regex keywordRegex{"(\\b" + _headerKeyword + "\\b)"};

		return regex_replace(codeBefore, keywordRegex, _headerKeyword + toAppend);
	}

	inline string overrideList(ContractSet const& _contracts)
	{
		string overrideList;
		for (auto inheritedContract: _contracts)
			overrideList += inheritedContract->name() + ",";
		return overrideList.substr(0, overrideList.size() - 1);
	}
}

void AbstractContract::endVisit(ContractDefinition const& _contract)
{
	bool isFullyImplemented = _contract.annotation().unimplementedFunctions.empty();

	if (
		!isFullyImplemented &&
		!_contract.abstract() &&
		!_contract.isInterface()
	)
	{
		// TODO make this more memory-friendly by just passing a reference to
		// the source parts everywhere.
		auto location = _contract.location();

		string codeBefore = location.source->source().substr(location.start, location.end - location.start);
		string codeAfter = "abstract " + codeBefore;

		m_changes.push_back(
			UpgradeChange{UpgradeChange::Level::Safe, location, codeAfter}
		);
	}
}

void OverridingFunction::endVisit(ContractDefinition const& _contract)
{
	auto const& inheritedFunctions = m_overrideChecker.inheritedFunctions(_contract);

	for (auto const* function: _contract.definedFunctions())
	{
		ContractSet expectedContracts;

		if (!function->isConstructor())
		{
			/// Build list of contracts expected to be mentioned in the override list (if any).
			for (auto [begin, end] = inheritedFunctions.equal_range(function); begin != end; begin++)
				expectedContracts.insert((*begin)->annotation().contract);

			/// Add override with contract list, if needed.
			if (!function->overrides() && expectedContracts.size() > 1)
			{
				string codeAfter = placeAfterFunctionHeaderKeyword(
					function->location(),
					Declaration::visibilityToString(function->visibility()),
					"override(" + overrideList(expectedContracts) + ")"
				);

				m_changes.push_back(
					UpgradeChange{UpgradeChange::Level::Safe, function->location(), codeAfter}
				);
			}

			for (auto [begin, end] = inheritedFunctions.equal_range(function); begin != end; begin++)
			{
				auto& super = (**begin);
				auto functionType = FunctionType(*function).asCallableFunction(false);
				auto superType = FunctionType(super).asCallableFunction(false);

				if (functionType && functionType->hasEqualParameterTypes(*superType))
				{
					/// If function does not specify override and no override with
					/// contract list was added before.
					if (!function->overrides() && expectedContracts.size() <= 1)
					{
						string codeAfter = placeAfterFunctionHeaderKeyword(
							function->location(),
							Declaration::visibilityToString(function->visibility()),
							"override"
						);

						m_changes.push_back(
							UpgradeChange{UpgradeChange::Level::Safe, function->location(), codeAfter}
						);
					}
				}
			}
		}
	}
}

void VirtualFunction::endVisit(ContractDefinition const& _contract)
{
	auto const& inheritedFunctions = m_overrideChecker.inheritedFunctions(_contract);

	for (FunctionDefinition const* function: _contract.definedFunctions())
	{
		if (!function->isConstructor())
		{
			if (
				!function->markedVirtual() &&
				!function->isImplemented() &&
				!function->virtualSemantics() &&
				function->visibility() > Declaration::Visibility::Private
			)
			{
				string codeAfter = placeAfterFunctionHeaderKeyword(
					function->location(),
					Declaration::visibilityToString(function->visibility()),
					"virtual"
				);

				m_changes.push_back(
					UpgradeChange{UpgradeChange::Level::Safe, function->location(), codeAfter}
				);
			}

			for (auto [begin, end] = inheritedFunctions.equal_range(function); begin != end; begin++)
			{
				auto& super = (**begin);
				if (
					!function->markedVirtual() &&
					!super.virtualSemantics()
				)
				{
					string codeAfter = placeAfterFunctionHeaderKeyword(
						function->location(),
						Declaration::visibilityToString(function->visibility()),
						"virtual"
					);

					m_changes.push_back(
						UpgradeChange{UpgradeChange::Level::Safe, function->location(), codeAfter}
					);

				}
			}
		}
	}
}

}
}
