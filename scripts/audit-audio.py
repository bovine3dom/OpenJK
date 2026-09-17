#!/usr/bin/env python3
"""List map sound references. This report does not measure audibility."""

import argparse
from contextlib import ExitStack
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audit_jo', Path(__file__).with_name('audit-jo.py'))
icarus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icarus)
BEHAVIORS = {name.lower() for name in re.findall(r'ENUM2STRING\((BS_\w+)\)',
             (ROOT / 'code/game/Q3_Interface.cpp').read_text())}


def script_reference(value):
    return None if value.lower() in BEHAVIORS | {'', 'null', 'none', 'default'} else icarus.script_path(value)


def index_assets(roots, stack, loose_first=False):
    """Read game directories in order from low to high priority."""
    index = {}
    for root in roots:
        if not root.is_dir():
            raise ValueError(f"Missing game directory: {root}")
        loose = {p.relative_to(root).as_posix(): (str(p), p.read_bytes)
                 for p in sorted(root.rglob('*')) if p.is_file() and p.suffix.lower() != '.pk3'}
        packed = {}
        for path in sorted((p for p in root.iterdir() if p.is_file() and p.suffix.lower() == '.pk3'), key=lambda p: p.name.lower()):
            archive = stack.enter_context(zipfile.ZipFile(path))
            for entry in archive.infolist():
                if not entry.is_dir():
                    packed[entry.filename.lower()] = (
                        f"{path}:{entry.filename}", lambda a=archive, e=entry: a.read(e))
        index.update(packed if loose_first else loose)
        index.update(loose if loose_first else packed)
    return index


def parse_map(data, external=None):
    header = struct.unpack_from('<4si', data)
    if header not in ((b'RBSP', 1), (b'IBSP', 46)):
        raise ValueError(f"Unsupported BSP header: {header}")
    header_size = 8 + 8 * (18 if header[0] == b'RBSP' else 17)

    def lump(number):
        offset, size = struct.unpack_from('<ii', data, 8 + number * 8)
        if offset < header_size or size < 0 or offset + size > len(data):
            raise ValueError(f"Invalid BSP lump {number}")
        return data[offset:offset + size]

    text = external if external is not None else lump(0)
    lexer = shlex.shlex(text.rstrip(b'\0').decode('latin-1'), posix=True)
    lexer.whitespace_split = True
    lexer.commenters = ''
    lexer.escape = ''  # Preserve game paths that contain backslashes.
    tokens = iter(lexer)
    entities = []
    try:
        for token in tokens:
            if token != '{':
                raise ValueError('Expected entity opening brace')
            entity = {}
            key = next(tokens)
            while key != '}':
                value = next(tokens)
                if value in ('{', '}'):
                    raise ValueError('Missing entity value')
                entity[key.lower()] = value
                key = next(tokens)
            entities.append(entity)
    except StopIteration as error:
        raise ValueError('Incomplete entity') from error
    models = lump(7)
    if len(models) % 40:
        raise ValueError('Invalid BSP model lump size')
    bounds = [struct.unpack_from('<6f', models, i) for i in range(0, len(models), 40)]
    return entities, bounds


def resolve_sound(index, value):
    path = value.lower().replace('\\', '/')
    if not path or path in ('none', 'null', '0'):
        return {'status': 'off', 'reference': value}
    if '/' not in path or path.startswith('*') or not re.fullmatch(r'[\w./-]+', path):
        return {'status': 'dynamic', 'reference': value}
    # S_FindName strips the extension. S_LoadSound tries WAV, then MP3.
    stem = str(Path(path).with_suffix('')) if Path(path).suffix else path
    choices = [stem + '.wav', stem + '.mp3']
    selected = next((p for p in choices if p in index), None)
    return {'reference': value, 'status': 'resolved' if selected else 'missing',
            'attempted': choices, 'asset': selected,
            'source': index[selected][0] if selected else None}


def sound_sets(index):
    if 'sound/sound.txt' not in index:
        return {}
    source, read = index['sound/sound.txt']
    groups, current = [], None
    for line_number, line in enumerate(read().decode('latin-1').splitlines(), 1):
        words = line.split(';', 1)[0].split()
        if not words:
            continue
        key = words[0].lower()
        if key in ('generalset', 'localset', 'bmodelset'):
            current = {'name': words[1], 'kind': key, 'source': source,
                       'line': line_number, 'radius': 250, 'volume_range': [255, 255],
                       'time_between_waves': [10, 25], 'waves': []}
            groups.append(current)
        elif current and key == 'loopedwave':
            current['waves'].append({'kind': 'loop', **resolve_sound(index, 'sound/' + words[1] + '.wav')})
        elif current and key == 'subwaves':
            remaining = 8 - sum(w['kind'] == 'one-shot' for w in current['waves'])
            current['waves'].extend({'kind': 'one-shot', **resolve_sound(index, f'sound/{words[1]}/{w}.wav')}
                                    for w in words[2:2 + remaining])
            if len(words) - 2 > remaining:
                current.setdefault('ignored_subwaves', []).extend(words[2 + remaining:])
        elif current and key == 'radius':
            current['radius'] = int(words[1])
        elif current and key in ('volrange', 'timebetweenwaves'):
            current['volume_range' if key == 'volrange' else 'time_between_waves'] = sorted(map(int, words[1:3]))
    for group in groups:
        if group['kind'] == 'bmodelset':
            for stage, wave in enumerate(w for w in group['waves'] if w['kind'] == 'one-shot'):
                wave.update(stage=stage, role=('start', 'loop', 'stop')[stage] if stage < 3 else 'extra-stage',
                            kind='loop' if stage == 1 else 'one-shot')
    # The runtime parses each group type in this order. The last same-name set wins.
    return {g['name'].lower(): g for kind in ('generalset', 'localset', 'bmodelset') for g in groups if g['kind'] == kind}


def script_inventory(index):
    scripts = {}
    for name in sorted(index):
        if not name.lower().startswith('scripts/') or not name.lower().endswith('.ibi'):
            continue
        record = {'source': index[name][0], 'dependencies': [], 'sounds': [], 'controls': []}
        canonical = name.lower()
        if canonical in scripts:
            record['case_review'] = [scripts[canonical]['source'], index[name][0]]
        elif '.pk3:' not in index[name][0].lower() and (not name.endswith('.IBI') or name[:-4] != name[:-4].lower()):
            record['case_review'] = [index[name][0]]
        scripts[canonical] = record
        try:
            for block in icarus.blocks(index[name][1]()):
                op, values = block['op'], block['values']
                if op == 32 and values and isinstance(values[0], str):
                    record['dependencies'].append(icarus.script_path(values[0]))
                if op == 26 and len(values) >= 2 and isinstance(values[0], str):
                    key, value = values[:2]
                    if key.endswith('SCRIPT') and isinstance(value, str) and script_reference(value):
                        record['dependencies'].append(script_reference(value))
                    if any(s in key for s in ('SOUND', 'AMBIENT')):
                        record['controls'].append(block)
                    if key == 'SET_LOOPSOUND' and isinstance(value, str):
                        record['sounds'].append({'offset': block['offset'], 'channel': 'loop', **resolve_sound(index, value)})
                if op == 20:
                    if len(values) == 2 and all(isinstance(v, str) for v in values):
                        record['sounds'].append({'offset': block['offset'], 'channel': values[0],
                                                 **resolve_sound(index, values[1])})
                    else:
                        record['controls'].append(block)
                if op in (19, 30, 33, 34):  # affect, use, kill, remove
                    record['controls'].append(block)
        except (ValueError, struct.error, UnicodeError) as error:
            record['parse_error'] = str(error)
        record['dependencies'] = sorted(set(record['dependencies']))
    return scripts


def inventory(index, campaign, selected_maps=None):
    maps = []
    sets, scripts = sound_sets(index), script_inventory(index)
    for name in sorted(index):
        if not name.startswith('maps/') or not name.endswith('.bsp'):
            continue
        if selected_maps is not None and name not in selected_maps:
            continue
        ent_name = name[:-4] + '.ent'
        external = index.get(ent_name)
        bsp = index[name][1]()
        ent_data = external[1]() if external else None
        entities, bounds = parse_map(bsp, ent_data)
        candidates, controls, dependencies = [], [], set()
        for number, entity in enumerate(entities):
            dependencies.update(filter(None, (script_reference(v) for k, v in entity.items() if k.endswith('script'))))
            if any(k.startswith('target') or k.endswith('script') for k in entity) or entity.get('classname', '').startswith(('info_', 'func_door')):
                control = {'entity': number, 'fields': entity}
                if re.fullmatch(r'\*\d+', entity.get('model', '')):
                    model_id = int(entity['model'][1:])
                    if model_id >= len(bounds):
                        raise ValueError(f'{name}: invalid model {entity["model"]}')
                    control['model_bounds_local'] = bounds[model_id]
                controls.append(control)
        for number, entity in enumerate(entities):
            refs = {k: v for k, v in entity.items()
                    if re.search(r'sound|noise|music', k, re.I)}
            if entity.get('classname') == 'worldspawn':
                refs.setdefault('soundset', 'default')
            if not refs and entity.get('classname') != 'target_speaker':
                continue
            record = {'entity': number, 'fields': entity, 'references': refs,
                      'asset_candidates': {}, 'state': 'not-tested'}
            model = entity.get('model', '')
            if re.fullmatch(r'\*\d+', model):
                model_id = int(model[1:])
                if model_id >= len(bounds):
                    raise ValueError(f'{name}: invalid model {model}')
                record['model_bounds_local'] = bounds[model_id]
            for key, value in refs.items():
                if key == 'soundset':
                    record['sound_set'] = sets.get(value.lower())
                    record['sound_set_status'] = ('resolved' if value.lower() in sets else
                                                  'implicit-default' if key not in entity else 'missing')
                elif key == 'soundgroup':
                    count = int(entity.get('sounds', '0'))
                    if re.fullmatch(r'[^%]*%d[^%]*', value) and 0 < count <= 256:
                        record['sound_group'] = [resolve_sound(index, value % i) for i in range(1, count + 1)]
                    else:
                        record['sound_group_unresolved'] = value
                elif key == 'noise' or key == 'sound' or value.lower().startswith('sound/'):
                    record['asset_candidates'][key] = resolve_sound(index, value)
            cls = entity.get('classname', '')
            flags = int(entity.get('spawnflags', '0'))
            record['kind'] = ('global-bed' if cls == 'worldspawn' else
                              'global-bed-control' if cls.startswith('trigger_') and 'soundset' in entity else
                              'sound-set' if 'soundset' in entity else
                              'speaker' if cls == 'target_speaker' else 'other-sound-fields')
            if cls == 'target_speaker' and 'soundset' not in entity:
                record.update({'channel': 'loop' if flags & 3 else 'activator' if flags & 8 else
                               'global' if flags & 4 else 'world-one-shot',
                               'initial_state': 'on' if flags & 1 else 'off' if flags & 2 else 'event',
                               'use_action': 'toggle' if flags & 3 else 'play',
                               'activation_owner': 'activator' if flags & 8 else 'speaker'})
            target = entity.get('targetname')
            record['incoming_targets'] = [{'entity': i, 'field': k} for i, e in enumerate(entities)
                                          for k, v in e.items() if target and k != 'targetname' and
                                          (k.endswith('target') or k.startswith('target')) and v == target]
            candidates.append(record)
        pending, reachable = list(dependencies), set()
        while pending:
            dep = pending.pop()
            if dep not in reachable:
                reachable.add(dep)
                pending.extend(scripts.get(dep, {}).get('dependencies', []))
        maps.append({'map': name, 'bsp_source': index[name][0],
                     'bsp_sha256': hashlib.sha256(bsp).hexdigest(),
                     'entity_source': external[0] if external else index[name][0] + ':lump0',
                     'entity_sha256': hashlib.sha256(ent_data).hexdigest() if ent_data is not None else None,
                     'entity_count': len(entities), 'sound_candidates': candidates, 'controls': controls,
                     'scripts': sorted(reachable), 'missing_scripts': sorted(reachable - scripts.keys())})
    used_scripts = {s for m in maps for s in m['scripts']}
    return {'campaign': campaign, 'maps': maps, 'sound_sets': sets,
            'scripts': {s: scripts[s] for s in sorted(used_scripts) if s in scripts},
            'unreferenced_scripts': sorted(scripts.keys() - used_scripts),
            'missing_maps': sorted((selected_maps or set()) - {m['map'] for m in maps})}


def findings(report):
    result = []
    for map_record in report['maps']:
        def issue(kind, source, reference):
            result.append(dict(map=map_record['map'], kind=kind, source=source, reference=reference))
        for emitter in map_record['sound_candidates']:
            source = f"entity:{emitter['entity']}"
            if emitter.get('sound_set_status') == 'missing':
                issue('unresolved-sound-set', source, emitter['references']['soundset'])
            waves = list(emitter['asset_candidates'].values()) + emitter.get('sound_group', [])
            if emitter.get('sound_set'):
                waves += emitter['sound_set']['waves']
            for sound in waves:
                if sound['status'] in ('missing', 'dynamic'):
                    issue(sound['status'] + '-sound', source, sound['reference'])
        for name in map_record['scripts']:
            script = report['scripts'].get(name)
            if script is None:
                issue('unresolved-script', name, name)
                continue
            if 'parse_error' in script:
                issue('script-parse-error', name, script['parse_error'])
            if 'case_review' in script:
                issue('script-case-review', name, script['case_review'])
            for sound in script['sounds']:
                if sound['status'] in ('missing', 'dynamic'):
                    issue(sound['status'] + '-sound', f"{name}:{sound['offset']}", sound['reference'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', choices=('jo', 'ja'), required=True)
    parser.add_argument('--game-dir', type=Path, action='append', required=True,
                        help='Repeat in order from low to high search priority. Include the import overlay.')
    parser.add_argument('--loose-first', action='store_true', help='Match fs_dirbeforepak 1.')
    parser.add_argument('--all-maps', action='store_true', help='Include BSP files outside the campaign list.')
    parser.add_argument('--map', action='append', help='Scan only these map names.')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    policy = json.loads((ROOT / 'scripts/atmosphere-review.json').read_text())
    selected = args.map or policy['campaigns'][args.campaign]['order']
    selected = None if args.all_maps else {f'maps/{m}.bsp' for m in selected}
    with ExitStack() as stack:
        report = inventory(index_assets(args.game_dir, stack, args.loose_first), args.campaign, selected)
    report['findings'] = findings(report)
    report['map_selection'] = 'all-visible' if args.all_maps else 'explicit' if args.map else 'scripts/atmosphere-review.json'
    report.update({'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   'game_dirs': [str(p.resolve()) for p in args.game_dir],
                   'loose_first': args.loose_first, 'limits': [
                       'Static entity candidates only. No audibility tests.',
                       'Campaign maps use the existing campaign list, which includes the JO bonus map pit.',
                       'Script branches, dynamic references, code-generated sounds, and activation states need runtime tests.',
                       'Sound resolution assumes English. Missing literal files do not prove an audible defect.',
                       'Loose script case aliases require runtime file checks; the runtime requests an uppercase IBI extension.',
                       'Sound-set volume ranges apply to subwaves, not to loop master volume.',
                       'Listener positions, collision checks, and runtime search paths are not verified.']})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"{args.campaign}: {len(report['maps'])} maps; report: {args.output}")


if __name__ == '__main__':
    main()
