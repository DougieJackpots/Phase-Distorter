// Generated from verified SPC source sites. Do not edit.
#include "eb/spc700_audio_cpu.hpp"
#include "generated_audio_program.hpp"
#include <array>
namespace eb {
namespace {
struct EncodedSite { std::uint16_t operand; std::uint8_t opcode, length; };
static_assert(sizeof(EncodedSite) == 4);
constexpr unsigned first_site = 0x0500, last_site = 0x2FDC;
constexpr auto sites = [] {
    std::array<EncodedSite, last_site - first_site + 1> result{};
#define EB_SPC_SITE(address, opcode, operand, length) result[address - first_site] = {operand, opcode, length};
#include "compact_spc_sites.inc"
#undef EB_SPC_SITE
    return result;
}();
} // namespace
bool execute_translated_audio_instruction(Spc700AudioCpu& cpu) {
    const unsigned pc = cpu.program_counter;
    if (pc < first_site || pc > last_site) return false;
    const auto site = sites[pc - first_site];
    if (!site.length) return false;
    if (cpu.read_byte(std::uint16_t(pc)) != site.opcode) return false;
    if (site.length > 1 && cpu.read_byte(std::uint16_t(pc + 1)) != (site.operand & 255)) return false;
    if (site.length > 2 && cpu.read_byte(std::uint16_t(pc + 2)) != (site.operand >> 8)) return false;
    cpu.execute_opcode_semantics(site.opcode, site.operand, site.length);
    return true;
}
} // namespace eb
