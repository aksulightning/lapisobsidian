import Block from "../world/block/Block.js";
import * as THREE from "../../../../../../libraries/three.module.js";

export default class SoundManager {

    constructor() {

        this.audioListener = null;

        this.soundPool = {};
    }

    create(worldRenderer) {
        this.scene = worldRenderer.scene;

        this.audioListener = new THREE.AudioListener();
        worldRenderer.camera.add(this.audioListener);

        // Load initial sound pool
        for (let i in Block.sounds) {
            let sound = Block.sounds[i];

            // Load sound types
            this.loadSoundPool(sound.getStepSound());
        }
    }

    loadSoundPool(name) {
        let pool = [];
        let amount = 4;

        // Original synthesized buffers; no asset files are loaded.
        for (let i = 0; i < amount; i++) {
            pool.push(this.loadSound(name + i));
        }

        // Register sound pool
        this.soundPool[name] = pool;
    }

    loadSound(path) {
        if (!this.isCreated()) {
            return;
        }

        // Create sound
        let sound = new THREE.PositionalAudio(this.audioListener);
        sound.setRefDistance(0.1);
        sound.setRolloffFactor(6);
        sound.setFilter(sound.context.createBiquadFilter());
        sound.setVolume(0);

        // Original short noise/tone sounds. Generated audio is CC0-1.0.
        const rate = sound.context.sampleRate;
        const buffer = sound.context.createBuffer(1, Math.floor(rate * 0.12), rate);
        const samples = buffer.getChannelData(0);
        let seed = Array.from(path).reduce((n, c) => (n * 31 + c.charCodeAt(0)) >>> 0, 7);
        for (let i = 0; i < samples.length; i++) {
            seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5;
            const envelope = Math.pow(1 - i / samples.length, 3);
            samples[i] = (((seed >>> 0) / 4294967296 - 0.5) * 0.6 + Math.sin(i / rate * 1800) * 0.15) * envelope;
        }
        sound.setBuffer(buffer);
        this.scene.add(sound);

        return sound;
    }

    playSound(name, x, y, z, volume, pitch) {
        if (!this.isCreated()) return;
        let pool = this.soundPool[name];

        if (typeof pool === "undefined") {
            // Load sound pool
            this.loadSoundPool(name);
        } else if (pool.length > 0) {
            this.audioListener.context.resume().catch(() => {});
            // Play random sound in pool
            let sound = pool[Math.floor(Math.random() * pool.length)];
            if (typeof volume === "undefined" || typeof sound === "undefined") {
                return;
            }

            // Stop previous sound
            if (sound.isPlaying) {
                sound.stop();
            }

            // Update position
            sound.position.set(x, y, z);

            // Update volume and pitch
            sound.setVolume(volume);
            sound.filters[0].frequency.setValueAtTime(12000 * pitch, sound.context.currentTime);

            // Play sound
            sound.offset = 0;
            sound.play();
        }
    }

    isCreated() {
        return !(this.audioListener === null);
    }

}