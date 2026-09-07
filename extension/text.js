// Dictation inserts one paragraph. Control characters must never become keys,
// line breaks, or terminal escape sequences, regardless of model output.
// SPDX-License-Identifier: GPL-3.0-or-later
export function insertionText(text) {
    return String(text)
        .replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/gu, ' ')
        .replace(/\s+/gu, ' ')
        .trim();
}

// stableBytes is a UTF-8 prefix length, not a JavaScript UTF-16 index. Keep
// complete characters, sanitize control characters, and show a bounded tail.
export function previewParts(text, stableBytes, limit = 160) {
    const characters = [];
    let bytes = 0;
    const boundary = Math.max(0, Number.isFinite(stableBytes) ? Math.floor(stableBytes) : 0);
    for (let character of String(text)) {
        const code = character.codePointAt(0);
        bytes += code <= 0x7f ? 1 : code <= 0x7ff ? 2 : code <= 0xffff ? 3 : 4;
        if (/[\s\u0000-\u001f\u007f-\u009f\u2028\u2029]/u.test(character))
            character = ' ';
        if (character === ' ' && (!characters.length || characters.at(-1).text === ' '))
            continue;
        characters.push({text: character, stable: bytes <= boundary});
    }
    if (characters.at(-1)?.text === ' ')
        characters.pop();
    const truncated = characters.length > limit;
    const visible = characters.slice(-limit);
    let stable = '';
    let tentative = '';
    for (const character of visible) {
        if (character.stable)
            stable += character.text;
        else
            tentative += character.text;
    }
    if (truncated) {
        if (stable)
            stable = `…${stable}`;
        else
            tentative = `…${tentative}`;
    }
    return {stable, tentative};
}

export function previewMarkup(parts, tentativeColor) {
    const escape = text => text.replace(/&/gu, '&amp;').replace(/</gu, '&lt;').replace(/>/gu, '&gt;');
    return escape(parts.stable) + (parts.tentative
        ? `<span foreground="${tentativeColor}">${escape(parts.tentative)}</span>` : '');
}

export function validCursorRect(rect, frame) {
    return rect && frame && ['x', 'y', 'width', 'height'].every(key => Number.isFinite(rect[key])) &&
        rect.width >= 0 && rect.height > 0 && rect.height <= frame.height &&
        rect.x >= frame.x - 2 && rect.x <= frame.x + frame.width + 2 &&
        rect.y >= frame.y - 2 && rect.y + rect.height <= frame.y + frame.height + 2;
}

export function previewPosition(anchor, area, width, height) {
    const margin = 8;
    const left = area.x + margin;
    const top = area.y + margin;
    const right = Math.max(left, area.x + area.width - width - margin);
    const bottom = Math.max(top, area.y + area.height - height - margin);
    let y = anchor.y + anchor.height + margin;
    if (y > bottom)
        y = anchor.y - height - margin;
    return {x: Math.round(Math.max(left, Math.min(anchor.x, right))),
        y: Math.round(Math.max(top, Math.min(y, bottom)))};
}
