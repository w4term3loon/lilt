// SPDX-License-Identifier: GPL-3.0-or-later
// Dictation inserts one paragraph. Control characters must never become keys,
// line breaks, or terminal escape sequences, regardless of model output.
export function insertionText(text) {
    return String(text)
        .replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/gu, ' ')
        .replace(/\s+/gu, ' ')
        .trim();
}

export function isBrowserCommand(text) {
    return /^open browser[.!?]*$/i.test(insertionText(text));
}

export function startsBrowserCommand(text) {
    return /^open browser(?:[\s.!?,]|$)/i.test(insertionText(text));
}

export function isBrowserCommandPreview(text) {
    const phrase = insertionText(text).toLowerCase().replace(/[.!?]+$/, '');
    return phrase === 'open' || (phrase.startsWith('open ') && 'open browser'.startsWith(phrase));
}
