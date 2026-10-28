# For docs (task 76)

- `doc/classes/ProjectSettings.xml`, `rendering/rendering_device/staging_buffer/max_size_mb`: optional sentence.
  "Asynchronous downloads (`Texture2D.get_image_async()`, `RenderingDevice.texture_get_data_async()`,
  `buffer_get_data_async()`) never stall for room: a frame's downloads may use more than this, and the extra staging
  is freed once read. On the web, uploads are capped at 16 MB regardless of this setting." (Not changed on the branch;
  the class docs are the lead's call.)
- Wiki (Engine Additions, async readback page), if it says anything about large readbacks on the web: a full-HD HDR
  viewport can be read every frame on WebGPU, and the values match native.
