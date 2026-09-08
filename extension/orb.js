// SPDX-License-Identifier: GPL-3.0-or-later
const points = Array.from({length: 112}, (_, index) => {
    const y = 1 - 2 * (index + 0.5) / 112;
    const radius = Math.sqrt(1 - y * y);
    const angle = index * Math.PI * (3 - Math.sqrt(5));
    return {x: radius * Math.cos(angle), y, z: radius * Math.sin(angle)};
});

export function voiceIntensity(rms) {
    // Ignore low background noise, then compress loud sounds without clipping.
    const signal = Math.max(0, (rms - 0.012) / 0.18);
    return signal / (1 + signal);
}

export function drawOrb(context, width, height, intensity, elapsed) {
    const turn = elapsed * 0.22;
    const cosine = Math.cos(turn);
    const sine = Math.sin(turn);
    const projected = points.map(point => {
        const x = point.x * cosine + point.z * sine;
        const z = point.z * cosine - point.x * sine;
        return {x, y: point.y * 0.94 - z * 0.34, z: point.y * 0.34 + z * 0.94};
    }).sort((a, b) => a.z - b.z);
    context.save();
    context.translate(width / 2, height / 2);
    const scale = Math.min(width, height) / 64;
    context.scale(scale, scale);
    for (const point of projected) {
        const depth = (point.z + 1) / 2;
        const edge = Math.hypot(point.x, point.y);
        const ripple = Math.sin(Math.atan2(point.y, point.x) * 3 - elapsed * 7);
        // Loudness ripples around the rim; this is not a frequency spectrum.
        const radius = 19 + intensity * (2 + edge * (3 + 2 * ripple));
        const x = point.x * radius;
        const y = point.y * radius;
        const size = 0.65 + depth * 0.65;
        // A narrow outline keeps the transparent orb legible over pale windows.
        context.setSourceRGBA(0.08, 0.24, 0.23, 0.15 + depth * 0.12);
        context.arc(x, y, size + 0.45, 0, Math.PI * 2);
        context.fill();
        context.setSourceRGBA(0.24 + depth * 0.28, 0.64 + depth * 0.26,
            0.57 + depth * 0.30, 0.32 + depth * 0.62);
        context.arc(x, y, size, 0, Math.PI * 2);
        context.fill();
    }
    context.restore();
}
