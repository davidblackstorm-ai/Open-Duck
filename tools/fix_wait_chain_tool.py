from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X\tools\inspect_wait_chains.py');s=p.read_text().replace('import psutil','''class Entry(c.Structure):_fields_=[('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('pid',w.DWORD),('basePriority',w.LONG),('deltaPriority',w.LONG),('flags',w.DWORD)]
k=c.WinDLL('kernel32',use_last_error=True);k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=c.c_void_p;k.Thread32First.argtypes=[c.c_void_p,c.POINTER(Entry)];k.Thread32Next.argtypes=[c.c_void_p,c.POINTER(Entry)];k.CloseHandle.argtypes=[c.c_void_p]
snapshot=k.CreateToolhelp32Snapshot(4,0);entry=Entry();entry.size=c.sizeof(Entry);thread_ids=[];ok=k.Thread32First(snapshot,c.byref(entry))
while ok:
 if entry.pid==23720:thread_ids.append(entry.tid)
 ok=k.Thread32Next(snapshot,c.byref(entry))
k.CloseHandle(snapshot)''')
a=s.index("for p in psutil.process_iter");b=s.index('   count=w.DWORD',a);s=s[:a]+'for tid in thread_ids:\n'+''.join(line[2:]+'\n' for line in s[b:s.index('a.CloseThreadWaitChainSession(h)',b)].splitlines())+s[s.index('a.CloseThreadWaitChainSession(h)',b):];s=s.replace('thread.id','tid');p.write_text(s)
