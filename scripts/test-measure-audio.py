#!/usr/bin/env python3
"""Test PCM measurements and diagnostic output parsing."""

import array
import importlib.util
import math
from pathlib import Path
import sys
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location('measure_audio', Path(__file__).with_name('measure-audio.py'))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


class MeasurementTests(unittest.TestCase):
    def test_pcm(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'test.wav'
            samples = array.array('h', [0] * 200 + [16384, -16384] * 900)
            if sys.byteorder != 'little':
                samples.byteswap()
            with wave.open(str(path), 'wb') as wav:
                wav.setparams((2, 2, 1000, 0, 'NONE', 'not compressed'))
                wav.writeframes(samples.tobytes())
            result = audio.pcm_metrics(path)
            self.assertAlmostEqual(result['rms'], 0.5 * math.sqrt(0.9))
            self.assertAlmostEqual(result['peak_dbfs'], -6.0206, places=4)
            self.assertEqual(result['first_signal_seconds'], 0.1)
            self.assertEqual(result['tail_energy'], 250)
            self.assertEqual(result['clipped_samples'], 0)
            self.assertEqual(result['frames'], 1000)

    def test_fields(self):
        parsed = audio.fields('steam_audio active=1 sources=3\nsteam_audio timing rate=44100\n', 'steam_audio')
        self.assertEqual(parsed, [{'active': '1', 'sources': '3'}, {'kind': 'timing', 'rate': '44100'}])
        self.assertEqual(audio.fields('Steam Audio capture continuity: overlap_frames=0 gap_frames=0\n',
                                     'Steam Audio capture continuity:')[0]['gap_frames'], '0')

    def test_emitter_cases(self):
        record = {'sound_candidates': [dict(entity=3, fields={'classname': 'target_speaker', 'origin': '1 2 3'},
                                            channel='loop', initial_state='on',
                                            asset_candidates={'noise': dict(status='resolved', asset='sound/a.mp3', source='test')})]}
        case = audio.emitter_cases(record)[0]
        self.assertEqual(case['sound'], 'sound/a')
        self.assertEqual(case['positions'][0][1], [129, 2, 3])
        self.assertEqual(len(case['positions']), 18)

    def test_camera_calibration(self):
        class Session:
            position = [0, 0, 0]
            def cmd(self, command):
                if command.startswith('setviewpos'):
                    self.position = list(map(float, command.split()[1:4]))
                    self.position[2] += 12
                    return ''
                return 'steam_listener pos=' + ','.join(map(str, self.position)) + ' solid=0\n'
        session = Session()
        self.assertTrue(audio.move_listener(session, [10, 20, 30]))
        self.assertEqual(session.position, [10, 20, 30])

    def test_global_bed_and_door_positions(self):
        record = {'sound_candidates': [dict(entity=0, kind='global-bed', fields={'classname': 'worldspawn'},
                                            sound_set={'waves': [dict(kind='loop', status='resolved', asset='sound/bed.wav', source='test')]})],
                  'controls': [dict(entity=1, fields={'classname': 'info_player_start', 'origin': '1 2 3'}),
                               dict(entity=2, fields={'classname': 'func_door'}, model_bounds_local=[0, 0, 0, 8, 64, 96])]}
        self.assertEqual(audio.emitter_cases(record)[0]['positions'][0][1], [1, 2, 51])
        positions = dict(audio.control_positions(record, {}, [4, 32, 48]))
        self.assertEqual(positions['door-2-1'], [56, 32, 48])
        self.assertEqual(positions['door-2--1'], [-48, 32, 48])


if __name__ == '__main__':
    unittest.main()
