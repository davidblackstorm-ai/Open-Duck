"""Six offline MLPs trained ONLY from AssistTeacher actual-native-DSP CSVs.
Python/NumPy are build-time tools, never shipped or invoked by the VST.
No synthetic numeric recipe labels or Python surrogate audio renderer are used.
"""
from pathlib import Path
import argparse, csv, hashlib, json, os, sys
os.environ["OPENBLAS_NUM_THREADS"] = "1"
import numpy as np

FEATURES, H1, H2, CONTROLS, OUTPUTS = 96, 24, 16, 27, 43

def sigmoid(x):
    return 1 / (1 + np.exp(-np.clip(x, -30, 30)))

def run(x, params):
    w1,b1,w2,b2,w3,b3=params
    a=np.tanh(x@w1+b1);b=np.tanh(a@w2+b2);z=b@w3+b3
    p=z.copy();p[:,:CONTROLS]=sigmoid(z[:,:CONTROLS])
    return a,b,p

def loss(pred,y,mask):
    regression=np.mean(np.sum((pred[:,:CONTROLS]-y[:,:CONTROLS])**2*mask,axis=1)/mask.sum())
    z=pred[:,CONTROLS:];z=z-z.max(1,keepdims=True)
    categorical=np.mean(np.log(np.exp(z).sum(1))-np.sum(z*y[:,CONTROLS:],axis=1))
    return float(regression+.15*categorical)

def array(a):
    return "{"+",".join(f"{float(x):.9g}f" if "." in f"{float(x):.9g}" or "e" in f"{float(x):.9g}" else f"{float(x):.1f}f" for x in a.ravel())+"}"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datasets",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--report",type=Path,required=True)
    parser.add_argument("--golden",type=Path,required=True)
    parser.add_argument("--steps",type=int,default=1800)
    parser.add_argument("--seed",type=int,default=43030)
    args=parser.parse_args()
    manifest_path=args.datasets/'manifest.json'
    corpus_manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    if (corpus_manifest.get('pipeline_revision')!='quality-r2' or corpus_manifest.get('teacher_train_only') is not False
        or not all(corpus_manifest.get(k) is True for k in ('passed','teacher_unchanged','sources_unchanged'))
        or corpus_manifest.get('scenes_per_mode')!=144 or corpus_manifest.get('candidates_per_scene')!=8 or corpus_manifest.get('seed')!=42030):
        raise ValueError('A completed, frozen quality-r2 corpus is required; calibration and historical R1 data cannot train release R2 models')
    manifest_runs={r['mode']:r for r in corpus_manifest['runs']}
    if set(manifest_runs)!=set(range(6)):raise ValueError('Six distinct teacher runs are required')
    models=[];reports=[];golden=[];sources=[]
    for mode in range(6):
        path=args.datasets/f"mode-{mode}.csv"
        if hashlib.sha256(path.read_bytes()).hexdigest()!=manifest_runs[mode]['csv_sha256']:
            raise ValueError('Teacher CSV differs from frozen corpus manifest')
        raw=path.read_bytes();sources.append({"mode":mode,"path":f"{args.datasets.name}/mode-{mode}.csv","original_path":str(path),"sha256":hashlib.sha256(raw).hexdigest()})
        # Read named columns so provenance fields can be added without shifting labels/metrics.
        with path.open(newline="",encoding="utf-8-sig") as stream: records=list(csv.DictReader(stream))
        if len(records)!=144 or not all('eligibleCandidates' in r and 'rejectedCandidates' in r for r in records):
            raise ValueError('The complete R2 guarded teacher schema is required')
        columns=["mode","scene","split",*[f"f{i}" for i in range(FEATURES)],*[f"y{i}" for i in range(OUTPUTS)],"baselineObjective","teacherObjective"]
        data=np.asarray([[float(row[key]) for key in columns] for row in records],dtype=np.float32)
        if data.ndim!=2 or data.shape[1]!=3+FEATURES+OUTPUTS+2 or not np.isfinite(data).all():
            raise ValueError(f"Invalid teacher dataset: {path}")
        if not np.all(data[:,0]==mode):raise ValueError("Mode mismatch")
        split=data[:,2].astype(int);x=data[:,3:3+FEATURES];y=data[:,3+FEATURES:3+FEATURES+OUTPUTS]
        train=split==0;valid=split==1;test=split==2
        if min(train.sum(),valid.sum(),test.sum())<2:raise ValueError("Grouped train/validation/test splits must all contain >=2 scenes")
        mean=x[train].mean(0);std=np.maximum(x[train].std(0),.05).astype(np.float32)
        xn=np.clip((x-mean)/std,-8,8)
        rng=np.random.default_rng(args.seed+mode*1009)
        params=[rng.normal(0,.10,(FEATURES,H1)).astype(np.float32),np.zeros(H1,np.float32),
                rng.normal(0,.14,(H1,H2)).astype(np.float32),np.zeros(H2,np.float32),
                rng.normal(0,.14,(H2,OUTPUTS)).astype(np.float32),np.zeros(OUTPUTS,np.float32)]
        mask=np.zeros(CONTROLS,np.float32);mask[:8]=1
        if mode==2:mask[8:13]=1
        if mode==4:mask[8]=1;mask[11:27]=1
        if mode==5:mask[8:11]=1
        moments=[np.zeros_like(p) for p in params];velocities=[np.zeros_like(p) for p in params]
        ids=np.where(train)[0];best=None;best_loss=float("inf");best_step=0
        for step in range(1,args.steps+1):
            batch=rng.choice(ids,size=min(64,max(16,len(ids))),replace=True)
            a,b,p=run(xn[batch],params);target=y[batch];n=len(batch)
            dz=np.zeros_like(p)
            dz[:,:CONTROLS]=2*(p[:,:CONTROLS]-target[:,:CONTROLS])*mask/mask.sum()*p[:,:CONTROLS]*(1-p[:,:CONTROLS])/n
            logits=p[:,CONTROLS:];logits-=logits.max(1,keepdims=True);prob=np.exp(logits);prob/=prob.sum(1,keepdims=True)
            dz[:,CONTROLS:]=.15*(prob-target[:,CONTROLS:])/n
            db=(dz@params[4].T)*(1-b*b);da=(db@params[2].T)*(1-a*a)
            grads=[xn[batch].T@da,da.sum(0),a.T@db,db.sum(0),b.T@dz,dz.sum(0)]
            for index,(param,grad) in enumerate(zip(params,grads)):
                if index in (0,2,4):grad=grad+.002*param
                grad=np.clip(grad,-2,2)
                moments[index]=.9*moments[index]+.1*grad;velocities[index]=.999*velocities[index]+.001*grad*grad
                param-=.0025*(moments[index]/(1-.9**step))/(np.sqrt(velocities[index]/(1-.999**step))+1e-8)
            if step%20==0 or step==args.steps:
                value=loss(run(xn[valid],params)[2],y[valid],mask)
                if value<best_loss:best_loss=value;best=[p.copy() for p in params];best_step=step
        params=best;pred=run(xn,params)[2]
        model_hash=hashlib.sha256(b"".join(p.tobytes() for p in [mean,std,*params])).hexdigest()
        models.append((mean,std,params));test_ids=np.where(test)[0]
        for index in test_ids[:4]:golden.append([mode,*x[index].tolist(),*pred[index].tolist()])
        reports.append({"mode":mode,"model_sha256_float32":model_hash,"architecture":"96-24-16-43 tanh/tanh; sigmoid continuous, categorical shape logits; L2 0.002",
            "train_scenes":int(train.sum()),"validation_scenes":int(valid.sum()),"test_scenes":int(test.sum()),
            "split":"source synthesis families 0..7 train, 8..9 validation, 10..11 test; no crop split",
            "seed":args.seed+mode*1009,"source_attempts_total":sum(int(row.get("attempt",1)) for row in records),
            "source_max_attempts":max(int(row.get("attempt",1)) for row in records),
            "teacher_eligible_candidates":sum(int(row.get("eligibleCandidates",0)) for row in records),
            "teacher_guard_rejected_candidates":sum(int(row.get("rejectedCandidates",0)) for row in records),
            "best_validation_step":best_step,"validation_loss":best_loss,
            "heldout_active_control_MAE":float(np.sum(np.abs(pred[test,:CONTROLS]-y[test,:CONTROLS])*mask)/(test.sum()*mask.sum())),
            "heldout_shape_accuracy":float(np.mean(pred[test,CONTROLS:].argmax(1)==y[test,CONTROLS:].argmax(1))),
            "teacher_heldout_baseline_objective_mean":float(data[test,-2].mean()),"teacher_heldout_selected_objective_mean":float(data[test,-1].mean()),
            "audio_quality_of_model":"Reported separately by compiled C++ actual-DSP heldout render; parameter errors alone do not establish audio quality"})
        print(json.dumps(reports[-1]),flush=True)
    if len(models)!=6 or len({r["model_sha256_float32"] for r in reports})!=6:
        raise ValueError("Six independently trained, distinct weight sets are required")
    header="#pragma once\n#include \"LocalAssist.h\"\n#include <algorithm>\n#include <cmath>\nnamespace odx::ai::neural {\n"
    header+='inline constexpr bool trained=true;\ninline constexpr const char* version="OpenDuck-native-teacher-0.3.0-r2";\n'
    header+="struct Model { float mean[96], scale[96], w1[2304], b1[24], w2[384], b2[16], w3[688], b3[43]; };\ninline constexpr Model models[6] = {\n"
    for mean,std,params in models:header+="{"+",".join(array(v) for v in [mean,std,*params])+"},\n"
    header+="};\ninline Outputs infer(int mode,const Features& features) {\nconst auto&m=models[std::clamp(mode,0,5)];float x[96]{},a[24]{},b[16]{};Outputs out{};\n"
    header+="for(int i=0;i<96;++i)x[i]=std::clamp((features[i]-m.mean[i])/m.scale[i],-8.f,8.f);\n"
    header+="for(int j=0;j<24;++j){float z=m.b1[j];for(int i=0;i<96;++i)z+=x[i]*m.w1[i*24+j];a[j]=std::tanh(z);}\n"
    header+="for(int j=0;j<16;++j){float z=m.b2[j];for(int i=0;i<24;++i)z+=a[i]*m.w2[i*16+j];b[j]=std::tanh(z);}\n"
    header+="for(int j=0;j<43;++j){float z=m.b3[j];for(int i=0;i<16;++i)z+=b[i]*m.w3[i*43+j];out[j]=j<27?1.f/(1.f+std::exp(-std::clamp(z,-30.f,30.f))):z;}return out;\n}\n}\n"
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(header,encoding="utf-8")
    args.golden.parent.mkdir(parents=True,exist_ok=True)
    with args.golden.open("w",newline="",encoding="utf-8") as f:
        writer=csv.writer(f);writer.writerow(["mode",*[f"f{i}" for i in range(FEATURES)],*[f"y{i}" for i in range(OUTPUTS)]]);writer.writerows(golden)
    report={"version":"0.3.0-r2","teacher":"Actual OpenDuck C++ DSP via AssistTeacher; shared hard preservation guards before overlap optimization; Python does not render audio or supply recipe labels", "sources":sources,"models":reports,
            "training_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "corpus_manifest_sha256":hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            "python_version":sys.version.split()[0],"numpy_version":np.__version__,
            "header_sha256":hashlib.sha256(args.output.read_bytes()).hexdigest(),"runtime":"C++ FP32 CPU arrays; no Python, network or model downloads", "perceptual_quality":"Not established by objective training alone"}
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")

if __name__=="__main__":main()
