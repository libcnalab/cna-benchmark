# Current CNA integration

The suite was last accepted against CNA commit `421a38e4f` on 2026-10-27. The matching
`cna-benchmark` consumer commit is `ee012a8`.

## Streaming uploads

The streaming scene rewrites and redraws a complete texture every frame. XNA refuses `SetData`
while that texture is bound for sampling, and `SpriteBatch::End` deliberately leaves the last
drawn texture in pixel sampler slot zero. The scene therefore unbinds slot zero before its next
upload. This is a consumer responsibility, not a relaxation of CNA's resource-in-use rule.

The same run exposed avoidable CNA allocation work. A full `Texture2D::SetData(const Color*, int)`
now updates a unique single-level native texture renderer in place. If the renderer is shared with
another cached value wrapper, CNA still creates a detached renderer so one wrapper cannot observe
another wrapper's upload. Render targets retain their live-surface behavior. The change added no
public API, dependency, stub or intentional XNA deviation.

## Acceptance evidence

The 2026-10-27 Linux acceptance used Mesa through CNA's private Weston/Xwayland runner:

- A Release multi-renderer build carried OPENGLES3, SDL_GPU, SOFTWARE, HEADLESS and STUB.
- The benchmark's structural CTest suite passed 10/10, including repeated HEADLESS streaming.
- CNA's focused `Texture2DTest.*:Texture2DCacheReconstructionTest.*` slice passed 55/55.
- All seven scenes completed their standard 60 warm-up and 300 measured frames on SDL_GPU and
  OPENGLES3. Pixel probes passed and both JSON results passed `verify-result.py`.
- SOFTWARE completed the streaming scene. HEADLESS completed all seven scenes but cannot prove
  output pixels; STUB correctly skipped its five unsupported 3D scenes.

This evidence establishes functional completion, not a stable performance comparison. The host
was shared, and before/after timings outside the changed streaming scene varied too much to support
a performance claim. No further CNA failure was reproduced in the default renderer set.
