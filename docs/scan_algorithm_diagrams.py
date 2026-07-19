"""
扫描算法设计方案 — 示意图 (3幅)
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import os

C_BLUE   = "#2166AC"
C_GRAY   = "#555555"
C_LIGHT  = "#A0A0A0"
C_ORANGE = "#D55E00"
C_BG     = "#FFFFFF"

plt.rcParams.update({
    "font.family": "Microsoft YaHei",
    "font.size": 10,
    "axes.edgecolor": C_GRAY,
    "axes.linewidth": 0.8,
    "xtick.color": C_GRAY,
    "ytick.color": C_GRAY,
    "text.color": "#222222",
    "figure.dpi": 200,
    "figure.facecolor": C_BG,
    "axes.facecolor": C_BG,
})

OUT_DIR = os.path.join(os.path.dirname(__file__), "diagrams")
os.makedirs(OUT_DIR, exist_ok=True)


# ═══ 图 1: S 形扫描路径 ═══
def draw_s_curve():
    COLS, ROWS = 6, 5
    fig, ax = plt.subplots(figsize=(8, 6))
    ax.set_xlim(-0.6, COLS - 1 + 1.8)
    ax.set_ylim(-0.6, ROWS - 1 + 0.6)
    ax.set_aspect("equal")
    ax.axis("off")

    # 网格点
    for r in range(ROWS):
        for c in range(COLS):
            ax.plot(c, r, "o", color=C_LIGHT, markersize=3.0, zorder=2)

    # 路径箭头
    for r in range(ROWS):
        xs = list(range(COLS)) if r % 2 == 0 else list(range(COLS - 1, -1, -1))
        for i in range(COLS - 1):
            ax.annotate("", xy=(xs[i + 1], r), xytext=(xs[i], r),
                        arrowprops=dict(arrowstyle="->", color=C_BLUE if r % 2 == 0 else C_GRAY,
                                        lw=1.5, shrinkA=5, shrinkB=5))

    # 行间跳转
    for r in range(ROWS - 1):
        x = COLS - 1 if r % 2 == 0 else 0
        ax.plot([x, x], [r, r + 1], linestyle="dotted", color=C_LIGHT, lw=0.8)

    # 起点
    ax.plot(0, 0, marker="s", color=C_BLUE, markersize=10, zorder=10,
            fillstyle="none", markeredgewidth=1.5)
    # 终点
    last_r = ROWS - 1
    last_c = 0 if last_r % 2 != 0 else COLS - 1
    ax.plot(last_c, last_r, marker="s", color=C_ORANGE, markersize=10, zorder=10,
            fillstyle="none", markeredgewidth=1.5)

    # ── 注释 ──
    ax.annotate("起点 (0, 0)", xy=(0, 0), xytext=(0.8, -0.8),
                fontsize=8, color=C_BLUE, ha="center",
                arrowprops=dict(arrowstyle="->", color=C_BLUE, lw=0.8))

    ax.annotate("终点", xy=(last_c, last_r), xytext=(last_c + 0.8, last_r - 0.5),
                fontsize=8, color=C_ORANGE, ha="left",
                arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=0.8))

    # 两行示例标注
    ax.annotate("偶数行: 左→右", xy=(2.5, 1.5), xytext=(COLS + 0.3, 2.0),
                fontsize=8, color=C_BLUE, ha="left",
                arrowprops=dict(arrowstyle="->", color=C_BLUE, lw=0.8, connectionstyle="arc3,rad=0.2"))
    ax.annotate("奇数行: 右→左", xy=(2.5, 2.5), xytext=(COLS + 0.3, 3.0),
                fontsize=8, color=C_GRAY, ha="left",
                arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=0.8, connectionstyle="arc3,rad=-0.2"))

    fig.tight_layout(pad=0.3)
    fig.savefig(os.path.join(OUT_DIR, "01_s_curve_path.png"), bbox_inches="tight",
                facecolor=C_BG, edgecolor="none")
    plt.close(fig)


# ═══ 图 2: 球冠 Z 补偿 ═══
def draw_spherical_cap():
    Rv, h, z0, dH = 5.0, 2.0, 4.0, 0.3
    Rs = (Rv**2 + h**2) / (2 * h)
    cz = z0 - Rs + h

    fig, (ax_geo, ax_z) = plt.subplots(2, 1, figsize=(7, 7.5),
                                        gridspec_kw={"height_ratios": [1.0, 0.7]})
    fig.subplots_adjust(hspace=0.3)

    # ── 上图: 几何剖面 ──
    ax_geo.set_aspect("equal")
    ax_geo.set_xlim(-Rv - 1.2, Rv + 1.8)
    ax_geo.set_ylim(z0 - 0.6, z0 + h + 0.6)
    ax_geo.axis("off")

    ax_geo.axhline(y=z0, color=C_LIGHT, linestyle="--", lw=0.8)
    theta = np.linspace(0, np.pi, 400)
    xx = Rs * np.cos(theta)
    yy = cz + Rs * np.sin(theta)
    ax_geo.plot(xx[yy >= z0], yy[yy >= z0], color=C_BLUE, lw=1.8)
    ax_geo.plot([-Rv, Rv], [z0, z0], color=C_BLUE, lw=1.5)

    # 顶点
    ax_geo.plot(0, z0 + h, "o", color=C_ORANGE, markersize=6, zorder=10)

    # 尺寸标注
    ax_geo.annotate("", xy=(Rv, z0), xytext=(0, z0),
                    arrowprops=dict(arrowstyle="<->", color=C_GRAY, lw=0.8))
    ax_geo.text(Rv / 2, z0 - 0.22, "R", fontsize=9, color=C_GRAY, ha="center")
    ax_geo.annotate("", xy=(0.5, z0 + h), xytext=(0.5, z0),
                    arrowprops=dict(arrowstyle="<->", color=C_GRAY, lw=0.8))
    ax_geo.text(0.85, z0 + h / 2, "h_cap", fontsize=9, color=C_GRAY, va="center")

    # 两个采样点
    for sx, color, label, tx, ty in [
        (0,    C_ORANGE, "r = 0, Z = zBase − h_cap − dH", 0.3, z0 + h - dH + 0.25),
        (Rv,   C_GRAY,   "r = R, Z = zBase − dH",          Rv + 0.15, z0 - dH - 0.22),
    ]:
        sz = z0 + np.sqrt(max(0, Rs**2 - sx**2)) - (Rs - h) - dH
        ax_geo.plot(sx, sz, "o", color=color, markersize=6, zorder=10)
        ax_geo.annotate(label, xy=(sx, sz), xytext=(tx, ty),
                        fontsize=8, color=color, ha="left",
                        arrowprops=dict(arrowstyle="->", color=color, lw=0.7))

    # ── 下图: Z 曲线 ──
    ax_z.set_xlim(-0.2, Rv + 0.8)
    ax_z.set_ylim(z0 - h - 0.4, z0 + 0.2)
    ax_z.set_xlabel("r")
    ax_z.set_ylabel("Z", rotation=0, labelpad=10)
    ax_z.tick_params(labelsize=8)

    rv = np.linspace(0, Rv + 1.0, 500)
    zv = np.full_like(rv, z0)
    inner = rv <= Rv
    cap = np.sqrt(np.maximum(0, Rs**2 - rv[inner]**2)) - (Rs - h)
    zv[inner] = np.clip(z0 - cap - dH, 0, z0)

    ax_z.plot(rv, zv, color=C_BLUE, lw=1.8)
    ax_z.axhline(y=z0, color=C_LIGHT, linestyle="--", lw=0.8)
    ax_z.axvline(x=Rv, color=C_GRAY, linestyle="--", lw=0.7, alpha=0.5)

    ax_z.annotate("r ≤ R: Z = zBase − cap(r) − dH",
                  xy=(Rv / 2, (z0 + (z0 - h - dH)) / 2),
                  xytext=(Rv / 2 + 0.5, z0 - h + 0.5),
                  fontsize=8, color=C_BLUE, ha="left",
                  arrowprops=dict(arrowstyle="->", color=C_BLUE, lw=0.7))
    ax_z.annotate("r > R: Z = zBase",
                  xy=(Rv + 0.5, z0), xytext=(Rv + 0.3, z0 - 0.25),
                  fontsize=8, color=C_GRAY, ha="left",
                  arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=0.7))

    fig.tight_layout(pad=0.3)
    fig.savefig(os.path.join(OUT_DIR, "02_spherical_cap.png"), bbox_inches="tight",
                facecolor=C_BG, edgecolor="none")
    plt.close(fig)


# ═══ 图 3: 单点处理 — 流水线 + 线程解耦 ═══
def draw_scan_loop():
    fig, ax = plt.subplots(figsize=(11, 5))
    ax.set_xlim(0, 11)
    ax.set_ylim(0, 5)
    ax.axis("off")

    # ── 三条水平泳道 ──
    lanes = [
        (0.3, 3.0, 10.4, 1.7, "Movement Thread"),
        (0.3, 1.8, 10.4, 1.0, "Camera Thread"),
        (0.3, 0.5, 10.4, 1.0, "Save Thread"),
    ]
    for x, y, w, h, _ in lanes:
        rect = plt.Rectangle((x, y), w, h, linewidth=0.5, edgecolor=C_LIGHT,
                              facecolor="none", linestyle="dotted", zorder=0)
        ax.add_patch(rect)

    for x, y, w, h, label in lanes:
        ax.text(x + 0.15, y + h - 0.2, label, fontsize=8, color=C_GRAY, va="top", ha="left",
                fontweight="bold")

    # ── Movement Thread: 四步流水 ──
    step_w, step_h = 2.0, 1.0
    step_y = 3.5
    steps = [
        (1.2, "Moving\nsetSpeed + 并发移动"),
        (3.5, "Stabilizing\nDwell 稳定等待"),
        (5.8, "Capturing\n触发采集"),
        (8.1, "Enqueue\n帧数据入队"),
    ]
    for x, label in steps:
        rect = plt.Rectangle((x, step_y - step_h / 2), step_w, step_h,
                             linewidth=1.2, edgecolor=C_BLUE, facecolor="white", zorder=5)
        ax.add_patch(rect)
        ax.text(x + step_w / 2, step_y, label, ha="center", va="center", fontsize=8,
                fontweight="bold", zorder=6)

    # 步骤间箭头
    for i in range(len(steps) - 1):
        x1 = steps[i][0] + step_w
        x2 = steps[i + 1][0]
        ax.annotate("", xy=(x2, step_y), xytext=(x1, step_y),
                    arrowprops=dict(arrowstyle="->", color=C_BLUE, lw=1.5,
                                    shrinkA=2, shrinkB=2))

    # 循环回程
    ax.annotate("", xy=(1.2 + step_w / 2, step_y - step_h / 2),
                xytext=(8.1 + step_w / 2, step_y - step_h / 2),
                arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=1.5,
                                connectionstyle="arc3,rad=-0.4"))
    ax.text(5.0, step_y - step_h / 2 - 0.3, "next point", fontsize=7.5, color=C_ORANGE,
            ha="center", fontweight="bold")

    # ── Camera Thread: 持续推送 ──
    cam_bar = plt.Rectangle((1.2, 2.1), 7.0, 0.4, linewidth=0.6, edgecolor=C_GRAY,
                             facecolor=C_GRAY, alpha=0.15, zorder=3)
    ax.add_patch(cam_bar)
    ax.text(4.7, 2.3, "pushDataCallback ── 持续更新帧缓冲", fontsize=7.5,
            color=C_GRAY, ha="center", va="center")

    # Camera → Capturing (垂直虚线)
    cap_center_x = 5.8 + step_w / 2
    ax.annotate("", xy=(cap_center_x, step_y - step_h / 2), xytext=(cap_center_x, 2.5),
                arrowprops=dict(arrowstyle="->", color=C_LIGHT, lw=1.0, linestyle="dashed",
                                shrinkA=2, shrinkB=2))
    ax.text(cap_center_x + 0.15, 3.0, "读取", fontsize=7, color=C_GRAY, ha="left",
            bbox=dict(facecolor="white", edgecolor="none", pad=0))

    # ── Save Thread: 消费队列 ──
    save_bar = plt.Rectangle((8.1, 0.8), 2.0, 0.4, linewidth=0.6, edgecolor=C_GRAY,
                              facecolor=C_GRAY, alpha=0.15, zorder=3)
    ax.add_patch(save_bar)
    ax.text(9.1, 1.0, "imwrite → disk", fontsize=7.5,
            color=C_GRAY, ha="center", va="center")

    # Enqueue → Save (垂直虚线)
    enq_center_x = 8.1 + step_w / 2
    ax.annotate("", xy=(enq_center_x, 1.2), xytext=(enq_center_x, step_y - step_h / 2),
                arrowprops=dict(arrowstyle="->", color=C_LIGHT, lw=1.0, linestyle="dashed",
                                shrinkA=2, shrinkB=2))
    ax.text(enq_center_x + 0.15, 2.5, "入队", fontsize=7, color=C_GRAY, ha="left",
            bbox=dict(facecolor="white", edgecolor="none", pad=0))

    # ── 互斥锁标注 ──
    ax.text(5.8 + step_w / 2, 2.7, "frame_mutex", fontsize=6.5, color=C_GRAY, ha="center",
            bbox=dict(facecolor="white", edgecolor=C_LIGHT, lw=0.5, pad=3))
    ax.text(8.1 + step_w / 2, 1.6, "queue_mutex", fontsize=6.5, color=C_GRAY, ha="center",
            bbox=dict(facecolor="white", edgecolor=C_LIGHT, lw=0.5, pad=3))

    # ── pause/stop 指示 ──
    for sx, sy, anchor in [
        (3.5 + step_w / 2, step_y + step_h / 2, "upper"),    # Stabilizing
        (8.1 + step_w / 2, step_y + step_h / 2, "upper"),    # Enqueue
    ]:
        ax.annotate("pause/stop", xy=(sx, sy), xytext=(sx, sy + 0.45),
                    fontsize=7, color=C_ORANGE, ha="center",
                    arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=0.7))

    # 循环入口 stop 检查
    ax.annotate("stop", xy=(1.2 + step_w / 2, step_y + step_h / 2),
                xytext=(1.2 + step_w / 2, step_y + step_h / 2 + 0.45),
                fontsize=7, color=C_ORANGE, ha="center",
                arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=0.7))

    fig.tight_layout(pad=0.3)
    fig.savefig(os.path.join(OUT_DIR, "03_scan_loop.png"), bbox_inches="tight",
                facecolor=C_BG, edgecolor="none")
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    draw_s_curve()
    draw_spherical_cap()
    draw_scan_loop()
    print("done")
