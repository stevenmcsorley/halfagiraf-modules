#!/usr/bin/env python3
"""Prepare test-only neutral symbols from a hash-verified original host.

Original checkout is read-only. Only comments, include paths and identifiers are
normalized in the generated comparison copy; processing expressions are retained.
Nothing from this build directory is installed in a module package.
"""
from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parent.parent
source=root.parent/'upstream-halfagiraf-vcv/src'
manifest=json.loads((root/'docs/sonavyre/REFERENCE-SOURCES.json').read_text())
by_hash={hashlib.sha256(p.read_bytes()).hexdigest():p for p in source.iterdir() if p.is_file()}
inputs={role:by_hash[digest] for role,digest in manifest['files'].items()}
text={role:p.read_text() for role,p in inputs.items()}
renames={p.name:role for role,p in inputs.items()}
name=re.search(r'struct (\w+) : Module',text['Host.cpp']).group(1)
renames[name]='ReferenceHost'
for pattern,new in [(r'static const int (\w+)','SONAVYRE_COMPLETE_SIZE'),(r'static const uint32_t (\w+)','SONAVYRE_COMPLETE_BANK'),(r'static const char\* (\w+)\[200\] =','SONAVYRE_COMPLETE_NAMES'),(r'static const char\* (\w+)\[200\]\[3\]','SONAVYRE_COMPLETE_LABELS')]:
 renames[re.search(pattern,text['Bank.hpp']).group(1)]=new
for match in re.finditer(r'\b(\w+)(Crc32|DecodeImage|ReadWav48k)\b',text['Decoder.hpp']):
 renames[match.group(0)]='reference'+match.group(2)
# GUI is irrelevant to the processing baseline; it is never compiled or installed.
text['Host.cpp']=text['Host.cpp'].split('// ─────────────────────────── OLED-style display')[0]
text['plugin.hpp']='\n'.join(line for line in text['plugin.hpp'].splitlines() if not line.startswith('extern Model*'))+'\n'
dest=root/'build/sonavyre-reference';dest.mkdir(parents=True,exist_ok=True)
for role,s in text.items():
 s='\n'.join(line for line in s.splitlines() if not line.lstrip().startswith('//') or re.search(r'copyright|licen[cs]e',line,re.I))+'\n'
 for old,new in sorted(renames.items(),key=lambda item:-len(item[0])):
  s=re.sub(r'(?<!\w)'+re.escape(old)+r'(?!\w)',lambda _:new,s)
 (dest/role).write_text(s)
print('Prepared hash-verified original processing reference with neutral source symbols.')
