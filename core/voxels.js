class VoxelWorld {
  constructor({
    wasm,
    chunkSize,
    width,
    height,
    depth,
    onLoad,
  }) {
    this.chunkSize = chunkSize;
    this.width = width;
    this.height = height;
    this.depth = depth;
    const maxFaces = Math.ceil(chunkSize * chunkSize * chunkSize * 0.5) * 6; // worst possible case
    const queueSize = width * depth * 2;
    const layout = [
      { id: 'voxels', type: Uint8Array, size: width * height * depth * VoxelWorld.fields.stride },
      { id: 'vertices', type: Uint8Array, size: maxFaces * 4 * 6 },
      { id: 'indices', type: Uint32Array, size: maxFaces * 6 },
      { id: 'heightmap', type: Int32Array, size: width * depth },
      { id: 'queueA', type: Int32Array, size: queueSize },
      { id: 'queueB', type: Int32Array, size: queueSize },
      { id: 'queueC', type: Int32Array, size: queueSize },
      { id: 'queueD', type: Int32Array, size: queueSize },
      { id: 'world', type: Int32Array, size: 3 },
      { id: 'bounds', type: Float32Array, size: 4 },
    ];
    const pages = Math.ceil(layout.reduce((total, { type, size }) => (
      total + size * type.BYTES_PER_ELEMENT
    ), 0) / 65536) + 1;
    const memory = new WebAssembly.Memory({ initial: pages, maximum: pages });
    fetch(wasm)
      .then((res) => res.arrayBuffer())
      .then((buffer) => (
        WebAssembly.instantiate(buffer, { env: { memory } })
      ))
      .then(({ instance }) => {
        this._generate = instance.exports.generate;
        this._floodLight = instance.exports.floodLight;
        this._removeLight = instance.exports.removeLight;
        this._propagate = instance.exports.propagate;
        this._simulate = instance.exports.simulate;
        this._mesh = instance.exports.mesh;
        let address = instance.exports.__heap_base * 1;
        layout.forEach(({ id, type, size }) => {
          this[id] = {
            address,
            view: new type(memory.buffer, address, size),
          };
          address += size * type.BYTES_PER_ELEMENT;
        });
        this.world.view.set([width, height, depth]);
        if (onLoad) {
          onLoad(this);
        }
        this.hasLoaded = true;
      })
      .catch((e) => console.error(e));
  }

  getVoxel(x, y, z) {
    const { width, height, depth } = this;
    if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
      return -1;
    }
    return (z * width * height + y * width + x) * VoxelWorld.fields.stride;
  }

  generate(seed) {
    const {
      world,
      heightmap,
      voxels,
    } = this;
    heightmap.view.fill(0);
    voxels.view.fill(0);
    this._generate(
      world.address,
      heightmap.address,
      voxels.address,
      seed
    );
    this.propagate();
  }

  propagate() {
    const {
      world,
      heightmap,
      voxels,
      queueA,
      queueB,
    } = this;
    this._propagate(
      world.address,
      heightmap.address,
      voxels.address,
      queueA.address,
      queueB.address
    );
  }

  mesh(x, y, z) {
    const {
      world,
      voxels,
      chunkSize,
      bounds,
      indices,
      vertices,
    } = this;
    const faces = this._mesh(
      world.address,
      x * chunkSize,
      y * chunkSize,
      z * chunkSize,
      chunkSize,
      voxels.address,
      bounds.address,
      indices.address,
      vertices.address
    );
    if (faces === -1) {
      throw new Error('Requested chunk is out of bounds');
    }
    return {
      bounds: new Float32Array(bounds.view),
      indices: new ((faces * 4 - 1) <= 65535 ? Uint16Array : Uint32Array)(
        indices.view.subarray(0, faces * 6)
      ),
      vertices: new Uint8Array(vertices.view.subarray(0, faces * 4 * 6)),
    };
  }

  update({
    type,
    x, y, z,
    r, g, b,
  }) {
    const { fields, neighbors } = VoxelWorld;
    const {
      width,
      height,
      depth,
      world,
      heightmap,
      voxels,
      queueA,
      queueB,
      queueC,
      queueD,
    } = this;
    if (x < 1 || x >= width - 1 || y < 0 || y >= height - 1 || z < 1 || z >= depth - 1) {
      return;
    }
    const voxel = this.getVoxel(x, y, z);
    const current = voxels.view[voxel];
    voxels.view.set([type, r, g, b], voxel);
    {
      const heightIndex = (z * width) + x;
      const voxelHeight = heightmap.view[heightIndex];
      if (type === 0) {
        if (y === voxelHeight) {
          for (let h = y - 1; h >= 0; h -= 1) {
            if (h === 0 || voxels.view[this.getVoxel(x, h, z)] !== 0) {
              heightmap.view[heightIndex] = h;
              break;
            }
          }
        }
      } else if (voxelHeight < y) {
        heightmap.view[heightIndex] = y;
      }
    }
    if (current === 0 && type !== 0) {
      const light = voxels.view[voxel + fields.light];
      if (light !== 0) {
        voxels.view[voxel + fields.light] = 0;
        queueA.view.set([voxel, light]);
        this._removeLight(
          world.address,
          heightmap.address,
          voxels.address,
          queueA.address,
          2,
          queueB.address,
          queueC.address,
          0,
          queueD.address
        );
      }
    }
    if (type === 0 && current !== 0) {
      let queued = 0;
      neighbors.forEach((offset) => {
        const nx = x + offset.x;
        const ny = y + offset.y;
        const nz = z + offset.z;
        const nv = this.getVoxel(nx, ny, nz);
        if (nv !== -1 && voxels.view[nv + fields.light] !== 0) {
          queueA.view[queued++] = nv;
        }
      });
      if (queued > 0) {
        this._floodLight(
          world.address,
          heightmap.address,
          voxels.address,
          queueA.address,
          queued,
          queueB.address
        );
      }
    }
  }

  simulate(steps) {
    const {
      world,
      heightmap,
      voxels,
    } = this;
    for (let i = 0; i < steps; i += 1) {
      this._simulate(
        world.address,
        heightmap.address,
        voxels.address
      );
    }
    this.propagate();
  }
}

VoxelWorld.fields = {
  type: 1,
  r: 1,
  g: 2,
  b: 3,
  light: 4,
  stride: 5,
};

VoxelWorld.neighbors = [
  { x: 1, y: 0, z: 0 },
  { x: -1, y: 0, z: 0 },
  { x: 0, y: 0, z: 1 },
  { x: 0, y: 0, z: -1 },
  { x: 0, y: 1, z: 0 },
  { x: 0, y: -1, z: 0 },
];

export default VoxelWorld;
