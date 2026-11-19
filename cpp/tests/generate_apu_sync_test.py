"""Use production synchronization methods and mailbox blocks in the test bus."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
source = Path(args.source).read_text()

def block(start):
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

parts = ['// Extracted from production snes_bus.cpp.']
for name in ['begin_audio_batch', 'end_audio_batch', 'cancel_audio_batch', 'synchronize_audio', 'advance_audio_time']:
    parts.append(block(source.index('void SnesBus::' + name + '(')))
condition = '    if (address >= 0x2140 && address <= 0x217f)'
read = source.index(condition, source.index('uint8_t SnesBus::read_io_register('))
write = source.index(condition, source.index('void SnesBus::write_io_register('))
parts.append('std::uint8_t SnesBus::read_port(std::uint16_t address) {\n' + block(read) + '\nreturn 0;\n}')
parts.append('void SnesBus::write_port(std::uint16_t address, std::uint8_t value) {\n' + block(write) + '\n}')
Path(args.output).write_text('\n\n'.join(parts) + '\n')
