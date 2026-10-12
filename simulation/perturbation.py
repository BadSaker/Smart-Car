"""Experiment perturbations follow simulated time, independent of UI refresh."""
import secrets
import numpy as np

PRESETS={1:(.005,1.,3.,1.),2:(.010,2.,8.,2.),3:(.020,4.,15.,4.)}
KEYS=('start_jitter','heading_jitter','brightness_jitter','noise')

def preset(p):
    if p['disturbance_level'] in PRESETS:
        p.update(zip(KEYS,PRESETS[p['disturbance_level']]))
    return p

class Perturbation:
    def __init__(self,p):
        self.enabled=p['disturbance_level']!=0
        if p['seed_auto']:p['seed']=secrets.randbelow(2147483648)
        self.seed=int(p['seed'])
        start_rng=np.random.default_rng(np.random.SeedSequence([self.seed,1]))
        self.offset=float(start_rng.uniform(-p['start_jitter'],p['start_jitter'])) if self.enabled else 0.
        self.heading=float(start_rng.uniform(-p['heading_jitter'],p['heading_jitter'])) if self.enabled else 0.
        self.amplitude=p['brightness_jitter'] if self.enabled else 0.
        self.rng=np.random.default_rng(np.random.SeedSequence([self.seed,2]));self.anchors=[]
    def brightness(self,now_ms):
        if not self.amplitude:return 0.
        seconds=now_ms/1000;index=int(seconds)
        while len(self.anchors)<=index+1:self.anchors.append(float(self.rng.uniform(-self.amplitude,self.amplitude)))
        phase=seconds-index;phase=phase*phase*(3-2*phase)
        return self.anchors[index]*(1-phase)+self.anchors[index+1]*phase
    def record(self):
        return {'seed':self.seed,'enabled':self.enabled,'start_offset_m':self.offset,'start_heading_deg':self.heading}
