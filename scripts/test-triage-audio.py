#!/usr/bin/env python3
"""Check static routing proposals and their playback context."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('triage', Path(__file__).with_name('triage-audio.py'))
triage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(triage)


class RoutingTests(unittest.TestCase):
    def test_policy(self):
        for path, context, mode in (
            ('SOUND\\PLAYER\\FOOTSTEPS\\stone_run1', 'World', 'full'),
            ('sound/weapons/blaster/fire', 'Voice', 'protected'),
            ('sound/weapons/blaster/fire', 'Local', 'legacy'),
            ('sound/movers/platforms/lift', 'World', 'protected'),
            ('sound/ambience/prototype/alarm1', 'World', 'protected'),
            ('sound/effects/explode10', 'World', 'full'),
            ('unknown', 'World', 'protected'),
        ):
            self.assertEqual(triage.classify(path, context)['route'], mode)
        self.assertEqual(triage.channel_context('CHAN_VOICE_GLOBAL'), 'Local')
        self.assertEqual(triage.channel_context('CHAN_VOICE_ATTEN'), 'Voice')

    def test_inventory(self):
        sound = dict(asset='sound/weapons/blaster/fire.wav', status='resolved', source='fixture')
        report = dict(maps=[dict(map='maps/test.bsp', scripts=['test.ibi'], sound_candidates=[
            dict(entity=1, fields={'classname': 'worldspawn'}, kind='global-bed',
                 sound_set={'waves': [sound]}, asset_candidates={}),
            dict(entity=2, fields={'classname': 'func_door', 'model': '*1'},
                 asset_candidates={'sound': sound}),
        ])], scripts={'test.ibi': {'sounds': [dict(sound, channel='CHAN_VOICE', offset=4),
                                            dict(reference='NULL', status='off', channel='loop')]}})
        rows = triage.triage(report)
        self.assertEqual([r['route'] for r in rows], ['legacy', 'protected', 'protected'])
        self.assertEqual(rows[2]['owner'], 'test.ibi')
        self.assertFalse(any(r['review'] for r in rows))


if __name__ == '__main__':
    unittest.main()
