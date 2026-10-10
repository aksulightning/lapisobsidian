/* Exercise the actual EM_JS bridge against a small WebAudio API double. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../src/LapisAudio.c'), 'utf8');
const Module = {}, HEAP16 = new Int16Array([0, 16384, -16384, 32767, 0]);
const buffers = [], sources = [], gains = [];
const context = {
    destination: {},
    createBuffer(channels, length, rate) {
        assert.equal(channels, 1); assert.equal(rate, 22050);
        const data = new Float32Array(length);
        const buffer = {length, getChannelData: () => data};
        buffers.push(buffer); return buffer;
    },
    createBufferSource() {
        const source = {playbackRate: {}, connect() {}, disconnect() {this.disconnected = true;},
            start() {this.started = true;}, stop() {this.stopped = true;}};
        sources.push(source); return source;
    },
    createGain() {
        const gain = {gain: {}, connect() {}, disconnect() {this.disconnected = true;}};
        gains.push(gain); return gain;
    }
};
const AUDIO = {context}, window = {AUDIO};
function bridge(name, args) {
    const start = source.indexOf('EM_JS(void,' + name + ',');
    assert.ok(start >= 0);
    const body = source.slice(source.indexOf('{', start) + 1, source.indexOf('\n});', start));
    return new Function('Module', 'window', 'AUDIO', 'HEAP16', ...args, body)
        .bind(null, Module, window, AUDIO, HEAP16);
}
const play = bridge('PlayPCM', ['voice', 'samples', 'count', 'rate', 'volume']);
const free = bridge('FreePCM', []);
play(0, 0, 5, 150, 35);
assert.deepEqual([...buffers[0].getChannelData(0)], [0, .5, -.5, 32767/32768, 0]);
assert.equal(sources[0].playbackRate.value, 1.5);
assert.equal(gains[0].gain.value, .35);
assert.ok(sources[0].started);
play(0, 0, 5, 0, 70);
assert.equal(buffers.length, 1, 'PCM is cached per clip');
assert.equal(sources[1].playbackRate.value, .05);
play(1, 0, 3, 100, 70);
assert.equal(buffers[1].length, 3, 'different clip lengths are respected');
for (let i=0; i<20; i++) play(0, 0, 5, 100, 70);
assert.equal(sources.length, 16, 'polyphony is capped');
sources[0].onended();
assert.ok(sources[0].disconnected && gains[0].disconnected);
play(0, 0, 5, 100, 70);
assert.equal(sources.length, 17, 'a completed voice frees a slot');
const oldBank = Module.lapisSounds;
free();
assert.equal(Module.lapisSounds, null);
assert.ok([...oldBank.sources].every(s => s.stopped));
play(0, 0, 5, 100, 70);
assert.equal(Module.lapisSounds.sources.size, 1);
// Deferred onended callbacks from the old connection cannot corrupt the new bank.
for (const s of [...oldBank.sources]) s.onended();
assert.equal(oldBank.sources.size, 0);
assert.equal(Module.lapisSounds.sources.size, 1);
free(); free();
AUDIO.context = null;
play(0, 0, 5, 100, 70);
assert.equal(Module.lapisSounds, null, 'no context means no queued playback');
console.log('WebAudio: PCM, variable lengths, caching, pitch/gain, voice limit and disconnect cleanup passed');
