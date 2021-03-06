import './core/music.js';
import Renderer from './core/renderer.js';
import SFX from './core/sfx.js';
import VoxelWorld from './core/voxels.js';
import Dome from './renderables/dome.js';
import Grid from './renderables/grid.js';
import VoxelChunk from './renderables/chunk.js';
import { Color, Group, Scene } from './vendor/three.js';

// Navigate to /#/animation to run the animation test
const isAnimationTest = location.hash.substr(2) === 'animation';

const renderer = new Renderer({
  dom: {
    fps: document.getElementById('fps'),
    renderer: document.getElementById('renderer'),
  },
  postprocessing: !navigator.userAgent.includes('Mobile'),
});

const { camera, controls } = renderer;
const meshes = [];
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

let light = 0;
let targetLight = 1;
const toggle = document.getElementById('light');
if (isAnimationTest) toggle.style.display = 'none';
const setTargetLight = (target) => {
  if (light !== targetLight) {
    return;
  }
  targetLight = target;
  toggle.className = target >= 0.5 ? 'day' : 'night';
};
[...toggle.getElementsByTagName('svg')].forEach((svg, i) => {
  svg.addEventListener('click', () => setTargetLight(i === 0 ? 1 : 0), false);
});
const updateLight = (intensity) => {
  light = intensity;
  Dome.material.uniforms.background.value.setHex(0x336699).multiplyScalar(Math.max(intensity, 0.05));
  scene.background.copy(Dome.material.uniforms.background.value).multiplyScalar(0.2);
  Grid.material.uniforms.fogColor.value.copy(scene.background);
  Grid.material.uniforms.intensity.value = Math.max(intensity, 0.1);
  VoxelChunk.material.uniforms.ambientIntensity.value = Math.max(Math.min(intensity, 0.7) / 0.7, 0.5) * 0.1;
  VoxelChunk.material.uniforms.sunlightIntensity.value = Math.min(intensity, 0.7);
};

const world = new VoxelWorld({
  wasm: '/core/voxels.wasm',
  ...(isAnimationTest ? {
    width: 96,
    height: 320,
    depth: 96,
  } : {
    width: 384,
    height: 128,
    depth: 384,
  }),
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
    voxels.matrixAutoUpdate = false;
    scene.add(grid);
    scene.add(voxels);
    scene.add(dome);

    world.generate({
      // type: Math.floor(Math.random() * 2),
      simulation: isAnimationTest,
    });
    if (isAnimationTest) {
      camera.position.set(
        world.width * 0.5 * scale,
        world.height * 0.2 * scale,
        world.depth * 2 * scale,
      );
    } else {
      const spawn = camera.position.set(
        Math.floor(world.width * 0.5),
        0,
        Math.floor(world.depth * 0.5)
      );
      spawn
        .add({
          x: 0.5,
          y: world.heightmap.view[spawn.z * world.width + spawn.x] + 1,
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
          if (chunk.geometry.getIndex() !== null) {
            voxels.add(chunk);
          }
        }
      }
    }

    if (isAnimationTest) {
      // Animation Test
      let t = 0;
      scene.onAnimationTick = ({ delta }) => {
        t += delta;
        if (t >= 5) {
          t = 0;
          world.generate({ simulation: true });
        } else {
          world.simulate(1);
        }
        for (let z = 0, i = 0; z < chunks.z; z += 1) {
          for (let y = 0; y < chunks.y; y += 1) {
            for (let x = 0; x < chunks.x; x += 1, i += 1) {
              const mesh = meshes[i];
              const geometry = world.mesh(x, y, z);
              if (geometry.indices.length > 0) {
                mesh.update(geometry);
                if (!mesh.parent) voxels.add(mesh);
              } else if (mesh.parent) {
                voxels.remove(mesh);
              }
            }
          }
        }
      };
    } else {
      // Block editing
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
      scene.onAnimationTick = ({ delta }) => {
        const { brush, buttons, raycaster } = controls;
        if (buttons.toggleDown) {
          // Toggle sunlight when pressing 'L'
          setTargetLight(targetLight === 0 ? 1 : 0);
        }
        if (light !== targetLight) {
          const s = delta * 2;
          updateLight(light + Math.min(Math.max(targetLight - light, -s), s));
        }

        // Process input
        const isPlacingBlock = buttons.secondaryDown;
        const isPlacingLight = buttons.tertiaryDown;
        const isRemoving = buttons.primaryDown;
        if (!(isPlacingBlock || isPlacingLight || isRemoving)) {
          return;
        }
        const hit = raycaster.intersectObjects(
          isRemoving ? voxels.children : [...voxels.children, grid]
        )[0] || false;
        if (!hit) {
          return;
        }
        if (sounds) {
          const sound = sounds.find(({ isPlaying }) => (!isPlaying));
          if (sound && sound.context.state === 'running') {
            sound.filter.type = isRemoving ? 'highpass' : 'lowpass';
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
          .addScaledVector(hit.face.normal, isRemoving ? -0.25 : 0.25)
          .floor();
        brush.shape = isPlacingLight ? 0 : 1;
        brush.size = isPlacingLight ? 2 : 6;
        let type;
        if (isPlacingBlock) type = 1;
        else if (isPlacingLight) type = 2;
        else type = 0;
        controls.getBrush(brush).forEach(({ x, y, z }) => (
          world.update({
            x: hit.point.x + x,
            y: hit.point.y + y,
            z: hit.point.z + z,
            type,
            r: Math.min(Math.max(color.r + (Math.random() - 0.5) * noise, 0), 0xFF),
            g: Math.min(Math.max(color.g + (Math.random() - 0.5) * noise, 0), 0xFF),
            b: Math.min(Math.max(color.b + (Math.random() - 0.5) * noise, 0), 0xFF),
          })
        ));
        const chunkX = Math.floor(hit.point.x / world.chunkSize);
        const chunkY = Math.floor(hit.point.y / world.chunkSize);
        const chunkZ = Math.floor(hit.point.z / world.chunkSize);
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
            if (geometry.indices.length > 0) {
              mesh.update(geometry);
              if (!mesh.parent) voxels.add(mesh);
            } else if (mesh.parent) {
              voxels.remove(mesh);
            }
          }
        });
      };
    }

    // Import by drag&drop or clicking the link on the info overlay
    {
      const importFile = (file) => {
        const reader = new FileReader();
        reader.onload = () => {
          world.importVoxels(new Uint8Array(reader.result))
            .then(() => {
              for (let z = 0, i = 0; z < chunks.z; z += 1) {
                for (let y = 0; y < chunks.y; y += 1) {
                  for (let x = 0; x < chunks.x; x += 1, i += 1) {
                    const mesh = meshes[i];
                    const geometry = world.mesh(x, y, z);
                    if (geometry.indices.length > 0) {
                      mesh.update(geometry);
                      if (!mesh.parent) voxels.add(mesh);
                    } else if (mesh.parent) {
                      voxels.remove(mesh);
                    }
                  }
                }
              }
            });
        };
        reader.readAsArrayBuffer(file);
      };
      document.addEventListener('dragover', (e) => e.preventDefault(), false);
      document.addEventListener('drop', (e) => {
        e.preventDefault();
        const [file] = e.dataTransfer.files;
        if (file && file.name.lastIndexOf('.blocks') === file.name.length - 7) {
          importFile(file);
        }
      }, false);
      const loader = document.createElement('input');
      loader.type = 'file';
      loader.accept = '.blocks';
      loader.style.display = 'none';
      document.body.appendChild(loader);
      document.getElementById('importVoxels').addEventListener('click', () => {
        loader.onchange = ({ target: { files: [file] } }) => importFile(file);
        loader.click(); 
      }, false);
    }

    // Export by clicking the link on the info overlay
    {
      const downloader = document.createElement('a');
      downloader.style.display = 'none';
      document.body.appendChild(downloader);
      document.getElementById('exportVoxels').addEventListener('click', () => (
        world.exportVoxels()
          .then((buffer) => {
            const blob = new Blob([buffer], { type: 'application/octet-stream' });
            downloader.download = `${Date.now()}.blocks`;
            downloader.href = URL.createObjectURL(blob);
            downloader.click();
          })
      ), false);
    }

    renderer.scene = scene;
    document.body.removeChild(document.getElementById('loading'));
  },
});
