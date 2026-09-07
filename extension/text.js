// SPDX-License-Identifier: GPL-3.0-or-later
// Dictation inserts one paragraph. Control characters must never become keys,
// line breaks, or terminal escape sequences, regardless of model output.
export function insertionText(text) {
    return String(text)
        .replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/gu, ' ')
        .replace(/\s+/gu, ' ')
        .trim();
}

// stableBytes is a UTF-8 prefix length, not a JavaScript UTF-16 index. Keep
// complete characters and sanitize controls without truncating composition.
export function compositionParts(text, stableBytes) {
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
    let stable = '';
    let tentative = '';
    for (const character of characters) {
        if (character.stable)
            stable += character.text;
        else
            tentative += character.text;
    }
    return {stable, tentative};
}
