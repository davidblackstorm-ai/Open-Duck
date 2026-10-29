"""Compare actual 35-case/23-WAV hotfix renders against the pinned R2 baseline.

No rendering, alignment, normalization, resampling or invented fallback occurs.
Missing new renders are a request/pending state, never a passing equivalence.
The invoking native runner supplies separately recorded executable/log hashes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import wave
from verify_quality_audio031 import (EXPECTED_WAVS, EXPECTED_CASES, validate as validate_audio,
                                     safe_path, require, digest)

OLD_BINARY_SHA256 = "a22be99e689d64304fcc525c9c4eeae9b793022836bcaca96cf125ccc636f271"
OLD_MANIFEST_SHA256 = "5c2b8593d40c3ba92cb6fcea935d6de965657f18e46e1d1645bf11e375b72b8b"

def requested(baseline, current):
    return {"schema":"openduck-host-hotfix-audio-equivalence-v1","version":"0.3.1",
            "status":"requested","passed":False,"baselineDirectory":str(baseline),
            "currentDirectory":str(current),"requiredCaseCount":35,"requiredWaveCount":23,
            "reason":"Actual new native render and complete pinned-baseline comparison required"}

def pcm_digest(path):
    with wave.open(str(path),"rb") as source:
        metadata = (source.getnchannels(),source.getsampwidth(),source.getframerate(),source.getnframes(),source.getcomptype())
        checksum = hashlib.sha256()
        total = 0
        while block := source.readframes(16384):
            checksum.update(block); total += len(block)
        require(total == metadata[0]*metadata[1]*metadata[3], f"Incomplete WAV sample payload: {path}")
    return metadata,checksum.hexdigest()

def compare_render_files(baseline, current):
    """File comparison only; caller must independently validate actual audio files."""
    baseline,current = Path(baseline),Path(current)
    require(baseline.resolve() != current.resolve(),"Paired directories must be distinct")
    require({p.name for p in baseline.glob('*.wav')} == set(EXPECTED_WAVS) and
            {p.name for p in current.glob('*.wav')} == set(EXPECTED_WAVS),"Paired WAV inventory is not exactly23 files")
    old_csv,new_csv = baseline/'quality-measures.csv',current/'quality-measures.csv'
    require(not old_csv.samefile(new_csv),"Paired CSV cannot be the same file/hardlink")
    require(old_csv.read_bytes() == new_csv.read_bytes(),"The35-case numeric CSV differs from baseline")
    records = []
    for name in EXPECTED_WAVS:
        old,new = baseline/name,current/name
        require(not old.samefile(new),f"Paired WAV cannot be the same file/hardlink: {name}")
        old_meta,old_pcm = pcm_digest(old);new_meta,new_pcm = pcm_digest(new)
        require(old_meta == new_meta and old_pcm == new_pcm,f"Native sample/format difference: {name}")
        records.append({"name":name,"baselineSHA256":digest(old),"currentSHA256":digest(new),
            "baselinePcmSHA256":old_pcm,"currentPcmSHA256":new_pcm,"samplesExactlyEqual":True,
            "sampleRate":new_meta[2],"channels":new_meta[0],"sampleWidthBytes":new_meta[1],"frames":new_meta[3]})
    return {"csvExactlyEqual":True,"csvSHA256":digest(old_csv),"waveFiles":records}

def validate(root, baseline, current, *, new_binary_sha256, new_renderer_sha256, renderer_log_sha256):
    root,baseline,current = safe_path(root),safe_path(baseline),safe_path(current)
    require(baseline == root/'build/quality-audio-v0.3.0' and current == root/'build/quality-audio-v0.3.1',
            "Explicit versioned baseline/current audit directories are required")
    for value in (new_binary_sha256,new_renderer_sha256,renderer_log_sha256):
        require(isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value),"Missing actual invocation fingerprint")
    require(new_binary_sha256 != OLD_BINARY_SHA256,"New render must target a newly fingerprinted hotfix binary")
    actual_binary=safe_path(root/'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3')
    actual_renderer=safe_path(root/'build/Revision030QualityAudit_artefacts/Release/Revision030QualityAudit.exe')
    actual_log=safe_path(root/'build/audio-v0.3.1.log')
    require(digest(actual_binary)==new_binary_sha256 and digest(actual_renderer)==new_renderer_sha256 and
            digest(actual_log)==renderer_log_sha256,"Current native invocation fingerprints do not match actual files")
    log=actual_log.read_text(encoding='utf-8-sig',errors='replace')
    require(bool(re.search(r'RENDERED\s+35\s+cases;',log)) and not re.search(r'^FAIL\s',log,re.MULTILINE),
            "Current renderer log does not report all35 cases successfully")
    manifest_path = safe_path(root/'build/non-daw-v0.3.0.json')
    require(digest(manifest_path) == OLD_MANIFEST_SHA256,"Historical R2 nonDAW manifest changed")
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    require(manifest.get('complete') is True and manifest.get('releaseBinarySHA256') == OLD_BINARY_SHA256 and
            manifest.get('checks',{}).get('audio',{}).get('passed') is True and
            manifest['checks']['audio'].get('count') == 35,"Incomplete old native audio provenance")
    proof_path = safe_path(root/'build/quality-audio-v0.3.0.json')
    expected = manifest.get('generatedEvidenceSHA256',{}).get('build/quality-audio-v0.3.0.json')
    require(expected == digest(proof_path),"Historical quality-audio evidence changed")
    old_proof = json.loads(proof_path.read_text(encoding='utf-8-sig'))
    require(old_proof.get('passed') is True and old_proof.get('rendererCaseCount') == 35 and
            old_proof.get('waveFileCount') == 23 and all(old_proof.get('checks',{}).values()),"Old audio validation was not complete")
    require(digest(baseline/'quality-measures.csv') == old_proof.get('csv',{}).get('sha256'),"Old CSV changed after baseline validation")
    old_records = old_proof.get('waveFiles',[])
    require(len(old_records)==23 and {row.get('name') for row in old_records} == set(EXPECTED_WAVS),"Old WAV manifest inventory changed")
    for row in old_records:
        require(digest(safe_path(baseline/row['name'])) == row.get('sha256'),f"Old WAV changed: {row['name']}")
    old_audio = validate_audio(root,baseline)
    new_audio = validate_audio(root,current)
    require(old_audio['passed'] is True and new_audio['passed'] is True,"Paired actual WAV validation failed")
    require(old_audio['fixtureManifest']['sha256'] == new_audio['fixtureManifest']['sha256'] and
            [row['sha256'] for row in old_audio['fixtures']] == [row['sha256'] for row in new_audio['fixtures']],
            "Paired renders do not share identical fixture inputs")
    require(old_audio['cases'] == new_audio['cases'] and len(new_audio['cases']) == len(EXPECTED_CASES) == 35,
            "Native numeric cases are not exactly equal")
    result = compare_render_files(baseline,current)
    return {"schema":"openduck-host-hotfix-audio-equivalence-v1","version":"0.3.1","status":"passed","passed":True,
        "baselineBinarySHA256":OLD_BINARY_SHA256,"currentBinarySHA256":new_binary_sha256,
        "currentRendererSHA256":new_renderer_sha256,"currentRendererLogSHA256":renderer_log_sha256,
        "baselineManifestSHA256":digest(manifest_path),"baselineAudioProofSHA256":digest(proof_path),
        "baselineDirectory":str(baseline),"currentDirectory":str(current),"caseCount":35,"waveCount":23,
        "fixturesSHA256":{row['name']:row['sha256'] for row in new_audio['fixtures']},**result,
        "scope":"Bit-exact PCM plus exact35-case numeric evidence for these explicit native renders. No alignment, resampling, normalization, perceptual score, DAW success or universal equivalence is inferred."}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',required=True,type=Path)
    parser.add_argument('--baseline',required=True,type=Path)
    parser.add_argument('--current',required=True,type=Path)
    parser.add_argument('--new-binary-sha256',required=True)
    parser.add_argument('--new-renderer-sha256',required=True)
    parser.add_argument('--renderer-log-sha256',required=True)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    result=validate(args.root,args.baseline,args.current,new_binary_sha256=args.new_binary_sha256,
                    new_renderer_sha256=args.new_renderer_sha256,renderer_log_sha256=args.renderer_log_sha256)
    output=safe_path(args.output,exists=False)
    require(output.is_relative_to(safe_path(args.root)/'build') and output.suffix=='.json',"Output must be a named build JSON")
    require(not output.exists(),"Refusing to overwrite an existing equivalence report; archive explicitly first")
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'cases':35,'waves':23,'output':str(output)}))

if __name__=='__main__':main()
