"""Extract the actual production bus dispatcher for the isolated contract test."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);args=p.parse_args()
s=Path(args.source).read_text();start=s.index('bool SnesBus::try_execute_native_sprite_operation(')
brace=s.index('{',start);depth=1;end=brace+1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
Path(args.output).write_text('// Extracted from production snes_bus.cpp; do not edit.\n'+s[start:end]+'\n')
