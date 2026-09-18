#!/usr/bin/env python3
"""Apply the runtime routing table to a static campaign sound inventory."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RULES = ROOT / 'shared/sound/audio_routes.inc'
POLICY = RULES.read_bytes()
ROWS = re.findall(r'AUDIO_(CONTEXT|PREFIX)\(("[^"]*"|\w+), (\w+), "([^"]+)"\)', POLICY.decode())


def classify(asset, context='World'):
    name = asset.lower().replace('\\', '/')
    for kind, key, mode, reason in ROWS:
        if (kind == 'CONTEXT' and context == key) or (kind == 'PREFIX' and name.startswith(key[1:-1])):
            return dict(route=mode.lower(), rule=reason)
    raise ValueError('The routing table has no fallback')


def channel_context(channel):
    if channel in ('CHAN_LOCAL', 'CHAN_LOCAL_SOUND', 'CHAN_VOICE_GLOBAL', 'CHAN_ANNOUNCER', 'CHAN_MUSIC', 'CHAN_AUTO_GLOBAL'):
        return 'Local'
    if channel in ('CHAN_VOICE', 'CHAN_VOICE_ATTEN'):
        return 'Voice'
    return 'Weapon' if channel == 'CHAN_WEAPON' else 'World'


def triage(inventory):
    records = []

    def add(map_name, owner, sound, context, **extra):
        if sound.get('status') == 'off':
            return
        asset = sound.get('asset') or sound.get('reference', '')
        decision = classify(asset, context)
        records.append(dict(map=map_name, owner=owner, asset=asset, context=context,
                            asset_status=sound.get('status'), source=sound.get('source'),
                            review=decision['rule'] == 'unclassified-review' or sound.get('status') != 'resolved',
                            **decision, **extra))

    for map_record in inventory['maps']:
        name = map_record['map']
        for emitter in map_record['sound_candidates']:
            entity = emitter['fields']
            context = 'Mover' if entity.get('model', '').startswith('*') else 'World'
            if emitter.get('kind') in ('global-bed', 'global-bed-control') or emitter.get('channel') == 'global':
                context = 'Local'
            sounds = list(emitter['asset_candidates'].values()) + emitter.get('sound_group', [])
            sounds += (emitter.get('sound_set') or {}).get('waves', [])
            for sound in sounds:
                add(name, f"entity:{emitter['entity']}", sound, context,
                    stage=sound.get('role'), channel=emitter.get('channel'),
                    classname=entity.get('classname'), origin=entity.get('origin'))
        for script_name in map_record['scripts']:
            for sound in inventory['scripts'].get(script_name, {}).get('sounds', []):
                add(name, script_name, sound, channel_context(sound.get('channel')),
                    channel=sound.get('channel'), offset=sound.get('offset'), context_inferred=True)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    records = triage(inventory)
    report = dict(campaign=inventory['campaign'], inventory=str(args.inventory.resolve()),
                  inventory_sha256=hashlib.sha256(args.inventory.read_bytes()).hexdigest(),
                  rules_sha256=hashlib.sha256(POLICY).hexdigest(),
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  counts=dict(Counter(r['route'] for r in records)),
                  review_count=sum(r['review'] for r in records), records=records,
                  limits=['Static proposals, not measured audibility or proof of activation.',
                          'Runtime channels and brush ownership take priority over asset prefixes.',
                          'Script owner context, dynamic names, and code-generated sounds need runtime checks.',
                          'Repeated references retain their map and script context.',
                          'Unclassified sounds retain legacy direct audio and need review.'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(inventory['campaign'], report['counts'], 'review:', report['review_count'])


if __name__ == '__main__':
    main()
