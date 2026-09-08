// SPDX-License-Identifier: GPL-3.0-or-later
let seed = 73;
function random() {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    return seed / 4294967296;
}
const points = Array.from({length: 96}, (_, index) => {
    const y = random() * 2 - 1;
    const angle = random() * Math.PI * 2;
    const radius = (12 + random() * 7) * Math.sqrt(1 - y * y);
    return {x: radius * Math.cos(angle), y: y * (12 + random() * 7),
        z: radius * Math.sin(angle), phase: random() * Math.PI * 2,
        size: 0.65 + random() * 0.55, band: index % 3};
});

export function voiceIntensity(rms) {
    // Ignore low background noise, then compress loud sounds without clipping.
    const signal = Math.max(0, (rms - 0.008) / 0.07);
    return signal / (1 + signal);
}

export function drawOrb(context, width, height, bands, elapsed, command = false) {
    const turn = elapsed * 0.12;
    const cosine = Math.cos(turn);
    const sine = Math.sin(turn);
    const projected = points.map(point => {
        const x = point.x * cosine + point.z * sine;
        const z = point.z * cosine - point.x * sine;
        // Low tones sway, mids fold vertically, highs scatter individual dots.
        const low = bands[0] * (point.band === 0 ? 1 : 0.3);
        const mid = bands[1] * (point.band === 1 ? 1 : 0.3);
        const high = bands[2] * (point.band === 2 ? 1 : 0.2);
        return {
            x: x + low * 7.5 * Math.sin(elapsed * 3 + point.phase)
                + high * 3.5 * Math.sin(elapsed * 11 + point.phase),
            y: point.y * 0.94 - z * 0.34 + mid * 8 * Math.cos(elapsed * 4 + point.phase)
                + high * 3.5 * Math.cos(elapsed * 13 + point.phase),
            z: point.y * 0.34 + z * 0.94, size: point.size, high,
        };
    }).sort((a, b) => a.z - b.z);
    context.save();
    context.translate(width / 2, height / 2);
    const scale = Math.min(width, height) / 64;
    context.scale(scale, scale);
    for (const point of projected) {
        const depth = Math.max(0, Math.min(1, (point.z / 19 + 1) / 2));
        const {x, y} = point;
        const size = point.size * (0.75 + depth * 0.5);
        // A narrow outline keeps the transparent orb legible over pale windows.
        context.setSourceRGBA(command ? 0.22 : 0.08, command ? 0.10 : 0.24,
            command ? 0.34 : 0.23, 0.15 + depth * 0.12);
        context.arc(x, y, size + 0.45, 0, Math.PI * 2);
        context.fill();
        if (command)
            context.setSourceRGBA(0.58 + depth * 0.22, 0.33 + depth * 0.26,
                0.83 + depth * 0.15, 0.32 + depth * 0.62);
        else
            context.setSourceRGBA(0.24 + depth * 0.28, 0.64 + depth * 0.26,
                0.57 + depth * 0.25 + point.high * 0.12, 0.32 + depth * 0.62);
        context.arc(x, y, size, 0, Math.PI * 2);
        context.fill();
    }
    context.restore();
}
