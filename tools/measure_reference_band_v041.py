"""Measure public BAND response with a controlled impulse, without audio devices."""
from pathlib import Path
import csv,json,subprocess,sys,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'work/pythonlibs'))
import numpy as np
base=root/'work/revision041/Reference'
exe=root/'build/ReferenceFunctionalProbe_artefacts/Release/ReferenceFunctionalProbe.exe'
module=Path(r'C:\Program Files\Common Files\VST3\Kickstart 2.vst3')
rows=[]
for norm in [0,.5,1]:
    for solo in [True,False]:
        name=f'Band-{20*2**(8*norm):g}-'+('Solo' if solo else 'Full')
        out=base/name;out.mkdir(exist_ok=True)
        tree=ET.parse(base/'DefaultState/reference-settings.private.xml')
        values={'bandOn':'1','bandSolo':str(int(solo)),'bandFrequency':str(norm),'mix':'0'}
        for child in tree.getroot():
            if child.get('id') in values:child.set('value',values[child.get('id')])
        settings=out/'settings.private.xml';tree.write(settings,encoding='utf-8',xml_declaration=True)
        config={'sampleRate':48000,'bpm':120,'warmupSamples':4096,'samples':32768,'signal':'impulse','impulseSample':4608,'level':.15,'settingsXml':str(settings)}
        cfg=out/'config.json';cfg.write_text(json.dumps(config,indent=2))
        result=subprocess.run([str(exe),str(module),str(out),str(cfg)],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
        (out/'probe.log').write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(name+result.stdout+result.stderr)
        data=list(csv.DictReader((out/'reference-output.csv').open()))
        y=np.array([float(r['output'])/.15000000596046448 for r in data])[512:]
        fits=[]
        for order in [1,2,3,4,6,8]:
            n=np.arange(order+1,min(len(y),max(order+50,int(48000/(20*2**(8*norm)))*3)))
            matrix=np.array([y[n-i] for i in range(1,order+1)]).T
            coeff=np.linalg.lstsq(matrix,-y[n],rcond=None)[0]
            residual=y[n]+matrix@coeff
            b=[float(y[k]+sum(coeff[j]*y[k-j-1] for j in range(min(k,order)))) for k in range(order+1)]
            fits.append({'order':order,'a':[1]+coeff.tolist(),'b':b,'maxResidual':float(np.max(np.abs(residual)))})
        transform=np.fft.rfft(y,n=65536)
        freq=np.fft.rfftfreq(65536,1/48000)
        response=[]
        for f in [5,20,40,80,160,320,640,1280,2560,5120,10240,20000]:
            k=int(np.argmin(abs(freq-f)));response.append({'Hz':float(freq[k]),'dB':float(20*np.log10(max(abs(transform[k]),1e-15))),'phase':float(np.angle(transform[k]))})
        meta=json.loads((out/'reference-metadata.json').read_text())
        rows.append({'case':name,'latencySamples':meta['latencySamples'],'firstNonzero':int(np.argmax(abs(y)>1e-12)),'impulsePeakIndex':int(np.argmax(abs(y))),'dcSum':float(y.sum()),'fits':fits,'response':response})
        print(name,'latency',meta['latencySamples'],'first',rows[-1]['firstNonzero'],'fit2/4',fits[1]['maxResidual'],fits[3]['maxResidual'],flush=True)
receipt={'method':'Actual installed VST3; public preset settings; MIX0; input impulse .15 at sample4608; 48kHz; no audio device. Band solo isolates low path, full path measures crossover reconstruction.','complete':True,'cases':rows}
(base/'band-measurement.json').write_text(json.dumps(receipt,indent=2))
