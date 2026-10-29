from pathlib import Path
import wave,numpy as np,json
p=Path(__file__).resolve().parents[1]
with wave.open(str(p/'build/visual-qa/audio-stress-48k.wav'),'rb') as f:
 sr=f.getframerate();ch=f.getnchannels();raw=np.frombuffer(f.readframes(f.getnframes()),dtype=np.uint8).reshape(-1,3)
 signed=(raw[:,0].astype(np.int32)|(raw[:,1].astype(np.int32)<<8)|(raw[:,2].astype(np.int32)<<16));signed=np.where(signed>=8388608,signed-16777216,signed)
 audio=(signed/8388608).reshape(-1,ch)[sr*2:,0]
spectrum=np.abs(np.fft.rfft(audio*np.hanning(len(audio))))**2;freq=np.fft.rfftfreq(len(audio),1/sr)
ratio=10*np.log10(np.sum(spectrum[freq>20000])/np.sum(spectrum))
result={'sample_rate':sr,'signal':'1 kHz sine; 80% ducking; step shape; 0.1 ms attack; 5 ms release; free 120 BPM','analysed_seconds':len(audio)/sr,'peak_after_startup':float(np.max(np.abs(audio))),'nonfinite_samples':int(np.sum(~np.isfinite(audio))),'energy_above_20kHz_relative_total_dB':float(ratio),'scope':'This is a measured high-frequency residual for one stress signal, not a universal alias-free guarantee.'}
(p/'build/audio-analysis.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
