//
//  shortbowGameManager.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A TypeScript port of the parts of
//  unpublishedScripts/marketplace/shortbow/shortbowGameManager.js that touch the engine API,
//  type-checked against the server entity script declarations by `npm run check`.
//

/// <reference path="../dist/overte-server-entity.d.ts" />

Script.include(Script.resolvePath("utils.js"));

type GameState = "idle" | "playing" | "between-waves" | "game-over";

interface Enemy {
    id: Uuid;
    lastKnownPosition: Vec3;
    lastHeartbeat: number;
}

interface GameMessage {
    type: "start-game" | "enemy-killed" | "enemy-escaped" | "enemy-heartbeat";
    entityID: Uuid;
    position: Vec3;
}

const TARGET_HIT_SOUND = SoundCache.getSound(Script.resolvePath("sounds/targetHit.wav"));
const POINTS_PER_KILL = 100;
const ENEMY_SPEED = 3.0;
const MAX_DISTANCE_FROM_GAME = 200;

function getPropertiesForEntities(entityIDs: Uuid[], desiredProperties: string[]): Entities.EntityProperties[] {
    return entityIDs.map(id => Entities.getEntityProperties(id, desiredProperties));
}

function findChildrenWithName(parentID: Uuid, name: string): Uuid[] {
    return Entities.getChildrenIDs(parentID).filter(id => Entities.getEntityProperties(id, "name").name === name);
}

function sendHighScore(entityID: Uuid, score: number, onResponse: (highScore: number) => void): void {
    const request = new XMLHttpRequest();
    request.onreadystatechange = () => {
        if (request.readyState === request.DONE && request.status === 200) {
            const response = JSON.parse(request.responseText) as { highScore?: number };
            if (response.highScore !== undefined) {
                onResponse(response.highScore);
            }
        }
    };
    request.open("GET", "https://example.invalid/highscore?entityID=" + entityID + "&score=" + score);
    request.timeout = 10000;
    request.send();
}

class ShortbowGameManager {
    private gameState: GameState = "idle";
    private readonly commChannelName: string;
    private rootPosition: Vec3 = Vec3.ZERO;
    private remainingEnemies: Enemy[] = [];
    private bowIDs: Uuid[] = [];
    private score = 0;
    private nextWaveTimer: number | null = null;
    private checkEnemiesTimer: number | null = null;

    private readonly rootEntityID: Uuid;
    private readonly scoreDisplayID: Uuid;

    constructor(rootEntityID: Uuid, scoreDisplayID: Uuid) {
        this.rootEntityID = rootEntityID;
        this.scoreDisplayID = scoreDisplayID;
        this.commChannelName = "shortbow-" + rootEntityID;
        Messages.subscribe(this.commChannelName);
        Messages.messageReceived.connect(this, this.onReceivedMessage);
        Messages.sendMessage(this.commChannelName, "hi");
    }

    cleanup(): void {
        Messages.unsubscribe(this.commChannelName);
        Messages.messageReceived.disconnect(this, this.onReceivedMessage);
        if (this.checkEnemiesTimer !== null) {
            Script.clearInterval(this.checkEnemiesTimer);
            this.checkEnemiesTimer = null;
        }
        this.bowIDs.forEach(id => Entities.deleteEntity(id));
        this.remainingEnemies.forEach(enemy => Entities.deleteEntity(enemy.id));
    }

    startGame(): void {
        if (this.gameState !== "idle") {
            return;
        }
        this.gameState = "playing";
        this.rootPosition = Entities.getEntityProperties(this.rootEntityID, "position").position ?? Vec3.ZERO;

        const bowSpawns = getPropertiesForEntities(findChildrenWithName(this.rootEntityID, "SB.BowSpawn"),
            ["position", "rotation"]);
        bowSpawns.forEach((props, i) => {
            Vec3.print("Creating bow: " + i, props.position ?? Vec3.ZERO);
            this.bowIDs.push(Entities.addEntity({
                type: "Model",
                name: "WG.Hifi-Bow",
                position: props.position,
                rotation: props.rotation,
                dimensions: { x: 0.04, y: 1.3, z: 0.2 },
                dynamic: true,
                gravity: { x: 0, y: -9.8, z: 0 },
                modelURL: Script.resolvePath("bow/models/bow-deadly.baked.fbx"),
                compoundShapeURL: Script.resolvePath("bow/models/bow_collision_hull.obj"),
                shapeType: "compound",
                script: Script.resolvePath("bow/bow.js"),
                userData: JSON.stringify({ grabbableKey: { grabbable: true } })
            }));
        });

        this.nextWaveTimer = Script.setTimeout(() => this.spawnEnemy(), 100);
        this.checkEnemiesTimer = Script.setInterval(() => this.checkEnemies(), 100);
    }

    spawnEnemy(): void {
        const rotation = Quat.fromPitchYawRollDegrees(0, Math.random() * 360, 0);
        const velocity = Vec3.multiply(ENEMY_SPEED, Quat.getFront(rotation));
        const entityID = Entities.addEntity({
            type: "Model",
            name: "SB.Enemy",
            position: this.rootPosition,
            rotation: rotation,
            velocity: velocity,
            modelURL: Script.resolvePath("models/Amber.baked.fbx"),
            script: Script.resolvePath("enemyClientEntity.js"),
            serverScripts: Script.resolvePath("enemyServerEntity.js"),
            userData: JSON.stringify({ gameChannel: this.commChannelName })
        });
        this.remainingEnemies.push({ id: entityID, lastKnownPosition: this.rootPosition, lastHeartbeat: Date.now() });

        Script.setTimeout(() => {
            const current = Entities.getEntityProperties(entityID, "velocity").velocity;
            if (current) {
                Entities.editEntity(entityID, { velocity: Vec3.sum(current, { x: 0, y: 5.0, z: 0 }) });
            }
        }, 500 + Math.random() * 4000);
    }

    checkEnemies(): void {
        for (let i = this.remainingEnemies.length - 1; i >= 0; --i) {
            const enemy = this.remainingEnemies[i];
            if (Vec3.distance(enemy.lastKnownPosition, this.rootPosition) > MAX_DISTANCE_FROM_GAME) {
                Entities.deleteEntity(enemy.id);
                this.remainingEnemies.splice(i, 1);
                Audio.playSound(TARGET_HIT_SOUND, { volume: 1.0, position: this.rootPosition });
                this.setScore(this.score + POINTS_PER_KILL);
            }
        }
    }

    endGame(): void {
        this.gameState = "game-over";
        sendHighScore(this.rootEntityID, this.score, highScore => print("High score:", highScore));
        if (this.nextWaveTimer !== null) {
            Script.clearTimeout(this.nextWaveTimer);
            this.nextWaveTimer = null;
        }
        this.cleanup();
    }

    setScore(score: number): void {
        this.score = score;
        Entities.editEntity(this.scoreDisplayID, { text: String(score) });
    }

    onReceivedMessage(channel: string, messageJSON: string, senderID: Uuid): void {
        if (channel !== this.commChannelName) {
            return;
        }
        const message = JSON.parse(messageJSON) as GameMessage;
        if (message.type === "start-game") {
            this.startGame();
        } else if (message.type === "enemy-heartbeat") {
            const enemy = this.remainingEnemies.find(candidate => candidate.id === message.entityID);
            if (enemy) {
                enemy.lastHeartbeat = Date.now();
                enemy.lastKnownPosition = message.position;
            }
        } else {
            print("Message from", senderID, "of type", message.type);
        }
    }
}

const manager = new ShortbowGameManager(Uuid.NONE, Uuid.NONE);
Script.scriptEnding.connect(() => manager.cleanup());
