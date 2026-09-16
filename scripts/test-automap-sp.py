#!/usr/bin/env python3
"""Test the datapad automap with real UI input in a headless window."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-rend2", "rdsp-vanilla"), default="rdsp-rend2")
    parser.add_argument("--campaign", choices=("ja", "jo"), default="ja")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 960x720x24", sys.executable, __file__,
                                "--inside", "--package", str(args.package), "--renderer", args.renderer, "--campaign", args.campaign])
    run = Path(tempfile.mkdtemp(prefix="automap.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(cl_renderer=args.renderer,r_mode=-1,r_customwidth=960,r_customheight=720,r_fullscreen=0,
                    in_nograb=1,s_initsound=0,developer=1,com_maxfps=60,r_ignoreGLErrors=int(args.renderer=="rdsp-vanilla"))
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k,v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    env = dict(os.environ,OJK_PROFILE=str(home),LIBGL_ALWAYS_SOFTWARE="1",LP_NUM_THREADS="1",SDL_AUDIODRIVER="dummy",
               OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS",str(root / "GameData_JO")))
    log = run / "console.log"
    records = {}
    print(f"Automap results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(["bash",str(args.package.resolve() / "launch-sp.sh"),str(root / "GameData"),
                                    "--campaign",args.campaign,"+devmap","kejim_base" if args.campaign=="jo" else "t2_wedge",
                                    "+wait","150","+echo","MAP_READY"],env=env,stdin=subprocess.PIPE,stdout=stream,
                                   stderr=subprocess.STDOUT,text=True,start_new_session=True)
        assert process.stdin
        stdin=process.stdin
        serial=0
        def text(): return re.sub(r"\^[0-9]","",log.read_text(errors="replace"))
        def wait(marker,start=0):
            deadline=time.monotonic()+300
            while time.monotonic()<deadline:
                output=text()[start:]
                if marker in output: return output
                if process.poll() is not None: raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")
        def cmd(value):
            nonlocal serial
            serial+=1
            marker=f"MAP_CMD_{serial}_DONE"
            start=len(text())
            line=f"{value}; wait 10; echo {marker}\n"
            assert len(line)<256
            stdin.write(line); stdin.flush()
            return wait(marker,start)
        def state(name):
            result=cmd("automap_status")
            match=re.search(r"automap map=[^\n]+",result)
            assert match,result
            value=dict(word.split("=",1) for word in match[0].split()[1:])
            records[name]=value
            return value
        def capture(name):
            cmd(f"screenshot_png {name}; wait 20")
            return subprocess.check_output(["ffmpeg","-v","error","-i",str(profile/"screenshots"/f"{name}.png"),
                                            "-frames:v","1","-pix_fmt","rgb24","-f","rawvideo","-"])
        def key(value):
            subprocess.run(["xdotool","key","--clearmodifiers",value],check=True)
            cmd("wait 15")
        def move(x,y):
            subprocess.run(["xdotool","mousemove","--window",window,"958","718"],check=True);cmd("wait 5")
            for _ in range(2):
                subprocess.run(["xdotool","mousemove","--window",window,"1","1"],check=True);cmd("wait 5")
            subprocess.run(["xdotool","mousemove_relative","--",str(x),str(y)],check=True);cmd("wait 10")
        def click(x,y,item):
            move(x,y)
            assert f"item: {item};" in cmd("ui_report")
            subprocess.run(["xdotool","click","1"],check=True);cmd("wait 15")
        try:
            wait("MAP_READY")
            window=subprocess.check_output(["xdotool","search","--onlyvisible","--pid",str(process.pid)],text=True).splitlines()[-1]
            subprocess.run(["xdotool","windowfocus","--sync",window],check=True)
            cmd("exitview; wait 200; helpusobi 1; god; notarget; d_npcfreeze 1; con_notifytime -1")
            if args.campaign=="ja": cmd("noclip; setviewpos 2688 640 -60 315; wait 60")
            cmd("save automap_test; datapad")
            assert "UI focus: datapad" in cmd("ui_report")
            click(170,432,"map_tab")
            assert "UI focus: datapadMapMenu;" in cmd("ui_report")
            initial=state("isometric")
            assert initial["valid"]=="1" and int(initial["triangles"])>100 and int(initial["drawn"])>10,initial
            assert initial["drawn"]==initial["triangles"],initial
            assert int(initial["lifts"])>0,initial
            iso=capture("isometric")
            before=cmd("campaign_status")
            move(310,220)
            subprocess.run(["xdotool","mousedown","1"],check=True)
            subprocess.run(["xdotool","mousemove_relative","--","40","20"],check=True);cmd("wait 10")
            subprocess.run(["xdotool","mouseup","1"],check=True);cmd("wait 10")
            dragged=state("drag_pan");assert dragged["centre"]!=initial["centre"]
            subprocess.run(["xdotool","mousemove_relative","--","20","0"],check=True);cmd("wait 10")
            assert state("released_pan")["centre"]==dragged["centre"]
            after=cmd("campaign_status")
            beforeOrigin=re.search(r"origin=([^\n]+)",before);afterOrigin=re.search(r"origin=([^\n]+)",after)
            assert beforeOrigin and afterOrigin and beforeOrigin[0]==afterOrigin[0]
            key("Home")
            key("t");top=state("top")
            assert top["tilt"]=="0.0"
            flat=capture("top")
            assert sum(abs(a-b) for a,b in zip(iso,flat))/len(iso)>1,"Tilt did not change the map"
            before=cmd("campaign_status")
            key("Right");pan=state("pan")
            after=cmd("campaign_status")
            assert pan["centre"]!=top["centre"]
            oldOrigin=re.search(r"origin=([^\n]+)",before);newOrigin=re.search(r"origin=([^\n]+)",after)
            assert oldOrigin and newOrigin and oldOrigin[0]==newOrigin[0],"Map input moved the player"
            click(289,48,"map_tilt")
            assert state("button_tilt")["tilt"]=="55.0"
            key("Home");key("equal")
            assert float(state("zoom")["span"])<float(initial["span"])
            if args.campaign=="jo":
                assert int(initial["markers"])==4,initial
                key("c");assert int(state("control")["shown"])>0
                image=capture("control")
                gold=sum(image[i]>180 and image[i+1]>120 and image[i+2]<110
                         for y in range(300,365) for x in range(445,515) for i in [(y*960+x)*3])
                assert gold>12,"Control marker was not drawn"
            key("l");lift=state("lift")
            assert int(lift["lifts_shown"])>0
            image=capture("lift")
            assert re.search(r"automap lift=\d+ stops=[2-8]",text()),"No known lift travel"
            click(590,48,"map_explode")
            navstate=cmd("automap_status")
            match=re.search(r"exploded=1 nav_polygons=(\d+) floors=(\d+) connections=(\d+)",navstate)
            assert match and int(match[1])>10 and int(match[2])>1,navstate
            records["navigation"]=dict(polygons=int(match[1]),floors=int(match[2]),connections=int(match[3]))
            parts=re.search(r"bsp_parts=(\d+) bsp_edges=(\d+)",navstate)
            assert parts and int(parts[1])>=int(initial["triangles"]) and int(parts[2])>=int(initial["edges"]),navstate
            assert int(state("exploded_bsp")["drawn"])==int(parts[1])
            bounds=[tuple(map(float,m.split(","))) for m in re.findall(r"bounds=([\d.,-]+)",navstate)]
            assert len(bounds)==int(match[2])
            for i,a in enumerate(bounds):
                for b in bounds[i+1:]:
                    assert a[2]<=b[0] or b[2]<=a[0] or a[3]<=b[1] or b[3]<=a[1],(a,b)
            expanded=capture("exploded")
            oldcentre=re.search(r"nav_centre=([^\n ]+)",navstate)[1]
            before=cmd("campaign_status")
            move(310,220)
            subprocess.run(["xdotool","mousedown","1"],check=True)
            subprocess.run(["xdotool","mousemove_relative","--","30","15"],check=True);cmd("wait 10")
            subprocess.run(["xdotool","mouseup","1"],check=True);cmd("wait 10")
            newcentre=re.search(r"nav_centre=([^\n ]+)",cmd("automap_status"))[1]
            assert newcentre!=oldcentre
            assert re.search(r"origin=([^\n]+)",before)[0]==re.search(r"origin=([^\n]+)",cmd("campaign_status"))[0]
            key("Home");key("equal")
            capture("exploded_player")
            key("Prior");key("Next");key("q");key("t")
            assert "exploded=1" in cmd("automap_status")
            capture("exploded_top")
            key("x")
            assert "exploded=0" in cmd("automap_status")
            whole=capture("whole_return")
            assert sum(abs(a-b) for a,b in zip(expanded,whole))/len(whole)>1
            assert state("whole")["drawn"]==initial["triangles"]
            # Other tabs use the same evenly spaced bottom row.
            click(270,432,"weapons");assert "datapadWeaponsMenu" in cmd("ui_report")
            click(170,432,"map_tab");assert "datapadMapMenu" in cmd("ui_report")
            key("Tab")
            assert "UI focus: datapadMapMenu;" not in cmd("ui_report")
            cmd("load automap_test; wait 150; datapad")
            key("F4"); assert state("loaded")["valid"]=="1"
            assert "exploded=0 nav_polygons=0 floors=0" in cmd("automap_status")
            key("Escape");cmd("vid_restart; wait 150; datapad")
            key("F4");assert state("restarted")["valid"]=="1"
            capture("restarted")
            move(310,220);subprocess.run(["xdotool","mousedown","1"],check=True)
            key("Escape")
            subprocess.run(["xdotool","mouseup","1"],check=True)
            cmd("datapad");key("F4")
            reopened=state("reopened")
            subprocess.run(["xdotool","mousemove_relative","--","30","0"],check=True);cmd("wait 10")
            assert state("cancelled_drag")["centre"]==reopened["centre"]
            key("Escape")
            stdin.write("quit\n");stdin.flush()
            assert process.wait(timeout=30)==0
        finally:
            if process.poll() is None:
                os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=15)
            (run/"results.json").write_text(json.dumps(records,indent=2))
    assert not re.search(r"ERROR:|Error:|Unknown command|GL_INVALID_|trying to load fallback",text()),log
    print("PASS: map tab, geometry, tilt, height, pan, buttons, close keys, and lifecycle")
    return 0

if __name__=="__main__": sys.exit(main())
