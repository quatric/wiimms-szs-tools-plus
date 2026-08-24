// C ABI bridge for Decaf's GPL-3.0 Latte ISA disassembler.
#include "latte_bridge.h"
#include "decaf/latte/latte_disassembler.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <gsl/gsl-lite.hpp>

extern "C" char * szs_latte_disassemble
    ( const unsigned char *data, size_t size )
{
    if (!data || !size || size % 8)
        return nullptr;
    try
    {
        const auto span = gsl::span<const uint8_t>(data,size);
        const std::string result = latte::disassemble(span,false);
        char *out = static_cast<char*>(std::malloc(result.size()+1));
        if (!out) return nullptr;
        std::memcpy(out,result.c_str(),result.size()+1);
        return out;
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}
