#!/usr/bin/env python3
"""Sobe o QEMU sem janela, manda comandos de mouse/teclado e tira prints."""
import subprocess, sys, time, os
steps = sys.argv[1:]   # ex: shot:a  key:h  move:100,50  btn:1  wait:1
p = subprocess.Popen(["qemu-system-i386","-drive","file=nimbus.img,format=raw","-m","128",
    "-display","none","-monitor","stdio","-serial","none","-no-reboot"],
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
def cmd(c): p.stdin.write(c+"\n"); p.stdin.flush()
time.sleep(4)
for s in steps:
    k, _, v = s.partition(":")
    if k == "shot":
        cmd(f"screendump /tmp/{v}.ppm"); time.sleep(1.5)
        subprocess.run(["convert", f"/tmp/{v}.ppm", f"/tmp/{v}.png"])
    elif k == "key":
        for ch in v.split(","): cmd(f"sendkey {ch}"); time.sleep(0.15)
    elif k == "move":
        x, y = v.split(","); cmd(f"mouse_move {x} {y}"); time.sleep(0.4)
    elif k == "btn": cmd(f"mouse_button {v}"); time.sleep(0.4)
    elif k == "wait": time.sleep(float(v))
cmd("quit"); time.sleep(0.5); p.terminate()
print(p.stderr.read()[:500])
