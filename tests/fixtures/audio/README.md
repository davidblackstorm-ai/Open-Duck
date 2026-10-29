# Native musical audio fixtures

The two WAVs are byte-identical copies from the dedicated OpenDuck TEST LAB project. Their provenance and SHA256 are recorded in fixture-manifest.json. No normalization or resampling is applied.

Run Revision030QualityAudit with this directory as its first argument and a new output directory as its second argument. It renders 35 cases through the real processor and exports latency-aligned WAVs plus quality-measures.csv. Renderer completion alone is not a subjective quality score; run Revision030QualityTests for the separate numerical assertions.
