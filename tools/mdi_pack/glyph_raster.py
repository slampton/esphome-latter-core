"""SVG path parser, elliptical-arc to Bezier converter, and 24x24 bytecode encoder."""

from __future__ import annotations

import math
import re

BUCKET_ICONS = 16
OP_MOVE = 251
OP_HLINE = 252
OP_VLINE = 253
OP_CUBIC = 254
OP_CLOSE = 255

NUM_RE = re.compile(r"\s*,?\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)")
FLAG_RE = re.compile(r"\s*,?\s*([01])")
CMD_RE = re.compile(r"\s*([MmZzLlHhVvCcSsQqTtAa])")


def fnv1a_32(norm_slug: str) -> int:
    h = 0x811C9DC5
    for b in norm_slug.encode("ascii"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def q_coord(v: float) -> int:
    return max(0, min(250, round((v + 0.5) * 10.0)))


def arc_to_cubics(
    x0: float,
    y0: float,
    rx: float,
    ry: float,
    phi_deg: float,
    large_arc: int,
    sweep: int,
    x: float,
    y: float,
) -> list[tuple[float, float, float, float, float, float]]:
    rx = abs(rx)
    ry = abs(ry)
    if rx < 1e-5 or ry < 1e-5 or (abs(x - x0) < 1e-6 and abs(y - y0) < 1e-6):
        return []
    phi = math.radians(phi_deg % 360.0)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)
    dx2 = (x0 - x) * 0.5
    dy2 = (y0 - y) * 0.5
    x1p = cos_phi * dx2 + sin_phi * dy2
    y1p = -sin_phi * dx2 + cos_phi * dy2
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s_lam = math.sqrt(lam)
        rx *= s_lam
        ry *= s_lam
    rx2, ry2 = rx * rx, ry * ry
    num = max(0.0, rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p)
    den = rx2 * y1p * y1p + ry2 * x1p * x1p
    coef = math.sqrt(num / den) if den > 1e-12 else 0.0
    if large_arc == sweep:
        coef = -coef
    cxp = coef * (rx * y1p / ry)
    cyp = coef * (-ry * x1p / rx)
    acx = cos_phi * cxp - sin_phi * cyp + (x0 + x) * 0.5
    acy = sin_phi * cxp + cos_phi * cyp + (y0 + y) * 0.5

    def vec_angle(ux: float, uy: float, vx: float, vy: float) -> float:
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    ux, uy = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx, vy = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    theta1 = vec_angle(1.0, 0.0, ux, uy)
    dtheta = vec_angle(ux, uy, vx, vy)
    if not sweep and dtheta > 0:
        dtheta -= 2.0 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2.0 * math.pi

    segs = max(1, math.ceil(abs(dtheta) / (math.pi / 2.0 + 1e-6)))
    dt = dtheta / segs
    alpha = (4.0 / 3.0) * math.tan(dt * 0.25)
    out = []
    for i in range(segs):
        t0 = theta1 + i * dt
        t1 = t0 + dt
        c0, s0 = math.cos(t0), math.sin(t0)
        c1, s1 = math.cos(t1), math.sin(t1)
        p1x, p1y = c0 - alpha * s0, s0 + alpha * c0
        p2x, p2y = c1 + alpha * s1, s1 - alpha * c1
        p3x, p3y = c1, s1

        def xform(px: float, py: float) -> tuple[float, float]:
            return (
                cos_phi * rx * px - sin_phi * ry * py + acx,
                sin_phi * rx * px + cos_phi * ry * py + acy,
            )

        bx1, by1 = xform(p1x, p1y)
        bx2, by2 = xform(p2x, p2y)
        bx3, by3 = (x, y) if i == segs - 1 else xform(p3x, p3y)
        out.append((bx1, by1, bx2, by2, bx3, by3))
    return out


def encode_svg_path(d: str) -> bytes:
    pos = 0
    n = len(d)
    cmd = ""
    cx = cy = sx = sy = prev_ctrl_x = prev_ctrl_y = 0.0
    prev_cmd = ""
    buf = bytearray()
    cur_qx = cur_qy = -1

    def emit_line(nx: float, ny: float) -> None:
        nonlocal cur_qx, cur_qy
        qx, qy = q_coord(nx), q_coord(ny)
        if qx == cur_qx and qy == cur_qy:
            return
        if qy == cur_qy:
            buf.extend((OP_HLINE, qx))
        elif qx == cur_qx:
            buf.extend((OP_VLINE, qy))
        else:
            buf.extend((qx, qy))
        cur_qx, cur_qy = qx, qy

    def emit_cubic(
        x1: float, y1: float, x2: float, y2: float, x3: float, y3: float
    ) -> None:
        nonlocal cur_qx, cur_qy
        qx1, qy1 = q_coord(x1), q_coord(y1)
        qx2, qy2 = q_coord(x2), q_coord(y2)
        qx3, qy3 = q_coord(x3), q_coord(y3)
        buf.extend((OP_CUBIC, qx1, qy1, qx2, qy2, qx3, qy3))
        cur_qx, cur_qy = qx3, qy3

    def read_num() -> float:
        nonlocal pos
        m = NUM_RE.match(d, pos)
        if not m:
            raise ValueError(f"Expected number at {pos} in {d!r}")
        pos = m.end()
        return float(m.group(1))

    def read_flag() -> int:
        nonlocal pos
        m = FLAG_RE.match(d, pos)
        if not m:
            raise ValueError(f"Expected flag at {pos} in {d!r}")
        pos = m.end()
        return int(m.group(1))

    def has_more_args() -> bool:
        nonlocal pos
        while pos < n and d[pos] in " \t\r\n,":
            pos += 1
        return pos < n and d[pos] not in "MmZzLlHhVvCcSsQqTtAa"

    while pos < n:
        while pos < n and d[pos] in " \t\r\n,":
            pos += 1
        if pos >= n:
            break
        m_cmd = CMD_RE.match(d, pos)
        if m_cmd:
            cmd = m_cmd.group(1)
            pos = m_cmd.end()
        if cmd in ("Z", "z"):
            buf.append(OP_CLOSE)
            cx, cy = sx, sy
            cur_qx, cur_qy = q_coord(cx), q_coord(cy)
            prev_cmd = cmd
            cmd = ""
            continue
        first_iter = True
        while first_iter or has_more_args():
            first_iter = False
            if cmd in ("M", "m"):
                rel = cmd == "m"
                nx = read_num() + (cx if rel else 0.0)
                ny = read_num() + (cy if rel else 0.0)
                cx, cy = nx, ny
                sx, sy = cx, cy
                cur_qx, cur_qy = q_coord(cx), q_coord(cy)
                buf.extend((OP_MOVE, cur_qx, cur_qy))
                prev_cmd = cmd
                cmd = "l" if rel else "L"
            elif cmd in ("L", "l"):
                rel = cmd == "l"
                cx = read_num() + (cx if rel else 0.0)
                cy = read_num() + (cy if rel else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("H", "h"):
                cx = read_num() + (cx if cmd == "h" else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("V", "v"):
                cy = read_num() + (cy if cmd == "v" else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("C", "c"):
                rel = cmd == "c"
                x1 = read_num() + (cx if rel else 0.0)
                y1 = read_num() + (cy if rel else 0.0)
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                x3 = read_num() + (cx if rel else 0.0)
                y3 = read_num() + (cy if rel else 0.0)
                emit_cubic(x1, y1, x2, y2, x3, y3)
                prev_ctrl_x, prev_ctrl_y = x2, y2
                cx, cy = x3, y3
                prev_cmd = cmd
            elif cmd in ("S", "s"):
                rel = cmd == "s"
                has_prev_c = prev_cmd in ("C", "c", "S", "s")
                x1 = (2.0 * cx - prev_ctrl_x) if has_prev_c else cx
                y1 = (2.0 * cy - prev_ctrl_y) if has_prev_c else cy
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                x3 = read_num() + (cx if rel else 0.0)
                y3 = read_num() + (cy if rel else 0.0)
                emit_cubic(x1, y1, x2, y2, x3, y3)
                prev_ctrl_x, prev_ctrl_y = x2, y2
                cx, cy = x3, y3
                prev_cmd = cmd
            elif cmd in ("Q", "q", "T", "t"):
                rel = cmd in ("q", "t")
                if cmd in ("Q", "q"):
                    qx1 = read_num() + (cx if rel else 0.0)
                    qy1 = read_num() + (cy if rel else 0.0)
                else:
                    has_prev_q = prev_cmd in ("Q", "q", "T", "t")
                    qx1 = (2.0 * cx - prev_ctrl_x) if has_prev_q else cx
                    qy1 = (2.0 * cy - prev_ctrl_y) if has_prev_q else cy
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                emit_cubic(
                    cx + (2.0 / 3.0) * (qx1 - cx),
                    cy + (2.0 / 3.0) * (qy1 - cy),
                    x2 + (2.0 / 3.0) * (qx1 - x2),
                    y2 + (2.0 / 3.0) * (qy1 - y2),
                    x2,
                    y2,
                )
                prev_ctrl_x, prev_ctrl_y = qx1, qy1
                cx, cy = x2, y2
                prev_cmd = cmd
            elif cmd in ("A", "a"):
                rel = cmd == "a"
                rx, ry, rot = read_num(), read_num(), read_num()
                la, sw = read_flag(), read_flag()
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                cubs = arc_to_cubics(cx, cy, rx, ry, rot, la, sw, x2, y2)
                if not cubs:
                    emit_line(x2, y2)
                else:
                    for c_seg in cubs:
                        emit_cubic(*c_seg)
                cx, cy = x2, y2
                prev_cmd = cmd
    return bytes(buf)


def verify_bytecode_stream(blob: bytes) -> None:
    i = 0
    n = len(blob)
    while i < n:
        op = blob[i]
        if op == OP_MOVE:
            if i + 2 >= n or blob[i + 1] > 250 or blob[i + 2] > 250:
                raise ValueError("Invalid OP_MOVE")
            i += 3
        elif op in (OP_HLINE, OP_VLINE):
            if i + 1 >= n or blob[i + 1] > 250:
                raise ValueError("Invalid OP_HLINE/OP_VLINE")
            i += 2
        elif op == OP_CUBIC:
            if i + 6 >= n or any(b > 250 for b in blob[i + 1 : i + 7]):
                raise ValueError("Invalid OP_CUBIC")
            i += 7
        elif op == OP_CLOSE:
            i += 1
        else:
            if i + 1 >= n or op > 250 or blob[i + 1] > 250:
                raise ValueError("Invalid implicit LINE_TO")
            i += 2
