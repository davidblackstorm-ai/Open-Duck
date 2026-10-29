"""Validate unchanged R2 data/weights and three exact reviewed source exceptions.

This source policy never substitutes for lifecycle/worker regressions, paired
audio evidence or a real DAW test. No virtual root or temporary source rewrite
is used: live and frozen source files are read directly and checked separately.
"""
from __future__ import annotations
import csv
import hashlib
import json
from pathlib import Path
import re
import stat

REQUIRED = {
    'Source/PluginProcessor.cpp', 'Source/PluginProcessor.h', 'Source/Engines.h',
    'Source/BehaviorLibrary.h', 'Source/AssistTrainingData.h', 'Source/PluginAssist.cpp',
    'Source/LocalAssist.cpp', 'Source/LocalAssist.h', 'Source/LocalAssistRender.cpp',
    'Source/LocalAssistRender.h', 'tools/AssistTeacher.cpp',
}

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


OLD_PROCESSOR_SHA256 = "520ff6ac97f2a5887af33e36cd45e0591d41dafc17d943dea96e67c14d509af2"
INITIAL_PROCESSOR_SHA256 = "f46d48f33a874b5fa8a7a7a7fffae98712ab24808654f181f7b94819a7f2d6a5"
HOST_PROCESSOR_SHA256 = "c9f11d7f04524affbfb2d945e4aa8d33f30809a597da96b823a2fc3bcbbfc01d"
NEW_PROCESSOR_SHA256 = "fd7643c6fab1b1dc7d617930f2104e363e035f8704a83b78b261163327f2feb0"
OLD_HEADER_SHA256 = "b856b8c98ee3876543d667c38566c93c7f6bd176fe4551f67ec28b93a838cac4"
NEW_HEADER_SHA256 = "216f00eb69672a20896237e7f62b922dc10ec35283316ae20047b38cbfd8c098"
SCOPE_TRANSFORMS_RELATIVE = "work/hotfix031/waveform/SCOPE-EXACT-TRANSFORMS.json"
SCOPE_TRANSFORMS_SHA256 = "bd0ede53bcd0c33776bf03ca1205bdecd44651ffdebefce3abae83476f96ae40"
OLD_LOCAL_ASSIST_SHA256 = "8c73c9d3b8f6056a6ab394dfb0083eab187f2c86ef7b3ea99d3c27e34fed5ccf"
NEW_LOCAL_ASSIST_SHA256 = "acba181e5c23ff7f492431929876b13847314c49fa488cebdb9b2155b831189d"
APPLY_FIX_SCRIPT_SHA256 = "386924477195719238fd37d16a26f5e66e6bb922246dbdf905ce3a8fe9af604b"
PINNED_INPUTS = {'docs/research-v0.3.0/teacher-final/manifest.json': '6ef52bac15a3c0564d1b7a8e1c62abe628869208df251bcc02d3d5b178224eed', 'docs/research-v0.3.0/training.json': '6c0d9d2f71db163d34d24cc27c1fcdd2ed2cb5894e4e76749f329d24ae9a9841', 'docs/research-v0.3.0/golden.csv': '371ad5e24cfb623af5910139c3433bb70b2412f5224281b4c2fbc87485d08893', 'build/training-reproducibility-v0.3.0-r2.json': '218c17e928dacb171d3a1f04a95b3b395d4ca1683e91917388c634b8b3417a86', 'Source/NeuralModels.h': '039333f56cbf30f346037c007cad246f09a7709ba9e9ecc787a1a55a160e9aa9', 'tools/train_assist.py': '05d99a2c119758f19bc9de7d3b9c67cc34ceef50d32a8ce0ccff5b232fa47b7b'}
PINNED_TEACHER_SOURCES = {'Source/PluginProcessor.cpp': '520ff6ac97f2a5887af33e36cd45e0591d41dafc17d943dea96e67c14d509af2', 'Source/PluginProcessor.h': 'b856b8c98ee3876543d667c38566c93c7f6bd176fe4551f67ec28b93a838cac4', 'Source/Engines.h': 'cfffd8d764f5e25c820f81f4dee55d3ffab0f98febcae54a1aaf94ccc56191a4', 'Source/BehaviorLibrary.h': 'd2a7b842e7608e957258b53201271414c567d276ad8b6387949ec51b99068f25', 'Source/LocalAssist.cpp': '8c73c9d3b8f6056a6ab394dfb0083eab187f2c86ef7b3ea99d3c27e34fed5ccf', 'Source/LocalAssist.h': '8285bf66a8b29c00a519ecc3e9fbf1a7e10fa92899e314515a4d52e61ec12550', 'Source/LocalAssistRender.cpp': '1cb871a2b4b3809b3adb46a4f88dbe09286f6d3fdc158b831cf240c906f413cd', 'Source/LocalAssistRender.h': 'd3d8efb0c22ea11d304ae6cf41b6d1256533aa97bda41984df82599ad26a5249', 'Source/PluginAssist.cpp': '2b19c8fd4d6daf7196bb7b1ca1b2a92b6f7add59ffcb4c0f069b38ae03ac9cdb', 'Source/AssistTrainingData.h': 'c16905be776da6602ff731847953285a82bc507b179a7ff36f90caad74ce5126', 'tools/AssistTeacher.cpp': 'f7cadf6a0aee4fbb67be5ea9f18f76a2b7c76e9ee619ccb944b1a7523249c00c'}

def secure_file(root, path):
    root, path = Path(root).resolve(), Path(path)
    require(path.is_absolute() and path.resolve().is_relative_to(root), "Lineage path escaped explicit project")
    for item in (path, *path.parents):
        attributes = item.lstat()
        require(not stat.S_ISLNK(attributes.st_mode) and not (getattr(attributes,"st_file_attributes",0) &
                getattr(stat,"FILE_ATTRIBUTE_REPARSE_POINT",0)), f"Lineage reparse path forbidden: {item}")
        if item == root: break
    require(path.is_file(), f"Lineage file missing: {path}")
    return path

def reconstruct_initial_processor(raw):
    """Exactly the two source replacements in the reviewed lifecycle fix script.

    The third replacement in that script is the CMake project version only.
    Source bytes are pinned before and after; no substring-only exception.
    """
    require(hashlib.sha256(raw).hexdigest() == OLD_PROCESSOR_SHA256, "Unpinned old processor")
    text = raw.decode("utf-8-sig")
    old = "    setLatencySamples(SpectralEngine::size + BandEngine::delay);\r\n    prepareRevision3(sr);\r\n    prepareLocalAssist();"
    if old not in text: old = old.replace("\r\n", "\n")
    require(text.count(old) == 1, "Unexpected prepareToPlay replacement count")
    eol = "\r\n" if "\r\n" in old else "\n"
    new = eol.join([
        "    prepareRevision3(sr);",
        "    prepareLocalAssist();",
        "    // Publish only the completed routing latency. An intermediate legacy value",
        "    // makes hosts that re-prepare after kLatencyChanged restart indefinitely.",
        "    setLatencySamples(revision3Mask.load() != 0 ? revision3LatencySamples()",
        "                                              : SpectralEngine::size + BandEngine::delay);"])
    text = text.replace(old, new)
    old2 = "    if(revision3Mask.load()!=0)setLatencySamples(revision3LatencySamples());"
    require(text.count(old2) == 1 and text.count(old2 + eol) == 1, "Unexpected secondary latency setter")
    result = text.replace(old2 + eol, "").encode("utf-8")
    require(hashlib.sha256(result).hexdigest() == INITIAL_PROCESSOR_SHA256, "Reconstructed processor hash differs")
    return result


STATE_COMMIT_SCRIPT_SHA256 = "9439cff160d995498b58c389f7e6175da173d064392d82481644cc6c22c7f1e3"
ASSIST_EXCEPTION_SCRIPT_SHA256 = "67e45c2f379a91f60559860ec0f369e40c987d677970996622c6ee7a9609e8ac"
STATE_COMMIT_REPLACEMENTS = [('    setLatencySamples(revision3Mask.load()?revision3LatencySamples():SpectralEngine::size+BandEngine::delay);\n    loadingState.store(false, std::memory_order_relaxed);\n', ''), ('    undo.clearUndoHistory();\n    resetLocalAssist();\n}\nvoid OpenDuckProcessor::processBlock', '    undo.clearUndoHistory();\n    resetLocalAssist();\n    loadingState.store(false, std::memory_order_relaxed);\n    // A host may synchronously save state from its latency notification. Publish\n    // it only after grouped shapes and all restore bookkeeping are committed.\n    setLatencySamples(revision3Mask.load()?revision3LatencySamples():SpectralEngine::size+BandEngine::delay);\n}\nvoid OpenDuckProcessor::processBlock')]
ASSIST_EXCEPTION_REPLACEMENTS = [('#include <cstring>\n', '#include <cstring>\n#include <exception>\n#include <new>\n'), ('    void run()\n    {\n        while(!stop.load())\n        {\n', '    // This is a std::thread, not juce::Thread: no JUCE exception wrapper\n    // surrounds it. Recover outside the unwound job lock, on this worker only.\n    // Error cleanup never touches another slot or publishes an applicable target.\n    void failedJob(int mode,const char* message) noexcept\n    {\n        try\n        {\n            std::unique_lock<std::mutex> lock(mutex);\n            if(mode<0 || mode>=6){cancelled=true;stop=true;return;}\n            if(active.load()!=mode)return;\n            auto&slot=slots[(size_t)mode];\n            while(pushing.load()!=0){lock.unlock();std::this_thread::yield();lock.lock();}\n            slot.hasCapture=false;slot.cachedSamples=0;\n            slot.proposal.improved=false;slot.proposal.targetImproved=false;\n            for(auto&channel:slot.capture)channel.clear();\n            for(auto&channel:audio)channel.clear();\n            // Even a genuine allocation failure must not throw while reporting\n            // itself. State::error remains available if the string cannot grow.\n            try{slot.status.message.assign(message);}catch(...){slot.status.message.clear();}\n            State finalState=State::error;\n            if(cancelled.load())finalState=State::cancelled;\n            else if(slot.revision.load()!=slot.status.contextRevision)finalState=State::stale;\n            auto expected=State::analyzing;\n            slot.state.compare_exchange_strong(expected,finalState,std::memory_order_release,std::memory_order_relaxed);\n            active=-1;processing=false;strengthJob=false;\n        }\n        catch(...)\n        {\n            // A failed OS synchronization primitive is not recoverable as a new\n            // job. Contain it at this thread boundary and make Apply impossible.\n            cancelled=true;stop=true;\n            if(mode>=0 && mode<6){auto expected=State::analyzing;\n                slots[(size_t)mode].state.compare_exchange_strong(expected,State::error,std::memory_order_release,std::memory_order_relaxed);}\n            active=-1;\n        }\n    }\n    void run() noexcept\n    {\n        while(!stop.load())\n        {\n            int failedMode=-1;\n            try\n            {\n'), ('            const int mode=active.load();if(mode<0)continue;\n', '            const int mode=active.load();if(mode<0)continue;failedMode=mode;\n'), ('            active=-1;processing=false;\n        }\n    }\n};\nController::Controller()', '            active=-1;processing=false;\n            }\n            catch(const std::bad_alloc&){failedJob(failedMode,"Insufficient memory for local analysis; no proposal was applied");}\n            catch(const std::exception&){failedJob(failedMode,"Local analysis failed; no proposal was applied");}\n            catch(...){failedJob(failedMode,"Unexpected local analysis failure; no proposal was applied");}\n        }\n    }\n};\nController::Controller()'), ('    const int count=(int)std::llround(context.sampleRate*seconds);\n    for(auto &channel:impl->audio)channel.assign((size_t)count,0);', '    if(impl->stop.load())return false;\n    auto& affected=impl->slots[(size_t)mode];\n    auto failedStart=[&](const char* message) noexcept\n    {\n        affected.hasCapture=false;affected.cachedSamples=0;\n        affected.proposal.improved=false;affected.proposal.targetImproved=false;\n        for(auto&channel:affected.capture)channel.clear();\n        for(auto&channel:impl->audio)channel.clear();\n        try{affected.status.message.assign(message);}catch(...){affected.status.message.clear();}\n        impl->cancelled=true;impl->active=-1;impl->written=0;impl->target=0;\n        impl->processing=false;impl->strengthJob=false;affected.state=State::error;\n        return false;\n    };\n    try\n    {\n    const int count=(int)std::llround(context.sampleRate*seconds);\n    for(auto &channel:impl->audio)channel.assign((size_t)count,0);'), ('    slot.state=State::capturing;\n    impl->active.store(mode,std::memory_order_release);\n    if(!impl->worker.joinable())impl->worker=std::thread([p=impl.get()]{p->run();});\n    impl->wake.notify_one();return true;\n}\nbool Controller::requestStrength', '    // The new worker can wait with active==-1. Do not expose writable capture\n    // buffers until thread construction, including its OS resources, succeeded.\n    if(!impl->worker.joinable())impl->worker=std::thread([p=impl.get()]{p->run();});\n    slot.state=State::capturing;\n    impl->active.store(mode,std::memory_order_release);\n    impl->wake.notify_one();return true;\n    }\n    catch(const std::bad_alloc&){return failedStart("Insufficient memory to start local capture");}\n    catch(const std::exception&){return failedStart("Unable to start local capture; no proposal was applied");}\n    catch(...){return failedStart("Unexpected local capture failure; no proposal was applied");}\n}\nbool Controller::requestStrength')]

def exact_normalized_replacements(raw,rules,expected):
    text=raw.decode("utf-8")
    newline="\r\n" if "\r\n" in text else "\n"
    text=text.replace("\r\n","\n")
    for old,new in rules:
        require(text.count(old)==1,"Approved host fix replacement does not match exactly once")
        text=text.replace(old,new,1)
    result=text.replace("\n",newline).encode("utf-8")
    require(hashlib.sha256(result).hexdigest()==expected,"Approved host fix reconstruction differs")
    return result

def reconstruct_host_processor(raw):
    initial=reconstruct_initial_processor(raw)
    return exact_normalized_replacements(initial,STATE_COMMIT_REPLACEMENTS,HOST_PROCESSOR_SHA256)


def load_scope_transforms(root):
    path = secure_file(root, Path(root)/SCOPE_TRANSFORMS_RELATIVE)
    require(sha(path) == SCOPE_TRANSFORMS_SHA256, "Reviewed scope transformation document changed")
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    files = document.get("files", {})
    expected = {"Source/PluginProcessor.cpp": (HOST_PROCESSOR_SHA256, NEW_PROCESSOR_SHA256, 11),
                "Source/PluginProcessor.h": (OLD_HEADER_SHA256, NEW_HEADER_SHA256, 2)}
    require(set(files) == set(expected), "Scope exception must name exactly processor cpp/header")
    for name, (old_sha, new_sha, count) in expected.items():
        row = files[name]
        require(set(row) == {"beforeSHA256", "proposedSHA256", "replacements"} and
                row["beforeSHA256"] == old_sha and row["proposedSHA256"] == new_sha,
                f"Scope before/after pins changed: {name}")
        require(isinstance(row["replacements"], list) and len(row["replacements"]) == count,
                f"Unexpected scope replacement inventory: {name}")
        for rule in row["replacements"]:
            require(set(rule) == {"before", "after"} and
                    all(isinstance(rule[key], str) and rule[key] for key in ("before", "after")) and
                    rule["before"] != rule["after"], "Invalid exact scope replacement")
    return document


def exact_scope_replacements(raw, rules, expected, reverse=False):
    """Preserve every byte outside the pinned display-only replacements, including EOLs."""
    result = raw
    for rule in reversed(rules) if reverse else rules:
        old = rule["after" if reverse else "before"].encode("utf-8")
        new = rule["before" if reverse else "after"].encode("utf-8")
        require(old and result.count(old) == 1, "Approved scope replacement must match exactly once")
        result = result.replace(old, new, 1)
    require(hashlib.sha256(result).hexdigest() == expected, "Approved scope reconstruction differs")
    return result


def reconstruct_scope_source(raw, relative, document):
    expected = {"Source/PluginProcessor.cpp": (HOST_PROCESSOR_SHA256, NEW_PROCESSOR_SHA256),
                "Source/PluginProcessor.h": (OLD_HEADER_SHA256, NEW_HEADER_SHA256)}
    require(relative in expected, "File is outside the approved scope exception")
    old_sha, new_sha = expected[relative]
    row = document["files"][relative]
    require(row["beforeSHA256"] == old_sha and row["proposedSHA256"] == new_sha and
            hashlib.sha256(raw).hexdigest() == old_sha, "Unpinned pre-scope source")
    result = exact_scope_replacements(raw, row["replacements"], new_sha)
    require(exact_scope_replacements(result, row["replacements"], old_sha, reverse=True) == raw,
            "Scope inverse does not recover byte-identical approved host source")
    return result


def reconstruct_processor(raw, scope_document=None):
    if scope_document is None:
        scope_document = load_scope_transforms(Path(__file__).resolve().parents[1])
    return reconstruct_scope_source(reconstruct_host_processor(raw), "Source/PluginProcessor.cpp", scope_document)


def reconstruct_header(raw, scope_document=None):
    if scope_document is None:
        scope_document = load_scope_transforms(Path(__file__).resolve().parents[1])
    return reconstruct_scope_source(raw, "Source/PluginProcessor.h", scope_document)

def reconstruct_local_assist(raw):
    require(hashlib.sha256(raw).hexdigest()==OLD_LOCAL_ASSIST_SHA256,"Unpinned old LocalAssist")
    result=exact_normalized_replacements(raw,ASSIST_EXCEPTION_REPLACEMENTS,NEW_LOCAL_ASSIST_SHA256)
    before=raw.decode("utf-8").replace("\r\n","\n")
    after=result.decode("utf-8").replace("\r\n","\n")
    require(before[before.index("namespace odx::ai"):before.index("struct Controller::Impl")]==
            after[after.index("namespace odx::ai"):after.index("struct Controller::Impl")],
            "AI feature/metric/guard/decode/refinement prefix changed")
    require(before[before.index("bool Controller::requestStrength"):]==
            after[after.index("bool Controller::requestStrength"):],
            "AI Strength/push/cancel/status/blend suffix changed")
    return result

def validate_pinned_inputs(root):
    for relative, expected in PINNED_INPUTS.items():
        require(sha(secure_file(root, Path(root)/relative)) == expected, f"Pinned R2 evidence changed: {relative}")

def validate_host_source(root):
    root = Path(root).resolve()
    frozen_dir = root / "docs/research-v0.3.0/teacher-final/frozen-source"
    scope_document = load_scope_transforms(root)
    for relative, expected in PINNED_TEACHER_SOURCES.items():
        frozen = secure_file(root, frozen_dir/relative)
        runtime = secure_file(root, root/relative)
        require(sha(frozen) == expected, f"Frozen R2 source changed: {relative}")
        if relative == "Source/PluginProcessor.cpp":
            old = secure_file(root, root/"work/crash-0.3.0-20261028/baseline-source/Source/PluginProcessor.cpp")
            require(old.read_bytes() == frozen.read_bytes(), "Incident baseline does not equal frozen teacher processor")
            expected_runtime = reconstruct_processor(old.read_bytes(), scope_document)
            require(runtime.read_bytes() == expected_runtime and sha(runtime) == NEW_PROCESSOR_SHA256,
                    "Runtime processor is not exactly the approved host-lifecycle plus scope exception")
        elif relative == "Source/PluginProcessor.h":
            expected_runtime = reconstruct_scope_source(frozen.read_bytes(), relative, scope_document)
            require(runtime.read_bytes() == expected_runtime and sha(runtime) == NEW_HEADER_SHA256,
                    "Runtime processor header is not exactly the approved scope exception")
        elif relative == "Source/LocalAssist.cpp":
            old = secure_file(root, root/"work/crash-0.3.0-20261028/baseline-source/Source/LocalAssist.cpp")
            require(old.read_bytes() == frozen.read_bytes(), "Incident LocalAssist baseline differs from frozen teacher")
            require(runtime.read_bytes() == reconstruct_local_assist(old.read_bytes()) and sha(runtime) == NEW_LOCAL_ASSIST_SHA256,
                    "Runtime LocalAssist is not exactly the approved exception-containment patch")
        else:
            require(sha(runtime) == expected, f"Non-exempt teacher/runtime source changed: {relative}")
    cmake = secure_file(root, root/"CMakeLists.txt").read_text(encoding="utf-8-sig")
    require(cmake.count("project(OpenDuckProX VERSION 0.3.1 LANGUAGES CXX)") == 1 and
            "project(OpenDuckProX VERSION 0.3.0 LANGUAGES CXX)" not in cmake,
            "The build must declare version0.3.1")
    return {"oldProcessorSHA256":OLD_PROCESSOR_SHA256,"hostProcessorSHA256":HOST_PROCESSOR_SHA256,
            "newProcessorSHA256":NEW_PROCESSOR_SHA256,
            "oldProcessorHeaderSHA256":OLD_HEADER_SHA256,"newProcessorHeaderSHA256":NEW_HEADER_SHA256,
            "reviewedFixScriptSHA256":APPLY_FIX_SCRIPT_SHA256,"exactSourceReconstruction":True,
            "oldLocalAssistSHA256":OLD_LOCAL_ASSIST_SHA256,"newLocalAssistSHA256":NEW_LOCAL_ASSIST_SHA256,
            "stateCommitScriptSHA256":STATE_COMMIT_SCRIPT_SHA256,"assistExceptionScriptSHA256":ASSIST_EXCEPTION_SCRIPT_SHA256,
            "otherEightFrozenSourcesUnchanged":True,"aiNumericalPrefixAndStrengthPushCancelStatusBlendUnchanged":True,
            "scopeTelemetryException":{"reviewedTransform":SCOPE_TRANSFORMS_RELATIVE,
                "reviewedTransformSHA256":SCOPE_TRANSFORMS_SHA256,"forwardAndInverseByteExact":True,
                "publicScopeFrameUnchanged":True,"fullProcessorHeaderRebuildRequired":True,
                "rawAiInputUnchanged":True,"traceUsesCorrectedDisplayFrame":True,
                "audioEquivalenceIsSeparateMandatoryGate":True},
            "exceptionScope":"Three exact source exceptions: processor host-lifecycle/state publication plus display-only FIFO/Freeze telemetry; processor private header layout/Freeze setter; affected-slot LocalAssist worker/capture exception containment. Eight other frozen sources, raw AI capture and numerical DSP/AI code remain unchanged. Full rebuild, waveform/Trace regressions, AI and paired audio equivalence are separate mandatory gates"}

def validate(root):
    root = Path(root).resolve()
    validate_pinned_inputs(root)
    host_policy = validate_host_source(root)
    relative = 'docs/research-v0.3.0/teacher-final/manifest.json'
    corpus_dir = root / Path(relative).parent
    corpus = json.loads((root / relative).read_text(encoding='utf-8-sig'))
    training = json.loads((root / 'docs/research-v0.3.0/training.json').read_text(encoding='utf-8-sig'))
    require(corpus.get('pipeline_revision') == 'quality-r2' and corpus.get('seed') == 42030 and
            corpus.get('teacher_train_only') is False,
            'A fresh quality-r2 native corpus with seed42030 is required')
    require(corpus.get('scenes_per_mode') == 144 and corpus.get('candidates_per_scene') == 8 and
            all(corpus.get(k) is True for k in ('passed', 'teacher_unchanged', 'sources_unchanged')),
            'The intact 864-scene, eight-candidate R2 teacher run is required')
    sources = corpus.get('source_sha256', {})
    require(sources == PINNED_TEACHER_SOURCES and set(sources) == REQUIRED, 'Frozen teacher inventory or fingerprints changed')
    for name, expected in sources.items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts and
                isinstance(expected, str) and re.fullmatch('[0-9a-f]{64}', expected), 'Invalid source fingerprint')
        runtime_expected = (NEW_PROCESSOR_SHA256 if name == 'Source/PluginProcessor.cpp' else
                            NEW_HEADER_SHA256 if name == 'Source/PluginProcessor.h' else
                            NEW_LOCAL_ASSIST_SHA256 if name == 'Source/LocalAssist.cpp' else expected)
        require(sha(secure_file(root, corpus_dir / 'frozen-source' / path)) == expected and
                sha(secure_file(root, root / path)) == runtime_expected,
                f'R2 runtime/teacher/frozen source policy differs: {name}')
    require(training.get('version') == '0.3.0-r2', 'Models were not retrained for R2')
    require(training.get('training_script_sha256') == sha(root / 'tools/train_assist.py') and
            training.get('corpus_manifest_sha256') == sha(root / relative), 'Training script or corpus provenance differs')
    require(sha(root / 'Source/NeuralModels.h') == training.get('header_sha256'), 'R2 weight export changed')
    header = (root / 'Source/NeuralModels.h').read_text(encoding='utf-8-sig')
    require('trained=true' in header and 'OpenDuck-native-teacher-0.3.0-r2' in header,
            'R1 weights or untrained placeholders cannot satisfy the quality gate')
    runs = {r['mode']: r for r in corpus.get('runs', [])}
    data = {r['mode']: r for r in training.get('sources', [])}
    models = {r['mode']: r for r in training.get('models', [])}
    require(set(runs) == set(data) == set(models) == set(range(6)), 'Six independent sector lineages required')
    require(len({r.get('model_sha256_float32') for r in models.values()}) == 6, 'Six distinct trained weight sets required')
    modes = []
    for mode in range(6):
        run, model = runs[mode], models[mode]
        csv_path = corpus_dir / f'mode-{mode}.csv'
        require(run.get('passed') is True and run.get('exit_code') == 0 and run.get('rows') == 144 and
                sha(csv_path) == run.get('csv_sha256') == data[mode].get('sha256'), f'R2 data mismatch for sector {mode}')
        require((model.get('train_scenes'), model.get('validation_scenes'), model.get('test_scenes')) == (96, 24, 24) and
                model.get('seed') == 43030 + mode * 1009, 'R2 grouped splits or fresh training seed changed')
        with csv_path.open(newline='', encoding='utf-8-sig') as stream:
            rows = list(csv.DictReader(stream))
        require(len(rows) == 144 and all(int(r['mode']) == mode for r in rows), 'Invalid R2 teacher records')
        require({split: sum(int(r['split']) == split for r in rows) for split in (0, 1, 2)} == {0:96, 1:24, 2:24},
                'Grouped R2 source split is incomplete')
        modes.append({'mode': mode, 'rows': 144, 'csvSHA256': sha(csv_path),
                      'modelSHA256': model['model_sha256_float32']})
    repro_path = root / 'build/training-reproducibility-v0.3.0-r2.json'
    repro = json.loads(repro_path.read_text(encoding='utf-8-sig'))
    require(repro.get('version') == '0.3.0-r2' and repro.get('pipeline_revision') == 'quality-r2' and
            repro.get('seed') == 43030 and repro.get('steps') == 1800 and
            all(repro.get(k) is True for k in ('passed', 'allSixModelStatisticsExactlyEqual', 'originalInputsUnchanged')) and
            repro.get('corpusManifestSHA256') == sha(root / relative) and
            repro.get('trainingScriptSHA256') == sha(root / 'tools/train_assist.py') and
            repro.get('originalTrainingReportSHA256') == sha(root / 'docs/research-v0.3.0/training.json'),
            'R2 deterministic repeated training has not passed for the current inputs')
    for label, original in (('NeuralModels.h', root / 'Source/NeuralModels.h'),
                            ('golden.csv', root / 'docs/research-v0.3.0/golden.csv')):
        row = repro.get('checks', {}).get(label, {})
        repeated = Path(row.get('repeatedPath', ''))
        require(repeated.is_absolute() and repeated.resolve().is_relative_to(root) and
                repeated.resolve() != original.resolve() and not repeated.samefile(original) and
                row.get('identical') is True and row.get('originalSHA256') == row.get('repeatedSHA256') == sha(original) and
                sha(repeated) == sha(original), f'Repeated R2 export does not match: {label}')
    return {'passed': True, 'version': '0.3.1', 'method': 'quality-r2-exact-approved-host-worker-scope-exceptions', 'hostLifecyclePolicy': host_policy,
            'corpusManifestSHA256': sha(root / relative), 'sourceSHA256': sources,
            'modelHeaderSHA256': training['header_sha256'], 'modes': modes,
            'trainingReproducibilitySHA256': sha(repro_path),
            'scope': 'Eight exact frozen sources and unchanged R2 data/weights plus three byte-exact source exceptions for processor host/scope telemetry, processor private header and LocalAssist worker safety. Full rebuild, waveform/Trace/lifecycle/AI tests and paired old/new audio equivalence are separate mandatory gates. No listening, DAW success or release eligibility is inferred.'}
