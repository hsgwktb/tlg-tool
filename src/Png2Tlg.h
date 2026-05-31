#ifndef PNG2TLG_H_
#define PNG2TLG_H_

#include <string>
#include <cstdint>

class Png2TlgConverter
{
public:
    static void convert(const std::string& inputPath, const std::string& outputPath);
};

#endif