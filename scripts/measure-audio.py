#!/usr/bin/env python3
"""Compare isolated legacy and Steam Audio captures at fixed listener positions."""

import argparse
import array
from contextlib import contextmanager
import hashlib
from itertools import combinations
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time
import wave

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = dict(cl_renderer='rdsp-vanilla', r_mode=-1, r_customwidth=640, r_customheight=480,
                r_fullscreen=0, s_initsound=1, s_musicvolume=0, s_volume=0.8, s_volumeVoice=1,
                s_separation=0.5, com_maxfps=60, r_ignoreGLErrors=1, developer=1, s_khz=44,
                s_steamAudio=1, s_steamReflections=1, s_steamPathing=1, s_steamReverb=0.2,
                s_steamTransientReverb=2.5, s_steamTransmission=0.12, s_steamLimiter=1,
                s_steamCache=1, cg_thirdPerson=0, cg_boltFlyby=0, s_language='english',
                cg_smoothPlayerPos=0, cg_smoothPlayerPlat=0, cg_smoothCamera=0, cg_errorDecay=0,
                cg_bobup=0, cg_bobpitch=0, cg_bobroll=0,
                cg_thirdPersonCameraDamp=1, cg_thirdPersonTargetDamp=1)
MODES = {'legacy': dict(s_steamAudio=0, cg_spatialAmbience=0, cg_alarmRelays=0),
         'steam': dict(s_steamAudio=1, cg_spatialAmbience=1, cg_alarmRelays=1),
         'steam-matched': dict(s_steamAudio=1, cg_spatialAmbience=0, cg_alarmRelays=0)}
ALARM_POSITIONS = [('panel', [-32, 160, 528]), ('panel-approach', [64, 160, 496]),
                   ('gun', [-464, -368, 64]),
                   ('behind-wall', [56, 0, 400]), ('exterior-route', [256, -128, 400]),
                   ('canyon-route', [640, -128, 400]), ('interior', [528, 200, 64]),
                   ('lower-door-west', [296, 160, 64]), ('lower-door-east', [400, 160, 64]),
                   ('upper-door-west', [280, -128, 384]), ('upper-door-east', [384, -128, 384])]


def db(value):
    return 20 * math.log10(max(value, 1e-6))


def pcm_metrics(path):
    with wave.open(str(path)) as wav:
        if wav.getsampwidth() != 2 or wav.getnchannels() != 2:
            raise ValueError('Expected stereo 16-bit PCM')
        rate, frames = wav.getframerate(), wav.getnframes()
        samples = array.array('h', wav.readframes(frames))
    if sys.byteorder != 'little':
        samples.byteswap()
    if not samples or len(samples) != frames * 2:
        raise ValueError('Empty or incomplete PCM capture')
    energy = sum(s * s for s in samples)
    rms = math.sqrt(energy / len(samples)) / 32768
    onset = next((i // 2 / rate for i, s in enumerate(samples) if abs(s) >= 33), None)
    jumps = [samples[i] - samples[i - 2] for i in range(2, len(samples))]
    boundary_jumps = [samples[2*i + ch] - samples[2*i + ch - 2]
                      for i in range(256, frames, 256) for ch in (0, 1)]
    return dict(rate=rate, frames=frames, rms_dbfs=db(rms), peak_dbfs=db(max(map(abs, samples)) / 32768),
                rms=rms, peak=max(map(abs, samples)), clipped_samples=sum(s in (-32768, 32767) for s in samples),
                max_sample_jump=max(map(abs, jumps), default=0),
                max_256_frame_jump=max(map(abs, boundary_jumps), default=0),
                rms_256_frame_jump=math.sqrt(sum(j*j for j in boundary_jumps) / max(1, len(boundary_jumps))),
                first_signal_seconds=onset, tail_energy=sum(s * s for s in samples[-rate:]) / 32768**2,
                envelope_100ms_dbfs=[db(math.sqrt(sum(s*s for s in samples[i:i + rate//5]) /
                                                len(samples[i:i + rate//5])) / 32768)
                                     for i in range(0, len(samples), rate//5)],
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def fields(text, prefix):
    return [dict([('kind', line.split()[0])] if '=' not in line.split()[0] else [],
                 **dict(word.split('=', 1) for word in line.split() if '=' in word))
            for line in re.findall(r'^(?:\d{4}-\d\d-\d\d \d\d:\d\d:\d\d )?' + re.escape(prefix) + r' ([^\n]+)', text, re.M)]


class Game:
    def __init__(self, process, log, profile):
        self.process, self.log, self.profile = process, log, profile
        self.serial = 0

    def text(self):
        return re.sub(r'\^[0-9]', '', self.log.read_text(errors='replace'))

    def wait(self, marker, start=0):
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            output = self.text()[start:]
            if marker in output:
                return output
            if self.process.poll() is not None:
                raise RuntimeError(f'Game exited: {self.log}')
            time.sleep(0.05)
        raise TimeoutError(f'Missing {marker}: {self.log}')

    def cmd(self, command):
        self.serial += 1
        marker = f'AUDIT_CMD_{self.serial}_DONE'
        line = f'{command}; wait 3; echo {marker}\n'
        if len(line) >= 256 or '\n' in command:
            raise ValueError(f'Invalid console command: {command}')
        start = len(self.text())
        self.process.stdin.write(line)
        self.process.stdin.flush()
        return self.wait(marker, start)

    def capture(self, seconds, wet=False, play=None):
        start = len(self.text())
        self.cmd(f's_steam_status reset; s_steam_record {seconds}{" wet" if wet else ""}' + (f'; {play}' if play else ''))
        output = self.wait('Steam Audio capture continuity:', start)
        path = self.profile / re.search(r'Steam Audio capture: (\S+)', output)[1]
        result = pcm_metrics(path)
        result.update(path=str(path), continuity=fields(output, 'Steam Audio capture continuity:')[0])
        return result


@contextmanager
def game(package, campaign, map_name, folder, freeze_scripts=False):
    home = folder / 'profile'
    profile = home / ('campaigns/jo/OpenJK' if campaign == 'jo' else 'OpenJK')
    profile.mkdir(parents=True)
    (profile / 'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in SETTINGS.items()))
    (profile / 'autoexec_sp.cfg').write_text('')
    env = dict(os.environ, OJK_PROFILE=str(home), OJK_JO_ASSETS=str(ROOT / 'GameData_JO'),
               SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='offscreen', EGL_PLATFORM='surfaceless')
    log = folder / 'console.log'
    with log.open('w') as stream:
        process = subprocess.Popen(['bash', str(package / 'launch-sp.sh'), str(ROOT / 'GameData'),
                                    '--campaign', campaign, '+devmap', map_name,
                                    '+wait', '100', '+echo', 'AUDIT_READY'], env=env, stdin=subprocess.PIPE,
                                   stdout=stream, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        session = Game(process, log, profile)
        try:
            session.wait('AUDIT_READY')
            session.cmd('exitview; wait 100')
            # A ROQ movie can outlast the game-camera skip and suspend acoustic mixing.
            for _ in range(90):
                if fields(session.cmd('s_steam_status'), 'steam_audio')[0]['active'] == '1':
                    break
                time.sleep(1)
            else:
                raise RuntimeError('Steam Audio did not become active after map setup')
            selection = fields(session.cmd('rml_selection_status'), 'rml_selection')
            if selection and selection[0]['active'] == '1':
                raise RuntimeError('Mission selection is active; complete the loadout before sampling')
            if freeze_scripts:
                if 'icarus_freeze active=1' not in session.cmd('ICARUS freeze 1'):
                    raise RuntimeError('This package does not support script freezing')
                session.cmd('cam_disable; wait 100')
            session.cmd('helpusobi 1; god; notarget; d_npcfreeze 1; cg_thirdPerson 0')
            session.cmd('noclip; con_notifytime -1; give weaponnum 3; give ammo; wait 30; weapon 3; wait 60')
            yield session
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=20)
            # The importer output is large and can be rebuilt from the recorded package.
            (profile / 'zz_jo_campaign.pk3').unlink(missing_ok=True)


def control_positions(map_record, emitter, origin):
    positions = []
    incoming = {link['entity'] for link in emitter.get('incoming_targets', [])}
    for control in map_record.get('controls', []):
        entity = control['fields']
        door = entity.get('classname', '').startswith('func_door')
        navigation = entity.get('classname') in ('info_navgoal', 'info_player_start', 'info_waypoint', 'waypoint', 'waypoint_navgoal')
        if control['entity'] not in incoming and not door and not navigation:
            continue
        point = list(map(float, entity.get('origin', '0 0 0').split()))
        bounds = control.get('model_bounds_local')
        if bounds:
            point = [point[i] + (bounds[i] + bounds[i + 3]) / 2 for i in range(3)]
        elif 'origin' not in entity:
            continue
        if math.dist(point, origin) > 768:
            continue
        if door and bounds:
            axis = min(range(2), key=lambda i: bounds[i + 3] - bounds[i])
            offset = (bounds[axis + 3] - bounds[axis]) / 2 + 48
            for sign in (1, -1):
                positions.append((f'door-{control["entity"]}-{sign}',
                                  [v + (sign * offset if i == axis else 0) for i, v in enumerate(point)]))
        elif navigation:
            positions.append((f'navigation-{control["entity"]}', [point[0], point[1], point[2] + 48]))
        else:
            positions.append((f'control-{control["entity"]}', [point[0] + 48, point[1], point[2]]))
    return positions


def emitter_cases(map_record):
    cases = []
    for emitter in map_record['sound_candidates']:
        entity = emitter['fields']
        if entity.get('classname') != 'target_speaker' or 'origin' not in entity:
            continue
        waves = emitter.get('sound_set', {}).get('waves', []) if emitter.get('sound_set') else []
        sounds = [w for w in waves if w['kind'] == 'loop' and w['status'] == 'resolved']
        if emitter.get('channel') == 'loop':
            sounds += [a for a in emitter['asset_candidates'].values() if a['status'] == 'resolved']
        if not sounds:
            continue
        origin = list(map(float, entity['origin'].split()))
        if len(origin) != 3:
            continue
        for sound in sounds:
            radius = emitter.get('sound_set', {}).get('radius', 1506) if emitter.get('sound_set') else 1506
            distance = max(32, min(128, radius / 4))
            positions = [(f'offset-{axis}-{sign}', [v + (sign * distance if i == axis else 0) for i, v in enumerate(origin)])
                         for axis in (0, 1, 2) for sign in (1, -1)]
            positions += [(f'raised-{axis}-{sign}', [v + (sign * distance if i == axis else 48 if i == 2 else 0)
                                                    for i, v in enumerate(origin)])
                          for axis in (0, 1) for sign in (1, -1)]
            positions += [(f'radius-{fraction}-{axis}-{sign}',
                           [v + (sign * radius * fraction if i == axis else 48 if i == 2 else 0)
                            for i, v in enumerate(origin)])
                          for fraction in (.5, .8) for axis in (0, 1) for sign in (1, -1)]
            positions += control_positions(map_record, emitter, origin)
            cases.append(dict(entity=emitter['entity'], sound=str(Path(sound['asset']).with_suffix('')),
                              source=sound['source'], origin=origin, target=entity.get('targetname'),
                              initial_state=emitter.get('initial_state', 'on'), positions=positions))
    for emitter in map_record['sound_candidates']:
        if emitter.get('kind') != 'global-bed' or not emitter.get('sound_set'):
            continue
        positions = []
        for control in map_record.get('controls', []):
            if control['fields'].get('classname') == 'info_player_start' and 'origin' in control['fields']:
                x, y, z = map(float, control['fields']['origin'].split())
                positions.extend([(f'start-{control["entity"]}', [x, y, z + 48]),
                                  (f'start-{control["entity"]}-nearby', [x + 32, y, z + 48])])
        for sound in emitter['sound_set']['waves']:
            if sound['kind'] == 'loop' and sound['status'] == 'resolved':
                cases.append(dict(entity=emitter['entity'], sound=str(Path(sound['asset']).with_suffix('')),
                                  source=sound['source'], kind='global-bed', initial_state='on', positions=positions))
    return cases


def move_listener(session, position):
    command_position = list(position)
    for _ in range(3):
        coordinates = ' '.join(f'{v:.2f}' for v in command_position)
        session.cmd(f'setviewpos {coordinates} 0; wait 30')
        listener = fields(session.cmd('s_steam_status'), 'steam_listener')[0]
        actual = list(map(float, listener['pos'].split(',')))
        if math.dist(actual, position) <= 0.05:
            return True
        command_position = [c + target - observed for c, target, observed in zip(command_position, position, actual)]
    return False


def measure(session, case, args, result):
    sound = case['sound']
    if not re.fullmatch(r'[\w/.-]+', sound):
        raise ValueError(f'Unsafe sound path: {sound}')
    result.update({**case, 'samples': [], 'rejected_positions': [], 'tested_state': 'requested-on'})
    session.cmd(f'set s_steamAuditSound {sound}; set s_steamAuditEntity -1; wait 90')
    if case['initial_state'] == 'off':
        target = case.get('target')
        if not target or not re.fullmatch(r'[\w.-]+', target):
            result['skipped'] = 'No safe literal activation target'
            return
        session.cmd(f'use {target}; wait 90')
    for label, position in case['positions']:
        if args.listener and label not in args.listener:
            continue
        if sum('invalid' not in s for s in result['samples']) >= args.listeners:
            break
        coordinates = ' '.join(f'{v:g}' for v in position)
        probe = fields(session.cmd(f's_steam_probe {coordinates}'), 'steam_probe')[0]
        if probe['solid'] != '0' or probe['hull_solid'] != '0':
            result['rejected_positions'].append(dict(label=label, probe=probe))
            continue
        if not move_listener(session, position):
            result['rejected_positions'].append(dict(label=label, reason='Camera did not reach the requested position'))
            continue
        sample = dict(label=label, requested_position=position, probe=probe, modes={})
        for mode in ('legacy', 'steam', 'steam-matched') if args.matched else ('legacy', 'steam'):
            for key, value in MODES[mode].items():
                session.cmd(f'set {key} {value}')
            session.cmd('wait 90')
            before = session.cmd('s_steam_status sources')
            if fields(before, 'steam_audio')[0]['active'] != str(MODES[mode]['s_steamAudio']):
                raise RuntimeError(f'{mode}: the requested audio backend is not active')
            listener = fields(before, 'steam_listener')[0]
            actual_probe = fields(session.cmd('s_steam_probe ' + listener['pos'].replace(',', ' ')), 'steam_probe')[0]
            sample['actual_probe'] = actual_probe
            if actual_probe['solid'] != '0' or actual_probe['hull_solid'] != '0':
                sample['invalid'] = 'Actual listener or test hull is solid'
                break
            capture = session.capture(args.seconds)
            after = session.cmd('s_steam_status sources')
            sources = [s for s in fields(before, 'steam_source') if s['selected'] == '1']
            after_sources = [s for s in fields(after, 'steam_source') if s['selected'] == '1']
            orientation = fields(before, 'steam_listener_axis')
            sample['modes'][mode] = dict(capture=capture, listener=listener,
                                         orientation=orientation[0] if orientation else None,
                                         listener_after=fields(after, 'steam_listener')[0],
                                         sources=sources, sources_after=after_sources,
                                         status=fields(after, 'steam_audio'),
                                         duplicate_loops=len({(s['entity'], s['sound']) for s in sources if s['loop'] == '1'}) <
                                         sum(s['loop'] == '1' for s in sources))
        if set(('legacy', 'steam')) <= sample['modes'].keys():
            legacy, steam = (sample['modes'][m]['capture'] for m in ('legacy', 'steam'))
            sample['gain_db'] = steam['rms_dbfs'] - legacy['rms_dbfs'] if legacy['rms'] else None
            sample['strong_legacy'] = legacy['rms_dbfs'] >= args.strong_dbfs
            sample['loss_candidate'] = sample['strong_legacy'] and sample['gain_db'] < -args.loss_db
            sample['position_match'] = math.dist(*(tuple(map(float, sample['modes'][m]['listener']['pos'].split(',')))
                                                   for m in ('legacy', 'steam'))) <= 0.05
            if not sample['position_match']:
                sample['invalid'] = 'Listener moved between modes'
            orientations = [sample['modes'][m]['orientation'] for m in ('legacy', 'steam')]
            sample['orientation_verified'] = all(orientations)
            if all(orientations) and any(math.dist(*(tuple(map(float, o[axis].split(','))) for o in orientations)) > .001
                                         for axis in ('forward', 'left', 'up')):
                sample['invalid'] = 'Listener turned between modes'
            sample['motion_verified'] = all('listener_motion' in c['continuity'] for c in (legacy, steam))
            if any(float(c['continuity'].get('listener_motion', 0)) > .05 or
                   float(c['continuity'].get('axis_motion', 0)) > .001 for c in (legacy, steam)):
                sample['invalid'] = 'Listener moved or turned during capture'
            if any(int(steam['continuity'][k]) for k in ('overlap_frames', 'gap_frames')) or int(legacy['continuity']['gap_frames']):
                sample['invalid'] = 'PCM is not continuous'
            if any(c['frames'] != args.seconds * c['rate'] for c in (legacy, steam)):
                sample['invalid'] = 'Wrong PCM length'
            if sample['strong_legacy'] and not sample['modes']['legacy']['sources']:
                sample['invalid'] = 'Strong PCM without a selected source'
        result['samples'].append(sample)
    if case['initial_state'] == 'off':
        session.cmd('set s_steamAudio 1; set cg_spatialAmbience 1; set cg_alarmRelays 1')
        session.cmd(f'use {case["target"]}; wait 400')
        result['off_capture'] = session.capture(args.seconds)
        result['off_sources'] = [s for s in fields(session.cmd('s_steam_status sources'), 'steam_source') if s['selected'] == '1']
        result['stale_sound_candidate'] = result['off_capture']['rms_dbfs'] > -60
    result['unsampled_positions'] = len(case['positions']) - len(result['samples']) - len(result['rejected_positions'])


def acoustic_tuning(session):
    session.cmd('set s_steamAuditSound sound/weapons/blaster/fire; set s_steamPathing 0; wait 100')
    results = []
    for room, position in (('canyon', '1692 -1692 32'), ('room', '528 200 64')):
        if not move_listener(session, list(map(float, position.split()))):
            results.append(dict(room=room, skipped='Camera did not reach the requested position'))
            continue
        session.cmd('wait 200')
        for name, wet, send, enabled in (('default', .2, 2.5, 1), ('normal-send', .2, 1, 1),
                                        ('half-wet', .1, 2.5, 1), ('disabled', .2, 2.5, 0)):
            session.cmd(f'set s_steamReverb {wet}; set s_steamTransientReverb {send}; set s_steamReflections {enabled}; wait 400')
            result = dict(room=room, treatment=name, wet=wet, transient_send=send, reflections=enabled)
            status = session.cmd('s_steam_status')
            result['listener'] = fields(status, 'steam_listener')[0]
            result['probe'] = fields(session.cmd('s_steam_probe ' + result['listener']['pos'].replace(',', ' ')), 'steam_probe')[0]
            if result['probe']['solid'] != '0' or result['probe']['hull_solid'] != '0':
                result['skipped'] = 'Listener or test hull is solid'
            else:
                result['capture'] = session.capture(4, wet=True, play='s_steam_emit sound/weapons/blaster/fire.wav')
                result['status'] = fields(session.cmd('s_steam_status'), 'steam_audio')
            results.append(result)
    return results


def summarize(report):
    ranked, neighbors, warnings = [], [], []
    for map_record in report['maps']:
        for case in map_record['cases']:
            identity = dict(map=map_record['map'], entity=case.get('entity'), sound=case.get('sound'))
            valid = []
            if case.get('stale_sound_candidate'):
                warnings.append(dict(identity, kind='stale-sound-candidate', capture=case['off_capture']['path']))
            for sample in case.get('samples', []):
                for mode, data in sample['modes'].items():
                    for kind, detected in (('clipping', data['capture']['clipped_samples']), ('duplicate-loops', data['duplicate_loops'])):
                        if detected:
                            warnings.append(dict(identity, kind=kind, label=sample['label'], mode=mode, count=detected))
                if sample.get('gain_db') is None or 'invalid' in sample:
                    continue
                ranked.append(dict(identity, label=sample['label'], gain_db=sample['gain_db'],
                                   strong_legacy=sample['strong_legacy'], loss_candidate=sample['loss_candidate'],
                                   captures={k: v['capture']['path'] for k, v in sample['modes'].items()}))
                if sample['strong_legacy']:
                    valid.append(sample)
            for a, b in combinations(valid, 2):
                distance = math.dist(*(tuple(map(float, s['modes']['steam']['listener']['pos'].split(','))) for s in (a, b)))
                if distance <= 128:
                    neighbors.append(dict(identity, listeners=[a['label'], b['label']], distance=distance,
                                          gain_change_db=b['gain_db'] - a['gain_db']))
    report['ranked_samples'] = sorted(ranked, key=lambda s: s['gain_db'])
    report['neighbor_changes'] = sorted(neighbors, key=lambda s: -abs(s['gain_change_db']))
    report['warnings'] = warnings


def main():
    signal.signal(signal.SIGINT, signal.default_int_handler)
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(128 + signum))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--package', type=Path, default=ROOT / 'build/ready')
    parser.add_argument('--map', action='append')
    parser.add_argument('--alarm', action='store_true')
    parser.add_argument('--acoustic-tuning', action='store_true', help='Capture wet blaster responses in two Kejim rooms.')
    parser.add_argument('--matched', action='store_true')
    parser.add_argument('--bake', action='store_true')
    parser.add_argument('--freeze-scripts', action='store_true', help='Hold the loaded script state and disable cinematic cameras.')
    parser.add_argument('--emitters', type=int, default=1)
    parser.add_argument('--entity', type=int, action='append', help='Test only these static entity indices.')
    parser.add_argument('--listeners', type=int, default=2)
    parser.add_argument('--listener', action='append', help='Test only these listener labels.')
    parser.add_argument('--transmission', type=float, default=0.12)
    parser.add_argument('--seconds', type=int, choices=range(1, 11), default=3)
    parser.add_argument('--strong-dbfs', type=float, default=-35)
    parser.add_argument('--loss-db', type=float, default=3)
    args = parser.parse_args()
    if args.emitters < 1 or args.listeners < 1:
        parser.error('Emitter and listener limits must be positive')
    if not math.isfinite(args.strong_dbfs) or not math.isfinite(args.loss_db) or args.loss_db < 0:
        parser.error('Use finite thresholds and a non-negative loss limit')
    if not 0 <= args.transmission <= 1:
        parser.error('Transmission must be between zero and one')
    SETTINGS['s_steamTransmission'] = args.transmission
    inventory = json.loads(args.inventory.read_text())
    if (args.alarm or args.acoustic_tuning) and inventory['campaign'] != 'jo':
        parser.error('The alarm and acoustic fixtures require JO')
    package = args.package.resolve()
    run = Path(tempfile.mkdtemp(prefix='measure.', dir=ROOT / 'build/audio-audit'))
    report = dict(campaign=inventory['campaign'], package=str(package), settings=SETTINGS, modes=MODES,
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  package_manifest_sha256=hashlib.sha256((package / 'source-manifest.txt').read_bytes()).hexdigest(),
                  inventory=str(args.inventory.resolve()), inventory_sha256=hashlib.sha256(args.inventory.read_bytes()).hexdigest(),
                  command=sys.argv, bake=args.bake, script_freeze=args.freeze_scripts,
                  audio_driver='dummy', strong_dbfs=args.strong_dbfs,
                  loss_db=args.loss_db, run_finished=False, maps=[], limits=[
                      'Sparse static speaker and global-bed samples, not full campaign coverage.',
                      'Sound-name isolation can include more than one emitter and changes channel pressure and reflection priority.',
                      'Sources run in the loaded map state. Script branches and gameplay progression are not replayed.',
                      'PCM uses steady loops. First signal and tail energy do not measure event onset or reverb decay.',
                      'Thresholds identify candidates. They are not acceptance rules.',
                      'Collision checks do not establish navigation reachability. Desktop listening is required.'])
    print(f'Audio measurements: {run}', flush=True)
    for map_record in inventory['maps']:
        name = Path(map_record['map']).stem
        if args.map and name not in args.map or (args.alarm or args.acoustic_tuning) and name != 'kejim_post':
            continue
        cases = emitter_cases(map_record)
        if args.entity:
            cases = [c for c in cases if c['entity'] in args.entity]
        if args.alarm:
            cases = [{**c, 'positions': ALARM_POSITIONS} for c in cases if c.get('target') == 'defense_alarm_sound']
        result = dict(map=name, eligible_emitters=len(cases), unsampled_emitters=max(0, len(cases) - args.emitters), cases=[])
        report['maps'].append(result)
        folder = run / name
        folder.mkdir()
        try:
            if cases or args.acoustic_tuning:
                with game(package, inventory['campaign'], name, folder, args.freeze_scripts) as session:
                    result['runtime_search_path'] = session.cmd('path')
                    result['runtime_player_state'] = session.cmd('campaign_status')
                    result['runtime_mix_settings'] = session.cmd('s_volume; s_volumeVoice; s_musicvolume; s_khz; s_separation; s_steamReverb; s_steamTransientReverb; s_steamTransmission; s_steamLimiter')
                    if args.bake:
                        session.cmd('s_steam_bake')
                    if args.acoustic_tuning:
                        result['acoustic_tuning'] = acoustic_tuning(session)
                    else:
                        for case in cases[:args.emitters]:
                            measured = {}
                            result['cases'].append(measured)
                            measure(session, case, args, measured)
            else:
                result['skipped'] = 'No supported static loop speaker or global bed'
        except (RuntimeError, TimeoutError, ValueError, IndexError, OSError, EOFError, wave.Error) as error:
            result['error'] = f'{type(error).__name__}: {error}'
        (run / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        valid = sum('invalid' not in s for c in result['cases'] for s in c.get('samples', []))
        print(name, result.get('error', result.get('skipped', f'{len(result["cases"])} cases, {valid} valid samples')), flush=True)
    summarize(report)
    report['run_finished'] = True
    (run / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    if any('error' in m for m in report['maps']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
