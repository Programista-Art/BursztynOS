#!/usr/bin/env python3
"""One native Ring 3 PCM session, without persistent disks."""
import argparse
import array
import wave
import json
from pathlib import Path
import re
import sys
import test_hda_pcm_qemu as capture

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",required=True,type=Path)
    parser.add_argument("--analyze",action="store_true")
    args=parser.parse_args()
    out=args.out.resolve()
    capture.PHASES=("BOTH","GAIN_A_25","STREAM_MUTE","STREAM_RESTORE","MASTER_MUTE",
                    "MASTER_RESTORE","ONLY_B","SILENCE","DONE")
    args.iso=capture.ROOT/"BursztynOS.iso"
    args.ovmf=Path("/usr/share/ovmf/OVMF.fd")
    args.timeout=150
    args.test_token="hda-ring3"
    if not args.analyze:
        capture.capture(out,args)
    timings=json.loads((out/"phase-markers.json").read_text())
    sustained,windows=capture.continuity(out/"mixed.wav",timings)
    # Choose stable component levels separately from full-phase continuity.
    # A missing source can leave a nonzero sum and pass a zero-run filter.
    with wave.open(str(out/"mixed.wav")) as wav:
        rate=wav.getframerate()
        samples=array.array("h",wav.readframes(wav.getnframes()))
    if sys.byteorder!="little":
        samples.byteswap()
    left=samples[::2]
    for phase,lo,hi in capture.phase_sample_ranges(timings,rate,len(left)):
        width=rate//2
        for start in range(lo,hi-width+1,rate//20):
            chunk=left[start:start+width]
            if capture.near_zero_intervals(chunk,start,rate) and phase!="master_mute":
                continue
            levels=[[capture.amplitude(chunk[i:i+rate//10],hz,rate)
                     for i in range(0,width,rate//10)] for hz in (440,660)]
            if all(max(v)-min(v)<=max(5,.01*max(v)) for v in levels):
                windows[phase]=(start/rate,(start+width)/rate)
                break
    result=capture.analyze(out/"mixed.wav",windows)
    result["continuity"]=sustained
    evidence=(out/"serial.log").read_text()
    result["guest"]={key:int(value) for key,value in re.findall(r"\[HDA-PCM\] R3_(\w+)=(\d+)",evidence)}
    verdict="PASS"
    try:
        assert result["guest"]["OPENS"]==2, "Two Ring 3 streams did not open"
        assert result["guest"]["CLEANED"]>=1, "Terminated producer was not cleaned up"
        w=result["windows"]
        a=w["both"]["amplitude_440"]
        b=w["both"]["amplitude_660"]
        assert min(a,b)>100 and result["dominant_leading_hz"]==[440,660]
        for phase in ("gain_a_25","stream_restore","master_restore"):
            assert .20 < w[phase]["amplitude_440"]/a < .30, phase
            assert .85 < w[phase]["amplitude_660"]/b < 1.15, phase
        assert sustained["master_mute"]["peak"]<=1, "master mute"
        assert w["only_b"]["amplitude_440"]<a*.02
        assert .85 < w["only_b"]["amplitude_660"]/b < 1.15
        assert w["stream_mute"]["amplitude_440"]<w["both"]["amplitude_440"]*.02
        assert .85 < w["stream_mute"]["amplitude_660"]/w["both"]["amplitude_660"] < 1.15
        assert .20 < w["stream_restore"]["amplitude_440"]/w["both"]["amplitude_440"] < .30
    except AssertionError as e:
        verdict=f"FAIL {e}"
    result["signals"]=verdict
    mute_middle=sum(windows["master_mute"])/2
    intervals=result["near_zero_intervals_between_signals_s_and_ms"]
    unintended=[(start,ms) for start,ms in intervals if not start<=mute_middle<=start+ms/1000]
    result["unintended_silence"]=unintended
    result["unintended_silence_ms"]=sum(ms for _,ms in unintended)
    result["continuous_playback"]=(
        result["guest"]["RECOVERIES"]==0 and result["guest"]["STARVED"]==0 and
        all(x["longest_near_zero_ms"]<2 and x["jumps_over_1000"]==0
            for name,x in sustained.items() if name!="master_mute"))
    (out/"measurements.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result,indent=2))
    return int(verdict!="PASS" or not result["continuous_playback"])
if __name__=="__main__":
    sys.exit(main())
