// SPDX-License-Identifier: GPL-3.0-or-later
import {insertionText, isBrowserCommand, isBrowserCommandPreview, startsBrowserCommand} from '../extension/text.js';

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

for (const text of ['open browser', 'Open browser.', ' OPEN   BROWSER! '])
    assert(isBrowserCommand(text), 'Recognize the complete browser command');
for (const text of ['', null, 'Please open browser', 'Do not open browser.', 'Open browser and search', '"open browser"'])
    assert(!isBrowserCommand(text), 'Ordinary dictation must not launch an app');

for (const [input, expected] of [
    ['  Hello\nworld.\r\n', 'Hello world.'],
    ['Árvíztűrő tükörfúrógép 🦉', 'Árvíztűrő tükörfúrógép 🦉'],
    ['one\u2028two\u2029three', 'one two three'],
    ['hello\t\x1b\x00world\x7f', 'hello world'],
    ['\n\r\t', ''],
    ['<b> & "next"', '<b> & "next"'],
    ['🦉'.repeat(200), '🦉'.repeat(200)],
]) {
    assert(insertionText(input) === expected,
        `Wrong insertion text for ${JSON.stringify(input)}`);
}

for (const text of ['open browser', 'Open browser.', 'open browser please'])
    assert(startsBrowserCommand(text), 'First two words trigger the browser');
for (const text of ['open browsers', 'please open browser', 'do not open browser'])
    assert(!startsBrowserCommand(text), 'Only the exact opening words trigger');
for (const text of ['open', 'open b', 'open browser'])
    assert(isBrowserCommandPreview(text), 'Command candidates stay out of composition');
assert(!isBrowserCommandPreview('open the document'), 'Noncommands return to dictation');
print('Unicode text sanitization and browser command tests passed');
