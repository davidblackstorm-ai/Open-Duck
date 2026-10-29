import ctypes as c,json
from ctypes import wintypes as w
class Entry(c.Structure):_fields_=[('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('pid',w.DWORD),('basePriority',w.LONG),('deltaPriority',w.LONG),('flags',w.DWORD)]
k=c.WinDLL('kernel32',use_last_error=True);k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=c.c_void_p;k.Thread32First.argtypes=[c.c_void_p,c.POINTER(Entry)];k.Thread32Next.argtypes=[c.c_void_p,c.POINTER(Entry)];k.CloseHandle.argtypes=[c.c_void_p]
snapshot=k.CreateToolhelp32Snapshot(4,0);entry=Entry();entry.size=c.sizeof(Entry);thread_ids=[];ok=k.Thread32First(snapshot,c.byref(entry))
while ok:
 if entry.pid==23720:thread_ids.append(entry.tid)
 ok=k.Thread32Next(snapshot,c.byref(entry))
k.CloseHandle(snapshot)
class Thread(c.Structure):_fields_=[('pid',w.DWORD),('tid',w.DWORD),('waitMs',w.DWORD),('switches',w.DWORD)]
class Lock(c.Structure):_fields_=[('name',w.WCHAR*128),('timeout',c.c_longlong),('alertable',w.BOOL)]
class Object(c.Union):_fields_=[('thread',Thread),('lock',Lock)]
class Node(c.Structure):_fields_=[('kind',c.c_int),('status',c.c_int),('object',Object)]
a=c.WinDLL('advapi32',use_last_error=True);a.OpenThreadWaitChainSession.argtypes=[w.DWORD,c.c_void_p];a.OpenThreadWaitChainSession.restype=c.c_void_p;a.GetThreadWaitChain.argtypes=[c.c_void_p,c.c_void_p,w.DWORD,w.DWORD,c.POINTER(w.DWORD),c.POINTER(Node),c.POINTER(w.BOOL)];a.GetThreadWaitChain.restype=w.BOOL;a.CloseThreadWaitChainSession.argtypes=[c.c_void_p]
h=a.OpenThreadWaitChainSession(0,None)
result=[]
for tid in thread_ids:
 count=w.DWORD(16);nodes=(Node*16)();cycle=w.BOOL()
 ok=a.GetThreadWaitChain(h,None,0,tid,c.byref(count),nodes,c.byref(cycle))
 if ok and count.value>1:
  row={'tid':tid,'cycle':bool(cycle.value),'chain':[]}
  for n in nodes[:count.value]:row['chain'].append({'kind':n.kind,'status':n.status,'info':{'pid':n.object.thread.pid,'tid':n.object.thread.tid} if n.kind==8 else n.object.lock.name})
  result.append(row)
a.CloseThreadWaitChainSession(h)
print(json.dumps(result,indent=2))
