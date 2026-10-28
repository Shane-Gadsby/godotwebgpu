# For docs (task 77)

- `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md` (wherever task 71's get_image() warning is described): add that debug
  web builds also warn once per resource for a script's synchronous `RenderingDevice.texture_get_data()` (empty array)
  and `buffer_get_data()` (zeros), pointing at the `_async` methods; release and native never.
- Class reference: done on the branch (`doc/classes/RenderingDevice.xml`, one sentence on each web note).
- Wiki (Engine Additions), if task 71's warning is listed there: the same one line.
