"""Audit each completed frozen teacher mode without racing a CSV still being written."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,subprocess,time

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--datasets',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);pending=set(range(6));running={};done=[]
    summary={'binary_sha256':hashlib.sha256(a.exe.read_bytes()).hexdigest(),'runs':done}
    def run(mode):
        for render,name in [(0,'features'),(1,'render')]:
            report=a.output/f'mode-{mode}-{name}.json';log=a.output/f'mode-{mode}-{name}.log'
            with log.open('w',encoding='utf-8') as stream:
                command=[str(a.exe),'--datasets',str(a.datasets),'--mode',str(mode),'--render',str(render),'--output',str(report)]
                result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,check=False)
            if result.returncode!=0:return {'mode':mode,'passed':False,'failed_stage':name,'exit_code':result.returncode,'log':str(log)}
        return {'mode':mode,'passed':True}
    start=time.perf_counter()
    with ThreadPoolExecutor(max_workers=2) as pool:
        while pending or running:
            try:
                manifest=json.loads((a.datasets/'manifest.json').read_text(encoding='utf-8'))
                ready={row['mode'] for row in manifest['runs'] if row['passed']}
            except (OSError,json.JSONDecodeError):ready=set()
            for mode in sorted(pending & ready):
                if len(running)>=2:break
                running[pool.submit(run,mode)]=mode;pending.remove(mode);print('AUDIT START',mode,flush=True)
            for future in list(running):
                if future.done():
                    result=future.result();done.append(result);del running[future]
                    summary['elapsed_seconds']=time.perf_counter()-start
                    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)
            if pending or running:time.sleep(2)
    summary['passed']=len(done)==6 and all(row['passed'] for row in done)
    summary['elapsed_seconds']=time.perf_counter()-start
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    return 0 if summary['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
