// SPDX-License-Identifier: GPL-3.0-or-later

function random(index) {
    let value = Math.imul(index ^ 73, 1597334677);
    value = Math.imul(value ^ (value >>> 16), 2246822507);
    return ((value ^ (value >>> 16)) >>> 0) / 4294967296;
}

// Smooth seeded noise: nearby moments share a flow instead of flickering.
function noise(time) {
    const step = Math.floor(time);
    const fraction = time - step;
    const blend = fraction * fraction * (3 - 2 * fraction);
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
    size: 0.7 + random(102 + i * 4) * 0.9,
    phase: random(103 + i * 4) * 100,
}));

// Ubuntu orange #E95420 and light aubergine #77216F (official brand palette).
const orange = [233 / 255, 84 / 255, 32 / 255];
const purple = [119 / 255, 33 / 255, 111 / 255];

export function voiceIntensity(rms) {
    const signal = Math.max(0, (rms - 0.008) / 0.035);
    return signal / (1 + signal);
}

export function drawOrb(context, width, height, bands, elapsed, command = 0, loading = 0) {
    const color = orange.map((value, i) => value + (purple[i] - value) * command);
    const centers = clusters.map((cluster, i) => ({
        x: cluster.x + 4 * noise(elapsed * 0.45 + cluster.phase)
            + bands[0] * 14 * noise(elapsed * 1.3 + cluster.phase),
        y: cluster.y + 4 * noise(elapsed * 0.45 + cluster.phase + 200)
            + bands[1] * 14 * noise(elapsed * 1.6 + cluster.phase),
        strength: bands[i % 3],
    }));
    context.save();
    context.translate(width / 2, height / 2);
    const scale = Math.min(width, height) / 160;
    context.scale(scale, scale);
    for (const [index, wisp] of wisps.entries()) {
        const center = centers[wisp.cluster];
        const drift = 1.5 + bands[2] * 8;
        const spread = 1 + center.strength * 0.55;
        const x = (center.x + wisp.x) * spread + drift * noise(elapsed * 1.8 + wisp.phase);
        const y = (center.y + wisp.y) * spread + drift * noise(elapsed * 1.8 + wisp.phase + 300);
        const angle = index / wisps.length * Math.PI * 2 + elapsed * 3;
        const ringX = 20 * Math.cos(angle);
        const ringY = 20 * Math.sin(angle);
        const trail = 0.18 + 0.82 * (index / wisps.length) ** 2;
        context.setSourceRGBA(...color, (0.65 + center.strength * 0.35) * (1 - loading) + trail * loading);
        context.arc(x + (ringX - x) * loading, y + (ringY - y) * loading,
            wisp.size * (1 - loading) + 0.85 * loading, 0, Math.PI * 2);
        context.fill();
    }
    context.restore();
}
