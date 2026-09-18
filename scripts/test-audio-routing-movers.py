#!/usr/bin/env python3
"""Compare actual Kejim lift and sliding-door events with each audio route."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile

spec = importlib.util.spec_from_file_location('measure', Path(__file__).with_name('measure-audio.py'))
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=measure.ROOT / 'build/ready')
    args = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix='movers.', dir=measure.ROOT / 'build/audio-routing'))
    report = dict(package=str(args.package.resolve()), map='kejim_post', cases=[], finished=False)
    fixtures = [('large-lift', 't2', [728, -480, 64], 'sound/movers/platforms/largeplat_move_lp', 10),
                ('sliding-door', 'tower_door', [224, 224, 64], 'sound/movers/doors/door1move', 2)]
    print(f'Mover routing results: {folder}', flush=True)
    try:
        with measure.game(args.package.resolve(), 'jo', 'kejim_post', folder, True) as game:
            for name, target, position, asset, seconds in fixtures:
                game.cmd('set s_steamAuditEntity -1; set s_steamRoute -1; wait 90')
                assert measure.move_listener(game, position), (name, 'Listener did not reach the fixture')
                probe = measure.fields(game.cmd('s_steam_probe ' + ' '.join(map(str, position))), 'steam_probe')[0]
                assert probe['solid'] == probe['hull_solid'] == '0', probe
                game.cmd('save audio_routing_mover; wait 30')
                status = game.cmd(f'use {target}; wait 10; s_steam_status sources')
                sources = [s for s in measure.fields(status, 'steam_source') if s['sound'] == asset and s['loop'] == '1']
                assert len(sources) == 1, (name, sources, status)
                entity = sources[0]['entity']
                assert sources[0]['route'] == 'protected' and sources[0]['rule'] == 'brush-mover', sources
                case = dict(name=name, target=target, entity=entity, listener=position, probe=probe, modes={})
                report['cases'].append(case)
                for mode, route, wet in [('legacy-direct', 0, 0), ('full', 2, 0),
                                         ('protected-direct', -1, 0), ('protected-wet', -1, .2)]:
                    game.cmd('load audio_routing_mover; wait 100')
                    game.cmd(f'set s_steamAuditEntity {entity}; set s_steamRoute {route}; set s_steamReverb {wet}; wait 90')
                    start = len(game.text())
                    capture = game.capture(seconds, play=f'use {target}; wait 10; s_steam_status sources')
                    sources = [s for s in measure.fields(game.text()[start:], 'steam_source') if s['entity'] == entity]
                    assert sources, (name, mode, 'No mover sound reached the mixer')
                    assert capture['rms'] > 0 and not capture['clipped_samples'], capture
                    assert all(capture['continuity'][k] == '0' for k in ('gap_frames', 'overlap_frames')), capture
                    assert float(capture['continuity']['listener_motion']) <= .05, capture
                    case['modes'][mode] = dict(capture=capture, sources=sources)
                    game.cmd('wait 600')
                    stopped = [s for s in measure.fields(game.cmd('s_steam_status sources'), 'steam_source')
                               if s['entity'] == entity and s['loop'] == '1']
                    assert not stopped, (name, 'Mover loop did not stop', stopped)
                case['protected_gain_db'] = (case['modes']['protected-direct']['capture']['rms_dbfs'] -
                                              case['modes']['legacy-direct']['capture']['rms_dbfs'])
                assert abs(case['protected_gain_db']) < 1, case['protected_gain_db']
                print(name, 'protected direct gain:', case['protected_gain_db'], flush=True)
        report['finished'] = True
    finally:
        (folder / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: moving lift and door, direct level, protected routing, reflections mix, and stopped loops')


if __name__ == '__main__':
    main()
