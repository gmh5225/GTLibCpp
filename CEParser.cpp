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
#include "GTLibc.cpp"

static DWORD GAME_BASE_ADDRESS = 0x00400000;
GTLibc gtlibc(true);

using namespace std;

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
    vector<shared_ptr<CheatEntry>> entries;

    void addEntry(shared_ptr<CheatEntry> entry)
    {
        entries.push_back(entry);
    }
};

DWORD getBaseAddress(const string &moduleName)
{
    return gtlibc.GetGameBaseAddress();
}

DWORD parseAddress(const string &address)
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
    DWORD baseAddress = moduleName.empty() ? 0 : getBaseAddress(moduleName);
    return baseAddress + offset;
}

vector<DWORD> parseOffsets(const string &offsets)
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

vector<vector<DWORD>> parseHotkeys(const string &hotkeys)
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

void parseNestedCheatEntries(const string &parentNode, shared_ptr<CheatEntry> &parentEntry)
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
        DWORD address = variableType == "Auto Assembler Script" ? 0 : parseAddress(matches[1].str());

        vector<DWORD> offsets = parseOffsets(entryStr);
        vector<vector<DWORD>> hotkeys = parseHotkeys(entryStr);

        shared_ptr<CheatEntry> entry = make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);
        parentEntry->NestedEntries.push_back(entry);

        parseNestedCheatEntries(entryStr, entry);
    }
}

CheatEntries parseCheatEntries(const string &xmlData)
{
    CheatEntries result;
    smatch entryMatches;
    regex entryRegex("<CheatEntry>([\\s\\S]*?)</CheatEntry>");
    auto entriesBegin = sregex_iterator(xmlData.begin(), xmlData.end(), entryRegex);
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
        DWORD address = variableType == "Auto Assembler Script" ? 0 : parseAddress(matches[1].str());

        vector<DWORD> offsets = parseOffsets(entryStr);
        vector<vector<DWORD>> hotkeys = parseHotkeys(entryStr);
        shared_ptr<CheatEntry> entry = make_shared<CheatEntry>(description, id, variableType, address, offsets, hotkeys);

        result.addEntry(entry);

        parseNestedCheatEntries(entryStr, entry);
    }

    return result;
}

using DataType = std::variant<BYTE, int16_t, int32_t, int64_t, float, double, std::string>;

template <typename T>
T ReadPointerOffset(DWORD address, const DWORD offset)
{
    T result = gtlibc.ReadPointerOffset<T>(address, offset);
    return result;
}

template <typename T>
T ReadPointerOffsets(DWORD address, const std::vector<DWORD> &offsetsList)
{
    T result = gtlibc.ReadPointerOffsets<T>(address, offsetsList);
    return result;
}

template <typename T>
T ReadAddress(DWORD address)
{
    T result = gtlibc.ReadAddress<T>(address);
    return result;
}

DWORD ReadPointerOffsetsUntilLast(DWORD address, const std::vector<DWORD> &offsetsList)
{
    DWORD staticAddress = address - GAME_BASE_ADDRESS;
    DWORD result = gtlibc.ReadPointerOffset<DWORD>(GAME_BASE_ADDRESS, staticAddress);

    if (offsetsList.size() > 1)
    {
        for (size_t i = 0; i < offsetsList.size() - 1; ++i)
        {
            result = gtlibc.ReadPointerOffset<DWORD>(result, offsetsList[i]);
        }
    }

    // Add the last offset to the result
    result += offsetsList.back();
    return result;
}

DataType ReadAddressGeneric(const std::string &dataType, DWORD address, const std::vector<DWORD> &offsetsList = {})
{
    static const std::unordered_map<std::string, std::function<DataType(DWORD, const std::vector<DWORD> &)>> typeMap =
        {
            {"Byte", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<BYTE>(addr) : ReadAddress<BYTE>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"2 Bytes", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<int16_t>(addr) : ReadAddress<int16_t>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"4 Bytes", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<int32_t>(addr) : ReadAddress<int32_t>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"8 Bytes", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<int64_t>(addr) : ReadAddress<int64_t>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"Float", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<float>(addr) : ReadAddress<float>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"Double", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? ReadAddress<double>(addr) : ReadAddress<double>(ReadPointerOffsetsUntilLast(addr, offs)); }},
            {"String", [](DWORD addr, const std::vector<DWORD> &offs)
             { return offs.empty() ? std::string(gtlibc.ReadString(addr, 0xFF)) : std::string(gtlibc.ReadString(ReadPointerOffsetsUntilLast(addr, offs), 0xFF)); }},
        };

    const auto it = typeMap.find(dataType);
    if (it == typeMap.end())
    {
        throw std::runtime_error("Invalid data type specified");
    }
    return it->second(address, offsetsList);
}

void PrintValue(const DataType &value)
{
    std::visit([](const auto &item)
               { std::cout << "Value: " << item << std::endl; },
               value);
}

std::string ReadCheatTable(const std::string &filename)
{
    std::ifstream ifs(filename);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        (std::istreambuf_iterator<char>()));
    return content;
}

void PrintGenericTable(CheatEntries &cheatEntries)
{
    for (auto &entry : cheatEntries.entries)
    {
        std::cout << "Description: " << entry->Description << std::endl;
        std::cout << "ID: " << entry->Id << std::endl;
        std::cout << "VariableType: " << entry->VariableType << std::endl;
        std::cout << "Address: " << entry->Address << std::endl;
        std::cout << "Offsets: ";
        for (auto &offset : entry->Offsets)
        {
            std::cout << offset << " ";
        }
        std::cout << std::endl;
        std::cout << "Hotkeys: ";
        for (auto &hotkey : entry->Hotkeys)
        {
            std::cout << "[";
            for (auto &key : hotkey)
            {
                std::cout << key << " ";
            }
            std::cout << "] ";
        }
        std::cout << std::endl;
        std::cout << std::endl;
    }
}

// Check if cheat table is valid XML check for tags.
bool IsValidCheatTable(const std::string &xmlData)
{
    std::string_view xmlDataView = xmlData;
    if (xmlDataView.find("<CheatEntries>") == std::string::npos)
    {
        return false;
    }
    if (xmlDataView.find("</CheatEntries>") == std::string::npos)
    {
        return false;
    }
    return true;
}

void ReadGenericTable(CheatEntries &cheatEntries)
{
    for (auto &entry : cheatEntries.entries)
    {
        const DWORD address = entry->Address;
        const vector<DWORD> offsets = entry->Offsets;

        vector<DWORD> offsetsSorted = offsets;
        std::reverse(offsetsSorted.begin(), offsetsSorted.end());

        if (offsets.size() >= 1)
        {
            std::cout << "Description: " << entry->Description;
            std::cout << " Address: " << to_hex_str(address);
            std::cout << " Offsets: ";
            for (auto &offset : offsetsSorted)
            {
                std::cout << to_hex_str(offset) << ",";
            }

            auto result = ReadAddressGeneric(entry->VariableType, address, offsetsSorted);
            PrintValue(result);
        }

        if (offsets.size() == 0 && address != 0)
        {
            std::cout << "Description: " << entry->Description << " ";
            DataType result = ReadAddressGeneric(entry->VariableType, address);
            PrintValue(result);
        }
    }
}

int main()
{
    // Selecting the cheat table file.
    //string cheatTableFile = "assault_cube.ct";
    string cheatTableFile = "igi.ct";

    // Read the cheat table file
    string xmlData = ReadCheatTable(cheatTableFile);

    bool isValid = IsValidCheatTable(xmlData);
    if (!isValid)
    {
        std::cout << "Invalid XML file"
                  << " '" << cheatTableFile << "'" << std::endl;
        return EXIT_FAILURE;
    }

    // Finding the game process
    std::string gameName = "igi";
    gtlibc.FindGameProcess(gameName);
    GAME_BASE_ADDRESS = gtlibc.GetGameBaseAddress();

    // Parse the XML data and populate the cheat entries
    CheatEntries cheatEntries = parseCheatEntries(xmlData);

    // Print the cheat entries with read values
    ReadGenericTable(cheatEntries);

    // Print count the cheat entries
    std::cout << "Count: " << cheatEntries.entries.size() << std::endl;

    return 0;
}
