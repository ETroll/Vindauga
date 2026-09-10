#pragma once
#include <QList>
#include <QString>

// FreeRDP's VAAPI H264 decoding defaults to /dev/dri/renderD128, which on a hybrid-GPU
// machine is often the NVIDIA card. NVIDIA's VAAPI implementation is far less complete
// than Intel's and produces repeated "Failed to transfer video frame (status=-38)
// (Function not implemented)" decode failures during video, visible as screen regions
// that stop updating (FreeRDP ignores a failed decode instead of requesting a new
// keyframe). Pinning to the Intel render node reduces the failure rate substantially.
//
// This file provides both a best-effort default (prefer non-NVIDIA when several GPUs
// exist) and the mechanism that lets the user override it from settings; see
// AppSettings::hardwareVideoDecoding and preferredVaapiDevice.
namespace vindauga {

// One detected GPU candidate for VAAPI decoding.
struct VaapiDeviceInfo {
    QString path;   // e.g. "/dev/dri/renderD129"
    QString label;  // human-readable, e.g. "Intel (i915)", for the settings dropdown
    QString driver; // raw kernel driver name (i915/nvidia/amdgpu/...), for pickPreferredVaapiDevice
};

// Reads /dev/dri/renderD* and /sys/class/drm/<node>/device/uevent (DRIVER=...) to list
// the VAAPI candidates on this machine. Empty list if /dev/dri is missing or empty; that
// is not an error, just nothing to choose from.
QList<VaapiDeviceInfo> listVaapiDevices();

// Best-effort heuristic: prefer the first non-NVIDIA device, else the first device, else
// an empty string (no recommendation; FreeRDP's own default selection applies).
QString pickPreferredVaapiDevice(const QList<VaapiDeviceInfo>& devices);

// Sets FreeRDP's own environment variables (read by libavcodec_init in h264_ffmpeg.c)
// before an RDP session starts, based on the user's settings:
//   - hardwareDecodingEnabled == false: FREERDP_HWCTX_TYPE=none (forces software decoding;
//     avoids VAAPI failures entirely at the cost of more CPU).
//   - explicitDevicePath non-empty: FREERDP_VAAPI_DEVICE=<explicitDevicePath> (the user's
//     own choice, always overrides the heuristic).
//   - otherwise: the pickPreferredVaapiDevice() result, if any device was found; FreeRDP's
//     default selection applies silently if none was.
// Safe to call repeatedly (at startup and whenever either setting changes): it always sets
// exactly the variables the situation requires and clears the others.
void applyVaapiEnvironment(bool hardwareDecodingEnabled, const QString& explicitDevicePath);

} // namespace vindauga
