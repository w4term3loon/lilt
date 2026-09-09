// SPDX-License-Identifier: GPL-3.0-or-later
// Dictation inserts one paragraph. Control characters must never become keys,
// line breaks, or terminal escape sequences, regardless of model output.
export function insertionText(text) {
    return String(text)
        .replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/gu, ' ')
        .replace(/\s+/gu, ' ')
        .trim();
}

export function applicationName(text) {
    return insertionText(text).normalize('NFKC').toLowerCase().replace(/[.!?]+$/, '').trim();
}

export function commandName(text) {
    const phrase = applicationName(text);
    return phrase.startsWith('open ') ? phrase.slice(5) : null;
}

export function commandApplication(text, apps, partial = false) {
    const name = commandName(text);
    if (!name || !apps?.get(name))
        return null;
    // A draft saying "open Code" may still become "open Code Insiders".
    if (partial && [...apps.keys()].some(candidate => candidate.startsWith(`${name} `)))
        return null;
    return apps.get(name);
}

export function isCommandPreview(text, apps) {
    if (applicationName(text) === 'open')
        return true;
    const name = commandName(text);
    return Boolean(name && apps && [...apps.keys()].some(candidate => candidate.startsWith(name)));
}
