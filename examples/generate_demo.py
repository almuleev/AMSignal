from pathlib import Path
import math
import random

out = Path(__file__).resolve().with_name('demo-signals.txt')
fs = 1024
rng = random.Random(1729)
tones = [(12, .14), (24, .07), (39, .10), (48, .05), (61, .08), (88, .09),
         (103, .06), (117, .07), (136, .05), (151, .06), (173, .04), (192, .035)]
phases = [rng.uniform(-math.pi, math.pi) for _ in tones]
drive = [sum(a*math.sin(2*math.pi*f*n/fs+p) for (f, a), p in zip(tones, phases))
         + .45*rng.uniform(-1, 1) for n in range(36864)]
sensor_rng = random.Random(1730)
ref_a = [x + .01*sensor_rng.uniform(-1, 1) for x in drive]
ref_b = [x + .01*sensor_rng.uniform(-1, 1) for x in drive]
# Both responses are driven by the actual arithmetic mean used as the FRF reference.
drive = [(a+b)/2 for a, b in zip(ref_a, ref_b)]

def response(f0, damping):
    w = 2*math.pi*f0
    c = 2*fs
    den = c*c + 2*damping*w*c + w*w
    b = [w*w/den, 2*w*w/den, w*w/den]
    a = [2*(w*w-c*c)/den, (c*c-2*damping*w*c+w*w)/den]
    y = []
    for n, x in enumerate(drive):
        y.append(b[0]*x + (b[1]*drive[n-1]-a[0]*y[n-1] if n else 0)
                 + (b[2]*drive[n-2]-a[1]*y[n-2] if n > 1 else 0))
    return y

ra, rb = response(32, 0.045), response(75, 0.065)
with out.open('w', encoding='utf-8', newline='\n') as f:
    f.write('X_Value\tReference A [V]\tReference B [V]\tResponse A [V]\tResponse B [V]\n')
    for n in range(32768):
        i = n + 4096
        f.write(f'{n/fs:.10f}\t{ref_a[i]:.10f}\t{ref_b[i]:.10f}\t{ra[i]:.10f}\t{rb[i]:.10f}\n')
print(out)
