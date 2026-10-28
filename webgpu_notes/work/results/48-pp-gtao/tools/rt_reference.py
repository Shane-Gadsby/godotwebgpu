#!/usr/bin/env python3
"""Ray traced ambient occlusion reference for gtao_scene.gd (same geometry and camera).

AO(p) = 1/pi * integral over the hemisphere of V(w) cos(theta) dw, V = 0 if a ray from p along w hits geometry
within the AO radius R, 1 otherwise: the quantity GTAO estimates (Jimenez et al. 2016, eq. 1; XeGTAO compares
against a reference of this form). Cosine-weighted Monte Carlo with M rays per pixel.

Usage: rt_reference.py out.npz [W H M R]
"""

import sys

import numpy as np

W = int(sys.argv[2]) if len(sys.argv) > 2 else 960
H = int(sys.argv[3]) if len(sys.argv) > 3 else 540
M = int(sys.argv[4]) if len(sys.argv) > 4 else 256
R = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
FOV = 60.0
EYE = np.array([2.6, 1.9, 3.4])
TARGET = np.array([0.0, 0.45, 0.0])

# (size, center, yaw degrees), as gtao_scene.gd
BOXES = [
    (np.array([1.0, 1.0, 1.0]), np.array([0.0, 0.5, 0.0]), 30.0),
    (np.array([5.0, 2.0, 0.3]), np.array([0.0, 1.0, -1.6]), 0.0),
    (np.array([0.3, 0.6, 0.3]), np.array([-1.2, 0.3, 0.6]), 0.0),
]
SPHERE_C = np.array([1.25, 0.5, 0.55])
SPHERE_R = 0.5
PLANE_HALF = 10.0
INF = np.float64(1e30)


def rot_y(deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    # Godot rotation about +Y: x' = c x + s z, z' = -s x + c z
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def hit_plane(o, d):
    t = np.full(o.shape[0], INF)
    n = np.zeros_like(o)
    with np.errstate(divide="ignore", invalid="ignore"):
        tt = -o[:, 1] / d[:, 1]
    p = o + d * tt[:, None]
    ok = (tt > 0) & (np.abs(p[:, 0]) <= PLANE_HALF) & (np.abs(p[:, 2]) <= PLANE_HALF) & (d[:, 1] != 0)
    t[ok] = tt[ok]
    n[:, 1] = np.where(o[:, 1] >= 0, 1.0, -1.0)
    return t, n


def hit_box(o, d, size, center, yaw):
    Rm = rot_y(yaw)
    # world -> local: inverse rotation (transpose)
    lo = (o - center) @ Rm  # (R^T (o-c))^T = (o-c)^T R
    ld = d @ Rm
    half = size / 2
    with np.errstate(divide="ignore", invalid="ignore"):
        inv = 1.0 / ld
        t1 = (-half - lo) * inv
        t2 = (half - lo) * inv
    tmin = np.minimum(t1, t2)
    tmax = np.maximum(t1, t2)
    tnear = np.max(tmin, axis=1)
    tfar = np.min(tmax, axis=1)
    ok = (tnear <= tfar) & (tfar > 0) & (tnear > 0)
    t = np.where(ok, tnear, INF)
    axis = np.argmax(tmin, axis=1)
    ln = np.zeros_like(o)
    idx = np.arange(o.shape[0])
    ln[idx, axis] = -np.sign(ld[idx, axis])
    n = ln @ Rm.T
    return t, n


def hit_sphere(o, d):
    oc = o - SPHERE_C
    b = np.sum(oc * d, axis=1)
    c = np.sum(oc * oc, axis=1) - SPHERE_R**2
    disc = b * b - c
    ok = disc >= 0
    sq = np.sqrt(np.maximum(disc, 0))
    t0 = -b - sq
    t1 = -b + sq
    t = np.where(t0 > 0, t0, t1)
    ok &= t > 0
    t = np.where(ok, t, INF)
    p = o + d * t[:, None]
    n = (p - SPHERE_C) / SPHERE_R
    return t, n


def trace(o, d):
    t, n = hit_plane(o, d)
    for size, c, yaw in BOXES:
        tb, nb = hit_box(o, d, size, c, yaw)
        m = tb < t
        t = np.where(m, tb, t)
        n[m] = nb[m]
    ts, ns = hit_sphere(o, d)
    m = ts < t
    t = np.where(m, ts, t)
    n[m] = ns[m]
    return t, n


def camera_rays():
    f = TARGET - EYE
    f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    th = np.tan(np.radians(FOV) / 2)
    aspect = W / H
    xs = ((np.arange(W) + 0.5) / W * 2 - 1) * th * aspect
    ys = (1 - (np.arange(H) + 0.5) / H * 2) * th
    X, Y = np.meshgrid(xs, ys)
    d = X[..., None] * r + Y[..., None] * u + f
    d = d.reshape(-1, 3)
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    o = np.broadcast_to(EYE, d.shape).copy()
    return o, d


def main():
    o, d = camera_rays()
    t, n = trace(o, d)
    hit = t < INF
    p = o + d * t[:, None]
    # Face the normal towards the camera (single sided plane seen from above anyway).
    flip = np.sum(n * d, axis=1) > 0
    n[flip] *= -1
    ao = np.ones(W * H)
    rng = np.random.default_rng(1)
    idx = np.nonzero(hit)[0]
    chunk = 4096
    for s in range(0, idx.size, chunk):
        ii = idx[s : s + chunk]
        k = ii.size
        P = np.repeat(p[ii], M, axis=0)
        N = np.repeat(n[ii], M, axis=0)
        # Cosine weighted directions around N (stratified in a sqrt(M) grid).
        g = int(np.sqrt(M))
        su = (np.tile(np.arange(M) % g, k) + rng.random(k * M)) / g
        sv = (np.tile(np.arange(M) // g, k) + rng.random(k * M)) / g
        rr = np.sqrt(su)
        phi = 2 * np.pi * sv
        lx, ly, lz = rr * np.cos(phi), rr * np.sin(phi), np.sqrt(np.maximum(0, 1 - su))
        a = np.where(np.abs(N[:, 0:1]) > 0.9, [[0, 1, 0]], [[1, 0, 0]])
        T = np.cross(N, a)
        T /= np.linalg.norm(T, axis=1, keepdims=True)
        B = np.cross(N, T)
        D = T * lx[:, None] + B * ly[:, None] + N * lz[:, None]
        th, _ = trace(P + N * 1e-4, D)
        vis = (th > R).reshape(k, M).mean(axis=1)
        ao[ii] = vis
        if (s // chunk) % 20 == 0:
            print(f"{s}/{idx.size}", flush=True)
    np.savez_compressed(
        sys.argv[1],
        ao=ao.reshape(H, W),
        hit=hit.reshape(H, W),
        depth=(t * np.sum(d * ((TARGET - EYE) / np.linalg.norm(TARGET - EYE)), axis=1)).reshape(H, W),
        normal=n.reshape(H, W, 3),
    )


if __name__ == "__main__":
    main()
