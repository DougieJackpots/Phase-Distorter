#!/usr/bin/env python3
"""Extract the production clock kernel for the dependency-free differential."""
from pathlib import Path
import argparse
import re

def function(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    # Ignore braces inside comments/string literals while retaining original text.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for match in tokens.finditer(source, brace):
        token = match.group()
        if token == '{': depth += 1
        elif token == '}':
            depth -= 1
            if not depth: return source[begin:match.end()]
    raise ValueError('Unbalanced function: ' + signature)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fetch-output', type=Path)
    args = parser.parse_args()
    source = args.source.read_text()
    fast = function(source, 'void SnesBus::advance_clocks(unsigned clocks, bool pause_for_refresh)')
    event = function(source, 'unsigned SnesBus::next_hardware_event_clocks() const')
    result = event.replace('SnesBus::', 'ClockModel::') + '\n' + fast.replace('SnesBus::advance_clocks', 'ClockModel::fast_advance') + '\n'
    args.output.write_text(result)
    if args.fetch_output:
        fetch = function(source, 'bool SnesBus::try_fetch_rom_instruction(')
        args.fetch_output.write_text(fetch.replace('SnesBus::', 'FetchModel::') + '\n')
