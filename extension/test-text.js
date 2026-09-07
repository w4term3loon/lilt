import {insertionText, previewParts, previewMarkup, validCursorRect, previewPosition} from './text.js';

const cases = [
    ['  Hello\nworld.\r\n', 'Hello world.'],
    ['Árvíztűrő tükörfúrógép 🦉', 'Árvíztűrő tükörfúrógép 🦉'],
    ['one\u2028two\u2029three', 'one two three'],
    ['hello\t\x1b\x00world\x7f', 'hello world'],
    ['\n\r\t', ''],
];
for (const [input, expected] of cases) {
    const result = insertionText(input);
    if (result !== expected)
        throw new Error(`Expected ${JSON.stringify(expected)}, got ${JSON.stringify(result)}`);
    if (/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/u.test(result))
        throw new Error('Control character escaped the insertion boundary');
}
print(`${cases.length} text insertion boundary tests passed`);

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

for (const [bytes, stable, tentative] of [
    [0, '', 'Ár 🦉 next'], [1, '', 'Ár 🦉 next'], [2, 'Á', 'r 🦉 next'],
    [4, 'Ár ', '🦉 next'], [7, 'Ár ', '🦉 next'], [8, 'Ár 🦉', ' next'],
    [100, 'Ár 🦉 next', ''],
]) {
    const parts = previewParts('Ár 🦉 next', bytes);
    assert(parts.stable === stable && parts.tentative === tentative,
        `Wrong UTF-8 preview boundary at ${bytes}: ${JSON.stringify(parts)}`);
}
const escaped = previewMarkup(previewParts('<b> & "next"', 4), '#73737c');
assert(escaped === '&lt;b&gt; <span foreground="#73737c">&amp; "next"</span>',
    `Model output must remain literal: ${escaped}`);
assert(JSON.stringify(previewParts(' \nÁ\t\t🦉\u0000 ', 4)) ===
    JSON.stringify({stable: 'Á', tentative: ' 🦉'}), 'Sanitize and split using original UTF-8 offsets');
assert(previewParts('  \u0000\n ', 10).stable === '' &&
    previewParts('  \u0000\n ', 10).tentative === '', 'Whitespace must hide preview');
const long = previewParts('🦉'.repeat(200), 400);
assert([...long.stable + long.tentative].length === 161 &&
    long.stable.startsWith('…'), 'Long preview must be a bounded complete-character tail');

const frame = {x: -1280, y: 0, width: 1280, height: 720};
assert(validCursorRect({x: -1000, y: 80, width: 1, height: 18}, frame),
    'Negative monitor coordinates are valid');
assert(!validCursorRect({x: 100, y: 80, width: 1, height: 18}, frame),
    'Reject stale caret from a different window/monitor');
assert(!validCursorRect({x: -1000, y: 80, width: 0, height: 0}, frame),
    'Reject cleared IBus geometry');
assert(!validCursorRect({x: NaN, y: 80, width: 1, height: 18}, frame),
    'Reject non-finite geometry');
for (const anchor of [
    {x: -1279, y: 0, height: 18}, {x: -5, y: 695, height: 18},
    {x: -2000, y: 2000, height: 18},
]) {
    const pos = previewPosition(anchor, frame, 420, 70);
    assert(pos.x >= -1272 && pos.x + 420 <= -8 && pos.y >= 8 && pos.y + 70 <= 712,
        `Keep preview inside workarea: ${JSON.stringify(pos)}`);
}
const above = previewPosition({x: 10, y: 685, height: 18}, {x: 0, y: 24, width: 1280, height: 696}, 400, 60);
assert(above.y === 617, 'Move above caret when there is no room below');
print('Preview Unicode, sanitization, markup, bounded tail, and monitor placement tests passed');
