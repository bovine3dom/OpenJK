#!/usr/bin/env python3
"""Test map parsing and asset search priority without retail assets."""

from contextlib import ExitStack
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('audit_audio', Path(__file__).with_name('audit-audio.py'))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


def bsp(magic=b'RBSP', version=1):
    size = 152 if magic == b'RBSP' else 144
    text = b'{\n"classname" "target_speaker"\n"noise" "sound\\alarm"\n"model" "*0"\n}\0'
    data = bytearray(size)
    struct.pack_into('<4si', data, 0, magic, version)
    struct.pack_into('<ii', data, 8, size, len(text))
    struct.pack_into('<ii', data, 64, size + len(text), 40)
    return data + text + struct.pack('<6f4i', -1, -2, -3, 1, 2, 3, 0, 0, 0, 0)


class AuditTests(unittest.TestCase):
    def test_formats_and_external_entities(self):
        for magic, version in ((b'RBSP', 1), (b'IBSP', 46)):
            entities, bounds = audio.parse_map(bsp(magic, version))
            self.assertEqual(entities[0]['noise'], 'sound\\alarm')
            self.assertEqual(bounds[0], (-1, -2, -3, 1, 2, 3))
            self.assertEqual(audio.parse_map(bsp(magic, version), b'{ }')[0], [{}])

    def test_sets_and_scripts(self):
        def block(op, *values):
            return struct.pack('<iiB', op, len(values), 0) + b''.join(
                struct.pack('<ii', 4, len(v.encode()) + 1) + v.encode() + b'\0' for v in values)
        ibi = b'IBI\0' + struct.pack('<f', 1.57)
        text = b'''type ambientset
localSet Alarm
radius 500
loopedWave alarm
subWaves effects beep beep2
volRange 90 20
timeBetweenWaves 10 5
bmodelSet Door
subWaves effects beep beep2 beep
'''
        files = {'sound/sound.txt': text, 'sound/alarm.mp3': b'audio',
                 'sound/effects/beep.wav': b'audio',
                 'scripts/a.ibi': ibi + block(32, 'b') + block(20, 'CHAN_AUTO', 'sound/alarm.wav'),
                 'scripts/b.ibi': ibi + block(26, 'SET_LOOPSOUND', 'NULL')}
        index = {k: (k, lambda v=v: v) for k, v in files.items()}
        sets = audio.sound_sets(index)
        self.assertEqual(sets['alarm']['radius'], 500)
        self.assertEqual(sets['alarm']['volume_range'], [20, 90])
        self.assertEqual(sets['alarm']['waves'][0]['asset'], 'sound/alarm.mp3')
        self.assertEqual(sets['alarm']['waves'][2]['status'], 'missing')
        self.assertEqual([w['kind'] for w in sets['door']['waves']], ['one-shot', 'loop', 'one-shot'])
        self.assertEqual([w['role'] for w in sets['door']['waves']], ['start', 'loop', 'stop'])
        scripts = audio.script_inventory(index)
        self.assertEqual(scripts['scripts/a.ibi']['dependencies'], ['scripts/b.ibi'])
        self.assertEqual(scripts['scripts/a.ibi']['sounds'][0]['asset'], 'sound/alarm.mp3')
        self.assertEqual(scripts['scripts/b.ibi']['sounds'][0]['status'], 'off')
        self.assertEqual(audio.resolve_sound(index, '*pain')['status'], 'dynamic')

    def test_invalid_map(self):
        data = bsp()
        struct.pack_into('<ii', data, 8, len(data), 1)
        with self.assertRaises(ValueError):
            audio.parse_map(data)
        with self.assertRaises(ValueError):
            audio.parse_map(bsp(), b'{ "noise"')

    def test_priority_and_inventory(self):
        with tempfile.TemporaryDirectory() as temp, ExitStack() as stack:
            root = Path(temp)
            (root / 'sound').mkdir()
            (root / 'sound/alarm.wav').write_bytes(b'loose')
            for name in ('a.pk3', 'z.pk3'):
                with zipfile.ZipFile(root / name, 'w') as archive:
                    archive.writestr('sound/alarm.wav', name.encode())
                    archive.writestr('maps/test.bsp', bsp())
            index = audio.index_assets([root], stack)
            self.assertEqual(index['sound/alarm.wav'][1](), b'z.pk3')
            report = audio.inventory(index, 'jo')
            candidate = report['maps'][0]['sound_candidates'][0]
            self.assertEqual(candidate['asset_candidates']['noise']['asset'], 'sound/alarm.wav')
            self.assertEqual(candidate['state'], 'not-tested')
            self.assertEqual(audio.index_assets([root], stack, True)['sound/alarm.wav'][1](), b'loose')
            overlay = root / 'overlay'
            overlay.mkdir()
            (overlay / 'maps').mkdir()
            (overlay / 'maps/test.ent').write_text('{ "classname" "worldspawn" "soundset" "absent" }')
            report = audio.inventory(audio.index_assets([root, overlay], stack), 'jo', {'maps/test.bsp', 'maps/absent.bsp'})
            self.assertEqual(report['maps'][0]['sound_candidates'][0]['kind'], 'global-bed')
            self.assertEqual(report['missing_maps'], ['maps/absent.bsp'])
            self.assertEqual(audio.findings(report)[0]['kind'], 'unresolved-sound-set')


if __name__ == '__main__':
    unittest.main()
