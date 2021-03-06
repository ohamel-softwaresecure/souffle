/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2017, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ProvenanceTransformer.cpp
 *
 * Implements AstTransformer for adding provenance information via extra columns
 *
 ***********************************************************************/

#include "AstArgument.h"
#include "AstAttribute.h"
#include "AstClause.h"
#include "AstLiteral.h"
#include "AstNode.h"
#include "AstProgram.h"
#include "AstRelation.h"
#include "AstRelationIdentifier.h"
#include "AstTransforms.h"
#include "AstTranslationUnit.h"
#include "AstType.h"
#include "AstUtils.h"
#include "AstVisitor.h"
#include "AuxArityAnalysis.h"
#include "BinaryConstraintOps.h"
#include "FunctorOps.h"
#include "Global.h"
#include "RelationRepresentation.h"
#include "Util.h"
#include <cassert>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace souffle {

/**
 * Helper functions
 */
const std::string identifierToString(const AstRelationIdentifier& name) {
    std::stringstream ss;
    ss << name;
    return ss.str();
}

inline AstRelationIdentifier makeRelationName(
        const AstRelationIdentifier& orig, const std::string& type, int num = -1) {
    AstRelationIdentifier newName(identifierToString(orig));
    newName.append(type);
    if (num != -1) {
        newName.append((const std::string&)std::to_string(num));
    }

    return newName;
}

std::unique_ptr<AstRelation> makeInfoRelation(
        AstClause& originalClause, size_t originalClauseNum, AstTranslationUnit& translationUnit) {
    AstRelationIdentifier name =
            makeRelationName(originalClause.getHead()->getName(), "@info", originalClauseNum);

    // initialise info relation
    auto infoRelation = new AstRelation();
    infoRelation->setName(name);
    // set qualifier to INFO_RELATION
    infoRelation->setQualifier(INFO_RELATION);

    // create new clause containing a single fact
    auto infoClause = new AstClause();
    auto infoClauseHead = new AstAtom();
    infoClauseHead->setName(name);

    infoRelation->addAttribute(std::make_unique<AstAttribute>("clause_num", AstTypeIdentifier("number")));
    infoClauseHead->addArgument(std::make_unique<AstNumberConstant>(originalClauseNum));

    // add head relation as meta info
    std::vector<std::string> headVariables;

    // a method to stringify an AstArgument, translating functors and aggregates
    // keep a global counter of functor and aggregate numbers, which increment for each unique
    // functor/aggregate
    int functorNumber = 0;
    int aggregateNumber = 0;
    auto getArgInfo = [&](AstArgument* arg) -> std::string {
        if (auto* var = dynamic_cast<AstVariable*>(arg)) {
            std::stringstream varName;
            var->print(varName);
            return varName.str();
        } else if (auto* constant = dynamic_cast<AstConstant*>(arg)) {
            std::stringstream constName;
            constant->print(constName);
            return constName.str();
        } else if (nullptr != dynamic_cast<AstUnnamedVariable*>(arg)) {
            return "_";
        } else if (nullptr != dynamic_cast<AstFunctor*>(arg)) {
            auto functorName = "functor_" + std::to_string(functorNumber);
            functorNumber++;
            return functorName;
        } else if (nullptr != dynamic_cast<AstAggregator*>(arg)) {
            auto aggregateName = "agg_" + std::to_string(aggregateNumber);
            aggregateNumber++;
            return aggregateName;
        }
        assert(false && "Unhandled argument type");
    };

    // add head arguments
    for (auto& arg : originalClause.getHead()->getArguments()) {
        headVariables.push_back(getArgInfo(arg));
    }

    // join variables in the head with commas
    std::stringstream headVariableString;
    headVariableString << join(headVariables, ",");

    // add an attribute to infoRelation for the head of clause
    infoRelation->addAttribute(
            std::make_unique<AstAttribute>(std::string("head_vars"), AstTypeIdentifier("symbol")));
    infoClauseHead->addArgument(
            std::make_unique<AstStringConstant>(translationUnit.getSymbolTable(), headVariableString.str()));

    // visit all body literals and add to info clause head
    for (size_t i = 0; i < originalClause.getBodyLiterals().size(); i++) {
        auto lit = originalClause.getBodyLiterals()[i];
        const AstAtomLiteral* atomLit = dynamic_cast<AstAtomLiteral*>(lit);
        const AstAtom* atom = nullptr;
        if (atomLit != nullptr) {
            atom = atomLit->getAtom();
        }

        // add an attribute for atoms and binary constraints
        if (atom != nullptr || dynamic_cast<AstBinaryConstraint*>(lit) != nullptr) {
            infoRelation->addAttribute(std::make_unique<AstAttribute>(
                    std::string("rel_") + std::to_string(i), AstTypeIdentifier("symbol")));
        }

        if (atom != nullptr) {
            std::string relName = identifierToString(atom->getName());

            // for an atom, add its name and variables (converting aggregates to variables)
            if (dynamic_cast<AstAtom*>(lit) != nullptr) {
                std::string atomDescription = relName;

                for (auto& arg : atom->getArguments()) {
                    atomDescription.append("," + getArgInfo(arg));
                }

                infoClauseHead->addArgument(std::make_unique<AstStringConstant>(
                        translationUnit.getSymbolTable(), atomDescription));
                // for a negation, add a marker with the relation name
            } else if (dynamic_cast<AstNegation*>(lit) != nullptr) {
                infoClauseHead->addArgument(std::make_unique<AstStringConstant>(
                        translationUnit.getSymbolTable(), ("!" + relName)));
            }
            // for a constraint, add the constraint symbol and LHS and RHS
        } else if (auto con = dynamic_cast<AstBinaryConstraint*>(lit)) {
            std::string constraintDescription = toBinaryConstraintSymbol(con->getOperator());

            constraintDescription.append("," + getArgInfo(con->getLHS()));
            constraintDescription.append("," + getArgInfo(con->getRHS()));

            infoClauseHead->addArgument(std::make_unique<AstStringConstant>(
                    translationUnit.getSymbolTable(), constraintDescription));
        }
    }

    // generate and add clause representation
    std::stringstream ss;
    originalClause.print(ss);

    infoRelation->addAttribute(std::make_unique<AstAttribute>("clause_repr", AstTypeIdentifier("symbol")));
    infoClauseHead->addArgument(
            std::make_unique<AstStringConstant>(translationUnit.getSymbolTable(), ss.str()));

    // set clause head and add clause to info relation
    infoClause->setHead(std::unique_ptr<AstAtom>(infoClauseHead));
    infoRelation->addClause(std::unique_ptr<AstClause>(infoClause));

    return std::unique_ptr<AstRelation>(infoRelation);
}

/** Transform eqrel relations to explicitly define equivalence relations */
void transformEqrelRelation(AstRelation& rel) {
    assert(rel.getRepresentation() == RelationRepresentation::EQREL &&
            "attempting to transform non-eqrel relation");
    assert(rel.getArity() == 2 && "eqrel relation not binary");

    rel.setQualifier(rel.getQualifier() - EQREL_RELATION + BTREE_RELATION);

    // transitivity
    // transitive clause: A(x, z) :- A(x, y), A(y, z).
    auto transitiveClause = new AstClause();
    auto transitiveClauseHead = new AstAtom(rel.getName());
    transitiveClauseHead->addArgument(std::make_unique<AstVariable>("x"));
    transitiveClauseHead->addArgument(std::make_unique<AstVariable>("z"));

    auto transitiveClauseBody = new AstAtom(rel.getName());
    transitiveClauseBody->addArgument(std::make_unique<AstVariable>("x"));
    transitiveClauseBody->addArgument(std::make_unique<AstVariable>("y"));

    auto transitiveClauseBody2 = new AstAtom(rel.getName());
    transitiveClauseBody2->addArgument(std::make_unique<AstVariable>("y"));
    transitiveClauseBody2->addArgument(std::make_unique<AstVariable>("z"));

    transitiveClause->setHead(std::unique_ptr<AstAtom>(transitiveClauseHead));
    transitiveClause->addToBody(std::unique_ptr<AstLiteral>(transitiveClauseBody));
    transitiveClause->addToBody(std::unique_ptr<AstLiteral>(transitiveClauseBody2));
    rel.addClause(std::unique_ptr<AstClause>(transitiveClause));

    // symmetric
    // symmetric clause: A(x, y) :- A(y, x).
    auto symClause = new AstClause();
    auto symClauseHead = new AstAtom(rel.getName());
    symClauseHead->addArgument(std::make_unique<AstVariable>("x"));
    symClauseHead->addArgument(std::make_unique<AstVariable>("y"));

    auto symClauseBody = new AstAtom(rel.getName());
    symClauseBody->addArgument(std::make_unique<AstVariable>("y"));
    symClauseBody->addArgument(std::make_unique<AstVariable>("x"));

    symClause->setHead(std::unique_ptr<AstAtom>(symClauseHead));
    symClause->addToBody(std::unique_ptr<AstLiteral>(symClauseBody));
    rel.addClause(std::unique_ptr<AstClause>(symClause));

    // reflexivity
    // reflexive clause: A(x, x) :- A(x, _).
    auto reflexiveClause = new AstClause();
    auto reflexiveClauseHead = new AstAtom(rel.getName());
    reflexiveClauseHead->addArgument(std::make_unique<AstVariable>("x"));
    reflexiveClauseHead->addArgument(std::make_unique<AstVariable>("x"));

    auto reflexiveClauseBody = new AstAtom(rel.getName());
    reflexiveClauseBody->addArgument(std::make_unique<AstVariable>("x"));
    reflexiveClauseBody->addArgument(std::make_unique<AstUnnamedVariable>());

    reflexiveClause->setHead(std::unique_ptr<AstAtom>(reflexiveClauseHead));
    reflexiveClause->addToBody(std::unique_ptr<AstLiteral>(reflexiveClauseBody));
    rel.addClause(std::unique_ptr<AstClause>(reflexiveClause));
}

bool ProvenanceTransformer::transformSubtreeHeights(AstTranslationUnit& translationUnit) {
    static auto program = translationUnit.getProgram();
    const auto& auxArityAnalysis = *translationUnit.getAnalysis<AuxiliaryArity>();

    // get next level number
    auto getNextLevelNumber = [&](std::vector<AstArgument*> levels) {
        if (levels.empty()) {
            return static_cast<AstArgument*>(new AstNumberConstant(0));
        }

        if (levels.size() == 1) {
            return static_cast<AstArgument*>(new AstIntrinsicFunctor(FunctorOp::ADD,
                    std::unique_ptr<AstArgument>(levels[0]), std::make_unique<AstNumberConstant>(1)));
        }

        auto currentMax = new AstIntrinsicFunctor(FunctorOp::MAX, std::unique_ptr<AstArgument>(levels[0]),
                std::unique_ptr<AstArgument>(levels[1]));

        for (size_t i = 2; i < levels.size(); i++) {
            currentMax = new AstIntrinsicFunctor(FunctorOp::MAX, std::unique_ptr<AstArgument>(currentMax),
                    std::unique_ptr<AstArgument>(levels[i]));
        }

        return static_cast<AstArgument*>(new AstIntrinsicFunctor(FunctorOp::ADD,
                std::unique_ptr<AstArgument>(currentMax), std::make_unique<AstNumberConstant>(1)));
    };

    for (auto relation : program->getRelations()) {
        if (relation->getRepresentation() == RelationRepresentation::EQREL) {
            // Explicitly expand eqrel relation
            transformEqrelRelation(*relation);
        }
    }

    for (auto relation : program->getRelations()) {
        // generate info relations for each clause
        // do this before all other transformations so that we record
        // the original rule without any instrumentation
        for (auto clause : relation->getClauses()) {
            if (!isFact(*clause)) {
                // add info relation
                program->addRelation(
                        makeInfoRelation(*clause, getClauseNum(program, clause), translationUnit));
            }
        }

        relation->addAttribute(
                std::make_unique<AstAttribute>(std::string("@rule_number"), AstTypeIdentifier("number")));
        relation->addAttribute(
                std::make_unique<AstAttribute>(std::string("@level_number"), AstTypeIdentifier("number")));
        for (size_t i = 0; i < auxArityAnalysis.getArity(relation) - 2; i++) {
            relation->addAttribute(std::make_unique<AstAttribute>(
                    std::string("@sublevel_number_" + std::to_string(i)), AstTypeIdentifier("number")));
        }
        for (auto clause : relation->getClauses()) {
            size_t clauseNum = getClauseNum(program, clause);
            std::function<std::unique_ptr<AstNode>(std::unique_ptr<AstNode>)> rewriter =
                    [&](std::unique_ptr<AstNode> node) -> std::unique_ptr<AstNode> {
                // add provenance columns

                if (auto atom = dynamic_cast<AstAtom*>(node.get())) {
                    // rule number
                    atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    // max level
                    atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    // level number
                    for (size_t i = 0; i < auxArityAnalysis.getArity(atom) - 2; i++) {
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    }
                } else if (auto neg = dynamic_cast<AstNegation*>(node.get())) {
                    auto atom = neg->getAtom();
                    // rule number
                    atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    // max level
                    atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    // level number
                    for (size_t i = 0; i < auxArityAnalysis.getArity(atom) - 2; i++) {
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    }
                }

                // otherwise - apply mapper recursively
                node->apply(makeLambdaAstMapper(rewriter));
                return node;
            };

            // add unnamed vars to each atom nested in arguments of head
            clause->getHead()->apply(makeLambdaAstMapper(rewriter));

            // if fact, level number is 0
            if (isFact(*clause)) {
                clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(0));
                for (size_t i = 0; i < auxArityAnalysis.getArity(relation) - 1; i++) {
                    clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(0));
                }
            } else {
                std::vector<AstArgument*> bodyLevels;

                for (size_t i = 0; i < clause->getBodyLiterals().size(); i++) {
                    auto lit = clause->getBodyLiterals()[i];

                    // add unnamed vars to each atom nested in arguments of lit
                    lit->apply(makeLambdaAstMapper(rewriter));

                    // add provenance columns to lit; first rule num, then level nums
                    if (auto atom = dynamic_cast<AstAtom*>(lit)) {
                        // rule num
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                        atom->addArgument(
                                std::make_unique<AstVariable>("@level_number_" + std::to_string(i)));
                        // level nums
                        for (size_t j = 0; j < auxArityAnalysis.getArity(atom) - 2; j++) {
                            atom->addArgument(std::make_unique<AstUnnamedVariable>());
                        }
                        bodyLevels.push_back(new AstVariable("@level_number_" + std::to_string(i)));
                    }
                }

                // add provenance columns to head lit
                // rule number
                clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(clauseNum));
                // max level
                clause->getHead()->addArgument(std::unique_ptr<AstArgument>(getNextLevelNumber(bodyLevels)));
                // level numbers
                size_t numAtoms = getBodyLiterals<AstAtom>(*clause).size();
                for (size_t j = 0; j < numAtoms; j++) {
                    clause->getHead()->addArgument(
                            std::make_unique<AstVariable>("@level_number_" + std::to_string(j)));
                }
                for (size_t j = numAtoms; j < auxArityAnalysis.getArity(relation) - 2; j++) {
                    clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(-1));
                }
            }
        }
    }
    return true;
}

bool ProvenanceTransformer::transformMaxHeight(AstTranslationUnit& translationUnit) {
    auto program = translationUnit.getProgram();

    // get next level number
    auto getNextLevelNumber = [&](std::vector<AstArgument*> levels) {
        if (levels.empty()) {
            return static_cast<AstArgument*>(new AstNumberConstant(0));
        }

        if (levels.size() == 1) {
            return static_cast<AstArgument*>(new AstIntrinsicFunctor(FunctorOp::ADD,
                    std::unique_ptr<AstArgument>(levels[0]), std::make_unique<AstNumberConstant>(1)));
        }

        auto currentMax = new AstIntrinsicFunctor(FunctorOp::MAX, std::unique_ptr<AstArgument>(levels[0]),
                std::unique_ptr<AstArgument>(levels[1]));

        for (size_t i = 2; i < levels.size(); i++) {
            currentMax = new AstIntrinsicFunctor(FunctorOp::MAX, std::unique_ptr<AstArgument>(currentMax),
                    std::unique_ptr<AstArgument>(levels[i]));
        }

        return static_cast<AstArgument*>(new AstIntrinsicFunctor(FunctorOp::ADD,
                std::unique_ptr<AstArgument>(currentMax), std::make_unique<AstNumberConstant>(1)));
    };

    for (auto relation : program->getRelations()) {
        if (relation->getRepresentation() == RelationRepresentation::EQREL) {
            // Explicitly expand eqrel relation
            transformEqrelRelation(*relation);
        }
    }

    for (auto relation : program->getRelations()) {
        // generate info relations for each clause
        // do this before all other transformations so that we record
        // the original rule without any instrumentation
        for (auto clause : relation->getClauses()) {
            if (!isFact(*clause)) {
                // add info relation
                program->addRelation(
                        makeInfoRelation(*clause, getClauseNum(program, clause), translationUnit));
            }
        }

        relation->addAttribute(
                std::make_unique<AstAttribute>(std::string("@rule_number"), AstTypeIdentifier("number")));
        relation->addAttribute(
                std::make_unique<AstAttribute>(std::string("@level_number"), AstTypeIdentifier("number")));

        for (auto clause : relation->getClauses()) {
            size_t clauseNum = getClauseNum(program, clause);

            // mapper to add two provenance columns to atoms
            struct M : public AstNodeMapper {
                using AstNodeMapper::operator();

                std::unique_ptr<AstNode> operator()(std::unique_ptr<AstNode> node) const override {
                    // add provenance columns
                    if (auto atom = dynamic_cast<AstAtom*>(node.get())) {
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    } else if (auto neg = dynamic_cast<AstNegation*>(node.get())) {
                        auto atom = neg->getAtom();
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                    }

                    // otherwise - apply mapper recursively
                    node->apply(*this);
                    return node;
                }
            };

            // add unnamed vars to each atom nested in arguments of head
            clause->getHead()->apply(M());

            // if fact, level number is 0
            if (isFact(*clause)) {
                clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(0));
                clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(0));
            } else {
                std::vector<AstArgument*> bodyLevels;

                for (size_t i = 0; i < clause->getBodyLiterals().size(); i++) {
                    auto lit = clause->getBodyLiterals()[i];

                    // add unnamed vars to each atom nested in arguments of lit
                    lit->apply(M());

                    // add two provenance columns to lit; first is rule num, second is level num
                    if (auto atom = dynamic_cast<AstAtom*>(lit)) {
                        atom->addArgument(std::make_unique<AstUnnamedVariable>());
                        atom->addArgument(std::make_unique<AstVariable>("@level_num_" + std::to_string(i)));
                        bodyLevels.push_back(new AstVariable("@level_num_" + std::to_string(i)));
                    }
                }

                // add two provenance columns to head lit
                clause->getHead()->addArgument(std::make_unique<AstNumberConstant>(clauseNum));
                clause->getHead()->addArgument(std::unique_ptr<AstArgument>(getNextLevelNumber(bodyLevels)));
            }
        }
    }
    return true;
}

bool ProvenanceTransformer::transform(AstTranslationUnit& translationUnit) {
    if (Global::config().get("provenance") == "subtreeHeights") {
        return ProvenanceTransformer::transformSubtreeHeights(translationUnit);
    } else {
        return ProvenanceTransformer::transformMaxHeight(translationUnit);
    }
}

}  // end of namespace souffle
