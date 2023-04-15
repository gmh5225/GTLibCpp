#pragma once
#ifndef CE_PARSER_H
#define CE_PARSER_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <memory>
#include <cstdint>
#include <variant>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <ranges>
using namespace std;

namespace GTLIBC
{
    class CheatEntry
    {
    public:
        string Description;
        int Id;
        string VariableType;
        DWORD Address;
        vector<DWORD> Offsets;
        vector<vector<DWORD>> Hotkeys;
        vector<shared_ptr<CheatEntry>> NestedEntries;

        CheatEntry(const string &description, int id, const string &variableType, DWORD address,
                   const vector<DWORD> &offsets, const vector<vector<DWORD>> &hotkeys)
            : Description(description), Id(id), VariableType(variableType), Address(address),
              Offsets(offsets), Hotkeys(hotkeys) {}
    };

    class CheatEntries
    {
    public:
        // Create default constructor and parameterized constructor with setting game base address.
        CheatEntries() = default;
        CheatEntries(DWORD gameBaseAddress) : gameBaseAddress(gameBaseAddress) {}

        vector<shared_ptr<CheatEntry>> entries;

        void addEntry(shared_ptr<CheatEntry> entry)
        {
            entries.push_back(entry);
        }

        void SetGameBaseAddress(DWORD gameBaseAddress)
        {
            this->gameBaseAddress = gameBaseAddress;
        }

        // Add ParseCheatTable declaration.
        CheatEntries ParseCheatTable(const string &cheatTablePath);

    private:
        // Create variable and method to get base address of the game.
        DWORD gameBaseAddress = 0x00400000;
        DWORD getGameBaseAddress()
        {
            return gameBaseAddress;
        }

        DWORD ParseAddress(const string &address);
        vector<DWORD> ParseOffsets(const string &offsets);
        vector<vector<DWORD>> ParseHotkeys(const string &hotkeys);
        void ParseNestedCheatEntries(const string &parentNode, shared_ptr<CheatEntry> &parentEntry);
    };

    DWORD CheatEntries::ParseAddress(const string &address)
    {
        smatch matches;
        regex_search(address, matches, regex("(\"([^\"]+)\")?([^+]+)?\\s*(\\+)?\\s*(0x)?([0-9A-Fa-f]+)?"));
        string moduleName = matches[2].str().empty() ? matches[3].str() : matches[2].str();
        string offsetStr = matches[6].str();

        bool isModuleNameHex = std::all_of(moduleName.begin(), moduleName.end(), [](char c)
                                           { return std::isxdigit(c); });

        if (isModuleNameHex)
        {
            DWORD absoluteAddress = stoul(moduleName, nullptr, 16);
            return absoluteAddress;
        }

        DWORD offset = offsetStr.empty() ? 0 : stoul(offsetStr, nullptr, 16);
        DWORD baseAddress = moduleName.empty() ? 0 : gameBaseAddress;
        return baseAddress + offset;
    }

    vector<DWORD> CheatEntries::ParseOffsets(const string &offsets)
    {
        vector<DWORD> result;
        smatch matches;
        regex offsetRegex("<Offset>([0-9a-fA-F]+)</Offset>");

        auto offsetsBegin = sregex_iterator(offsets.begin(), offsets.end(), offsetRegex);
        auto offsetsEnd = sregex_iterator();

        for (auto i = offsetsBegin; i != offsetsEnd; ++i)
        {
            result.push_back(stoul((*i)[1].str(), nullptr, 16));
        }

        return result;
    }

    vector<vector<DWORD>> CheatEntries::ParseHotkeys(const string &hotkeys)
    {
        vector<vector<DWORD>> result;
        smatch matches;
        regex hotkeyRegex("<Hotkey>([\\s\\S]*?)<Keys>([\\s\\S]*?)</Keys>([\\s\\S]*?)</Hotkey>");

        auto hotkeysBegin = sregex_iterator(hotkeys.begin(), hotkeys.end(), hotkeyRegex);
        auto hotkeysEnd = sregex_iterator();

        for (auto i = hotkeysBegin; i != hotkeysEnd; ++i)
        {
            vector<DWORD> keys;
            string keysStr = (*i)[2].str();
            // trim the keysStr.
            keysStr = keysStr.substr(1, keysStr.size() - 2);

            smatch keyMatches;
            regex keyRegex("<Key>([0-9]+)</Key>");

            auto keysBegin = sregex_iterator(keysStr.begin(), keysStr.end(), keyRegex);
            auto keysEnd = sregex_iterator();

            for (auto j = keysBegin; j != keysEnd; ++j)
            {
                keys.push_back(stoul((*j)[1].str()));
            }
            result.push_back(keys);
        }
        return result;
    }

    void CheatEntries::ParseNestedCheatEntries(const string &parentNode, shared_ptr<CheatEntry> &parentEntry)
    {
        smatch entryMatches;
        regex entryRegex("<CheatEntry>.*?</CheatEntry>");
        auto entriesBegin = sregex_iterator(parentNode.begin(), parentNode.end(), entryRegex);
        auto entriesEnd = sregex_iterator();

        for (auto i = entriesBegin; i != entriesEnd; ++i)
        {
            string entryStr = (*i).str();
            smatch matches;

            regex_search(entryStr, matches, regex("<Description>(.*?)</Description>"));
            string description = matches[1].str();

            regex_search(entryStr, matches, regex("<ID>(\\d+)</ID>"));
            int id = stoi(matches[1].str());

            regex_search(entryStr, matches, regex("<VariableType>(.*?)</VariableType>"));
            string variableType = matches[1].str();

            regex_search(entryStr, matches, regex("<Address>(.*?)</Address>"));
            DWORD address = variableType == "Auto Assembler Script" ? 0 : ParseAddress(matches[1].str());

            vector<DWORD> offsets = ParseOffsets(entryStr);
            vector<vector<DWORD>> hotkeys = ParseHotkeys(entryStr);

            shared_ptr<CheatEntry> entry = make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);
            parentEntry->NestedEntries.push_back(entry);

            ParseNestedCheatEntries(entryStr, entry);
        }
    }

    CheatEntries CheatEntries::ParseCheatTable(const string &cheatTablePath)
    {
        CheatEntries cheatTable;
        smatch entryMatches;
        regex entryRegex("<CheatEntry>([\\s\\S]*?)</CheatEntry>");
        auto entriesBegin = sregex_iterator(cheatTablePath.begin(), cheatTablePath.end(), entryRegex);
        auto entriesEnd = sregex_iterator();

        for (auto i = entriesBegin; i != entriesEnd; ++i)
        {
            string entryStr = (*i).str();
            smatch matches;

            regex_search(entryStr, matches, regex("<Description>(.*?)</Description>"));
            string description = matches[1].str();

            regex_search(entryStr, matches, regex("<ID>(\\d+)</ID>"));
            int id = stoi(matches[1].str());

            regex_search(entryStr, matches, regex("<VariableType>(.*?)</VariableType>"));
            string variableType = matches[1].str();

            regex_search(entryStr, matches, regex("<Address>(.*?)</Address>"));
            DWORD address = variableType == "Auto Assembler Script" ? 0 : ParseAddress(matches[1].str());

            vector<DWORD> offsets = ParseOffsets(entryStr);
            vector<vector<DWORD>> hotkeys = ParseHotkeys(entryStr);
            shared_ptr<CheatEntry> entry = make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);

            cheatTable.addEntry(entry);

            ParseNestedCheatEntries(entryStr, entry);
        }

        return cheatTable;
    }
} // namespace GTLibc

#endif