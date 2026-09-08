// SPDX-License-Identifier: GPL-3.0-or-later

function random(index) {
    let value = Math.imul(index ^ 73, 1597334677);
    value = Math.imul(value ^ (value >>> 16), 2246822507);
    return ((value ^ (value >>> 16)) >>> 0) / 4294967296;
}

// Quintic interpolation keeps the seeded drift smooth through each interval.
function noise(time) {
    const step = Math.floor(time);
    const fraction = time - step;
    const blend = fraction ** 3 * (fraction * (fraction * 6 - 15) + 10);
    return 2 * (random(step) * (1 - blend) + random(step + 1) * blend) - 1;
}

const clusters = Array.from({length: 6}, (_, i) => ({
    x: (random(i * 4) - 0.5) * 17,
    y: (random(i * 4 + 1) - 0.5) * 17,
    phase: random(i * 4 + 2) * 100,
}));
const wisps = Array.from({length: 72}, (_, i) => ({
    cluster: i % clusters.length,
    x: (random(100 + i * 4) - 0.5) * 10,
    y: (random(101 + i * 4) - 0.5) * 10,
    size: 0.6 + random(102 + i * 4) * 0.65,
    phase: random(103 + i * 4) * 100,
}));

// Ubuntu orange #E95420 and light aubergine #77216F (official brand palette).
const orange = [233 / 255, 84 / 255, 32 / 255];
const purple = [119 / 255, 33 / 255, 111 / 255];

export function voiceIntensity(rms) {
    const signal = Math.max(0, (rms - 0.008) / 0.035);
    return signal / (1 + signal);
}

function unit(value) {
    return Number.isFinite(value) ? Math.min(1, Math.max(0, value)) : 0;
}

export function sampleOrb(bands, elapsed, command = 0, loading = 0) {
    bands = Array.from({length: 3}, (_, i) => unit(bands?.[i]));
    elapsed = Number.isFinite(elapsed) ? Math.max(0, elapsed) : 0;
    command = unit(command);
    loading = unit(loading);
    const color = orange.map((value, i) => value + (purple[i] - value) * command);
    const activity = Math.max(...bands);
    const centers = clusters.map(cluster => ({
        x: cluster.x + 3.2 * noise(elapsed * 0.22 + cluster.phase),
        y: cluster.y + 2.8 * noise(elapsed * 0.22 + cluster.phase + 200),
    }));
    // Speech moves the formation together; high frequencies add smaller detail.
    const breath = 1 + bands[0] * 0.95 + bands[1] * 0.35;
    const lift = 1 + bands[0] * 0.65 + bands[1] * 0.6;
    const tilt = 0.08 * noise(elapsed * 0.2 + 40)
        + bands[1] * 0.12 * noise(elapsed * 0.6 + 60);
    const cos = Math.cos(tilt);
    const sin = Math.sin(tilt);
    const drift = 0.9 + bands[2] * 2.5;
    const expansion = 1 + 1.3 * command;
    const dots = wisps.map((wisp, index) => {
        const center = centers[wisp.cluster];
        const cloudX = ((center.x + wisp.x) * breath
            + drift * noise(elapsed * 0.65 + wisp.phase)) * 1.3;
        const cloudY = ((center.y + wisp.y) * lift
            + drift * noise(elapsed * 0.65 + wisp.phase + 300)) * 1.3;
        const rotatedX = cloudX * cos - cloudY * sin;
        const rotatedY = cloudX * sin + cloudY * cos;
        const angle = index / wisps.length * Math.PI * 2 + elapsed * 3;
        let x = (rotatedX + (20 * Math.cos(angle) - rotatedX) * loading) * expansion;
        let y = (rotatedY + (20 * Math.sin(angle) - rotatedY) * loading) * expansion;
        const radius = wisp.size * (1 + 0.18 * command);
        const limit = 68 - radius;
        const shoulder = limit * 0.72;
        const distance = Math.hypot(x, y);
        if (distance > shoulder) {
            // A soft boundary preserves expansion without clipping the canvas.
            const bounded = shoulder + (limit - shoulder)
                * (1 - Math.exp(-(distance - shoulder) / (limit - shoulder)));
            x *= bounded / distance;
            y *= bounded / distance;
        }
        const trail = 0.45 + 0.55 * (index / wisps.length) ** 2;
        const alpha = ((0.65 + activity * 0.3) * (1 - loading) + trail * loading)
            * (index % 3 ? 1 - loading : 1);
        return {x, y, radius, alpha};
    });
    return {color, dots, spin: 3 * loading};
}

export function drawOrb(context, width, height, frame, completion = null) {
    context.save();
    context.translate(width / 2, height / 2);
    const scale = Math.min(width, height) / 160;
    context.scale(scale, scale);
    const age = Number.isFinite(completion) ? Math.max(0, completion) : 0;
    const t = unit(age / 0.35);
    const gather = t * t * (3 - 2 * t);
    const fading = unit((age - 0.35) / 0.25);
    const fade = 1 - fading * fading * (3 - 2 * fading);
    const turn = (frame.spin ?? 0) * 0.35 * (t - t ** 3 + 0.5 * t ** 4);
    const cos = Math.cos(turn);
    const sin = Math.sin(turn);
    for (const dot of frame.dots) {
        const alpha = dot.alpha * (1 - gather);
        if (alpha < 0.005) continue;
        context.setSourceRGBA(...frame.color, alpha);
        context.arc((dot.x * cos - dot.y * sin) * (1 - gather),
            (dot.x * sin + dot.y * cos) * (1 - gather),
            dot.radius + (1.2 - dot.radius) * gather, 0, Math.PI * 2);
        context.fill();
    }
    if (gather * fade > 0.005) {
        context.setSourceRGBA(...frame.color, gather * fade);
        context.arc(0, 0, 1.2, 0, Math.PI * 2);
        context.fill();
    }
    context.restore();
}
