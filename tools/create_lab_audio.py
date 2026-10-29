from pathlib import Path
import numpy as np, wave, struct
root=(Path(__file__).resolve().parents[1]/'OpenDuck Pro X - TEST LAB Project')/'Samples'/'Imported'
root.mkdir(parents=True,exist_ok=True)
sr=44100;beat=60/145;length=int(16*beat*sr);t=np.arange(length)/sr;kick=np.zeros(length);bass=np.zeros(length)
for i in range(16):
 start=int(i*beat*sr);n=min(length-start,int(.32*sr));u=np.arange(n)/sr
 kick[start:start+n]+=.09*np.sin(2*np.pi*(48*u+75*.02*(1-np.exp(-u/.02))))*np.exp(-u/.085)
 bass[start:start+n]+=.04*(np.sin(2*np.pi*65.406*u)+.25*np.sin(2*np.pi*130.812*u))*np.minimum(1,u/.008)*np.exp(-u/.28)
for name,a in [('LAB-Kick-145.wav',kick),('LAB-Bass-145.wav',bass)]:
 with wave.open(str(root/name),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(sr);f.writeframes((np.column_stack([a,a])*32767).astype('<i2').tobytes())
def vlq(value):
 data=[value&127];value>>=7
 while value:data.insert(0,(value&127)|128);value>>=7
 return bytes(data)
track=b'\x00\xff\x51\x03'+int(60_000_000/145).to_bytes(3,'big')
for i in range(16):track+=vlq(0 if i==0 else 360)+b'\x90\x3c\x64'+vlq(120)+b'\x80\x3c\x00'
track+=b'\x00\xff\x2f\x00'
(root/'LAB-Trigger-145.mid').write_bytes(b'MThd'+struct.pack('>IHHH',6,0,1,480)+b'MTrk'+struct.pack('>I',len(track))+track)
print(root)
