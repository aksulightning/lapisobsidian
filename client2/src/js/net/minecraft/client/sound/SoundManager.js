import Block from "../world/block/Block.js";
import * as THREE from "../../../../../../libraries/three.module.js";
import {synthesize} from '../../../../../../adapter/audio.mjs';

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
            pool.push(this.loadSound(name, i));
        }

        // Register sound pool
        this.soundPool[name] = pool;
    }

    loadSound(path, variant = 0) {
        if (!this.isCreated()) {
            return;
        }

        // Create sound
        let sound = new THREE.PositionalAudio(this.audioListener);
        sound.setRefDistance(3);
        sound.setRolloffFactor(1);
        sound.setMaxDistance(32);
        sound.setVolume(0);

        // Original short noise/tone sounds. Generated audio is CC0-1.0.
        const rate = sound.context.sampleRate;
        const samples = synthesize(path, rate, variant);
        const buffer = sound.context.createBuffer(1, samples.length, rate);
        buffer.getChannelData(0).set(samples);
        sound.setBuffer(buffer);
        this.scene.add(sound);

        return sound;
    }

    playSound(name, x, y, z, volume = 1, pitch = 1) {
        if (!this.isCreated()) return;
        if (![x,y,z,volume,pitch].every(Number.isFinite) || volume<=0 || pitch<=0) return;
        // Limit pools from server-provided names; reclaim stopped resources.
        if (!this.soundPool[name] && Object.keys(this.soundPool).length >= 64) {
            const oldest=Object.keys(this.soundPool)[0];
            for(const sound of this.soundPool[oldest]) { if(sound.isPlaying)sound.stop();sound.disconnect();this.scene.remove(sound); }
            delete this.soundPool[oldest];
        }
        let pool = this.soundPool[name];

        if (typeof pool === "undefined") {
            // Load sound pool
            this.loadSoundPool(name);
            pool = this.soundPool[name];
        }
        if (pool.length > 0) {
            this.audioListener.context.resume().catch(() => {});
            // Play random sound in pool
            let sound = pool.find(sound=>!sound.isPlaying) || pool[0];
            if (typeof volume === "undefined" || typeof sound === "undefined") {
                return;
            }

            // Stop previous sound
            if (sound.isPlaying) {
                sound.stop();
            }

            // Update position
            sound.position.set(x, y, z);
            sound.updateMatrixWorld(true);

            // Update volume and pitch
            sound.setVolume(Math.min(2,volume));
            sound.setPlaybackRate(Math.max(.25,Math.min(4,pitch)));

            // Play sound
            sound.offset = 0;
            sound.play();
        }
    }

    isCreated() {
        return !(this.audioListener === null);
    }

}
