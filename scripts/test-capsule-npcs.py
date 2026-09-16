#!/usr/bin/env python3
"""Check capsule coverage for stock non-humanoid and rigid NPC models."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    parser.add_argument("--walls", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    package = args.package.resolve()
    models = {name: (f"models/players/{name}/model.glm", -103) for name in
              ("protocol", "r2d2", "r5d2", "gonk", "assassin_droid", "hazardtrooper", "rockettrooper",
               "saber_droid", "probe", "sentry", "howler", "rancor", "mutant_rancor", "tauntaun", "atst")}
    models.update(interrogator=("models/players/interrogator/model.glm", -79),
                  wampa=("models/players/wampa/model.glm", -64),
                  mouse=("models/players/mouse/lower.md3", -103),
                  remote=("models/players/remote_sp/lower.md3", -103),
                  seeker=("models/items/remote.md3", -103))
    suite = Path(tempfile.mkdtemp(prefix="capsule-npcs.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, com_maxfps=20, developer=1, s_initsound=0, r_ignoreGLErrors=0,
                    r_debugContext=1, r_ext_multisample=args.msaa, r_g2GpuSkinning=1, r_g2GpuValidate=1)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Controlled capsule fixture.\n")
    commands = ["exec krildor-traverse.cfg", "give all", "wait 80", "weapon 3", "wait 80", "noclip",
                "d_npcfreeze 1", "cg_draw2D 0", "cg_drawGun 0", "cg_thirdPerson 0", "con_notifytime -1",
                "r_ssaoDebug 2", f"r_capsuleShadowWalls {args.walls}", "r_capsuleShadowStrength 1"]
    for name, (model, height) in models.items():
        spawn = "testG2Model" if model.endswith(".glm") else "testmodel"
        commands += [f"echo OJK_CAPS_{name}_BEGIN", f"setviewpos 2688 640 {height} 315", "wait 30",
                     f"{spawn} {model}", "wait 10", "setviewpos 2640 688 -40 315", "wait 30",
                     "r_capsuleShadows 0", "wait 10", f"screenshot_png caps_{name}_off", "wait 2",
                     "r_capsuleShadows 1", "r_capsuleShadowDebug 1", "wait 10", f"screenshot_png caps_{name}_on",
                     "wait 2", f"echo OJK_CAPS_{name}_END"]
        if name == "protocol":
            commands += ["testsurface r_hand root", "r_capsuleShadowDebug 1", "wait 10", "echo OJK_CAPS_HAND"]
    commands += ["testG2Model models/players/rocks/model.glm", "r_capsuleShadowDebug 1", "wait 10", "echo OJK_CAPS_PROP",
                 "echo OJK_CAPS_DONE", "quit"]
    (profile / "capsule-npcs.cfg").write_text("\n".join(commands)+"\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="offscreen",
               EGL_PLATFORM="surfaceless", SDL_AUDIODRIVER="dummy")
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    print(f"NPC capsule results: {suite}", flush=True)
    with (suite / "console.log").open("w") as stream:
        subprocess.run(["timeout", "--kill-after=5s", "900s", "bash", str(package / "launch-sp.sh"),
                        os.environ.get("OJK_ASSETS", str(root / "GameData")), "+devmap", "t2_wedge",
                        "+exec", "capsule-npcs.cfg"], env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    text = (suite / "console.log").read_text(errors="replace")
    if ("OJK_CAPS_DONE" not in text or re.search(r"ERROR:|Unknown command|mismatch|GL_INVALID_|GL_OUT_OF_MEMORY|"
            r"trying to load fallback|llvmpipe|softpipe|OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text)):
        raise RuntimeError("Capsule fixture or hardware renderer failed")
    results = {}
    for name, (model, _) in models.items():
        section = text.split(f"OJK_CAPS_{name}_BEGIN", 1)[-1].split(f"OJK_CAPS_{name}_END", 1)[0]
        counts = re.findall(rf"Capsules: {re.escape(model)} root=\S+ count=(\d+)", section)
        if not counts or not 0 < int(counts[-1]) <= 12:
            raise RuntimeError(f"No bounded capsule set for {name}")
        images = []
        for mode in ("off", "on"):
            image = profile / "screenshots" / f"caps_{name}_{mode}.png"
            images.append(subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                "-frames:v", "1", "-pix_fmt", "gray", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout)
        if any(len(image) != 960*720 for image in images):
            raise RuntimeError(f"Missing capsule capture: {name}")
        darkened = sum(a-b > 2 for a, b in zip(*images))
        results[name] = dict(capsules=int(counts[-1]), darkened_pixels=darkened)
        if name in ("protocol", "r2d2", "r5d2", "gonk", "mouse", "remote", "seeker") and darkened < 20:
            raise RuntimeError(f"No visible droid capsule occlusion: {name}")
    hands = re.findall(r"Capsules: models/players/protocol/model.glm root=r_hand count=(\d+)", text)
    if not hands or not 0 < int(hands[-1]) < results["protocol"]["capsules"]:
        raise RuntimeError("Detached protocol hand retained a full-body proxy")
    if "Capsules: models/players/rocks/model.glm" in text:
        raise RuntimeError("Scenery received an NPC capsule set")
    (suite / "result.json").write_text(json.dumps(results, indent=2)+"\n")
    print(json.dumps(results, indent=2))
    print("PASS: non-humanoid capsules, rigid droids, visible occlusion, and detached-part filtering")


if __name__ == "__main__":
    main()
