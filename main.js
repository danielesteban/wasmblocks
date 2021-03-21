import '../core/music.js';
import Renderer from './core/renderer.js';
import SFX from './core/sfx.js';
import VoxelWorld from './core/voxels.js';
import Dome from './renderables/dome.js';
import Grid from './renderables/grid.js';
import VoxelChunk from './renderables/chunk.js';
import { Color, Group, Scene } from './vendor/three.js';

const renderer = new Renderer({
  fps: document.getElementById('fps'),
  renderer: document.getElementById('renderer'),
});

const { camera, controls } = renderer;
const meshes = [];
const neighbors = [
  { x: -1, z: -1 },
  { x: 0, z: -1 },
  { x: 1, z: -1 },
  { x: -1, z: 0 },
  { x: 0, z: 0 },
  { x: 1, z: 0 },
  { x: -1, z: 1 },
  { x: 0, z: 1 },
  { x: 1, z: 1 },
];
const scale = 0.5;
const scene = new Scene();
scene.background = new Color(0x0a141e);
scene.add(camera);
const sfx = new SFX({ listener: controls.listener });
let sounds;
Promise.all([...Array(4)].map(() => (
  sfx.load('/sounds/plop.ogg')
    .then((sound) => {
      sound.filter = sound.context.createBiquadFilter();
      sound.setFilter(sound.filter);
      sound.setRefDistance(8);
      scene.add(sound);
      return sound;
    })
))).then((sfx) => { sounds = sfx; });

const world = new VoxelWorld({
  wasm: '/core/voxels.wasm',
  chunkSize: 32,
  width: 384,
  height: 128,
  depth: 384,
  onLoad: () => {
    const chunks = {
      x: world.width / world.chunkSize,
      y: world.height / world.chunkSize,
      z: world.depth / world.chunkSize,
    };
    const origin = { x: world.width * 0.5 * scale, z: world.depth * 0.5 * scale };
    const dome = new Dome(origin);
    const grid = new Grid(origin);
    const voxels = new Group();
    scene.add(grid);
    scene.add(voxels);
    scene.add(dome);

    world.generate(Math.floor(Math.random() * 2147483647));
    {
      const spawn = camera.position.set(
        Math.floor(world.width * 0.5),
        0,
        Math.floor(world.depth * 0.5),
      );
      spawn
        .add({
          x: 0.5,
          y: world.heightmap.buffer[spawn.z * world.width + spawn.x] + 1,
          z: 0.5,
        })
        .multiplyScalar(scale)
        .add({ x: 0, y: 1.6, z: 0 });
    }
    for (let z = 0; z < chunks.z; z += 1) {
      for (let y = 0; y < chunks.y; y += 1) {
        for (let x = 0; x < chunks.x; x += 1) {
          const chunk = new VoxelChunk({
            x: x * world.chunkSize,
            y: y * world.chunkSize,
            z: z * world.chunkSize,
            geometry: world.mesh(x, y, z),
            scale,
          });
          meshes.push(chunk);
          if (chunk.geometry.getIndex()) {
            voxels.add(chunk);
          }
        }
      }
    }

    const remesh = ({ x, y, z }) => {
      const chunkX = Math.floor(x / world.chunkSize);
      const chunkY = Math.floor(y / world.chunkSize);
      const chunkZ = Math.floor(z / world.chunkSize);
      const topY = Math.min(chunkY + 1, chunks.y - 1);
      neighbors.forEach((neighbor) => {
        const x = chunkX + neighbor.x;
        const z = chunkZ + neighbor.z;
        if (x < 0 || x >= chunks.x || z < 0 || z >= chunks.z) {
          return;
        }
        for (let y = 0; y <= topY; y += 1) {
          const mesh = meshes[z * chunks.x * chunks.y + y * chunks.x + x];
          const geometry = world.mesh(x, y, z);
          if (geometry.indices.length) {
            mesh.update(geometry);
            if (!mesh.parent) voxels.add(mesh);
          } else {
            if (mesh.parent) voxels.remove(mesh);
          }
        }
      });
    };

    scene.onAnimationTick = () => {
      const { brush, buttons, raycaster } = controls;
      const isPlacing = buttons.secondaryDown;
      const isRemoving = buttons.primaryDown;
      if (!(isPlacing || isRemoving)) {
        return;
      }
      const hit = raycaster.intersectObjects(isPlacing ? [...voxels.children, grid] : voxels.children)[0];
      if (!hit) {
        return;
      }
      if (sounds) {
        const sound = sounds.find(({ isPlaying }) => (!isPlaying));
        if (sound && sound.context.state === 'running') {
          sound.filter.type = isPlacing ? 'lowpass' : 'highpass';
          sound.filter.frequency.value = (Math.random() + 0.5) * 1000;
          sound.position.copy(hit.point);
          sound.play();
        }
      }
      const color = {
        r: Math.floor(Math.random() * 256),
        g: Math.floor(Math.random() * 256),
        b: Math.floor(Math.random() * 256),
      };
      const noise = ((color.r + color.g + color.b) / 3) * brush.noise;
      hit.point
        .divideScalar(scale)
        .addScaledVector(hit.face.normal, isPlacing ? 0.25 : -0.25)
        .floor();
      controls.getBrush(brush).forEach(({ x, y, z }) => (
        world.update({
          x: hit.point.x + x,
          y: hit.point.y + y,
          z: hit.point.z + z,
          type: isPlacing ? 1 : 0,
          r: Math.min(Math.max(color.r + (Math.random() - 0.5) * noise, 0), 0xFF),
          g: Math.min(Math.max(color.g + (Math.random() - 0.5) * noise, 0), 0xFF),
          b: Math.min(Math.max(color.b + (Math.random() - 0.5) * noise, 0), 0xFF),
        })
      ));
      remesh(hit.point);
    };

    // Animation test
    // You'll need to reduce the world size first
    // if you want to try this with more than 1fps
    // 
    // let t = 0;
    // scene.onAnimationTick = ({ delta }) => {
    //   t += delta;
    //   if (t >= 1) {
    //     world.generate(Math.floor(Math.random() * 2147483647));
    //     t = 0;
    //   } else {
    //     world.simulate(Math.max(Math.floor(delta * 100), 1));
    //   }
    //   for (let z = 0, i = 0; z < chunks.z; z += 1) {
    //     for (let y = 0; y < chunks.y; y += 1) {
    //       for (let x = 0; x < chunks.x; x += 1, i += 1) {
    //         const mesh = meshes[i];
    //         const geometry = world.mesh(x, y, z);
    //         if (geometry.indices.length) {
    //           mesh.update(geometry);
    //           if (!mesh.parent) voxels.add(mesh);
    //         } else {
    //           if (mesh.parent) voxels.remove(mesh);
    //         }
    //       }
    //     }
    //   }
    // };

    renderer.scene = scene;
    document.body.removeChild(document.getElementById('loading'));
  },
});

