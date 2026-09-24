var Godot;
var WebAssembly = {};
WebAssembly.instantiate = function(buffer, imports) {};
WebAssembly.instantiateStreaming = function(response, imports) {};

// WebGPU is not covered by Closure's default externs, so ADVANCED_OPTIMIZATIONS
// would otherwise rename these (e.g. `navigator.gpu` -> `navigator.g`), making
// Engine.requestWebGPUDevice() report "WebGPU is not supported" in every browser.
Navigator.prototype.gpu;
var GPU = function() {};
GPU.prototype.requestAdapter = function(options) {};
var GPURequestAdapterOptions = {};
GPURequestAdapterOptions.powerPreference;
var GPUAdapter = function() {};
GPUAdapter.prototype.features;
GPUAdapter.prototype.limits;
GPUAdapter.prototype.requestDevice = function(descriptor) {};
var GPUDeviceDescriptor = {};
GPUDeviceDescriptor.requiredFeatures;
GPUDeviceDescriptor.requiredLimits;
var GPUDevice = function() {};
GPUDevice.prototype.lost;
var GPUDeviceLostInfo = {};
GPUDeviceLostInfo.reason;
GPUDeviceLostInfo.message;
var GPUUncapturedErrorEvent = {};
GPUUncapturedErrorEvent.error;
