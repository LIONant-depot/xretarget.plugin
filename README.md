# xretarget.plugin

The **Retarget** resource: moves animation from one skeleton to another. It names a source and a destination skeleton and holds the bone mapping
(automatic, then corrected by the user), the calibration poses, the model-frame registration and the policies (travel, unmapped bones). The animation
editor applies it to clips imported on the source skeleton and bakes an ordinary clip of the destination skeleton.

- `source/xretarget_descriptor.h`: what the user edits and saves.
- `source/xretarget.h`: the compiled resource (the resolved mapping and signatures of both skeletons).
- `source/compiler/`: the compiler: reads both skeletons' bone manifests, maps the bones (xAnimation's `retarget` module) and writes the result.

The algorithm lives in the [xAnimation](https://github.com/LIONant-depot/xAnimation) library; this plugin is the resource and its editor.

## License
MIT.
