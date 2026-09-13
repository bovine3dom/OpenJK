#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package=$(realpath -- "${1:-$root/build/ready}")
mkdir -p "$root/build/smoke"
suite=$(mktemp -d "$root/build/smoke/squad.XXXXXXXX")
for level in 0 3 4; do
    OJK_SMOKE_ROOT="$suite/$level" bash "$root/scripts/smoke-sp.sh" "$package" t1_sour \
        +set d_npcai "$level" +exec squad-smoke.cfg
    logs=("$suite/$level"/t1_sour.*/console.log)
    log=${logs[0]}
    grep -q 'OJK_SQUAD_FIXTURE_COMPLETE' "$log" || {
        printf 'FAIL: fixture did not finish. See %s\n' "$log" >&2
        exit 1
    }
    if [[ "$level" == 0 ]]; then
        if grep -q 'squad event=' "$log"; then
            printf 'FAIL: diagnostics appeared at level 0\n' >&2
            exit 1
        fi
    else
        for event in group_insert voice_dispatch; do
            grep -q "squad event=$event " "$log" || {
                printf 'FAIL: missing %s at level %s. See %s\n' "$event" "$level" "$log" >&2
                exit 1
            }
        done
        if [[ "$level" == 3 ]]; then
            if grep -Eq 'squad event=(commander_pass|voice_request|voice_suppress) ' "$log"; then
                printf 'FAIL: DETAIL records appeared at level 3\n' >&2
                exit 1
            fi
        else
            for event in commander_pass cp_request cp_result voice_request; do
                grep -q "squad event=$event " "$log" || {
                    printf 'FAIL: missing %s. See %s\n' "$event" "$log" >&2
                    exit 1
                }
            done
        fi
    fi
done
printf 'PASS: squad trace levels 0, 3, 4. Results: %s\n' "$suite" | tee "$suite/result.txt"
