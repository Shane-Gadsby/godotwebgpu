// Godot loads its side module at runtime through Module.dynamicLibraries (the JS
// shell sets it), not by linking it at build time. That difference decides whether
// the side module is instantiated during a worker's own early bootstrap.
Module['dynamicLibraries'] = ['side.wasm'];
