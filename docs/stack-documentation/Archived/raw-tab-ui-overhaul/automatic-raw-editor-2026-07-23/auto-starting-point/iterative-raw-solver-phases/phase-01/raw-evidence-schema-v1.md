# Raw Evidence Schema v1

## Identity And Lifetime

The public Phase 01 API is `Stack::RawEvidence::RawTechnicalEvidenceRecord` in
`src/Raw/RawTechnicalEvidence.h`.

Its identity is:

```text
source_identity = SHA256(original file bytes)
decode_identity = SHA256(canonical decode-affecting fields)
evidence_identity = SHA256(source_identity | decode_identity | feature_version)
```

The canonical decode fields include decoder backend/version, normalization
version, linearization policy, active/masked-area policy, black/white
overrides, opcode/gain application policy, decoded layout, sample format, raw
dimensions, and CFA pattern. Exposure, WB choice, Local Range, Finish Tone,
Display Fit, crop presentation, and solver settings are not raw-decode fields.

The cache is keyed by `evidence_identity`. It returns an immutable const record
and is invalidated naturally by source bytes, decode policy, or feature version.

## Measurement Contract

Every numeric measurement uses `EvidenceMeasurement`:

```text
valid
value or JSON null
units
stage
provenance: measured | metadata | derived | fallback | unavailable
uncertainty01
reason
```

Missing metadata never becomes an ordinary zero. The record itself also names
the source, decode, feature, normalization, sensor rectangle, CFA, plane order,
orientation transform, sample count, runtime, warnings, and status.

## Normalization

For stored sample `r` at sensor coordinate `(u,v)` and output color plane `c`:

```text
l = LinearizationTable[r] when present and enabled, else r
b(u,v,c) = repeating BlackLevel + BlackLevelDeltaH[u] + BlackLevelDeltaV[v]
b_max(c) = maximum applicable b for color c over the active sensor classes
x(u,v,c) = (l - b(u,v,c)) / (WhiteLevel(c) - b_max(c))
```

The evidence path preserves values below zero and above one. It does not clamp
before percentiles, clipping, or black diagnostics. Invalid tables,
non-finite values, or non-positive denominators invalidate the affected
samples and lower/stop the record rather than substituting display data.

## Sensor Geometry

`ActiveArea` and `MaskedAreas` remain in stored sensor coordinates. When an
explicit ActiveArea is absent, the decoded visible rectangle is a named
fallback. Masked pixels may contribute to stored-code black mean/std/delta,
but are excluded from active photographic raw samples.

Orientation is stored separately as the sensor-to-photographic transform.
Changing only orientation cannot change raw plane statistics.

## Planes, Clipping, And Headroom

The output plane order is declared `R, G, B`; CFA pattern and DNG
`CFAPlaneColor` map stored sensels into those records.

Each plane retains:

```text
sample count
maximum applicable black
white level
LinearResponseLimit or explicit unavailable state
p999 normalized raw
near-nonlinear fraction
near-white fraction
clipped fraction
headroom EV
technical WB multiplier
WB-scaled headroom EV
```

Headroom uses:

```text
limit_c = min(1, valid LinearResponseLimit; otherwise 1)
h_c = log2(limit_c / max(epsilon, p999_c))
h_wb_c = log2(limit_c / max(epsilon, g_c * p999_c))
```

Technical WB gains are normalized to geometric mean one. DNG AsShotNeutral is
metadata provenance; decoder camera WB is a named lower-confidence fallback.

CFA-aligned 2x2 superpixels aggregate the two green sensels into one green
plane maximum, then count zero, one, two, or three clipped output planes. The
record does not infer co-sited clipping from independent mosaic-wide counts.

## Noise

Valid DNG NoiseProfile coefficients retain normalized raw units:

```text
variance_c(x) = S_c*x + O_c
sigma_c(x) = sqrt(S_c*x + O_c)
SNR_c(x) = x / sigma_c(x)
```

One coefficient pair applies to every plane; otherwise pairs follow DNG
`CFAPlaneColor` order and are mapped into declared output R/G/B records.
Diagnostic SNR samples use fixed declared normalized signal levels. No lift
limit or objective threshold is chosen in Phase 01.

When NoiseProfile is missing, S/O and SNR are unavailable. BaselineNoise may
be reported as capability context but is not converted into camera-specific
coefficients.

## Opcode And Profile Coverage

The record reports counts and application state separately for OpcodeList1,
OpcodeList2, and OpcodeList3. The Phase 01 evidence record is pre-opcode unless
its decode identity explicitly declares otherwise. Current Stack GainMap
support is reported as pipeline capability, not silently applied to sensor
safety measurements.

ProfileGainTableMap and ProfileGainTableMap2 presence is reported. Neither is
implemented by this phase.

## Consumer Rule

Phase 02 may consume this frozen API and its uncertainty. It may not reparse
the original RAW independently, reinterpret null as zero, or move any control.
Any schema, normalization, or decoder identity change creates a new version and
regenerates the focused fixtures and corpus report.
