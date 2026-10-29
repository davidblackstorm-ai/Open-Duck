# Public settings and measured SLOPE scope

Updated 2026-10-29. The measured reference is Kickstart 2 VST3 2.0.1. This report uses public saved controls and synthetic audio only. No reference implementation code was inspected. The complete numerical manifest is [FUNCTIONAL-COMPARISON-v0.4.1.json](FUNCTIONAL-COMPARISON-v0.4.1.json).

The independent reconstruction sorts control points by x, repeats each control according to its `factor`, and evaluates a periodic uniform cubic parametric B-spline in x and gain. Inverting x reconstructs all 16 default 8192-node curves with maximum gain error 9.02e-8. Ten controlled public-setting mutations also fit, with maximum error 8.85e-8. Periodic Catmull-Rom interpolation between these nodes is a separate audio sampling stage and retains the reference's signed overshoot.

Seven independent saved geometries of Wave 1 corroborate a specific edit: controls 0 and 2 retain their positions; controls 1, 3, 4 and 5 all translate by the same positive x displacement. Their y coordinates and multiplicities remain unchanged. The seven displacements are 862, 1128, 1028, 1414, 2189, 2609 and 1714 table units in an 8192-node period. The outer controls lie at x0 and x8110; the interior control range initially spans x573 through x4096. Selecting controls by `factor` alone would be incorrect.

The production reconstruction uses precomputed segment polynomials, monotone segment search and bounded safeguarded Newton inversion. It samples four integer nodes before Catmull-Rom. Compared with the seven independent rendered geometries, maximum gain error is 1.44e-7. SLOPE zero retains the original measured table exactly. No audio-thread table rebuild, allocation or waiting lock is introduced by this sampler.

These observations support Wave 1's measured positive translations. Extending the same outer-fixed/internal-shift rule to the other 15 forms is an implementation generalization consistent with their control topology. It is not a measured SLOPE gesture for those forms. Likewise, finite samples at negative/positive extremes demonstrate numerical safety, not reference equivalence or experimentally established GUI collision limits.

The graph guide uses the translated `slopeTargetBounds`, following the editable controls. The earlier interpretation that the guide should remain fixed is withdrawn: the observed drag produced no saved control changes, so it could not establish guide behaviour during a successful edit. No guide parity claim follows from that observation.

The current official [manual](https://downloads.cableguys.com/Nicky-Romero-Kickstart-2-Manual.pdf?v=2.0.8) describes dragging the coloured region to move the sloped part. Its retrieved header identifies v2.0.9; numerical evidence here refers specifically to the measured installed v2.0.1. The manual supports control intent, not an unmeasured mathematical or pixel-level equivalence claim.

Only aggregate settings, synthetic measurement hashes and errors are included in the public reports. Private XML, project contents, personal paths and track names are excluded.
