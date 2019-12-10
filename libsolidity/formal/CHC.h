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

/**
 * Model checker based on Constrained Horn Clauses.
 *
 * A Solidity contract's CFG is encoded into a system of Horn clauses where
 * each block has a predicate and edges are rules.
 *
 * The entry block is the constructor which has no in-edges.
 * The constructor has one out-edge to an artificial block named _Interface_
 * which has in/out-edges from/to all public functions.
 *
 * Loop invariants for Interface -> Interface' are state invariants.
 */

#pragma once

#include <libsolidity/formal/SMTEncoder.h>

#include <libsolidity/formal/CHCSolverInterface.h>

#include <libsolidity/interface/ReadFile.h>

#include <set>

namespace dev
{
namespace solidity
{

class CHC: public SMTEncoder
{
public:
	CHC(
		smt::EncodingContext& _context,
		langutil::ErrorReporter& _errorReporter,
		std::map<h256, std::string> const& _smtlib2Responses,
		ReadCallback::Callback const& _smtCallback,
		smt::SMTSolverChoice _enabledSolvers
	);

	void analyze(SourceUnit const& _sources);

	std::set<Expression const*> const& safeAssertions() const { return m_safeAssertions; }

	/// This is used if the Horn solver is not directly linked into this binary.
	/// @returns a list of inputs to the Horn solver that were not part of the argument to
	/// the constructor.
	std::vector<std::string> unhandledQueries() const;

private:
	/// Visitor functions.
	//@{
	bool visit(ContractDefinition const& _node) override;
	void endVisit(ContractDefinition const& _node) override;
	bool visit(FunctionDefinition const& _node) override;
	void endVisit(FunctionDefinition const& _node) override;
	bool visit(IfStatement const& _node) override;
	bool visit(WhileStatement const&) override;
	bool visit(ForStatement const&) override;
	void endVisit(FunctionCall const& _node) override;
	void endVisit(Break const& _node) override;
	void endVisit(Continue const& _node) override;

	void visitAssert(FunctionCall const& _funCall);
	void internalFunctionCall(FunctionCall const& _funCall);
	void unknownFunctionCall(FunctionCall const& _funCall);
	//@}

	/// Helpers.
	//@{
	void reset();
	void eraseKnowledge();
	void clearIndices(ContractDefinition const* _contract, FunctionDefinition const* _function = nullptr) override;
	bool shouldVisit(ContractDefinition const& _contract) const;
	bool shouldVisit(FunctionDefinition const& _function) const;
	void setCurrentBlock(smt::SymbolicFunctionVariable const& _block, std::vector<smt::Expression> const* _arguments = nullptr);
	//@}

	/// Sort helpers.
	//@{
	smt::SortPointer constructorSort();
	smt::SortPointer interfaceSort();
	smt::SortPointer sort(FunctionDefinition const& _function);
	smt::SortPointer sort(ASTNode const* _block);
	// Sort for function calls. This is:
	// (stateVarsSorts inputSorts outputSorts stateVarSorts)
	smt::SortPointer summarySort(FunctionDefinition const& _function);
	//@}

	/// Predicate helpers.
	//@{
	/// @returns a new block of given _sort and _name.
	std::unique_ptr<smt::SymbolicFunctionVariable> createSymbolicBlock(smt::SortPointer _sort, std::string const& _name);

	/// Genesis predicate.
	smt::Expression genesis() { return (*m_genesisPredicate)({}); }
	/// Interface predicate over current variables.
	smt::Expression interface();
	/// Error predicate over current variables.
	smt::Expression error();
	smt::Expression error(unsigned _idx);

	/// Creates a block for the given _node.
	std::unique_ptr<smt::SymbolicFunctionVariable> createBlock(ASTNode const* _node, std::string const& _prefix = "");
	// Creates a call block for the given function _node.
	std::unique_ptr<smt::SymbolicFunctionVariable> createSummaryBlock(FunctionDefinition const* _node);

	/// Creates a new error block to be used by an assertion.
	/// Also registers the predicate.
	void createErrorBlock();

	void connectBlocks(smt::Expression const& _from, smt::Expression const& _to, smt::Expression const& _constraints = smt::Expression(true));

	/// @returns the symbolic values of the state variables at the beginning
	/// of the current transaction.
	std::vector<smt::Expression> initialTxStateVariables();
	/// @returns the symbolic values of the state variables at the beginning
	/// of the current function.
	std::vector<smt::Expression> initialInternalStateVariables();
	/// @returns the symbolic values of the state variables with the given _index.
	/// Used as helper for the two functions above.
	std::vector<smt::Expression> stateVariablesAtIndex(int _index);
	/// @returns the current symbolic values of the current state variables.
	std::vector<smt::Expression> currentStateVariables();

	/// @returns the current symbolic values of the current function's
	/// input and output parameters.
	std::vector<smt::Expression> currentFunctionVariables();
	/// @returns the same as currentFunctionVariables plus
	/// local variables.
	std::vector<smt::Expression> currentBlockVariables();

	/// @returns the predicate name for a given node.
	std::string predicateName(ASTNode const* _node);
	/// @returns a predicate application over the current scoped variables.
	smt::Expression predicate(smt::SymbolicFunctionVariable const& _block);
	/// @returns a predicate application over @param _arguments.
	smt::Expression predicate(smt::SymbolicFunctionVariable const& _block, std::vector<smt::Expression> const& _arguments);
	/// @returns the summary predicate for the called function.
	smt::Expression predicate(FunctionCall const& _funCall);
	/// @returns a predicate that defines a function summary.
	smt::Expression summary(FunctionDefinition const& _function);
	//@}

	/// Solver related.
	//@{
	/// Adds Horn rule to the solver.
	void addRule(smt::Expression const& _rule, std::string const& _ruleName);
	/// @returns true if query is unsatisfiable (safe).
	bool query(smt::Expression const& _query, langutil::SourceLocation const& _location);
	//@}

	/// Misc.
	//@{
	/// Returns a prefix to be used in a new unique block name
	/// and increases the block counter.
	std::string uniquePrefix();
	//@}

	/// Predicates.
	//@{
	/// Genesis predicate.
	std::unique_ptr<smt::SymbolicFunctionVariable> m_genesisPredicate;

	/// Implicit constructor predicate.
	/// Explicit constructors are handled as functions.
	std::unique_ptr<smt::SymbolicFunctionVariable> m_constructorPredicate;

	/// Artificial Interface predicate.
	/// Single entry block for all functions.
	std::unique_ptr<smt::SymbolicFunctionVariable> m_interfacePredicate;

	/// Artificial Error predicate.
	/// Single error block for all assertions.
	std::unique_ptr<smt::SymbolicFunctionVariable> m_errorPredicate;

	/// Function predicates.
	std::map<FunctionDefinition const*, std::unique_ptr<smt::SymbolicFunctionVariable>> m_summaries;
	//@}

	/// Variables.
	//@{
	/// State variables sorts.
	/// Used by all predicates.
	std::vector<smt::SortPointer> m_stateSorts;
	/// State variables.
	/// Used to create all predicates.
	std::vector<VariableDeclaration const*> m_stateVariables;
	//@}

	/// Verification targets.
	//@{
	std::vector<Expression const*> m_verificationTargets;

	/// Assertions proven safe.
	std::set<Expression const*> m_safeAssertions;
	//@}

	/// Control-flow.
	//@{
	FunctionDefinition const* m_currentFunction = nullptr;

	/// The current block.
	smt::Expression m_currentBlock = smt::Expression(true);

	/// Counter to generate unique block names.
	unsigned m_blockCounter = 0;

	/// Whether a function call was seen in the current scope.
	bool m_unknownFunctionCallSeen = false;

	/// Block where a loop break should go to.
	smt::SymbolicFunctionVariable const* m_breakDest = nullptr;
	/// Block where a loop continue should go to.
	smt::SymbolicFunctionVariable const* m_continueDest = nullptr;
	//@}

	/// CHC solver.
	std::shared_ptr<smt::CHCSolverInterface> m_interface;

	/// ErrorReporter that comes from CompilerStack.
	langutil::ErrorReporter& m_outerErrorReporter;

	/// SMT solvers that are chosen at runtime.
	smt::SMTSolverChoice m_enabledSolvers;
};

}
}
