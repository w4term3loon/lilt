// SPDX-License-Identifier: GPL-3.0-or-later
import Cairo from 'cairo';

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
const wisps = Array.from({length: 36}, (_, i) => ({
    cluster: i % clusters.length,
    x: (random(100 + i * 4) - 0.5) * 10,
    y: (random(101 + i * 4) - 0.5) * 10,
    size: 5 + random(102 + i * 4) * 4,
    phase: random(103 + i * 4) * 100,
}));

function mist(red, green, blue) {
    const surface = new Cairo.ImageSurface(Cairo.Format.ARGB32, 24, 24);
    const context = new Cairo.Context(surface);
    const gradient = new Cairo.RadialGradient(12, 12, 0, 12, 12, 12);
    for (const [radius, alpha] of [[0, 0.5], [0.25, 0.3], [0.6, 0.09], [1, 0]])
        gradient.addColorStopRGBA(radius, red, green, blue, alpha);
    context.setSource(gradient);
    context.paint();
    context.$dispose();
    return surface;
}
// Reuse two small soft sprites; no per-frame blur, texture generation, or library.
const orange = mist(1, 0.43, 0.08);
const purple = mist(0.72, 0.37, 1);

export function voiceIntensity(rms) {
    const signal = Math.max(0, (rms - 0.008) / 0.07);
    return signal / (1 + signal);
}

export function drawOrb(context, width, height, bands, elapsed, command = false) {
    const centers = clusters.map((cluster, i) => ({
        x: cluster.x + 4 * noise(elapsed * 0.45 + cluster.phase)
            + bands[0] * 4 * noise(elapsed * 1.3 + cluster.phase),
        y: cluster.y + 4 * noise(elapsed * 0.45 + cluster.phase + 200)
            + bands[1] * 4 * noise(elapsed * 1.6 + cluster.phase),
        strength: bands[i % 3],
    }));
    context.save();
    context.translate(width / 2, height / 2);
    const scale = Math.min(width, height) / 64;
    context.scale(scale, scale);
    for (const wisp of wisps) {
        const center = centers[wisp.cluster];
        const drift = 1.5 + bands[2] * 2;
        const x = center.x + wisp.x + drift * noise(elapsed * 0.8 + wisp.phase);
        const y = center.y + wisp.y + drift * noise(elapsed * 0.8 + wisp.phase + 300);
        const size = wisp.size * (1 + 0.1 * noise(elapsed * 0.5 + wisp.phase));
        context.save();
        context.translate(x, y);
        context.scale(size / 12, size / 12);
        context.setSourceSurface(command ? purple : orange, -12, -12);
        context.paintWithAlpha(0.65 + center.strength * 0.3);
        context.restore();
    }
    context.restore();
}
