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
      { id: 'voxels', type: Uint8Array, size: width * height * depth * 5 },
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
    (WebAssembly.instantiateStreaming ? (
      WebAssembly.instantiateStreaming(fetch(wasm), { env: { memory } })
    ) : (
      fetch(wasm).then((res) => res.arrayBuffer()).then((buffer) => (
        WebAssembly.instantiate(buffer, { env: { memory } })
      ))
    ))
      .then(({ instance }) => {
        this._mesh = instance.exports.mesh;
        this._generate = instance.exports.generate;
        this._propagate = instance.exports.propagate;
        this._simulate = instance.exports.simulate;
        this._update = instance.exports.update;
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
      voxels.address,
      bounds.address,
      indices.address,
      vertices.address,
      chunkSize,
      x * chunkSize,
      y * chunkSize,
      z * chunkSize
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

  update({
    type,
    x, y, z,
    r, g, b,
  }) {
    const {
      world,
      heightmap,
      voxels,
      queueA,
      queueB,
      queueC,
      queueD,
    } = this;
    this._update(
      world.address,
      heightmap.address,
      voxels.address,
      queueA.address,
      queueB.address,
      queueC.address,
      queueD.address,
      type,
      x, y, z,
      r, g, b
    );
  }
}

export default VoxelWorld;
