/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2020, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file SerialisationStream.h
 *
 * Defines a common base class for relation serialisation streams.
 *
 ***********************************************************************/

#pragma once

#include "RamTypes.h"
#include "RecordTable.h"
#include "SymbolTable.h"
#include "json11.h"

#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace souffle {

using json11::Json;

template <bool readOnlyTables>
class SerialisationStream {
public:
    virtual ~SerialisationStream() = default;

protected:
    template <typename A>
    using RO = std::conditional_t<readOnlyTables, const A, A>;

    SerialisationStream(RO<SymbolTable>& symTab, RO<RecordTable>& recTab, Json types,
            std::vector<std::string> relTypes, size_t auxArity = 0)
            : symbolTable(symTab), recordTable(recTab), types(std::move(types)),
              typeAttributes(std::move(relTypes)), arity(typeAttributes.size() - auxArity),
              auxiliaryArity(auxArity) {}

    SerialisationStream(
            RO<SymbolTable>& symTab, RO<RecordTable>& recTab, Json types, char const* const relationName)
            : symbolTable(symTab), recordTable(recTab), types(std::move(types)) {
        setupFromJson(relationName);
    }

    SerialisationStream(RO<SymbolTable>& symTab, RO<RecordTable>& recTab,
            const std::map<std::string, std::string>& rwOperation)
            : symbolTable(symTab), recordTable(recTab) {
        auto&& relationName = rwOperation.at("name");

        std::string parseErrors;
        types = Json::parse(rwOperation.at("types"), parseErrors);
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed.");

        setupFromJson(relationName.c_str());
    }

    RO<SymbolTable>& symbolTable;
    RO<RecordTable>& recordTable;
    Json types;
    std::vector<std::string> typeAttributes;

    size_t arity = 0;
    size_t auxiliaryArity = 0;

private:
    void setupFromJson(char const* const relationName) {
        auto&& relInfo = types[relationName];
        arity = static_cast<size_t>(relInfo["arity"].long_value());
        auxiliaryArity = static_cast<size_t>(relInfo["auxArity"].long_value());

        assert(relInfo["types"].is_array());
        auto&& relTypes = relInfo["types"].array_items();
        assert(relTypes.size() == (arity + auxiliaryArity));

        for (size_t i = 0; i < arity + auxiliaryArity; ++i) {
            auto&& type = relTypes[i].string_value();
            assert(!type.empty() && "malformed types tag");
            typeAttributes.push_back(type);
        }
    }
};

}  // namespace souffle
