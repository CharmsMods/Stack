# Runtime and IPC Contract

## Process Boundary

`Stack.exe` never loads ONNX Runtime, DirectML, or checkpoint code. It launches
the pinned `StackModelService.exe` from the validated package. The helper may
load only the two model paths present in its package manifest.

Control operations are versioned:

- `Health`
- `Denoise`
- `Cancel`
- `Shutdown`

Control messages travel through a per-process named pipe. Pixel payloads travel
through uniquely named, size-checked shared-memory mappings. Every request
contains protocol version, generation, model kind, dimensions, stride, buffer
size, and quality. The helper rejects unknown fields that change buffer
interpretation, overflowed sizes, path input, and unowned mapping names.

Only the service's control thread reads from or writes to the synchronous pipe.
The inference worker places completion responses in a guarded queue for that
thread to publish. This avoids a Windows synchronous-handle deadlock between a
blocking control read and a worker completion write.

## Tiling

```text
interactive preview: 256 x 256, 32-pixel overlap
settled / export:     384 x 384, 64-pixel overlap
```

Inputs use reflect padding to multiples of eight. Overlaps use raised-cosine
weights and normalized accumulation. Cancellation is checked between tiles.
An output from a generation older than Stack's latest request is discarded.

## GPU Transfer

The intended production path is a fenced PBO ring: the render owner must not
map a PBO in the frame that issued its download, and upload/publication must
follow the same generation check. That PBO path remains open; the current
development integration must not be described as having completed it. Model
work and pipe waits remain outside the UI thread.

DirectML is selected when its execution provider initializes successfully.
`STACK_RESTORMER_FORCE_CPU=1` is the explicit development fallback and
diagnostic path. It does not change the package, model, or recipe identity.

## Failure Contract

If the selected AI method is enabled and its exact package is missing,
tampered, incompatible, or crashes:

- no new image render is accepted;
- the last accepted image may remain only as a visibly stale reference;
- export is blocked;
- the UI names the package/action required; and
- processing resumes only after the package is restored, Denoise is disabled,
  or Classical Multiscale is explicitly selected.

There is no silent neutral pass-through and no silent Classical fallback for an
authored AI recipe.
