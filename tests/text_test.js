// SPDX-License-Identifier: GPL-3.0-or-later
import {insertionText, applicationName, commandApplication, isCommandPreview} from '../extension/text.js';

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

const apps = new Map([
    ['browser', 'firefox.desktop'], ['terminal', 'org.gnome.Terminal.desktop'],
    ['text editor', 'org.gnome.TextEditor.desktop'], ['code', 'code.desktop'],
    ['code insiders', 'code-insiders.desktop'], ['duplicate', null],
]);
for (const text of ['open browser', 'Open browser.', ' OPEN   BROWSER! '])
    assert(commandApplication(text, apps) === 'firefox.desktop', 'Recognize the browser alias');
assert(commandApplication('Open terminal!', apps) === 'org.gnome.Terminal.desktop', 'Recognize terminal');
assert(commandApplication('Open Text Editor.', apps, true) === 'org.gnome.TextEditor.desktop', 'Match a complete multiword name');
for (const text of ['', null, 'Please open browser', 'Do not open browser.', 'Open browser and search', '"open browser"', 'open duplicate', 'open unknown', 'open terminator', 'open terminal; rm anything'])
    assert(!commandApplication(text, apps), 'Unknown, ambiguous and ordinary dictation must not launch an app');
assert(!commandApplication('open code', apps, true), 'Wait for a potentially longer app name');
assert(commandApplication('open code', apps) === 'code.desktop', 'Finish resolves the shorter exact name');
assert(commandApplication('open code insiders', apps, true) === 'code-insiders.desktop', 'Prefer the completed longer name');
assert(applicationName('  TEXT  Editor! ') === 'text editor', 'Normalize app names');

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

for (const text of ['open', 'open b', 'open terminal', 'open text e', 'open code'])
    assert(isCommandPreview(text, apps), 'Command candidates stay out of composition');
assert(!isCommandPreview('open the document', apps), 'Noncommands return to dictation');
print('Unicode text sanitization and installed-app command tests passed');
