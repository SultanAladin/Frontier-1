UTAH TEAPOT: APPROXIMATE SIGNED DISTANCE VOLUME

APPROXIMATE: source is open; no reliable closed-solid sign is claimed

Exact Euclidean point-to-triangle distance via MeshBVH; sign by majority of three oblique ray parities, deduplicating coincident triangle hits. Topology audited by tolerance-welded edges; query geometry is not modified.

A mesh volume, not a 2D surface texture or projection-height alias. Signing uses an odd-even fill rule, not a constructive union of overlapping solids.

Strict mode requires a closed consistently oriented edge-manifold source; topology checks cannot certify absence of geometric self-intersections.

Open/intersecting sources do not define an unambiguous solid. Approximate signs are explicitly flagged; use unsigned distances or supply a repaired closed mesh when reliable signs are required.

Samples are voxel centres, not voxel corners. Trilinear reconstruction introduces grid-resolution error; stored distances are unclamped Float32 values in scene units.

A 512×512 texture-map bake is independent of volume resolution. Volume resolution is bounded to 16–128 per axis and 250,000 source triangles.