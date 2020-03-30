/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file AstArgument.h
 *
 * Define the classes Argument, Variable, and Constant to represent
 * variables and constants in literals. Variable and Constant are
 * sub-classes of class argument.
 *
 ***********************************************************************/

#pragma once

#include "AggregateOp.h"
#include "AstAbstract.h"
#include "AstNode.h"
#include "AstType.h"
#include "FunctorOps.h"
#include "SymbolTable.h"
#include "Util.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace souffle {

/**
 * Named Variable
 */
class AstVariable : public AstArgument {
public:
    AstVariable(std::string name) : name(std::move(name)) {}

    /** set variable name */
    void setName(const std::string& name) {
        this->name = name;
    }

    /** @return variable name */
    const std::string& getName() const {
        return name;
    }

    AstVariable* clone() const override {
        auto* res = new AstVariable(name);
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << name;
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstVariable&>(node);
        return name == other.name;
    }

    /** variable name */
    std::string name;
};

/**
 * Unnamed Variable
 */
class AstUnnamedVariable : public AstArgument {
public:
    AstUnnamedVariable* clone() const override {
        auto* res = new AstUnnamedVariable();
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << "_";
    }
};

/**
 * Counter
 */
class AstCounter : public AstArgument {
public:
    AstCounter* clone() const override {
        auto* res = new AstCounter();
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << "$";
    }
};

/**
 * Abstract Constant
 */
class AstConstant : public AstArgument {
public:
    AstConstant* clone() const override = 0;

    /** @return String representation of Constant */
    const std::string& getConstant() const {
        return constant;
    }

protected:
    void print(std::ostream& os) const override {
        os << getConstant();
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstConstant&>(node);
        return constant == other.constant;
    }

    AstConstant(std::string value) : constant(std::move(value)){};

private:
    const std::string constant;
};

/**
 * String Constant
 */
class AstStringConstant : public AstConstant {
public:
    explicit AstStringConstant(std::string value) : AstConstant(std::move(value)) {}

    AstStringConstant* clone() const override {
        auto* res = new AstStringConstant(getConstant());
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << "\"" << getConstant() << "\"";
    }
};

/**
 * Numeric Constant
 *
 * The constant can be initialized with type.
 * If this is the case, the typesystem will be forced to use it.
 * Otherwise the type is inferred from context.
 */
class AstNumericConstant : public AstConstant {
public:
    enum class Type { Int, Uint, Float };

    AstNumericConstant(RamSigned value) : AstConstant(std::to_string(value)), type(Type::Int) {}

    AstNumericConstant(std::string constant, std::optional<Type> type = std::nullopt)
            : AstConstant(std::move(constant)), type(type) {}

    AstNumericConstant* clone() const override {
        auto* copy = new AstNumericConstant(getConstant(), getType());
        copy->setSrcLoc(getSrcLoc());
        return copy;
    }

    const std::optional<Type>& getType() const {
        return type;
    }

    void setType(Type newType) {
        type = newType;
    }

protected:
    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstNumericConstant&>(node);
        return AstConstant::equal(node) && type == other.type;
    }

private:
    std::optional<Type> type;
};

/**
 * Nil Constant
 */
class AstNilConstant : public AstConstant {
public:
    AstNilConstant() : AstConstant("nil"){};

    AstNilConstant* clone() const override {
        auto* res = new AstNilConstant();
        res->setSrcLoc(getSrcLoc());
        return res;
    }
};

/**
 * Abstract Term
 */
class AstTerm : public AstArgument {
protected:
    AstTerm() = default;
    AstTerm(std::vector<std::unique_ptr<AstArgument>> operands) : args(std::move(operands)){};

public:
    /** get arguments */
    std::vector<AstArgument*> getArguments() const {
        return toPtrVector(args);
    }

    /** add argument to argument list */
    void addArgument(std::unique_ptr<AstArgument> arg) {
        args.push_back(std::move(arg));
    }

    std::vector<const AstNode*> getChildNodes() const override {
        auto res = AstArgument::getChildNodes();
        for (auto& cur : args) {
            res.push_back(cur.get());
        }
        return res;
    }

    void apply(const AstNodeMapper& map) override {
        for (auto& arg : args) {
            arg = map(std::move(arg));
        }
    }

protected:
    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstTerm&>(node);
        return equal_targets(args, other.args);
    }

    /** Arguments */
    std::vector<std::unique_ptr<AstArgument>> args;
};

/**
 * Functor class
 */

class AstFunctor : public AstTerm {
public:
    virtual TypeAttribute getReturnType() const = 0;
    virtual TypeAttribute getArgType(const size_t arg) const = 0;

protected:
    AstFunctor() = default;
    explicit AstFunctor(std::vector<std::unique_ptr<AstArgument>> operands) : AstTerm(std::move(operands)) {}
};

/**
 * Intrinsic Functor
 */
class AstIntrinsicFunctor : public AstFunctor {
public:
    template <typename... Operands>
    AstIntrinsicFunctor(FunctorOp function, Operands... operands) : function(function) {
        std::unique_ptr<AstArgument> tmp[] = {std::move(operands)...};
        for (auto& cur : tmp) {
            addArgument(std::move(cur));
        }
        assert(isValidFunctorOpArity(function, args.size()) && "invalid number of arguments for functor");
    }

    AstIntrinsicFunctor(FunctorOp function, std::vector<std::unique_ptr<AstArgument>> operands)
            : AstFunctor(std::move(operands)), function(function) {
        assert(isValidFunctorOpArity(function, args.size()) && "invalid number of arguments for functor");
    }

    /** get function */
    FunctorOp getFunction() const {
        return function;
    }

    /** set function */
    void setFunction(const FunctorOp functor) {
        function = functor;
    }

    /** get the return type of the functor. */
    TypeAttribute getReturnType() const override {
        return functorReturnType(function);
    }

    /** get type of the functor argument*/
    TypeAttribute getArgType(const size_t arg) const override {
        return functorOpArgType(arg, function);
    }

    AstIntrinsicFunctor* clone() const override {
        std::vector<std::unique_ptr<AstArgument>> argsCopy;
        for (auto& arg : args) {
            argsCopy.emplace_back(arg->clone());
        }
        auto res = new AstIntrinsicFunctor(function, std::move(argsCopy));
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        if (isInfixFunctorOp(function)) {
            os << "(";
            os << join(args, getSymbolForFunctorOp(function), print_deref<std::unique_ptr<AstArgument>>());
            os << ")";
        } else {
            os << getSymbolForFunctorOp(function);
            os << "(";
            os << join(args, ",", print_deref<std::unique_ptr<AstArgument>>());
            os << ")";
        }
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstIntrinsicFunctor&>(node);
        return function == other.function && AstFunctor::equal(node);
    }

    /** Function */
    FunctorOp function;
};

/**
 * User-Defined Functor
 */
class AstUserDefinedFunctor : public AstFunctor {
public:
    explicit AstUserDefinedFunctor(std::string name) : AstFunctor(), name(std::move(name)){};
    AstUserDefinedFunctor(std::string name, std::vector<std::unique_ptr<AstArgument>> args)
            : AstFunctor(std::move(args)), name(std::move(name)){};

    /** get name */
    const std::string& getName() const {
        return name;
    }

    /** get type of the functor argument*/
    TypeAttribute getArgType(const size_t arg) const override {
        return argTypes->at(arg);
    }

    /** get type of the functor argument*/
    TypeAttribute getReturnType() const override {
        return returnType.value();
    }

    void setTypes(std::vector<TypeAttribute> argumentsTypes, TypeAttribute retType) {
        assert(argumentsTypes.size() == args.size() && "Size of types must match size of arguments");
        argTypes = argumentsTypes;
        returnType = retType;
    }

    const std::vector<TypeAttribute>& getArgsTypes() const {
        return argTypes.value();
    }

    AstUserDefinedFunctor* clone() const override {
        auto res = new AstUserDefinedFunctor(name);
        // Set args
        for (auto& arg : args) {
            res->args.emplace_back(arg->clone());
        }
        // Only copy types if they have already been set.
        if (returnType.has_value()) {
            res->setTypes(argTypes.value(), returnType.value());
        }
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << '@' << name << "(" << join(args, ",", print_deref<std::unique_ptr<AstArgument>>()) << ")";
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstUserDefinedFunctor&>(node);
        return name == other.name && AstFunctor::equal(node);
    }

    std::optional<std::vector<TypeAttribute>> argTypes;
    std::optional<TypeAttribute> returnType;

    /** name of user-defined functor */
    const std::string name;
};

/**
 * Record
 */
class AstRecordInit : public AstTerm {
public:
    AstRecordInit(std::optional<AstQualifiedName> ty) : type(std::move(ty)) {}

    AstRecordInit* clone() const override {
        auto res = new AstRecordInit(type);
        for (auto& cur : args) {
            res->args.emplace_back(cur->clone());
        }
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        if (type) {
            os << "@" << type << " ";
        }

        os << "[" << join(args, ",", print_deref<std::unique_ptr<AstArgument>>()) << "]";
    }

    /** Implements the node comparison for this node type */
    bool equal(const AstNode& node) const override {
        assert(nullptr != dynamic_cast<const AstRecordInit*>(&node));
        const auto& other = static_cast<const AstRecordInit&>(node);
        return equal_targets(args, other.args) && type == other.type;
    }

    /** The type of the record in question, if specified.
     *  If not specified, we'll try to infer.
     */
    std::optional<AstQualifiedName> type;
};

/**
 * An argument that takes a values and converts it into a new sum type branch.
 */
class AstSumInit : public AstTerm {
public:
    AstSumInit(AstQualifiedName ty, std::string branch, std::unique_ptr<AstTerm> arg)
            : type(std::move(ty)), branch(std::move(branch)) {
        addArgument(std::move(arg));
    }

    const AstArgument& getArgument() const {
        return *arguments.begin();
    }

    const std::string& getBranch() const {
        return branch;
    }

    /** Creates a clone of this AST sub-structure */
    AstSumInit* clone() const override {
        auto res = new AstSumInit(type, branch, souffle::clone(arg));
        res->setSrcLoc(getSrcLoc());
        return res;
    }

    /** Mutates this node */
    void apply(const AstNodeMapper& map) override {
        arg = map(std::move(arg));
    }

    /** Obtains a list of all embedded child nodes */
    std::vector<const AstNode*> getChildNodes() const override {
        auto res = AstArgument::getChildNodes();
        res.push_back(arg.get());
        return res;
    }

    /** The type of the record in question */
    AstQualifiedName type;

protected:
    void print(std::ostream& os) const override {
        os << "@" << type << " " << branch << "[" << *arg << "]";
    }

    /** Implements the node comparison for this node type */
    bool equal(const AstNode& node) const override {
        assert(nullptr != dynamic_cast<const AstSumInit*>(&node));
        const auto& other = static_cast<const AstSumInit&>(node);
        return type == other.type && branch == other.branch && *arg == *other.arg;
    }

    /** The sum type branch name */
    std::string branch;

    /** The list of components to be aggregated into a record */
    std::unique_ptr<AstArgument> arg;
};

/**
 * An argument capable of casting a value of one type into another.
 */
class AstTypeCast : public AstArgument {
public:
    AstTypeCast(std::unique_ptr<AstArgument> value, AstQualifiedName type)
            : value(std::move(value)), type(std::move(type)) {}

    /** Get value */
    AstArgument* getValue() const {
        return value.get();
    }

    /** Get type */
    const AstQualifiedName& getType() const {
        return type;
    }

    /** Set type */
    void setType(const AstQualifiedName& type) {
        this->type = type;
    }

    std::vector<const AstNode*> getChildNodes() const override {
        auto res = AstArgument::getChildNodes();
        res.push_back(value.get());
        return res;
    }

    AstTypeCast* clone() const override {
        auto res = new AstTypeCast(std::unique_ptr<AstArgument>(value->clone()), type);
        res->setSrcLoc(getSrcLoc());
        return res;
    }

    void apply(const AstNodeMapper& map) override {
        value = map(std::move(value));
    }

protected:
    void print(std::ostream& os) const override {
        os << "as(" << *value << "," << type << ")";
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstTypeCast&>(node);
        return type == other.type && equal_ptr(value, other.value);
    }

    /** The value to be casted */
    std::unique_ptr<AstArgument> value;

    /** The target type name */
    AstQualifiedName type;
};

/**
 * An argument aggregating a value from a sub-query.
 */
class AstAggregator : public AstArgument {
public:
    AstAggregator(AggregateOp fun, std::unique_ptr<AstArgument> expr = nullptr)
            : fun(fun), targetExpression(std::move(expr)) {}

    /** Get aggregate operator */
    AggregateOp getOperator() const {
        return fun;
    }

    /** Set aggregate operator */
    void setOperator(AggregateOp op) {
        fun = op;
    }

    /** Get target expression */
    const AstArgument* getTargetExpression() const {
        return targetExpression.get();
    }

    /** Get body literals */
    std::vector<AstLiteral*> getBodyLiterals() const {
        return toPtrVector(body);
    }

    void setBody(std::vector<std::unique_ptr<AstLiteral>> bodyLiterals) {
        body = std::move(bodyLiterals);
    }

    std::vector<const AstNode*> getChildNodes() const override {
        auto res = AstArgument::getChildNodes();
        if (targetExpression) {
            res.push_back(targetExpression.get());
        }
        for (auto& cur : body) {
            res.push_back(cur.get());
        }
        return res;
    }

    AstAggregator* clone() const override {
        auto* res = new AstAggregator(fun, std::unique_ptr<AstArgument>(souffle::clone(targetExpression)));
        for (const auto& cur : body) {
            res->body.emplace_back(cur->clone());
        }
        res->setSrcLoc(getSrcLoc());
        return res;
    }

    void apply(const AstNodeMapper& map) override {
        if (targetExpression) {
            targetExpression = map(std::move(targetExpression));
        }
        for (auto& cur : body) {
            cur = map(std::move(cur));
        }
    }

protected:
    void print(std::ostream& os) const override {
        switch (fun) {
            case AggregateOp::SUM:
            case AggregateOp::FSUM:
            case AggregateOp::USUM:
                os << "sum";
                break;
            case AggregateOp::MIN:
            case AggregateOp::FMIN:
            case AggregateOp::UMIN:
                os << "min";
                break;
            case AggregateOp::MAX:
            case AggregateOp::FMAX:
            case AggregateOp::UMAX:
                os << "max";
                break;
            case AggregateOp::COUNT:
                os << "count";
                break;
            case AggregateOp::MEAN:
                os << "mean";
                break;
        }
        if (targetExpression) {
            os << " " << *targetExpression;
        }
        os << " : ";
        if (body.size() > 1) {
            os << "{ ";
        }
        os << join(body, ", ", print_deref<std::unique_ptr<AstLiteral>>());
        if (body.size() > 1) {
            os << " }";
        }
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstAggregator&>(node);
        return fun == other.fun && equal_ptr(targetExpression, other.targetExpression) &&
               equal_targets(body, other.body);
    }

private:
    /** Aggregation operator */
    AggregateOp fun;

    /** Aggregation expression */
    std::unique_ptr<AstArgument> targetExpression;

    /** Body literal of sub-query */
    std::vector<std::unique_ptr<AstLiteral>> body;
};

/**
 * Subroutine Argument
 */
class AstSubroutineArgument : public AstArgument {
public:
    AstSubroutineArgument(size_t index) : index(index) {}

    /** Return argument index */
    size_t getNumber() const {
        return index;
    }

    AstSubroutineArgument* clone() const override {
        auto* res = new AstSubroutineArgument(index);
        res->setSrcLoc(getSrcLoc());
        return res;
    }

protected:
    void print(std::ostream& os) const override {
        os << "arg_" << index;
    }

    bool equal(const AstNode& node) const override {
        const auto& other = static_cast<const AstSubroutineArgument&>(node);
        return index == other.index;
    }

private:
    /** Index of argument in argument list*/
    size_t index;
};

}  // end of namespace souffle
