// SPDX-License-Identifier: GPL-3.0-or-later
import {insertionText, compositionParts} from '../text.js';

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

for (const [input, expected] of [
    ['  Hello\nworld.\r\n', 'Hello world.'],
    ['Árvíztűrő tükörfúrógép 🦉', 'Árvíztűrő tükörfúrógép 🦉'],
    ['one\u2028two\u2029three', 'one two three'],
    ['hello\t\x1b\x00world\x7f', 'hello world'],
    ['\n\r\t', ''],
]) {
    assert(insertionText(input) === expected,
        `Wrong insertion text for ${JSON.stringify(input)}`);
}

for (const [bytes, stable, tentative] of [
    [0, '', 'Ár 🦉 next'], [1, '', 'Ár 🦉 next'], [2, 'Á', 'r 🦉 next'],
    [4, 'Ár ', '🦉 next'], [7, 'Ár ', '🦉 next'], [8, 'Ár 🦉', ' next'],
    [100, 'Ár 🦉 next', ''],
]) {
    const parts = compositionParts('Ár 🦉 next', bytes);
    assert(parts.stable === stable && parts.tentative === tentative,
        `Wrong UTF-8 composition boundary at ${bytes}: ${JSON.stringify(parts)}`);
}
assert(JSON.stringify(compositionParts(' \nÁ\t\t🦉\u0000 ', 4)) ===
    JSON.stringify({stable: 'Á', tentative: ' 🦉'}), 'Sanitize using original UTF-8 offsets');
assert(JSON.stringify(compositionParts('  \u0000\n ', 10)) ===
    JSON.stringify({stable: '', tentative: ''}), 'Whitespace must clear composition');
assert(JSON.stringify(compositionParts('<b> & "next"', 4)) ===
    JSON.stringify({stable: '<b> ', tentative: '& "next"'}), 'Keep model markup literal');
const long = compositionParts('🦉'.repeat(200), 400);
assert(long.stable === '🦉'.repeat(100) && long.tentative === '🦉'.repeat(100),
    'Composition must retain complete text beyond the former preview limit');
print('Text sanitization, UTF-8 stability, literal markup, and full composition tests passed');
