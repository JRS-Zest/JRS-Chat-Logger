#pragma once
#include <cstdint>
#include <vector>

// C# PacketCrypt.cs の完全移植
// item.dat 復号用

class PacketCrypt {
public:
    static int GenerateScenarioDecodeKey(int seed);
    static std::vector<uint8_t> DecodeScenarioBuffer(const uint8_t* data, size_t len, int decodeKey);

private:
    static const uint8_t AucDataTable[448];
    static const uint8_t XorKeys[213];
};
