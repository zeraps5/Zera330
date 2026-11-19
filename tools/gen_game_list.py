"""Gera frontends/launcher/games.inc (set, título, ano, fabricante) a partir do
`mame -listxml`, só com as máquinas do driver neogeo.cpp.

Uso: python tools/gen_game_list.py <listxml.xml>
"""
import os
import sys
import xml.etree.ElementTree as ET


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    rows = []
    for _, m in ET.iterparse(sys.argv[1]):
        if m.tag != 'machine':
            continue
        if (m.get('sourcefile', '').endswith('neogeo.cpp') and m.get('isbios') != 'yes'
                and m.get('isdevice') != 'yes'):
            rows.append((m.get('name'), m.findtext('description') or '',
                         m.findtext('year') or '', m.findtext('manufacturer') or ''))
        m.clear()
    out = ['/* GERADO por tools/gen_game_list.py a partir do -listxml do MAME (set, título,',
           ' * ano, fabricante): dados do catálogo, não editar à mão. */']
    for r in sorted(rows):
        out.append('{ %s },' % ', '.join(c_string(v) for v in r))
    os.makedirs('frontends/launcher', exist_ok=True)
    with open('frontends/launcher/games.inc', 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    names = {os.path.splitext(f)[0] for f in os.listdir('roms/zips') if f.endswith('.zip')}
    known = {r[0] for r in rows}
    print('%d sets no catálogo; sem nome: %s' % (len(rows), sorted(names - known)))


if __name__ == '__main__':
    main()
