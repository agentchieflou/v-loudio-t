# Third-party code

Code we ship that we did not write, vendored as plain files (no
submodules). Everything here must be under a permissive license (noprod's
"Dependencies: we own what we ship"): MIT, BSD, Apache-2.0, ISC, Zlib,
MIT-0, CC0 or public domain.

| Directory | What | Version | License |
|---|---|---|---|
| `vst3sdk/` | Steinberg VST3 SDK: `pluginterfaces`, `base`, `public.sdk`, `cmake` | 3.8.1 (`v3.8.1_build_84`) | MIT (`vst3sdk/LICENSE.txt`); `public.sdk/source/vst/moduleinfo/json.h` is public domain (Unlicense) |

Elsewhere in the repo: `Framework/include/stb_truetype.h` (v1.26, MIT or
public domain, at your choice).

## vst3sdk

From https://github.com/steinbergmedia/vst3sdk at tag `v3.8.1_build_84`,
with these submodule revisions:

| Path | Commit |
|---|---|
| (root) `CMakeLists.txt`, `LICENSE.txt`, `README.md` | `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` |
| `base` | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` |
| `cmake` | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` |
| `pluginterfaces` | `4f547e8e102b47de4a8b8aaf343c73b700786372` |
| `public.sdk` | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` |

The files are unmodified. Left out: `vstgui4`, `doc`, `tutorials`, the
example plug-ins (`public.sdk/samples/vst`) and the hosting examples we
don't build (`public.sdk/samples/vst-hosting/editorhost`, which needs GTK
on Linux, and `inspectorapp`, which needs VSTGUI). Kept from the hosting
examples: `validator` (run in CI on every SDK-built plugin) and
`audiohost`, whose headers the validator's own tests include; its CMake
only builds it when JACK is found, and we never build it.

To update: replace the five directories above with the new tag's
contents, delete the same left-out parts, update this table, and run the
`vst3` CI job.
