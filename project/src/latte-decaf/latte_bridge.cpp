// C ABI bridge for Decaf's GPL-3.0 Latte ISA disassembler.
#include "latte_bridge.h"
#include "decaf/latte/latte_disassembler.h"
#include "assembler/src/shader_assembler.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <gsl/gsl-lite.hpp>
#include <limits>

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

static void write32le ( unsigned char *dst, uint32_t value )
{
    dst[0] = static_cast<unsigned char>(value);
    dst[1] = static_cast<unsigned char>(value >> 8);
    dst[2] = static_cast<unsigned char>(value >> 16);
    dst[3] = static_cast<unsigned char>(value >> 24);
}

extern "C" unsigned char * szs_latte_assemble
    ( const char *text, size_t size, size_t *output_size )
{
    if (output_size) *output_size = 0;
    if (!text || !size || !output_size) return nullptr;
    try
    {
        Shader shader;
        shader.path = "<latte>";
        if (!assembleShaderCode(shader,std::string_view(text,size))) return nullptr;

        size_t total = shader.cfInsts.size() * 8;
        const auto include_words = [&total](uint32_t base,
                                            const std::vector<uint32_t> &words) {
            if (words.empty()) return true;
            const size_t offset = static_cast<size_t>(base) * 8;
            if (words.size() > (std::numeric_limits<size_t>::max()-offset)/4)
                return false;
            const size_t end = offset + words.size()*4;
            if (end > total) total = end;
            return true;
        };
        if (!include_words(shader.aluClauseBaseAddress,shader.aluClauseData)
            || !include_words(shader.texClauseBaseAddress,shader.texClauseData)
            || !total)
            return nullptr;

        auto *out = static_cast<unsigned char*>(std::calloc(1,total));
        if (!out) return nullptr;
        for (size_t i=0; i<shader.cfInsts.size(); i++)
        {
            write32le(out+i*8,shader.cfInsts[i].word0.value);
            write32le(out+i*8+4,shader.cfInsts[i].word1.value);
        }
        const auto write_words = [out](uint32_t base,
                                       const std::vector<uint32_t> &words) {
            auto *dst = out + static_cast<size_t>(base)*8;
            for (const auto value : words) { write32le(dst,value); dst += 4; }
        };
        write_words(shader.aluClauseBaseAddress,shader.aluClauseData);
        write_words(shader.texClauseBaseAddress,shader.texClauseData);
        *output_size = total;
        return out;
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}

extern "C" void szs_latte_free ( void *data )
{
    std::free(data);
}
