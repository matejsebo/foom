#!/usr/bin/env python3
#
#    Copyright 2026 Matej Šebo
#
#    This file is part of FOOM.
#
#    FOOM is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    FOOM is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.

"""Writes the import rule tables of national datasets and their fixtures.

For every dataset in datasets/*.py this writes

  symbol sets/<dataset>-ISOM 2017-2.crt
  symbol sets/<dataset>-ISSprOM 2019.crt
  test/data/import-rules/<dataset>.geojson

from one list of the dataset's classes, taken from its public feature
catalogue (FOOM's own mapping; see the design notes,
"Import rule tables"). Edit the dataset files, not the outputs, and run

  python test/data/import-rules/make_tables.py

from the repository root. test/import_rules_t.cpp checks the outputs.

A class is a dict:
  layer   the layer name as in the data (the fixture's foom_layer)
  attrs   the classifying attributes of the rule, {name: value}
  geom    'P' point, 'L' line, 'A' area
  isom    the ISOM 2017-2 symbol number, or None: not mapped
  spr     the ISSprOM 2019 symbol number, or None: not mapped
  extra   attributes of the fixture feature only (optional)
  why     for an unmapped class: the reason, written above its line (optional)
  q       the whole query, in place of one built from layer and attrs (optional)
  in_rule 'layer' to add layer_name = <layer> to the query; a string to add
          layer_name ~= <string>; None (default) for the attributes only
"""

import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
SETS = {'ISOM 2017-2': 'isom', 'ISSprOM 2019': 'spr'}

LICENCE = """\
#
#    This file is part of FOOM.
#
#    FOOM is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    FOOM is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.
"""


def quote(text):
    text = str(text)
    if re.fullmatch(r'[A-Za-z0-9_.]+', text):
        return text
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def query(c):
    if 'q' in c:
        return c['q']
    terms = []
    in_rule = c.get('in_rule')
    if in_rule == 'layer':
        terms.append('layer_name = ' + quote(c['layer']))
    elif in_rule:
        terms.append('layer_name ~= ' + quote(in_rule))
    for key, value in c['attrs'].items():
        terms.append(quote(key) + ' = ' + quote(value))
    return ' AND '.join(terms)


def write_crt(name, module, set_id, key):
    classes = module.CLASSES
    lines = ['#', '#    Copyright 2026 Matej Šebo', LICENCE.rstrip('\n'), '#']
    lines.append('# FOOM import rules: %s to %s.' % (module.TITLE, set_id))
    lines.append('# Written by test/data/import-rules/make_tables.py from datasets/%s.py; edit that file.' % name)
    lines.append('#')
    for text in module.HEADER:
        lines.append(('# ' + text).rstrip())
    lines.append('#')
    lines.append('# The classes of the catalogue without a symbol in %s:' % set_id)
    lines.append('#')
    unmapped = [c for c in classes if c[key] is None]
    last_why = None
    seen = set()
    for c in unmapped:
        if query(c) in seen:
            continue  # the same class in another geometry
        seen.add(query(c))
        why = c.get('why')
        if why and why != last_why:
            lines.append('# (%s)' % why)
            last_why = why
        lines.append('# not mapped: ' + query(c))
    lines.append('')
    # General rules first: in OOM's format the last matching rule wins.
    mapped = [c for c in classes if c[key] is not None]
    mapped.sort(key=lambda c: query(c).count(' AND ') + query(c).count('('))
    width = max(len(str(c[key])) for c in mapped) + 2
    for c in mapped:
        lines.append(str(c[key]).ljust(width) + query(c))
    path = os.path.join(ROOT, 'symbol sets', '%s-%s.crt' % (name, set_id))
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def geometry(g, i):
    x = 10.0 * (i % 20)
    y = -10.0 * (i // 20)
    if g == 'P':
        return {'type': 'Point', 'coordinates': [x, y]}
    if g == 'L':
        return {'type': 'LineString', 'coordinates': [[x, y], [x + 6, y + 2]]}
    return {'type': 'Polygon', 'coordinates': [[[x, y], [x + 6, y], [x + 6, y + 6], [x, y + 6], [x, y]]]}


def write_fixture(name, module):
    features = []
    for i, c in enumerate(module.CLASSES):
        props = {'foom_layer': c['layer']}
        props.update(c.get('extra', {}))
        props.update(c['attrs'])
        features.append({'type': 'Feature', 'properties': props, 'geometry': geometry(c['geom'], i)})
    path = os.path.join(HERE, name + '.geojson')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('{"type": "FeatureCollection",\n"name": "%s",\n"features": [\n' % name)
        f.write(',\n'.join(json.dumps(feature, ensure_ascii=False) for feature in features))
        f.write('\n]}\n')


def main():
    folder = os.path.join(HERE, 'datasets')
    for file in sorted(os.listdir(folder)):
        if not file.endswith('.py'):
            continue
        name = file[:-3]
        spec = importlib.util.spec_from_file_location(name, os.path.join(folder, file))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for set_id, key in SETS.items():
            write_crt(name, module, set_id, key)
        write_fixture(name, module)
        print('%s: %d classes' % (name, len(module.CLASSES)))


if __name__ == '__main__':
    sys.exit(main())
